#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
  
#include <BLEDevice.h>
#include <SimpleTimer.h>

#define ADC_EN              14  //ADC_EN is the ADC detection enable port
#define ADC_PIN             34

#define uS_TO_S_FACTOR 1000000     /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  598        /* Time ESP32 will go to sleep (in seconds) 3600 seconds = 1 hour */

#define LED_BUILTIN 2

const char* ssid     = "SookYenFarm";    // SSID Wifi
const char* password = "0863741677";   // Password Wifi
const char* serverName = "http://tonofarm.herokuapp.com/esp-post-data.php";
String apiKeyValue = "API-KEY";
String sensorLocation = "12.7581423,102.1468503";

#define LYWSD03MMC_ADDR "a4:c1:38:1d:88:2e"

uint32_t chipId = 0;

float Temp;
float Humidity;
float Pressure;
float vbatt;
float batt_percent;

// ESP32 Battery
int vref = 1100;
uint16_t v = analogRead(ADC_PIN);
float battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);


BLEClient* pClient;

static BLEAddress htSensorAddress(LYWSD03MMC_ADDR);

bool connectionSuccessful = false;

// The remote service we wish to connect to.
static BLEUUID serviceUUID("ebe0ccb0-7a0a-4b0c-8a1a-6ff2997da3a6");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("ebe0ccc1-7a0a-4b0c-8a1a-6ff2997da3a6");

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
      Serial.println("Connected");
    }

    void onDisconnect(BLEClient* pclient) {
      Serial.println("Disconnected");
      if (!connectionSuccessful) {
        Serial.println("RESTART");
        ESP.restart();
      }
    }
};

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
  
  Serial.print("Notify callback for characteristic ");
  Serial.println(pBLERemoteCharacteristic->getUUID().toString().c_str());
  Temp = (pData[0] | (pData[1] << 8)) * 0.01; //little endian 
  Humidity = pData[2];
  vbatt = ((pData[4] * 256) + pData[3]) / 1000.0;
  batt_percent = (vbatt - 2.1) * 100.0;
  Serial.printf("temperature = %.1f : humidity = %.1f : vbatt = %.1f : battery_percent = %.1f \n", Temp, Humidity, vbatt, batt_percent);
  
  // Send Data to Server
  connect();
  
  pClient->disconnect();

  //delay(5000);
  // Configure the wake up source as timer wake up  
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  
  // ESP Deep Sleep Mode
  esp_deep_sleep_start();
}

void registerNotification() {

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
  }
  Serial.println(" - Found our service");

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
  }
  Serial.println(" - Found our characteristic");
  pRemoteCharacteristic->registerForNotify(notifyCallback);
}


void connect() {
   // We start by connecting to a WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  // Check timeout
  unsigned long timeout = millis();
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    timeout = millis();
    Serial.println(timeout);

    // Time Out got deep_sleep_mode save battery
    if (timeout > 30000) {
      Serial.print("TIME_OUT: ");
      Serial.println(timeout);
      Serial.println("Sleeping 10 minutes ..");
      
      // Configure the wake up source as timer wake up  
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  
      // ESP Deep Sleep Mode
      esp_deep_sleep_start();
    }
  }
  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // ChipID
  for(int i=0; i<17; i=i+8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  
  Serial.print("ESP ChipID: ");
  Serial.println(chipId);
  
  // Wi-Fi Signal
  long rssi;
  rssi = WiFi.RSSI();
  Serial.print("WiFi Signal: ");
  Serial.println(rssi);

  // LED Stop
  digitalWrite(LED_BUILTIN, HIGH);
  delay(10);
  
  Serial.print("connecting to ");
  Serial.println(serverName);

  HTTPClient http;
  http.begin(serverName);

  // Specify content-type header
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // We now create a URI for the request
  String httpRequestData = "api_key=";
  httpRequestData += apiKeyValue;
  httpRequestData += "&sensor=";
  httpRequestData += chipId;
  httpRequestData += "&location=";  
  httpRequestData += sensorLocation;
  httpRequestData += "&value1=";  
  httpRequestData += Temp;
  httpRequestData += "&value2=";  
  httpRequestData += Humidity;
  httpRequestData += "&value3=";  
  httpRequestData += batt_percent;
  httpRequestData += "&value4=";  
  httpRequestData += rssi;
  httpRequestData += "&value5=";  
  httpRequestData += battery_voltage;
  
  Serial.print("httpRequestData: ");
  Serial.println(httpRequestData);

  // Send HTTP POST request
  int httpResponseCode = http.POST(httpRequestData);
  
  // Show Connect Status
  digitalWrite(LED_BUILTIN, LOW);   // turn the LED on (HIGH is the voltage level)
  delay(1000);

  if (httpResponseCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();
  digitalWrite(LED_BUILTIN, HIGH);
  delay(1000);
}


void setup() {
  Serial.begin(115200);
  Serial.setTimeout(2000);
  delay(10);
  pinMode(LED_BUILTIN, OUTPUT);  
  
  Serial.println("Starting MJ client...");
  delay(500);

  BLEDevice::init("ESP32");
  createBleClientWithCallbacks();
  delay(500);
  connectSensor();
  registerNotification();
}

void loop() {
  // do nothing
}

void createBleClientWithCallbacks() {
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
}

void connectSensor() {
  pClient->connect(htSensorAddress);
  connectionSuccessful = true;
}
