# TESFMTX — ESP32-S3 + Elechouse KT0803L FM Transmitter

Kontrol modul FM transmitter Elechouse (chip **KT0803L**) pakai ESP32-S3, lewat
WiFi hotspot + halaman web lokal. Tidak perlu install app tambahan, tinggal
connect WiFi lalu buka browser.

## Wiring

| Modul KT0803L | ESP32-S3 |
|---|---|
| SDA | GPIO 35 |
| SCL | GPIO 36 |
| VCC | 3.3V |
| GND | GND |

> KT0803L itu device 3.3V. Pastikan modul kamu memang diberi suplai 3.3V
> (banyak modul elechouse sudah ada regulator onboard, tapi cek dulu).

Jack audio pada modul (line-in) terhubung langsung secara analog ke pin
INL/INR chip — ini di luar kendali firmware. Firmware ini cuma mengatur sisi
digital KT0803L: frekuensi, mute, mono/stereo, PGA (gain audio), dan RF gain
(kuat pancar).

Komponen kecil-panjang bertuliskan **"EACD"** di sebelah IC kemungkinan besar
adalah crystal/resonator referensi clock untuk KT0803L — bukan sesuatu yang
diatur lewat software.

## Cara pakai setelah firmware ter-flash

1. ESP32-S3 otomatis bikin hotspot:
   - SSID: `TESFMTX`
   - Password: `testingfm123`
2. Connect HP/laptop ke hotspot itu.
3. Buka browser ke `http://192.168.4.1`.
4. Atur frekuensi (76.0–108.0 MHz), mute, mono/stereo, PGA, dan RF gain.
   Semua setting otomatis tersimpan ke flash (NVS), jadi tetap kepakai
   walau ESP32 di-restart.
5. Colok sumber audio ke jack modul, nyalakan radio FM di frekuensi yang
   sama untuk tes terima siarannya.

## Build via GitHub Actions

Workflow `.github/workflows/main.yml` otomatis:

1. Compile sketch `TESFMTX/TESFMTX.ino` untuk board **ESP32-S3** (fqbn
   `esp32:esp32:esp32s3`), sekalian install library **KT0803** (by Rob
   Tillaart) dari Library Manager.
2. Menghasilkan dua artifact di tab **Actions → run terakhir → Artifacts**:
   - `TESFMTX-build-output` — semua file hasil compile (bootloader,
     partition table, `.bin` aplikasi, `.elf`, `.map`).
   - `TESFMTX-merged-flash-bin` — **satu file `.bin` gabungan** yang sudah
     berisi bootloader + partition table + boot_app0 + aplikasi pada offset
     yang benar, siap di-flash ke `0x0` langsung (plug and play, tidak perlu
     mikirin banyak offset).

Trigger workflow dengan push ke branch `main`/`master`, atau jalankan manual
lewat tab **Actions → Build TESFMTX Firmware (ESP32-S3) → Run workflow**.

## Cara flash hasil build

### Opsi A — pakai file merged (paling gampang)

```bash
pip install esptool
esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x0 TESFMTX-merged-flash.bin
```

Ganti `/dev/ttyUSB0` sesuai port ESP32-S3 kamu (di Windows biasanya
`COM3`, `COM4`, dst).

### Opsi B — pakai Arduino IDE

Kalau mau lebih gampang lagi tanpa CLI: buka `TESFMTX/TESFMTX.ino` di
Arduino IDE, install library **KT0803** (Rob Tillaart) lewat Library
Manager, pilih board **ESP32S3 Dev Module**, lalu klik **Upload**.

## Library yang dipakai

- [KT0803 by Rob Tillaart](https://github.com/RobTillaart/KT0803) — support
  KT0803 / KT0803K / KT0803L / KT0803M lewat class `KT0803L`.
