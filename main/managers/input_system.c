#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "globals.h"
#include "wifi_sys.h"
#include "ota_sys.h"
#include "pin_system.h"

// --- Extern dari display_system.c ---
extern int storeGetTotal(int kat, int sub);
extern void storeSetupField(int kat, int sub);
extern void setOledBrightness(uint8_t level);
extern const int totalSubKat[];

// ==========================================================
// CHARSET — 2 MODE KEYBOARD
// ANGKA: cycling 0-9 (10 karakter) — default, super cepat buat nomor
// HURUF: A-Z + simbol (58 karakter) — buat email, nama, dll
// ==========================================================
// Charset didefinisikan lokal di handleStoreInput mode 5

uint32_t ms() { return (uint32_t)(esp_timer_get_time() / 1000); }

// Forward
void handleStoreInput(int btn);

// ==========================================================
// BUILD targetID
// ML (ada Zone ID): "idGame|zoneID"
// Lainnya         : langsung value
// Dipanggil saat transisi layar 6 → layar 9
// ==========================================================
void buildTargetID() {
    memset(targetID, 0, sizeof(targetID));
    if (totalField == 2) {
        // Ada dua field (contoh: ML = User ID + Zone ID)
        snprintf(targetID, sizeof(targetID), "%s|%s",
                 field[0].value, field[1].value);
    } else {
        snprintf(targetID, sizeof(targetID), "%s", field[0].value);
    }
}

// ==========================================================
// SELESAI INPUT PIN — dipanggil begitu buffer PIN_LEN digit
// penuh, ATAU pas user TAHAN OK buat "selesai" lebih awal.
// Beda perlakuan tergantung appMode PIN yang lagi aktif:
//  30 = PIN transaksi (TF/BAYAR)   -> verifikasi, sukses ke layar 11
//  31 = PIN lama (Edit PIN)        -> verifikasi, sukses ke layar 32
//  32 = PIN baru (Edit PIN)        -> gak ada verifikasi, langsung simpan
// ==========================================================
static void pinHandleSelesai(void) {
    if (appMode == 32) {
        // PIN baru: gak dicek benar/salah, siapapun isinya langsung disimpan.
        // Kalau kepencet SELESAI sebelum genap PIN_LEN digit, jangan simpan
        // dulu — user masih ngetik.
        if (strlen(pinBuf) != PIN_LEN) return;
        pin_change(pinBuf);
        memset(pinBuf, 0, sizeof(pinBuf));
        pinPrev = 0;
        appMode = 35;
        return;
    }

    // appMode 30 / 31: perlu digit lengkap biar bisa dicocokkan sama PIN
    // yang tersimpan (pin_verify udah nolak sendiri kalau lockout aktif).
    if (strlen(pinBuf) != PIN_LEN) return;

    bool ok = pin_verify(pinBuf);
    memset(pinBuf, 0, sizeof(pinBuf));
    pinPrev = 0;

    if (!ok) return; // Salah / lagi lockout -> tetap di layar yang sama

    if (appMode == 30) {
        // PIN transaksi benar -> eksekusi TF/BAYAR
        // TODO: panggil API H2H di sini. Hasil API -> appMode 11 (berhasil)
        // atau 12 (gagal). Untuk placeholder, langsung ke TRX Berhasil:
        appMode = 11;
    } else if (appMode == 31) {
        appMode = 32; // PIN lama benar -> lanjut input PIN baru
    }
}

