#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>

// --- TENTUKAN PIN I2C ESP32-S3 LU ---
const int SDA_PIN = 8;
const int SCL_PIN = 9;

// Pin IRQ dan RESET sebenarnya ada di modul PN532,
// Tapi kalau lu cuma pasang SDA SCL VCC GND, pin ini bisa di-set ke dummy pin.
#define PN532_IRQ   (2)
#define PN532_RESET (3)

// Inisialisasi modul via I2C
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// URL yang mau dimasukkan ke dalam kartu (GANTI PAKAI LINK LU)
// Contoh: linktr.ee/namalu, instagram.com/namalu, dll
const char * urlData = "linktr.ee/hacker_orca"; 

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10); // Tunggu serial monitor aktif
  Serial.println("\n--- PN532 NFC WRITER MEMULAI ---");

  // Inisialisasi I2C ESP32-S3
  Wire.begin(SDA_PIN, SCL_PIN);

  nfc.begin();

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
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer buat nyimpen ID kartu
  uint8_t uidLength;                        // Panjang ID (4 bytes buat Mifare, 7 bytes buat NTAG)

  // Tunggu sampai ada kartu yang nempel (timeout 1 detik biar nggak ngehang)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000);

  if (success) {
    Serial.println("\n==================================");
    Serial.println("Kartu NFC Terdeteksi!");
    Serial.print("UID Kartu: ");
    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.print(uid[i], HEX); Serial.print(" ");
    }
    Serial.println("");

    // --- LOGIKA PENULISAN NDEF ---

    // 1. Jika Kartu Jenis NTAG2xx (Biasanya UID 7 Byte - Ini kartu stiker/modern)
    if (uidLength == 7) {
      Serial.println("Tipe Kartu: NTAG2xx");
      Serial.println("Memulai proses format dan write NDEF (URL)...");
      
      // Tulis URL (1 = Prefix "http://www.", 2 = Prefix "https://", 3 = "http://", 4 = "https://www.")
      // Karena linktr.ee/xxx biasanya pakai https://, kita pakai prefix 2 atau 4.
      uint8_t ndefWritten = nfc.ntag2xx_WriteNDEFURI(2, urlData, uidLength);
      
      if (ndefWritten) {
        Serial.println("[BERHASIL] Data sudah ditulis ke kartu NTAG!");
        Serial.println("Cabut kartu, lalu tempelkan ke belakang HP lu buat ngetes.");
      } else {
        Serial.println("[GAGAL] Penulisan error atau kartu dilock (Read-Only).");
      }
    }
    
    // 2. Jika Kartu Jenis Mifare Classic 1K (Biasanya UID 4 Byte - Kartu putih tebal/KTM)
    else if (uidLength == 4) {
      Serial.println("Tipe Kartu: Mifare Classic");
      Serial.println("Memulai proses format kartu ke NDEF...");
      
      // Autentikasi default key A
      uint8_t keya[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, keya)) {
        
        // Coba format jadi NDEF dulu
        if (nfc.mifareclassic_FormatNDEF()) {
          Serial.println("Format NDEF sukses! Menulis data URL...");
          
          // Tulis URL (Prefix sama seperti di atas)
          if (nfc.mifareclassic_WriteNDEFURI(1, 2, urlData)) {
            Serial.println("[BERHASIL] Data sudah ditulis ke kartu Mifare Classic!");
            Serial.println("Cabut kartu, lalu tempelkan ke belakang HP lu buat ngetes.");
          } else {
            Serial.println("[GAGAL] Gagal menulis URL ke kartu.");
          }
        } else {
          Serial.println("[GAGAL] Gagal memformat NDEF (mungkin bukan kartu kosong).");
        }
      } else {
        Serial.println("[GAGAL] Gagal autentikasi kartu (Password/Key salah).");
      }
    }
    
    else {
      Serial.println("Tipe kartu tidak didukung oleh script ini.");
    }
    
    Serial.println("==================================\n");
    
    // Tunggu lu nyabut kartunya biar nggak spam nulis terus-terusan
    delay(3000); 
  }
}
