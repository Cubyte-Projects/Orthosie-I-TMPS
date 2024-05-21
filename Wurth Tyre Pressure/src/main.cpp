#include <Arduino.h>
// TPMS BLE ESP32-C3
// 2020 RA6070
// v0.4 06/08/20

#include "BLEDevice.h"
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi credentials
const char* ssid = "Wurth-Test";  // Replace with your WiFi SSID
const char* password = "Redexpert1";  // Replace with your WiFi password

// MQTT Broker details
const char* mqtt_server = "192.168.1.99";
const char* mqtt_user = "Your User name";
const char* mqtt_password = "MQTT Password";
const char* mqtt_topic = "tpms/data"; // Topic name

// Function Prototypes
String retmanData(String txt, int shift);
byte retByte(String Data, int start);
long returnData(String Data, int start);
int returnBatt(String Data);
int returnAlarm(String Data);
void setup_wifi();
void reconnect();

// Variables
BLEScan* pBLEScan;
BLEClient* pClient;
static BLEAddress *pServerAddress;
WiFiClient espClient;
PubSubClient client(espClient);

// TPMS BLE SENSORS known addresses
String knownAddresses[] = { "80:ea:ca:11:49:94" , "81:ea:ca:21:49:e1" , "82:ea:ca:31:49:d4" , "83:ea:ca:41:4c:32" };   // use ble scanner to find address

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
          sensorIndex = i + 1;  // Store sensor index
          break;
        }
      }
      if (known) {
        Serial.print("tyre sensor ");
        Serial.println(sensorIndex);  // Print sensor number
        String instring = retmanData(ManufData, 0); 
        Serial.println("Manufacturer data: " + instring);
        Serial.print("Device found: ");
        Serial.println(Device.getRSSI());

        // Tire data
        float temperature = returnData(instring, 12) / 100.0;
        float pressureKpa = returnData(instring, 8) / 1000.0;
        float pressureBar = returnData(instring, 8) / 100000.0;
        int battery = returnBatt(instring);
        bool alarm = returnAlarm(instring);

        // Print data
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println("C°");
        Serial.print("Pressure: ");
        Serial.print(pressureKpa);
        Serial.println("Kpa");
        Serial.print("Pressure: ");
        Serial.print(pressureBar);
        Serial.println("bar");
        Serial.print("Battery: ");
        Serial.print(battery);
        Serial.println("%");
        if (alarm) {
          Serial.println("ALARM!");
        }
        Serial.println("");

        // Publish data to MQTT
        String payload = "{";
        payload += "\"sensor\":" + String(sensorIndex) + ",";
        payload += "\"temperature\":" + String(temperature) + ",";
        payload += "\"pressure_kpa\":" + String(pressureKpa) + ",";
        payload += "\"pressure_bar\":" + String(pressureBar) + ",";
        payload += "\"battery\":" + String(battery) + ",";
        payload += "\"alarm\":" + String(alarm);
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

  Serial.println("Starting BLE scan...");
  BLEScanResults scanResults = pBLEScan->start(5);
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
  // Return battery percentage
  return retByte(Data, 16);
}

int returnAlarm(String Data) {
  // Return alarm flag
  return retByte(Data, 17);
}