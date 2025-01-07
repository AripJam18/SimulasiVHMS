#include <Messages.h>
unsigned long previousMillis1 = 0;
const long interval1 = 5000;

const uint8_t ESCIT = 0x10;
const uint8_t STX = 2;
const uint8_t ETX = 3;
const uint8_t maxMsgLen = 100;

void setup() {
  Serial.begin(9600);
  Serial2.begin(9600);  // Initialize Serial2 if you're using it

  Serial.println(F("Starting..."));

  Messages::printMessage();
}

void loop() {
  Messages::sendMessage(Serial2, previousMillis1, interval1);
  getVHMS();
}

void data_V(const uint8_t* buffer, size_t len, bool chkOk) {
  dumpLine(buffer, len, chkOk);
  if (len == 20) {  // Panjang data yang valid adalah 20 byte
    String dataString = "";

    // Membuat array untuk menyimpan setiap part data
    String parts[6];

    // Menambahkan Front-Left suspension pressure
    parts[0] = getPressureValue(buffer, 3);
    dataString += parts[0];
    dataString += "-";

    // Menambahkan Front-Right suspension pressure
    parts[1] = getPressureValue(buffer, 5);
    dataString += parts[1];
    dataString += "-";

    // Menambahkan Rear-Left suspension pressure
    parts[2] = getPressureValue(buffer, 7);
    dataString += parts[2];
    dataString += "-";

    // Menambahkan Rear-Right suspension pressure
    parts[3] = getPressureValue(buffer, 9);
    dataString += parts[3];
    dataString += "-";

    // Menambahkan Payload
    parts[4] = getPayloadValue(buffer, 13);
    dataString += parts[4];
    dataString += "-";

    // Menambahkan Identitas Unit (Dumptruck)
    parts[5] = F("HD78101KM");
    dataString += parts[5];

    // Validasi apakah semua 6 bagian data ada
    bool valid = true;
    for (int i = 0; i < 5; i++) {  // Cek bagian 0 sampai 4 (sensor)
      if (parts[i].toFloat() <= 0) {  // Jika ada nilai 0 atau kurang, dianggap tidak valid
        valid = false;
        break;
      }
    }

    if (valid) {
      Serial.println(F("Data valid, mengirim ke ESP32:"));
      Serial.println(dataString);  // Kirim ke Serial Monitor
      Serial2.println(dataString);  // Kirim ke ESP32
    } else {
      Serial.println(F("Data tidak valid, tidak dikirim:"));
      Serial.println(dataString);
    }
  }
}


// Fungsi untuk mendapatkan nilai tekanan suspensi sebagai angka saja
String getPressureValue(const uint8_t* buffer, size_t pos) {
  uint16_t susP = buffer[pos] + (buffer[pos + 1] << 8);
  float pressureMPa = (susP * 0.1) * 0.0980665;  // Konversi ke MPa
  return String(pressureMPa, 2); // Format dengan 2 digit desimal
}

// Fungsi untuk mendapatkan nilai payload sebagai angka saja
String getPayloadValue(const uint8_t* buffer, size_t pos) {
  uint16_t pay = buffer[pos] + (buffer[pos + 1] << 8);
  float payloadValue = pay * 0.1; // Konversi payload
  return String(payloadValue, 1); // Format dengan 1 digit desimal
}

void dumpLine(const uint8_t* buffer, size_t len, bool chkOk) {
  Serial.println();
  if (len < 10) {
    Serial.write(' ');
  }
}

enum RxState {
  waitForSTX,
  collectUntilETX,
  waitForCRC,
};

void getVHMS() {
  static RxState rxs = waitForSTX;
  static bool nextCharLiteral = false;
  static uint8_t chkSum;
  static uint8_t bIndex;
  static uint8_t buffer[maxMsgLen + 1];
  if (Serial2.available()) {
    uint8_t inChar = Serial2.read();
    if (nextCharLiteral) {
      chkSum ^= inChar;
      buffer[bIndex++] = inChar;
      nextCharLiteral = false;
      return;
    } else if (inChar == ESCIT) {
      chkSum ^= inChar;
      nextCharLiteral = true;
      return;
    }
    switch (rxs) {
      case waitForSTX:
        if (inChar == STX) {
          bIndex = 0;
          chkSum = inChar;
          buffer[bIndex++] = inChar;
          rxs = collectUntilETX;
        }
        break;
      case collectUntilETX:
        chkSum ^= inChar;
        buffer[bIndex++] = inChar;
        if (inChar == ETX) {
          rxs = waitForCRC;
        }
        break;
      case waitForCRC:
        buffer[bIndex++] = inChar;

        data_V((const uint8_t*)(buffer + 1), bIndex - 3, inChar == chkSum);
        bIndex = 0;
        rxs = waitForSTX;
        break;
    }
  }
}


//output data yang diharapkan  : "3.25-3.50-2.75-3.00-10.5-HD78101KM"
