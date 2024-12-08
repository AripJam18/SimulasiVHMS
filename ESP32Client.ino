#include <WiFi.h>
#include "Nextion.h"
#include <Wire.h>

const char* ssid = "ESP32-Server";
const char* password = "password123";
const char* host = "192.168.4.1";  // IP address of the server

#define RX1 4 // Pin untuk komunikasi dengan Arduino Mega
#define TX1 5
//HardwareSerial Serial1(1);

#define RX2 16 // Pin untuk komunikasi dengan Nextion
#define TX2 17

WiFiClient client;
String serialData = "";  // Variabel untuk menyimpan data dari Serial1
bool shouldSendData = false;  // Flag untuk mengatur apakah data harus dikirim
unsigned long startTime = 0;  // Untuk menghitung waktu autostop

// Objek Nextion untuk tombol dan teks
NexButton BtnStart = NexButton(0, 1, "BtnStart");
NexButton BtnStop = NexButton(0, 2, "BtnStop");
NexText TxtStatus = NexText(0, 3, "TxtStatus");
NexText TxtSSID = NexText(0, 4, "TxtSSID");
NexText TxtData = NexText(0, 5, "TxtData");
NexText TxtKirim = NexText(0, 6, "TxtKirim");

// Register tombol Nextion ke event list
NexTouch *nex_listen_list[] = {
  &BtnStart,
  &BtnStop,
  NULL
};

// Status state machine
enum State {
  IDLE,
  CONNECTING,
  TRANSMITTING,
  DISCONNECTED
};

State currentState = IDLE;  // Mulai dengan status IDLE

// Callback untuk tombol START
void BtnStartPopCallback(void *ptr) {
  Serial.println("BtnStartPopCallback");
  TxtSSID.setText("Connecting to WiFi...");
  currentState = CONNECTING;
  TxtStatus.setText("CONNECTING");
}

// Callback untuk tombol STOP
void BtnStopPopCallback(void *ptr) {
  Serial.println("BtnStopPopCallback");
  stopConnection();
}

// Fungsi reconnect ke server
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

// Fungsi untuk menghentikan koneksi
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
  
  // Arduino Mega
  Serial1.begin(9600, SERIAL_8N1, RX1, TX1);

  // Nextion
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2);
  nexInit();

  // Daftarkan event tombol ke callback
  BtnStart.attachPop(BtnStartPopCallback, &BtnStart);
  BtnStop.attachPop(BtnStopPopCallback, &BtnStop);

  TxtStatus.setText("System ready. Press START.");
  Serial.println("System ready.");
}

void loop() { 
  nexLoop(nex_listen_list);  // Cek event dari Nextion

  switch (currentState) {
    case IDLE:
      TxtStatus.setText("IDLE");
      break;

    case CONNECTING: {
      TxtStatus.setText("CONNECTING");
      Serial.println("Connecting to WiFi...");
      unsigned long wifiTimeout = millis();
      WiFi.begin(ssid, password);

      while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 10000) {  // Timeout 10 detik
        delay(1000);
        TxtSSID.setText("Connecting to WiFi...");
      }

      if (WiFi.status() == WL_CONNECTED) {
        TxtSSID.setText(WiFi.SSID().c_str());  // Tampilkan nama SSID yang terhubung
        Serial.printf("Connected to WiFi: %s\n", WiFi.SSID().c_str());
        TxtStatus.setText("TRANSMITTING");
        startTime = millis();
        shouldSendData = true;
        currentState = TRANSMITTING;  // Pindah ke state TRANSMITTING setelah berhasil
      } else {
        TxtSSID.setText("WiFi connection failed.");
        currentState = IDLE;  // Jika gagal, kembali ke state IDLE
        TxtStatus.setText("IDLE");
      }
      break;
    }

    case TRANSMITTING:
      TxtStatus.setText("TRANSMITTING");
      
      // Baca data dari Serial1
      if (Serial1.available()) {
        serialData = Serial1.readStringUntil('\n');
        if (!serialData.isEmpty()) {
          TxtData.setText(serialData.c_str());  // Tampilkan data yang diterima dari Arduino Mega
          Serial.printf("Received data: %s\n", serialData.c_str());
        }
      }

      // Kirim data ke server
      if (client.connected() && shouldSendData && !serialData.isEmpty()) {
        Serial.printf("Sending data to server: %s\n", serialData.c_str());
        if (client.println(serialData)) {
          TxtKirim.setText(serialData.c_str());  // Tampilkan data yang dikirim ke server
        } else {
          TxtKirim.setText("Send failed");
          Serial.println("Send failed");
          reconnect();  // Coba reconnect jika pengiriman gagal
        }
        serialData = "";  // Kosongkan buffer setelah pengiriman
      }

      // Cek koneksi server secara berkala
      static unsigned long lastCheck = 0;
      if (millis() - lastCheck > 5000) {  // Cek koneksi setiap 5 detik
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
  delay(1000);  // Tambahkan sedikit delay untuk mengurangi beban CPU
}

