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

// Constants for start frame and end frame
#define STX 2  // Start of Text
#define ETX 3  // End of Text

// Buffers and variables
#define BUFFER_SIZE 10
String payloadBuffer[BUFFER_SIZE];
String buffer = "";  // Buffer untuk menampung data sementara
int bufferIndex = 0;
bool shouldSendData = false;
unsigned long startTime = 0;

// Unit name for the printer header
const char* unitName = "HD78140KM";

// Nextion components
NexButton BtnStart = NexButton(0, 1, "BtnStart");
NexButton BtnStop = NexButton(0, 2, "BtnStop");
NexButton BtnScan = NexButton(0, 25, "BtnScan");
NexCombo CmbSSID = NexCombo(0, 30, "CmbSSID");
NexText TxtStatus = NexText(0, 28, "TxtStatus");
NexText TxtSSID = NexText(0, 29, "TxtSSID");
NexText TxtData = NexText(0, 10, "TxtData");
NexText TxtKirim = NexText(0, 12, "TxtKirim");
NexText TxtJam = NexText(0, 3, "TxtJam ");
NexNumber nRit = NexNumber(0, 27, "nRit");

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
void BtnScanPopCallback(void *ptr) {
  Serial.println("BtnScanPopCallback");

  TxtStatus.setText("Scanning for SSIDs...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    TxtStatus.setText("No networks found.");
    return;
  }

  String SSIDs = "";
  for (int i = 0; i < n; ++i) {
    if (i > 0) SSIDs += "\r\n";
    SSIDs += WiFi.SSID(i);
    Serial.println(WiFi.SSID(i));  // Debugging
  }

  String cmdTxt = String("CmbSSID.txt=\"") + String(n) + " Networks\"";
  sendCommand(cmdTxt.c_str());

  String cmdPath = String("CmbSSID.path=\"") + SSIDs + "\"";
  sendCommand(cmdPath.c_str());

  if (!recvRetCommandFinished()) {
    Serial.println("Error updating ComboBox.");
    TxtStatus.setText("Error updating ComboBox.");
    return;
  }

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
}

void BtnStopPopCallback(void *ptr) {
  Serial.println("BtnStopPopCallback");
  stopConnection();
  // Hapus atau ganti dengan kode untuk menampilkan buffer
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

  Serial1.begin(9600, SERIAL_8N1, RX1, TX1);
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2);
  nexInit();

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

      // Cek apakah ada data yang tersedia di Serial1
      while (Serial1.available()) {
        char c = Serial1.read();  // Membaca satu karakter dari Serial1

        if (c == STX) {  // Start frame ditemukan, reset buffer
          buffer = "";  // Deklarasikan buffer di sini
        } else if (c == ETX) {  // End frame ditemukan, proses data
          processData(buffer);
          buffer = "";  // Reset buffer setelah memproses
        } else {
          buffer += c;  // Tambahkan karakter ke buffer
        }
      }

      // Kirim data yang valid ke server jika buffer tidak kosong
      if (client.connected() && bufferIndex > 0) {
        String payload = payloadBuffer[bufferIndex - 1];
        Serial.printf("Sending data to server: %s\n", payload.c_str());
        if (client.println(payload)) {
          TxtKirim.setText(payload.c_str());  // Tampilkan data yang dikirim
        } else {
          TxtKirim.setText("Send failed");
          Serial.println("Send failed");
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

// Fungsi untuk memproses data yang diterima dari buffer
void processData(String data) {
  // Validasi data berdasarkan format yang diinginkan
  if (data.startsWith("#") && data.endsWith("#HD78101KM*")) {
    // Data valid
    Serial.println("Data valid: " + data);
    TxtData.setText(data.c_str());  // Tampilkan data valid di TxtData

    // Simpan data penuh untuk pengiriman ke server
    if (bufferIndex < BUFFER_SIZE) {
      payloadBuffer[bufferIndex++] = data;
    } else {
      // Geser buffer jika penuh
      for (int i = 1; i < BUFFER_SIZE; i++) {
        payloadBuffer[i - 1] = payloadBuffer[i];
      }
      payloadBuffer[BUFFER_SIZE - 1] = data;
    }
  } else {
    // Data tidak valid
    Serial.println("Data tidak valid: " + data);
  }
}

// Fungsi tambahan untuk menampilkan data terakhir dari buffer (jika diinginkan)
void printLast10Data() {
  // Pengaturan font dan perataan
  printer.justify('C');
  printer.setSize('M');
  printer.println(unitName);
  printer.println("--------------------------");
  printer.justify('L');
  printer.setSize('S');
  
  // Header untuk tabel
  printer.println("TIME     RIT     PAYLOAD");
  printer.println("--------------------------");

  // Menampilkan data terakhir yang ada di buffer
  for (int i = 0; i < BUFFER_SIZE; i++) {
    if (!payloadBuffer[i].isEmpty()) {
      // Mencetak data dengan format yang diinginkan
      printer.printf("09:00    1       %s\n", payloadBuffer[i].c_str());
    }
  }

  printer.println("");  // Menambahkan baris kosong setelah data
  printer.sleep();      // Mematikan printer setelah mencetak
}


//tambah pemeriksaan data valid dan tidak valid dengan adanya STK dan ETK
