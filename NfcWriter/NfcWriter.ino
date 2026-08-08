#include <Wire.h>

const int SDA_PIN = 8;
const int SCL_PIN = 9;

<<<<<<< HEAD
=======
// Pin IRQ dan RESET
#define PN532_IRQ   (2)
#define PN532_RESET (3)

// Inisialisasi modul via I2C
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// URL yang mau dimasukkan ke dalam kartu (Diubah jadi char[] agar kompatibel)
char urlData[] = "linktr.ee/hacker_orca"; 

>>>>>>> branch 'main' of git@github.com:xiter00/Fmtxtest.git
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
<<<<<<< HEAD
  delay(1000);
=======
  Serial.println("\n--- PN532 NFC WRITER MEMULAI ---");
>>>>>>> branch 'main' of git@github.com:xiter00/Fmtxtest.git

<<<<<<< HEAD
  Wire.setPins(SDA_PIN, SCL_PIN);
  Wire.begin();
  Wire.setClock(100000);

  Serial.println("\nMulai scan I2C...");
=======
  Wire.begin(SDA_PIN, SCL_PIN);
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Gagal menemukan modul PN532! Cek kabel SDA SCL atau DIP Switch I2C.");
    while (1);
  }

  Serial.print("Chip PN5"); Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata >> 8) & 0xFF, DEC);

  nfc.SAMConfig();
  Serial.println("\nSIAP! Tempelkan kartu NFC lu ke modul PN532...");
>>>>>>> branch 'main' of git@github.com:xiter00/Fmtxtest.git
}

void loop() {
<<<<<<< HEAD
  byte error, address;
  int nDevices = 0;
=======
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;
>>>>>>> branch 'main' of git@github.com:xiter00/Fmtxtest.git

<<<<<<< HEAD
  Serial.println("Scanning...");
=======
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000);
>>>>>>> branch 'main' of git@github.com:xiter00/Fmtxtest.git

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

<<<<<<< HEAD
    if (error == 0) {
      Serial.print("Device ketemu di alamat 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println(" !");
      nDevices++;
    } else if (error == 4) {
      Serial.print("Error gak dikenal di alamat 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
=======
    if (uidLength == 7) {
      Serial.println("Tipe Kartu: NTAG2xx");
      Serial.println("Memulai proses format dan write NDEF (URL)...");
      
      uint8_t ndefWritten = nfc.ntag2xx_WriteNDEFURI(2, urlData, uidLength);
      
      if (ndefWritten) {
        Serial.println("[BERHASIL] Data sudah ditulis ke kartu NTAG!");
      } else {
        Serial.println("[GAGAL] Penulisan error atau kartu dilock (Read-Only).");
      }
>>>>>>> branch 'main' of git@github.com:xiter00/Fmtxtest.git
    }
<<<<<<< HEAD
=======
    else if (uidLength == 4) {
      Serial.println("Tipe Kartu: Mifare Classic");
      Serial.println("Memulai proses format kartu ke NDEF...");
      
      uint8_t keya[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, keya)) {
        if (nfc.mifareclassic_FormatNDEF()) {
          Serial.println("Format NDEF sukses! Menulis data URL...");
          
          if (nfc.mifareclassic_WriteNDEFURI(1, 2, urlData)) {
            Serial.println("[BERHASIL] Data sudah ditulis ke kartu Mifare Classic!");
          } else {
            Serial.println("[GAGAL] Gagal menulis URL ke kartu.");
          }
        } else {
          Serial.println("[GAGAL] Gagal memformat NDEF.");
        }
      } else {
        Serial.println("[GAGAL] Gagal autentikasi kartu.");
      }
    }
    else {
      Serial.println("Tipe kartu tidak didukung.");
    }
    
    Serial.println("==================================\n");
    delay(3000); 
>>>>>>> branch 'main' of git@github.com:xiter00/Fmtxtest.git
  }

  if (nDevices == 0) {
    Serial.println("Gak ada device I2C ketemu sama sekali.\n");
  } else {
    Serial.println("Selesai scan.\n");
  }

  delay(3000);
}
