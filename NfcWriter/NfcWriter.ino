#include <Wire.h>

const int SDA_PIN = 8;
const int SCL_PIN = 9;
const uint8_t TEST_ADDR = 0x28;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(1000);

  Wire.setPins(SDA_PIN, SCL_PIN);
  Wire.begin();
  Wire.setClock(100000);

  Serial.println("\nKirim command GetFirmwareVersion ke alamat 0x28...");

  // Frame command GetFirmwareVersion (sesuai PN532 user manual):
  // 00 00 FF 02 FE D4 02 2A 00
  uint8_t cmd[] = {0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00};

  Wire.beginTransmission(TEST_ADDR);
  Wire.write(cmd, sizeof(cmd));
  uint8_t writeResult = Wire.endTransmission();

  Serial.print("Hasil write (0 = sukses): ");
  Serial.println(writeResult);

  delay(50);

  Serial.println("Membaca balasan mentah (20 byte)...");
  Wire.requestFrom((int)TEST_ADDR, 20);
  delay(10);

  int i = 0;
  while (Wire.available()) {
    uint8_t b = Wire.read();
    Serial.print("byte[");
    Serial.print(i++);
    Serial.print("] = 0x");
    if (b < 16) Serial.print("0");
    Serial.println(b, HEX);
  }

  if (i == 0) {
    Serial.println("Gak ada data balik sama sekali.");
  }

  Serial.println("\n--- Selesai, cek pola byte di atas ---");
  Serial.println("Pola PN532 asli biasanya diawali: 0x01 (ready byte, khusus I2C) lalu 00 00 FF 06 FA D5 03 ...");
}

void loop() {
  delay(5000);
}