// ==========================================================
// HANDLE JOYSTICK — ENTRY POINT
// ==========================================================
void handleJoystick() {
    static uint32_t lastPress = 0;
    uint32_t now = ms();

    // ---- LAYAR 5: AUTO-REPEAT RIGHT + TAP/TAHAN OK + debounce LEFT ----
    // Tahan RIGHT → cycling makin cepat (600ms=100ms/step, 1200ms=50ms/step)
    // OK: tap = tambah karakter, tahan ≥450ms = selesai (gak nambah karakter nyasar)
if (appMode == 5) {
        static uint32_t tHold   = 0;
        static bool     hold    = false;
        static uint32_t tRep    = 0;
        static uint32_t lastOL  = 0;  // Debounce noise OK & LEFT
        static bool     oPrev   = false;  // status OK frame sebelumnya
        static bool     lPrev   = false;  // status LEFT frame sebelumnya

        bool rDown = (gpio_get_level(PIN_RIGHT) == 0);
        bool lDown = (gpio_get_level(PIN_LEFT)  == 0);
        bool oDown = (gpio_get_level(PIN_OK)    == 0);

        if (rDown) {
            // --- Auto-repeat RIGHT ---
            if (!hold) {
                if (now - lastPress < 120) return;
                hold = true; tHold = now; tRep = now;
                lastPress = now;
                handleStoreInput(BTN_RIGHT);
            } else {
                // Ramp lebih agresif: mulai repeat lebih cepet (300ms,
                // bukan 600ms nunggu) dan step makin rapet sampai 25ms
                // di puncaknya biar geser karakter kerasa jauh lebih gesit.
                uint32_t d  = now - tHold;
                uint32_t iv = (d > 900) ? 25 : (d > 500) ? 50 : (d > 300) ? 90 : 0;
                if (iv > 0 && now - tRep >= iv) {
                    tRep = now;
                    handleStoreInput(BTN_RIGHT);
                }
            }
            return;
        } else {
            hold = false;
        }

        // --- OK: TAP = tambah karakter | TAHAN (≥450ms) = SELESAI, gak nambah apa-apa ---
        // --- LEFT: tetap sekali per penekanan (rising edge) ---
        static uint32_t tOKDown = 0;
        static bool     okLong  = false;
        #define OK_LONG_MS 450

        if (oDown) {
            if (!oPrev) {
                tOKDown = now;      // OK baru mulai ditekan
                okLong  = false;
            } else if (!okLong && (now - tOKDown >= OK_LONG_MS)) {
                // Ditahan cukup lama → SELESAI langsung, TANPA nambah karakter
                okLong    = true;
                charIdx   = 0;
                appMode   = 4;
                lastPress = now;   // cegah tombol yg masih ketahan kepencet ganda di mode 4
            }
        } else if (oPrev && !okLong) {
            // Dilepas sebelum jadi TAHAN → itu tap biasa → tambah 1 karakter
            handleStoreInput(BTN_OK);
        }
        oPrev = oDown;

        bool lPressed = lDown && !lPrev;
        lPrev = lDown;
        if (lPressed && (now - lastOL >= 150)) {  // redam noise mekanik
            lastOL = now;
            handleStoreInput(BTN_LEFT);
        }
        return;
    }

    // ---- LAYAR 14: PASSWORD WIFI — sama kayak LAYAR 5, TAP/TAHAN OK ----
    // OK: tap = tambah karakter | tahan ≥450ms = SUBMIT & connect (gak nambah
    // karakter nyasar pas submit — ini yang bikin password ke-input salah)
    else if (appMode == 14) {
        // Sama persis kayak LAYAR 5: charset ganti-gantian ANGKA/HURUF
        // tergantung `inputAngka`, bukan 1 charset gabungan tetap lagi.
        const char *csW    = inputAngka ? CS_ANGKA : CS_HURUF;
        const int   csWLen = inputAngka ? CS_ANGKA_LEN : CS_HURUF_LEN;

        static uint32_t tHold14   = 0;
        static bool     hold14    = false;
        static uint32_t tRep14    = 0;
        static uint32_t tOKDown14 = 0;
        static bool     okLong14  = false;
        static uint32_t lastOL14  = 0;
        static bool     oPrev14   = false;
        static bool     lPrev14   = false;
        static uint32_t tL14dbl   = 0;

        bool rDown = (gpio_get_level(PIN_RIGHT) == 0);
        bool lDown = (gpio_get_level(PIN_LEFT)  == 0);
        bool oDown = (gpio_get_level(PIN_OK)    == 0);

        if (rDown) {
            // --- Auto-repeat RIGHT, sama kayak LAYAR 5 ---
            if (!hold14) {
                if (now - lastPress < 120) return;
                hold14 = true; tHold14 = now; tRep14 = now;
                lastPress = now;
                charIdx = (charIdx + 1) % csWLen;
            } else {
                // Sama kayak layar 5: ramp lebih agresif biar gak kerasa delay
                // dan geser charset password jadi lebih cepet.
                uint32_t d  = now - tHold14;
                uint32_t iv = (d > 900) ? 25 : (d > 500) ? 50 : (d > 300) ? 90 : 0;
                if (iv > 0 && now - tRep14 >= iv) {
                    tRep14 = now;
                    charIdx = (charIdx + 1) % csWLen;
                }
            }
            return;
        } else {
            hold14 = false;
        }

        // --- OK: TAP = tambah karakter | TAHAN = SUBMIT connect ---
        if (oDown) {
            if (!oPrev14) {
                tOKDown14 = now;
                okLong14  = false;
            } else if (!okLong14 && (now - tOKDown14 >= OK_LONG_MS)) {
                // Ditahan cukup lama → langsung connect, TANPA nambah karakter
                okLong14  = true;
                charIdx   = 0;
                wifi_connect_selected();
                appMode   = 15;
                lastPress = now;
            }
        } else if (oPrev14 && !okLong14) {
            // Tap biasa → tambah 1 karakter ke password
            int l = strlen(wifiPassBuf);
            if (l < 63) {
                wifiPassBuf[l]   = csW[charIdx % csWLen];
                wifiPassBuf[l+1] = '\0';
                charIdx = 0;
            }
        }
        oPrev14 = oDown;

        // --- LEFT: 1x hapus char terakhir (kosong = ganti mode ANGKA/HURUF,
        // sama kayak LAYAR 5) | 2x cepat = batal (balik ke list) ---
        bool lPressed = lDown && !lPrev14;
        lPrev14 = lDown;
        if (lPressed && (now - lastOL14 >= 150)) {
            lastOL14 = now;
            bool dbl = (now - tL14dbl < 400) && tL14dbl;
            tL14dbl = now;
            if (dbl) {
                memset(wifiPassBuf, 0, sizeof(wifiPassBuf));
                charIdx = 0;
                appMode = 13;
            } else {
                int l = strlen(wifiPassBuf);
                if (l > 0) {
                    wifiPassBuf[l-1] = '\0';
                    charIdx = 0;
                } else {
                    inputAngka = !inputAngka;
                    charIdx    = 0;
                }
            }
        }
        return;
    }

    // ---- LAYAR 30/31/32: INPUT PIN — sama persis mekanismenya kayak
    // LAYAR 14 (password WiFi) / LAYAR 5 (input ID), cuma charset-nya
    // dikunci ANGKA (CS_ANGKA) doang karena PIN emang cuma angka.
    // > tahan = auto-repeat geser angka makin cepat
    // OK tap  = tambah 1 digit ke pinBuf
    // OK tahan (>=450ms) = SELESAI: coba verifikasi/simpan sekarang juga
    //           (kalau belum genap PIN_LEN digit, gak ngapa2in — nunggu
    //           digit lengkap dulu, lihat pinHandleSelesai())
    // < (ada isi) = hapus 1 digit terakhir
    // < (kosong)  = BATAL, keluar dari layar PIN
    // Auto-submit juga tetap jalan begitu digit ke-PIN_LEN masuk lewat tap.
    else if (appMode == 30 || appMode == 31 || appMode == 32) {
        static uint32_t tHoldP  = 0;
        static bool     holdP   = false;
        static uint32_t tRepP   = 0;
        static uint32_t lastOLP = 0;
        static bool     oPrevP  = false;
        static bool     lPrevP  = false;
        static uint32_t tOKDownP = 0;
        static bool     okLongP  = false;

        bool locked = pin_is_locked(NULL) && appMode != 32; // 32 (PIN baru) gak kena lockout

        bool rDown = (gpio_get_level(PIN_RIGHT) == 0);
        bool lDown = (gpio_get_level(PIN_LEFT)  == 0);
        bool oDown = (gpio_get_level(PIN_OK)    == 0);

        if (rDown) {
            // --- Auto-repeat RIGHT, sama kayak LAYAR 5/14 ---
            if (!locked) {
                if (!holdP) {
                    if (now - lastPress < 120) return;
                    holdP = true; tHoldP = now; tRepP = now;
                    lastPress = now;
                    pinPrev = (pinPrev + 1) % CS_ANGKA_LEN;
                } else {
                    uint32_t d  = now - tHoldP;
                    uint32_t iv = (d > 900) ? 25 : (d > 500) ? 50 : (d > 300) ? 90 : 0;
                    if (iv > 0 && now - tRepP >= iv) {
                        tRepP = now;
                        pinPrev = (pinPrev + 1) % CS_ANGKA_LEN;
                    }
                }
            }
            return;
        } else {
            holdP = false;
        }

        // --- OK: TAP = tambah digit | TAHAN (>=450ms) = SELESAI ---
        // (diabaikan total selagi lockout — gak boleh coba masukin apa2)
        if (oDown) {
            if (!oPrevP) {
                tOKDownP = now;
                okLongP  = false;
            } else if (!locked && !okLongP && (now - tOKDownP >= OK_LONG_MS)) {
                okLongP   = true;
                lastPress = now;
                pinHandleSelesai();
            }
        } else if (oPrevP && !okLongP && !locked) {
            // Tap biasa -> tambah 1 digit
            int l = strlen(pinBuf);
            if (l < PIN_LEN) {
                pinBuf[l]   = CS_ANGKA[pinPrev % CS_ANGKA_LEN];
                pinBuf[l+1] = '\0';
                pinPrev = 0;
                l++;
            }
            if (l == PIN_LEN) pinHandleSelesai(); // Auto-submit pas genap
        }
        oPrevP = oDown;

        // --- LEFT: hapus 1 digit terakhir | kosong = BATAL ---
        // (LEFT tetap boleh dipencet walau lockout — biar user tetap bisa
        // keluar/batal dari layar PIN sambil nunggu, cuma gak bisa nginput.)
        bool lPressed = lDown && !lPrevP;
        lPrevP = lDown;
        if (lPressed && (now - lastOLP >= 150)) {
            lastOLP = now;
            int l = strlen(pinBuf);
            if (l > 0) {
                pinBuf[l-1] = '\0';
                pinPrev = 0;
            } else {
                // Buffer kosong -> batal, balik ke layar sebelumnya
                memset(pinBuf, 0, sizeof(pinBuf));
                pinPrev = 0;
                if (appMode == 30) {
                    appMode = 9;  // Batal PIN transaksi -> balik pilih pembayaran
                } else {
                    // 31 (verifikasi PIN lama) atau 32 (isi PIN baru) -> Settings
                    appMode = 0; diSubMenu = true; katKursor = 3; subKursor = 6;
                }
            }
        }
        return;
    }

    // ---- CONNECTING (mode 15) — auto-transition, HARUS dicek tiap loop ----
    // Jangan taruh di bawah "if (btn == BTN_NONE) return;" karena kalau user
    // gak pencet tombol pas nunggu konek, status connected/failed gak pernah
    // kecek dan layar "Menghubungkan..." bakal nyangkut terus.
    if (appMode == 15) {
        if (wifiStatus == WIFI_STATUS_CONNECTED) appMode = 16;
        else if (wifiStatus == WIFI_STATUS_FAILED) appMode = 17;
        return;
    }

    // ---- OTA: CEK VERSI (mode 20) — auto-transition, sama kayak mode 15 ----
    if (appMode == 20) {
        if      (otaState == OTA_ST_NO_UPDATE)        appMode = 21;
        else if (otaState == OTA_ST_UPDATE_AVAILABLE)  appMode = 22;
        else if (otaState == OTA_ST_CHECK_FAILED)      appMode = 25;
        return;
    }

    // ---- OTA: DOWNLOADING (mode 23) — auto-transition ke sukses/gagal ----
    // Sukses bakal reboot sendiri dari task-nya, tapi tetep dicek di sini
    // buat jaga-jaga / nampilin state gagal.
    if (appMode == 23) {
        if (otaState == OTA_ST_FAILED) appMode = 25;
        return;
    }

    // ---- Debounce normal untuk semua mode lain ----
    if (now - lastPress < 250) return;

    int btn = BTN_NONE;
    if      (gpio_get_level(PIN_LEFT)  == 0) btn = BTN_LEFT;
    else if (gpio_get_level(PIN_RIGHT) == 0) btn = BTN_RIGHT;
    else if (gpio_get_level(PIN_OK)    == 0) btn = BTN_OK;
    if (btn == BTN_NONE) return;
    lastPress = now;

    // Delegasi ke store handler (layar 2-6, 9-12, dan konfirmasi PIN 35)
    if ((appMode >= 2 && appMode <= 6) || (appMode >= 9 && appMode <= 12) ||
        appMode == 35) {
        handleStoreInput(btn); return;
    }

    // ---- BRIGHTNESS (layar 1) ----
    if (appMode == 1) {
        if      (btn == BTN_LEFT)  { appMode=0; diSubMenu=true; katKursor=3; subKursor=0; }
        else if (btn == BTN_RIGHT) {
            kecerahan = (kecerahan + 20 > 255) ? 0 : kecerahan + 20;
            setOledBrightness((uint8_t)kecerahan);
        } else if (btn == BTN_OK) {
            kecerahan = (kecerahan - 20 < 0) ? 255 : kecerahan - 20;
            setOledBrightness((uint8_t)kecerahan);
        }
        return;
    }
    // ---- ABOUT (layar 7) ----
    if (appMode == 7) {
        if (btn == BTN_LEFT) { appMode=0; diSubMenu=true; katKursor=3; subKursor=1; }
        return;
    }

    // ---- REBOOT (layar 8) ----
    if (appMode == 8) {
        if      (btn == BTN_LEFT) { appMode=0; diSubMenu=true; katKursor=3; subKursor=2; }
        else if (btn == BTN_OK)   { esp_restart(); }
        return;
    }

    // ---- OTA: TIDAK ADA UPDATE (layar 21) — tombol kiri doang buat balik ----
    if (appMode == 21) {
        if (btn == BTN_LEFT) { appMode=0; diSubMenu=true; katKursor=3; subKursor=5; }
        return;
    }

    // ---- OTA: ADA UPDATE (layar 22) — < batal | OK mulai update ----
    if (appMode == 22) {
        if      (btn == BTN_LEFT) { appMode=0; diSubMenu=true; katKursor=3; subKursor=5; }
        else if (btn == BTN_OK)   { ota_update_start(); appMode=23; }
        return;
    }

    // ---- OTA: GAGAL (layar 25) — tombol kiri doang buat balik ----
    if (appMode == 25) {
        if (btn == BTN_LEFT) { appMode=0; diSubMenu=true; katKursor=3; subKursor=5; }
        return;
    }


    // ---- WIFI LIST (mode 13) ----
    if (appMode == 13) {
        if (wifiStatus == WIFI_STATUS_SCANNING) return;
        if (btn == BTN_LEFT) {
            if (wifiKursor > 0) {
                wifiKursor--;
                if (wifiKursor < wifiScroll) wifiScroll--;
            } else {
                appMode = 0; diSubMenu = true; katKursor = 3; subKursor = 3;
            }
        } else if (btn == BTN_RIGHT) {
            if (wifiKursor < wifiTotal - 1) {
                wifiKursor++;
                if (wifiKursor >= wifiScroll + 3) wifiScroll++;
            }
        } else if (btn == BTN_OK && wifiTotal > 0) {
            if (wifiList[wifiKursor].has_pass) {
                char saved[64];
                if (wifi_saved_lookup(wifiList[wifiKursor].ssid, saved, sizeof(saved))) {
                    // Password buat SSID ini udah tersimpan — langsung
                    // connect, gak perlu ngetik ulang tiap kali.
                    strncpy(wifiPassBuf, saved, sizeof(wifiPassBuf) - 1);
                    wifiPassBuf[sizeof(wifiPassBuf) - 1] = '\0';
                    wifi_connect_selected();
                    appMode = 15;
                } else {
                    memset(wifiPassBuf, 0, sizeof(wifiPassBuf));
                    charIdx    = 0;
                    inputAngka = true; // Reset ke mode ANGKA tiap mulai ngetik baru
                    appMode    = 14;
                }
            } else {
                wifiPassBuf[0] = '\0';
                wifi_connect_selected();
                appMode = 15;
            }
        }
        return;
    }

    // ---- WIFI SUKSES (mode 16) ----
    if (appMode == 16) {
        if (btn == BTN_LEFT) {
            appMode = 0; diSubMenu = true; katKursor = 3; subKursor = 3;
        }
        return;
    }

    // ---- WIFI GAGAL (mode 17) ----
    if (appMode == 17) {
        if (btn == BTN_LEFT) {
            // Balik ke list, scan ulang
            wifi_scan_start();
            appMode = 13;
        }
        return;
    }

    // ---- SAVED WIFI LIST (mode 18) ----
    // > geser | < kembali ke Settings | OK lihat detail (password + hapus)
    if (appMode == 18) {
        int cnt = wifi_saved_count();
        if (btn == BTN_LEFT) {
            if (savedWifiKursor > 0) {
                savedWifiKursor--;
                if (savedWifiKursor < savedWifiScroll) savedWifiScroll--;
            } else {
                appMode = 0; diSubMenu = true; katKursor = 3; subKursor = 4;
            }
        } else if (btn == BTN_RIGHT) {
            if (savedWifiKursor < cnt - 1) {
                savedWifiKursor++;
                if (savedWifiKursor >= savedWifiScroll + 3) savedWifiScroll++;
            }
        } else if (btn == BTN_OK && cnt > 0) {
            appMode = 19;
        }
        return;
    }

    // ---- SAVED WIFI DETAIL (mode 19) ----
    // < kembali ke list | OK = hapus entry ini
    if (appMode == 19) {
        if (btn == BTN_LEFT) {
            appMode = 18;
        } else if (btn == BTN_OK) {
            wifi_saved_delete(savedWifiKursor);
            int cnt = wifi_saved_count();
            if (savedWifiKursor >= cnt) savedWifiKursor = (cnt > 0) ? cnt - 1 : 0;
            appMode = 18;
        }
        return;
    }

    // ---- MENU UTAMA (layar 0) ----
    if (appMode == 0) {
        if (!diSubMenu) {
            // Carousel logo — > = next | < = prev | OK = masuk submenu
            if (btn == BTN_RIGHT) {
                katIdx   = (katIdx + 1) % 4;
                katKursor= katIdx;
                katArah  = 1; katAnim = true; katAnimT = now;
            } else if (btn == BTN_LEFT) {
                katIdx   = (katIdx - 1 + 4) % 4;
                katKursor= katIdx;
                katArah  = -1; katAnim = true; katAnimT = now;
            } else if (btn == BTN_OK) {
                diSubMenu = true; katKursor = katIdx;
                subKursor = 0;    atasMenu  = 0;
            }
        } else {
            // List submenu — > = geser | < = balik carousel | OK = pilih
            int lim = totalSubKat[katKursor];
            if (btn == BTN_RIGHT) {
                if (subKursor < lim-1) {
                    subKursor++;
                    if (subKursor >= atasMenu + 5) atasMenu++;
                } else { subKursor = 0; atasMenu = 0; }
            } else if (btn == BTN_LEFT) {
                diSubMenu = false; atasMenu = 0;
            } else if (btn == BTN_OK) {
                if (katKursor == 3) {
                    // Settings
                    if      (subKursor == 0) appMode = 1;
                    else if (subKursor == 1) appMode = 7;
                    else if (subKursor == 2) appMode = 8;
                    else if (subKursor == 3) { wifi_scan_start(); appMode = 13; }
                    else if (subKursor == 4) { savedWifiKursor = 0; savedWifiScroll = 0; appMode = 18; }
                    else if (subKursor == 5) { ota_check_start(); appMode = 20; }
                    else if (subKursor == 6) {
                        // Edit PIN — wajib masukin PIN LAMA dulu buat verifikasi
                        memset(pinBuf, 0, sizeof(pinBuf));
                        pinPrev = 0;
                        appMode = 31;
                    }
                } else {
                    // Toko → ke daftar item
                    itemKursor = 0; itemScroll = 0;
                    itemTotal  = storeGetTotal(katKursor, subKursor);
                    appMode    = 2;
                }
            }
        }
    }
}

