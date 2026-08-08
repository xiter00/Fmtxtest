#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>

// --- TENTUKAN PIN I2C ESP32-S3 LU ---
const int SDA_PIN = 8;
const int SCL_PIN = 9;

// Pin IRQ dan RESET
#define PN532_IRQ   (2)
#define PN532_RESET (3)

// Inisialisasi modul via I2C
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// URL yang mau dimasukkan ke dalam kartu (Diubah jadi char[] agar kompatibel)
char urlData[] = "linktr.ee/hacker_orca"; 

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("\n--- PN532 NFC WRITER MEMULAI ---");

  // 1. Inisialisasi I2C pertama kali
  Wire.begin(SDA_PIN, SCL_PIN);

  // 2. Inisialisasi modul PN532 (PERHATIAN: Baris ini biasanya mereset pin Wire!)
  nfc.begin();

  // 3. PAKSA KEMBALI pin I2C ke pin custom lu (SDA 8, SCL 9) 
  // agar tidak tertimpa oleh setelan default library Adafruit
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000); // Set kecepatan I2C ke 100kHz agar stabil di modul murah

  // Cek apakah modul terdeteksi
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Gagal menemukan modul PN532! Cek kabel SDA SCL atau DIP Switch I2C.");
    while (1); // Berhenti di sini kalau gagal
  }

  // Tampilkan data modul
  Serial.print("Chip PN5"); Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata >> 8) & 0xFF, DEC);

  // Konfigurasi modul untuk siap baca/tulis kartu
  nfc.SAMConfig();

  Serial.println("\nSIAP! Tempelkan kartu NFC lu ke modul PN532...");
}


void loop() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;

  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000);

  if (success) {
    Serial.println("\n==================================");
    Serial.println("Kartu NFC Terdeteksi!");
    Serial.print("UID Kartu: ");
    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.print(uid[i], HEX); Serial.print(" ");
    }
    Serial.println("");

    if (uidLength == 7) {
      Serial.println("Tipe Kartu: NTAG2xx");
      Serial.println("Memulai proses format dan write NDEF (URL)...");
      
      uint8_t ndefWritten = nfc.ntag2xx_WriteNDEFURI(2, urlData, uidLength);
      
      if (ndefWritten) {
        Serial.println("[BERHASIL] Data sudah ditulis ke kartu NTAG!");
      } else {
        Serial.println("[GAGAL] Penulisan error atau kartu dilock (Read-Only).");
      }
    }
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
  }
}
