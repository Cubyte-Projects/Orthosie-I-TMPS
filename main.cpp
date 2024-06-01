#include <Arduino.h>
#include "BLEDevice.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>

// WiFi credentials
const char* ssid = "Wurth-Test";  // Replace with your WiFi SSID
const char* password = "RedExpert2024";  // Replace with your WiFi password

// MQTT Broker details
const char* mqtt_server = "192.168.1.110";
const char* mqtt_user = "Cubyte";
const char* mqtt_password = "Juk£box0!";
const char* mqtt_topic = "tpms/data";

// Web server
WebServer server(80);

// Function Prototypes
String retmanData(String txt, int shift);
byte retByte(String Data, int start);
long returnData(String Data, int start);
int returnBatt(String Data);
int returnAlarm(String Data);
void setup_wifi();
void reconnect();
void handleRoot();
void sendSensorData();  // Make sure the function is declared

// Variables
BLEScan* pBLEScan;
BLEClient* pClient;
static BLEAddress *pServerAddress;
WiFiClient espClient;
PubSubClient client(espClient);

// TPMS BLE SENSORS known addresses
String knownAddresses[] = { "80:ea:ca:11:49:94" , "81:ea:ca:21:49:e1" , "82:ea:ca:31:49:d4" , "83:ea:ca:41:4c:32" };   // use ble scanner to find address

// Sensor data structure
struct SensorData {
  float temperature;
  float pressureKpa;
  float pressureBar;
  int battery;
  bool alarm;
} sensorData[4];

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    //Serial.print("Notify callback for characteristic ");
    //Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
    //Serial.print(" of data length ");
    //Serial.println(length);
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice Device) {
      pServerAddress = new BLEAddress(Device.getAddress());
      bool known = false;
      int sensorIndex = -1;
      String ManufData = Device.toString().c_str();
      for (int i = 0; i < (sizeof(knownAddresses) / sizeof(knownAddresses[0])); i++) {
        if (strcmp(pServerAddress->toString().c_str(), knownAddresses[i].c_str()) == 0) {
          known = true;
          sensorIndex = i;  // Store sensor index
          break;
        }
      }
      if (known) {
        Serial.print("tyre sensor ");
        Serial.println(sensorIndex + 1);  // Print sensor number
        String instring = retmanData(ManufData, 0); 
        Serial.println("Manufacturer data: " + instring);
        Serial.print("Device found: ");
        Serial.println(Device.getRSSI());

        // Tire data
        sensorData[sensorIndex].temperature = returnData(instring, 12) / 100.0;
        sensorData[sensorIndex].pressureKpa = returnData(instring, 8) / 1000.0;
        sensorData[sensorIndex].pressureBar = returnData(instring, 8) / 100000.0;
        sensorData[sensorIndex].battery = returnBatt(instring);
        sensorData[sensorIndex].alarm = returnAlarm(instring);

        // Print data
        Serial.print("Temperature: ");
        Serial.print(sensorData[sensorIndex].temperature);
        Serial.println("C°");
        Serial.print("Pressure: ");
        Serial.print(sensorData[sensorIndex].pressureKpa);
        Serial.println("Kpa");
        Serial.print("Pressure: ");
        Serial.print(sensorData[sensorIndex].pressureBar);
        Serial.println("bar");
        Serial.print("Battery: ");
        Serial.print(sensorData[sensorIndex].battery);
        Serial.println("%");
        if (sensorData[sensorIndex].alarm) {
          Serial.println("ALARM!");
        }
        Serial.println("");

        // Publish data to MQTT
        String payload = "{";
        payload += "\"sensor\":" + String(sensorIndex + 1) + ",";
        payload += "\"temperature\":" + String(sensorData[sensorIndex].temperature) + ",";
        payload += "\"pressure_kpa\":" + String(sensorData[sensorIndex].pressureKpa) + ",";
        payload += "\"pressure_bar\":" + String(sensorData[sensorIndex].pressureBar) + ",";
        payload += "\"battery\":" + String(sensorData[sensorIndex].battery) + ",";
        payload += "\"alarm\":" + String(sensorData[sensorIndex].alarm);
        payload += "}";

        client.publish(mqtt_topic, payload.c_str());

        Device.getScan()->stop();
        delay(100);
      }
    }
};