// ==========================================================
// HANDLE INPUT SEMUA LAYAR STORE
// ==========================================================
void handleStoreInput(int btn) {
    static uint32_t tLeft = 0;  // Terakhir tekan Left (buat double-click BATAL)
    #define DBL 200              // Window double-click (ms)

    uint32_t now = ms();

    // --------------------------------------------------
    // LAYAR 2: DAFTAR ITEM
    // > scroll | < kembali | OK pilih
    // --------------------------------------------------
    if (appMode == 2) {
        int tot = storeGetTotal(katKursor, subKursor);
        if (btn == BTN_RIGHT) {
            int abs = itemScroll + itemKursor;
            if (abs < tot-1) {
                if (itemKursor < 2) itemKursor++;
                else itemScroll++;
            } else { itemKursor=0; itemScroll=0; }
        } else if (btn == BTN_LEFT) {
            appMode=0; diSubMenu=true; katKursor=katKursor; subKursor=subKursor;
        } else if (btn == BTN_OK && tot > 0) {
            itemDipilih = itemScroll + itemKursor;
            appMode = 3;
        }
    }

    // --------------------------------------------------
    // LAYAR 3: DETAIL ITEM
    // < kembali | OK beli → setup field → layar 4
    // --------------------------------------------------
    else if (appMode == 3) {
     if (!checkstatus) {
        // Masih nunggu hasil cek produk dari server (task background) —
        // jangan proses OK/LEFT dulu biar gak kepencet berdasarkan status basi
        return;
     }
     if (itemtersedia == true) {
        if (btn == BTN_LEFT) {
            appMode = 2;
            checkstatus = false;
        } else if (btn == BTN_OK) {
            storeSetupField(katKursor, subKursor);
            fieldKursor = 0; charIdx = 0;
            appMode = 4;
            checkstatus = false;
        }
      }
      else {
         if (btn == BTN_LEFT) {
            appMode = 2;
            checkstatus = false;
      }
      }
    }

    // --------------------------------------------------
    // LAYAR 4: LIST INPUT FIELD
    // > ganti field | < kembali | OK edit/konfirmasi
    // --------------------------------------------------
    else if (appMode == 4) {
        int totRow = totalField + 1;
        if (btn == BTN_RIGHT) {
            fieldKursor = (fieldKursor + 1) % totRow;
        } else if (btn == BTN_LEFT) {
            appMode = 2;
        } else if (btn == BTN_OK) {
            if (fieldKursor < totalField) {
                // Edit field → ke input karakter
                charIdx    = 0; tLeft = 0;
                inputAngka = true; // Reset ke mode ANGKA setiap mulai field baru
                appMode    = 5;
            } else {
                // Tombol KONFIRMASI — cek semua field terisi
                bool ok = true;
                for (int f = 0; f < totalField; f++)
                    if (!strlen(field[f].value)) { ok=false; break; }
                if (ok) {
                appMode = 6;
                buildTargetID(); 
                }
            }
        }
    }

    // --------------------------------------------------
    // LAYAR 5: INPUT KARAKTER — 2 MODE KEYBOARD
    //
    // [ANGKA] default: cycling 0-9 saja (10 karakter, super cepat buat HP/ID!)
    // [HURUF]        : A-Z + a-z + 0-9 + simbol (68 karakter) — buat email/nama,
    //                  udah termasuk angka juga jadi gak perlu balik ke mode
    //                  ANGKA lagi kalau butuh angka di belakang huruf (email dll)
    //
    // > = next char (tahan = auto-repeat makin cepat)
    // OK TAP        = tambah char ke buffer (langsung, gak nunggu apa-apa)
    // OK TAHAN      = SELESAI — kembali ke field list (GAK nambah karakter)
    // < (1x)   = hapus char terakhir
    //            ATAU jika buffer KOSONG = GANTI MODE (ANGKA↔HURUF)
    // < (2x)   = BATAL — hapus semua, kembali ke field list
    // --------------------------------------------------
    else if (appMode == 5) {
        // Charset dari globals.h (satu sumber, sinkron sama display_system.c)
        const char *cs    = inputAngka ? CS_ANGKA : CS_HURUF;
        int         csLen = inputAngka ? CS_ANGKA_LEN : CS_HURUF_LEN;

        if (btn == BTN_RIGHT) {
            charIdx = (charIdx + 1) % csLen;
        }
        else if (btn == BTN_OK) {
            // SELESAI (tahan OK) udah ditangani di handleJoystick sebelum
            // sampai sini — jadi begitu btn == BTN_OK nyampe ke sini, itu
            // pasti tap biasa → tambah 1 karakter. Gak ada lagi karakter
            // nyelip pas mau selesai.
            int l = strlen(field[fieldKursor].value);
            if (l < 27) {
                field[fieldKursor].value[l]   = cs[charIdx % csLen];
                field[fieldKursor].value[l+1] = '\0';
                charIdx = 0;
            }
        }
        else if (btn == BTN_LEFT) {
            bool dbl = (now - tLeft < 400) && tLeft;
            tLeft = now;
            if (dbl) {
                // 2x LEFT = BATAL: hapus semua, balik ke field list
                memset(field[fieldKursor].value, 0, sizeof(field[fieldKursor].value));
                charIdx = 0; appMode = 4;
            } else {
                int l = strlen(field[fieldKursor].value);
                if (l > 0) {
                    // 1x LEFT + ada isi = HAPUS karakter terakhir
                    field[fieldKursor].value[l-1] = '\0';
                    charIdx = 0;
                } else {
                    // 1x LEFT + buffer KOSONG = GANTI MODE
                    inputAngka = !inputAngka;
                    charIdx    = 0;
                }
            }
        }
    }

    // --------------------------------------------------
    // LAYAR 6: KONFIRMASI
    // < kembali | OK → build targetID → layar 9
    // --------------------------------------------------
    else if (appMode == 6) {
        if (btn == BTN_LEFT) {
            appMode = 4;
            checkstatus = false;
            ceknickgagal = false;
        } else if (btn == BTN_OK) {
               // Bangun "idGame|zoneID" atau "nomorHP"
            caraBayar = 0;
            appMode   = 9;
            checkstatus = false;
            ceknickgagal = false;
        }
    }

    // --------------------------------------------------
    // LAYAR 9: AKSI BAYAR (BAYAR / QRIS)
    // > toggle | < kembali | OK eksekusi
    // --------------------------------------------------
    else if (appMode == 9) {
        if (btn == BTN_RIGHT) {
            caraBayar = (caraBayar == 0) ? 1 : 0;
        } else if (btn == BTN_LEFT) {
            appMode = 6;
        } else if (btn == BTN_OK) {
            if (caraBayar == 1) {
                appMode = 10; // QRIS -> tampilkan langsung, gak perlu PIN
            } else {
                // TF/BAYAR -> wajib PIN dulu, eksekusi sebenarnya baru
                // jalan setelah PIN benar (lihat appMode 30 di handleJoystick)
                memset(pinBuf, 0, sizeof(pinBuf));
                pinPrev = 0;
                appMode = 30;
            }
        }
    }

    // --------------------------------------------------
    // LAYAR 10: QRIS SCREEN
    // < kembali ke aksi bayar
    // --------------------------------------------------
    else if (appMode == 10) {
        if (btn == BTN_LEFT) appMode = 9;
    }
    else if (appMode == 11) {
        if (btn == BTN_LEFT) {
            appMode = 0; diSubMenu = false;
            katKursor = katIdx = 0;
            subKursor = 0; atasMenu = 0;
            trxberhasil = false; checkstatus = false;
            trxtimeout = false;
        }
        else if (btn == BTN_RIGHT) {
            appMode = 12;
            trxberhasil = false; checkstatus = false;
            trxtimeout = false;
        }
    }

    // --------------------------------------------------
    // LAYAR 11: TRX BERHASIL
    // LAYAR 12: TRX GAGAL
    // < kembali ke home (menu utama)
    // --------------------------------------------------
    else if (appMode == 12) {
        if (btn == BTN_LEFT) {
            appMode = 0; diSubMenu = false;
            katKursor = katIdx = 0;
            subKursor = 0; atasMenu = 0;
        }
        else if (btn == BTN_RIGHT) {
            appMode = 11;
        }
    }

    // --------------------------------------------------
    // LAYAR 35: PIN BERHASIL DIUBAH (konfirmasi)
    // < / OK -> balik ke Settings
    // --------------------------------------------------
    else if (appMode == 35) {
        if (btn == BTN_LEFT || btn == BTN_OK) {
            appMode = 0; diSubMenu = true; katKursor = 3; subKursor = 6;
        }
    }
}
