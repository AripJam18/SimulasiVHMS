#include <WiFi.h>
#include "Nextion.h"
#include <Wire.h>
#include <SoftwareSerial.h>
#include "Adafruit_Thermal.h"

// WiFi credentials
const char* password = "password123";
char selectedSSID[32] = "";  // Placeholder for selected SSID

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
NexButton BtnScan = NexButton(0, 25, "BtnScan"); // New Scan button
NexCombo CmbSSID = NexCombo(0, 30, "CmbSSID");   // New ComboBox
NexText TxtStatus = NexText(0, 28, "TxtStatus");
NexText TxtSSID = NexText(0, 29, "TxtSSID");
NexText TxtData = NexText(0, 10, "TxtData");
NexText TxtKirim = NexText(0, 12, "TxtKirim");
NexText TxtJam = NexText(0, 3, "TxtJam ");
NexNumber nRit= NexNumber(0, 27, "nRit");

NexTouch *nex_listen_list[] = {
  &BtnStart,
  &BtnStop,
  &BtnScan,
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
#include <vector> // Untuk menyimpan SSID sementara
void BtnScanPopCallback(void *ptr) {
  Serial.println("BtnScanPopCallback");

  // Menampilkan status "Scanning..." di layar Nextion
  TxtStatus.setText("Scanning for SSIDs...");

  // Memindai jaringan Wi-Fi
  int n = WiFi.scanNetworks();
  if (n == 0) {
    TxtStatus.setText("No networks found.");
    return;
  }

  // Membuat string SSID dengan pemisah newline (\r\n)
  String SSIDs = "";
  for (int i = 0; i < n; ++i) {
    if (i > 0) SSIDs += "\r\n";  // Tambahkan newline setelah SSID pertama
    SSIDs += WiFi.SSID(i);
    Serial.println(WiFi.SSID(i));  // Debugging: Cetak SSID ke Serial Monitor
  }

  // Mengirim jumlah jaringan ke properti .txt ComboBox
  String cmdTxt = String("CmbSSID.txt=\"") + String(n) + " Networks\"";
  sendCommand(cmdTxt.c_str());

  // Mengirim daftar SSID ke properti .path ComboBox
  String cmdPath = String("CmbSSID.path=\"") + SSIDs + "\"";
  sendCommand(cmdPath.c_str());

  // Menunggu Nextion untuk memproses perintah
  if (!recvRetCommandFinished()) {
    Serial.println("Error updating ComboBox.");
    TxtStatus.setText("Error updating ComboBox.");
    return;
  }

  // Memperbarui status menjadi "Scan complete"
  TxtStatus.setText("Scan complete. Select SSID.");
}



void BtnStartPopCallback(void *ptr) {
  Serial.println("BtnStartPopCallback");
  CmbSSID.getSelectedText(selectedSSID, sizeof(selectedSSID));

  if (strcmp(selectedSSID, "Select SSID") == 0 || strlen(selectedSSID) == 0) {
    TxtStatus.setText("Select a valid SSID.");
    return;
  }

  Serial.printf("Selected SSID: %s\n", selectedSSID);
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

void reconnect() {
  TxtStatus.setText("Reconnecting to server...");
  Serial.println("Attempting to reconnect to server...");

  int retries = 0;
  const int maxRetries = 5;
  while (!client.connect("192.168.4.1", 80)) {
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
  printer.justify('C');
  printer.setSize('M');
  printer.println(unitName);
  printer.println("--------------------------");
  
  printer.justify('L');
  printer.setSize('S');
  printer.println("TIME     RIT     PAYLOAD");
  printer.println("--------------------------");
  printer.println("");
  printer.sleep();

  // Register button callbacks
  BtnStart.attachPop(BtnStartPopCallback, &BtnStart);
  BtnStop.attachPop(BtnStopPopCallback, &BtnStop);
  BtnScan.attachPop(BtnScanPopCallback, &BtnScan);

  TxtStatus.setText("System ready. Press SCAN.");
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
      Serial.printf("Connecting to WiFi: %s\n", selectedSSID);
      WiFi.begin(selectedSSID, password);

      unsigned long wifiTimeout = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 10000) {
        delay(1000);
        TxtSSID.setText("Connecting...");
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
            // Tampilkan data penuh di TxtData
            TxtData.setText(serialData.c_str());
            Serial.printf("Received data: %s\n", serialData.c_str());

            // Simpan data penuh untuk pengiriman ke server
            if (bufferIndex < BUFFER_SIZE) {
                payloadBuffer[bufferIndex++] = serialData;
            } else {
                // Geser buffer jika penuh
                for (int i = 1; i < BUFFER_SIZE; i++) {
                    payloadBuffer[i - 1] = payloadBuffer[i];
                }
                payloadBuffer[BUFFER_SIZE - 1] = serialData;
            }

            // Pisahkan payload dari data penuh
            String parts[6];
            int index = 0;
            String tempData = serialData; // Salinan untuk parsing
            while (tempData.indexOf('-') > 0 && index < 5) {
                int pos = tempData.indexOf('-');
                parts[index] = tempData.substring(0, pos);
                tempData = tempData.substring(pos + 1);
                index++;
            }
            parts[index] = tempData; // Payload ada di parts[4]

            // Simpan payload ke buffer khusus pencetakan
            if (!parts[4].isEmpty() && bufferIndex < BUFFER_SIZE) {
                payloadBuffer[bufferIndex - 1] = parts[4]; // Simpan hanya payload
            }
        }
    }

    // Kirim data penuh ke server
    if (client.connected() && shouldSendData && bufferIndex > 0) {
        String payload = payloadBuffer[bufferIndex - 1];
        Serial.printf("Sending data to server: %s\n", payload.c_str());
        if (client.println(payload)) {
            TxtKirim.setText(payload.c_str()); // Tampilkan data penuh
        } else {
            TxtKirim.setText("Send failed");
            Serial.println("Send failed");
            reconnect();
        }
    }

    // Periksa koneksi server
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
  // Ambil nilai jam dan rit dari Nextion
  char jam[20];  // Array untuk menyimpan nilai jam
  uint32_t rit;  // Menggunakan uint32_t untuk rit

  // Mengambil nilai jam dari TxtJam (NexText)
  TxtJam.getText(jam, sizeof(jam));

  // Mengambil nilai rit dari nRit (NexNumber)
  nRit.getValue(&rit);

  // Mengatur format pencetakan
  printer.justify('C');
  printer.setSize('M');
  printer.println(unitName);
  printer.println("--------------------------");
  
  printer.justify('L');
  printer.setSize('S');
  printer.println("TIME     RIT     PAYLOAD");
  printer.println("--------------------------");

  // Mencetak data dengan jam, rit, dan payload
  for (int i = 0; i < BUFFER_SIZE; i++) {
    if (!payloadBuffer[i].isEmpty()) {
      // Format untuk mencetak data yang lebih terstruktur
      printer.printf("%s    %d       %s\n", jam, rit, payloadBuffer[i].c_str());
    }
  }

  // Akhiri cetakan dan matikan printer
  printer.println("");
  printer.sleep();
}