void setup() {
  // Opening serial port
  Serial.begin(115200);
  delay(100);
  
  // Connect to WiFi
  setup_wifi();
  
  // Setup MQTT
  client.setServer(mqtt_server, 1883);

  // Start the web server
  server.on("/", handleRoot);
  server.on("/data", sendSensorData);  // Ensure the function is correctly referenced
  server.begin();

  // BLE Init
  Serial.print("Init BLE. ");
  BLEDevice::init("");
  pClient = BLEDevice::createClient();
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  Serial.println("Done");
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  server.handleClient(); // Handle web server

  Serial.println("Starting BLE scan...");
  BLEScanResults scanResults = pBLEScan->start(2);
  Serial.println("BLE scan completed.");
  delay(1000);  // Delay to avoid flooding the output with messages
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("tpms/status", "connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// FUNCTIONS 

String retmanData(String txt, int shift) {
  // Return only manufacturer data string
  int start = txt.indexOf("data: ") + 6 + shift;
  return txt.substring(start, start + (36 - shift));  
}

byte retByte(String Data, int start) {
  // Return a single byte from string
  int sp = (start) * 2;
  char *ptr;
  return strtoul(Data.substring(sp, sp + 2).c_str(), &ptr, 16);
}

long returnData(String Data, int start) {
  // Return a long value with little endian conversion
  return retByte(Data, start) | retByte(Data, start + 1) << 8 | retByte(Data, start + 2) << 16 | retByte(Data, start + 3) << 24;
}

int returnBatt(String Data) {
  // Battery voltage
  return retByte(Data, 16);
}

int returnAlarm(String Data) {
  // Alarm state
  return retByte(Data, 17);
}

void handleRoot() {
  if (server.method() == HTTP_POST && server.hasArg("reset")) {
    for (int i = 0; i < 4; i++) {
      sensorData[i].temperature = 0.0;
      sensorData[i].pressureKpa = 0.0;
      sensorData[i].pressureBar = 0.0;
      sensorData[i].battery = 100;
      sensorData[i].alarm = false;
    }
  }

  String html = "<!DOCTYPE html><html><head><title>TPMS Data</title>";
  html += "<meta http-equiv='refresh' content='2'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body { font-family: Arial, sans-serif; background-color: #f2f2f2; } header { background-color: black; color: red; padding: 10px; text-align: center; } table { width: 100%; border-collapse: collapse; } table th { background-color: white; } table, th, td { border: 1px solid black; } th, td { padding: 10px; text-align: center; } .alarm-yes { background-color: red; color: white; } .alarm-no { background-color: green; color: white; } .reset-button { background-color: yellow; padding: 15px 30px; font-size: 18px; } footer { background-color: black; color: white; padding: 10px; text-align: center; position: fixed; bottom: 0; width: 100%; }</style>";
  html += "<script>setTimeout(function() { window.location.reload(); }, 5000);</script>";
  html += "</head><body>";
  html += "<header><h1>Tyre Pressure Monitoring System (TPMS)</h1></header>";
  html += "<table><tr><th>Sensor</th><th>Temperature (°C)</th><th>Pressure (Kpa)</th><th>Pressure (Bar)</th><th>Battery (%)</th><th>Alarm</th></tr>";
  for (int i = 0; i < 4; i++) {
    html += "<tr><td>" + String(i + 1) + "</td>";
    html += "<td>" + String(sensorData[i].temperature) + "</td>";
    html += "<td>" + String(sensorData[i].pressureKpa) + "</td>";
    html += "<td>" + String(sensorData[i].pressureBar) + "</td>";
    html += "<td>" + String(sensorData[i].battery) + "</td>";
    if (sensorData[i].alarm) {
      html += "<td class='alarm-yes'>Yes</td>";
    } else {
      html += "<td class='alarm-no'>No</td>";
    }
    html += "</tr>";
  }
  html += "</table>";

  html += "<div style='margin-bottom: 20px;'></div>";
  html += "<form action='/' method='post' style='text-align: center;'><input type='submit' name='reset' value='Reset Values' class='reset-button'></form>";
  html += "<footer>Wurth Electronics 2024</footer>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void sendSensorData() {
  // Create a JSON string to hold the sensor data
  String json = "{";
  for (int i = 0; i < 4; i++) {
    json += "\"sensor" + String(i + 1) + "\":{";
    json += "\"temperature\":" + String(sensorData[i].temperature) + ",";
    json += "\"pressure_kpa\":" + String(sensorData[i].pressureKpa) + ",";
    json += "\"pressure_bar\":" + String(sensorData[i].pressureBar) + ",";
    json += "\"battery\":" + String(sensorData[i].battery) + ",";
    json += "\"alarm\":" + String(sensorData[i].alarm);
    json += "}";
    if (i < 3) {
      json += ",";
    }
  }
  json += "}";

  // Send the JSON string as the response
  server.send(200, "application/json", json);
}


