#include <WiFi.h>
#include "Nextion.h"
#include <Wire.h>
#include <SoftwareSerial.h>
#include "Adafruit_Thermal.h"

// WiFi credentials
const char* ssid = "ESP32-Server";
const char* password = "password123";
const char* host = "192.168.4.1";

// Pin configurations
#define RX1 4  // Communication with Arduino Mega
#define TX1 5
#define RX2 16 // Communication with Nextion
#define TX2 17
#define PRINTER_RX 27 // Printer RX
#define PRINTER_TX 14 // Printer TX

// SoftwareSerial and Printer
SoftwareSerial printerSerial(PRINTER_RX, PRINTER_TX);
Adafruit_Thermal printer(&printerSerial);

// WiFi and client
WiFiClient client;

// Buffers and variables
#define BUFFER_SIZE 10
String payloadBuffer[BUFFER_SIZE];
int bufferIndex = 0;
bool shouldSendData = false;
unsigned long startTime = 0;

// Unit name for the printer header
const char* unitName = "HD78140KM";

// Nextion components
NexButton BtnStart = NexButton(0, 1, "BtnStart");
NexButton BtnStop = NexButton(0, 2, "BtnStop");
NexText TxtStatus = NexText(0, 3, "TxtStatus");
NexText TxtSSID = NexText(0, 4, "TxtSSID");
NexText TxtData = NexText(0, 5, "TxtData");
NexText TxtKirim = NexText(0, 6, "TxtKirim");

NexTouch *nex_listen_list[] = {
  &BtnStart,
  &BtnStop,
  NULL
};

// States
enum State {
  IDLE,
  CONNECTING,
  TRANSMITTING,
  DISCONNECTED
};
State currentState = IDLE;

// Button callbacks
void BtnStartPopCallback(void *ptr) {
  Serial.println("BtnStartPopCallback");
  TxtSSID.setText("Connecting to WiFi...");
  currentState = CONNECTING;
  TxtStatus.setText("CONNECTING");

  for (int i = 0; i < BUFFER_SIZE; i++) {
    payloadBuffer[i] = "";
  }
  bufferIndex = 0;
}

void BtnStopPopCallback(void *ptr) {
  Serial.println("BtnStopPopCallback");
  stopConnection();
  printLast10Data();
}

// Reconnect to server
void reconnect() {
  TxtStatus.setText("Reconnecting to server...");
  Serial.println("Attempting to reconnect to server...");

  int retries = 0;
  const int maxRetries = 5;
  while (!client.connect(host, 80)) {
    retries++;
    Serial.printf("Reconnect attempt %d/%d\n", retries, maxRetries);
    TxtStatus.setText("Reconnecting...");
    delay(1000);

    if (retries >= maxRetries) {
      TxtStatus.setText("Reconnect failed.");
      currentState = DISCONNECTED;
      return;
    }
  }
  currentState = TRANSMITTING;
}

// Stop connection
void stopConnection() {
  TxtSSID.setText("Stopping connection...");
  shouldSendData = false;

  if (client.connected()) {
    client.stop();
    TxtSSID.setText("Disconnected from server.");
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect();
    TxtSSID.setText("Disconnected from WiFi.");
  }

  delay(500);
  currentState = IDLE;
  TxtStatus.setText("IDLE");
}

void setup() {
  Serial.begin(115200);

  // Serial for Arduino Mega
  Serial1.begin(9600, SERIAL_8N1, RX1, TX1);

  // Serial for Nextion
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2);
  nexInit();

  // Printer initialization
  printerSerial.begin(9600);
  printer.begin();

  // Register button callbacks
  BtnStart.attachPop(BtnStartPopCallback, &BtnStart);
  BtnStop.attachPop(BtnStopPopCallback, &BtnStop);

  TxtStatus.setText("System ready. Press START.");
  Serial.println("System ready.");
}

void loop() {
  nexLoop(nex_listen_list);

  switch (currentState) {
    case IDLE:
      TxtStatus.setText("IDLE");
      break;

    case CONNECTING: {
      TxtStatus.setText("CONNECTING");
      Serial.println("Connecting to WiFi...");
      unsigned long wifiTimeout = millis();
      WiFi.begin(ssid, password);

      while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 10000) {
        delay(1000);
        TxtSSID.setText("Connecting to WiFi...");
      }

      if (WiFi.status() == WL_CONNECTED) {
        TxtSSID.setText(WiFi.SSID().c_str());
        Serial.printf("Connected to WiFi: %s\n", WiFi.SSID().c_str());
        TxtStatus.setText("TRANSMITTING");
        startTime = millis();
        shouldSendData = true;
        currentState = TRANSMITTING;
      } else {
        TxtSSID.setText("WiFi connection failed.");
        currentState = IDLE;
        TxtStatus.setText("IDLE");
      }
      break;
    }

    case TRANSMITTING:
      TxtStatus.setText("TRANSMITTING");

      if (Serial1.available()) {
        String serialData = Serial1.readStringUntil('\n');
        if (!serialData.isEmpty()) {
          TxtData.setText(serialData.c_str());
          Serial.printf("Received data: %s\n", serialData.c_str());

          // Extract payload (pl) from serialData
          String parts[6];
          int index = 0;
          while (serialData.indexOf('-') > 0 && index < 5) {
            int pos = serialData.indexOf('-');
            parts[index] = serialData.substring(0, pos);
            serialData = serialData.substring(pos + 1);
            index++;
          }
          parts[index] = serialData;

          // Save only the payload (parts[4]) to buffer
          if (bufferIndex < BUFFER_SIZE) {
            payloadBuffer[bufferIndex++] = parts[4];
          } else {
            for (int i = 1; i < BUFFER_SIZE; i++) {
              payloadBuffer[i - 1] = payloadBuffer[i];
            }
            payloadBuffer[BUFFER_SIZE - 1] = parts[4];
          }
        }
      }

      if (client.connected() && shouldSendData && bufferIndex > 0) {
        String payload = payloadBuffer[bufferIndex - 1];
        Serial.printf("Sending data to server: %s\n", payload.c_str());
        if (client.println(payload)) {
          TxtKirim.setText(payload.c_str());
        } else {
          TxtKirim.setText("Send failed");
          Serial.println("Send failed");
          reconnect();
        }
      }

      static unsigned long lastCheck = 0;
      if (millis() - lastCheck > 5000) {
        lastCheck = millis();
        if (!client.connected()) {
          TxtStatus.setText("Server disconnected.");
          Serial.println("Server disconnected.");
          reconnect();
        }
      }
      break;

    case DISCONNECTED:
      TxtSSID.setText("DISCONNECTED");
      stopConnection();
      break;
  }
  delay(1000);
}

void printLast10Data() {
  printer.justify('C');
  printer.setSize('M');
  printer.println(unitName);
  printer.println("--------------------------");
  printer.justify('L');
  printer.setSize('S');
  printer.println("TIME     RIT     PAYLOAD");
  printer.println("--------------------------");

  for (int i = 0; i < BUFFER_SIZE; i++) {
    if (!payloadBuffer[i].isEmpty()) {
      printer.printf("09:00    1       %s\n", payloadBuffer[i].c_str());
    }
  }

  printer.println("");
  printer.sleep();
}

//tambahan fungsi cetak dengan printer thermal di pin 14 dan 27
