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
// MENU NAV — skema klik/tahan KHUSUS buat layar PILIHAN MENU aja:
//   layar 0  (Menu Utama: carousel + submenu)
//   layar 2  (Daftar Item)
//   layar 4  (Daftar Field input)
//   layar 9  (Pilih metode Bayar)
//   layar 13 (Daftar WiFi)
//   layar 18 (Saved WiFi)
//
//   klik   KIRI  1x  = pilihan SEBELUMNYA
//   tahan  KIRI      = KEMBALI ke layar sebelumnya
//   klik   KANAN 1x  = pilihan SELANJUTNYA
//   tahan  KANAN     = pilihan SELANJUTNYA terus, geser makin cepet
//                       (auto-repeat kayak nge-hold tombol keyboard)
//   klik   OK        = PILIH
//
// Layar SELAIN yang di atas (konfirmasi, detail item, keyboard/PIN, dll)
// TETAP pakai mekanisme ASLI (klik kiri langsung = kembali, gak perlu
// ditahan) — persis kayak sebelum sistem menu baru ini ada.
// ==========================================================
typedef enum {
    JEV_NONE = 0,
    JEV_LEFT_CLICK,     // klik kiri     -> SEBELUMNYA
    JEV_LEFT_HOLD,      // tahan kiri    -> KEMBALI
    JEV_RIGHT_CLICK,    // klik kanan    -> SELANJUTNYA
    JEV_RIGHT_REPEAT,   // tahan kanan   -> SELANJUTNYA (berulang, auto-repeat)
    JEV_OK_CLICK,       // klik OK       -> PILIH
} JoyEvent;

#define MENU_HOLD_MS          450  // tahan kiri >= ini baru dianggap "tahan"
#define MENU_REPEAT_START_MS  300  // tahan kanan >= ini baru mulai auto-repeat
#define MENU_DEBOUNCE_MS      150  // redam noise mekanik antar klik

static JoyEvent pollMenuNav(void) {
    static bool     lPrev = false, rPrev = false, oPrev = false;
    static uint32_t tLDown = 0, tRDown = 0, tRRep = 0;
    static bool     lHoldFired = false;
    static uint32_t lastLClick = 0, lastRClick = 0, lastOClick = 0;

    uint32_t now = ms();
    bool lDown = (gpio_get_level(PIN_LEFT)  == 0);
    bool rDown = (gpio_get_level(PIN_RIGHT) == 0);
    bool oDown = (gpio_get_level(PIN_OK)    == 0);

    JoyEvent ev = JEV_NONE;

    // ---- KIRI: dilepas sebelum MENU_HOLD_MS = klik (SEBELUMNYA) |
    //            ditahan sampai MENU_HOLD_MS  = tahan (KEMBALI) ----
    if (lDown) {
        if (!lPrev) {
            tLDown = now; lHoldFired = false;
        } else if (!lHoldFired && (now - tLDown >= MENU_HOLD_MS)) {
            lHoldFired = true;
            if (now - lastLClick >= MENU_DEBOUNCE_MS) {
                lastLClick = now;
                ev = JEV_LEFT_HOLD;
            }
        }
    } else if (lPrev && !lHoldFired) {
        if (now - lastLClick >= MENU_DEBOUNCE_MS) {
            lastLClick = now;
            ev = JEV_LEFT_CLICK;
        }
    }
    lPrev = lDown;
    if (ev != JEV_NONE) { rPrev = rDown; oPrev = oDown; return ev; }

    // ---- KANAN: klik pas baru ditekan, abis itu auto-repeat makin
    //             ngebut selama ditahan ----
    if (rDown) {
        if (!rPrev) {
            tRDown = now; tRRep = now;
            if (now - lastRClick >= MENU_DEBOUNCE_MS) {
                lastRClick = now;
                ev = JEV_RIGHT_CLICK;
            }
        } else {
            uint32_t d  = now - tRDown;
            uint32_t iv = (d > 900) ? 25 : (d > 500) ? 50 : (d > MENU_REPEAT_START_MS) ? 90 : 0;
            if (iv > 0 && now - tRRep >= iv) {
                tRRep = now;
                ev = JEV_RIGHT_REPEAT;
            }
        }
    }
    rPrev = rDown;
    if (ev != JEV_NONE) { oPrev = oDown; return ev; }

    // ---- OK: klik doang ----
    if (oDown && !oPrev) {
        if (now - lastOClick >= MENU_DEBOUNCE_MS) {
            lastOClick = now;
            ev = JEV_OK_CLICK;
        }
    }
    oPrev = oDown;
    return ev;
}

// Klik-kanan ATAU tahan-kanan (repeat) — dua-duanya "maju satu langkah"
#define IS_NEXT(ev) ((ev) == JEV_RIGHT_CLICK || (ev) == JEV_RIGHT_REPEAT)

static void handleMenuNav(JoyEvent ev);

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
        // TODO: panggil API H2H di sini. Hasil API -> appMode 11 (satu
        // layar buat berhasil/timeout/gagal, ditentukan trxberhasil /
        // trxtimeout — lihat display_system.c). Untuk placeholder,
        // langsung ke situ, task SEND_ITEM di layar itu yang nentuin
        // status akhirnya:
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

    // ---- LAYAR PILIHAN MENU (0, 2, 4, 9, 13, 18) — skema klik/tahan baru ----
    if (appMode == 0 || appMode == 2 || appMode == 4 || appMode == 9 ||
        appMode == 13 || appMode == 18) {
        JoyEvent ev = pollMenuNav();
        if (ev == JEV_NONE) return;
        handleMenuNav(ev);
        return;
    }

    // ---- Debounce normal untuk semua mode lain (SAMA PERSIS kayak
    // sebelumnya — klik langsung = aksi, gak ada tahan) ----
    if (now - lastPress < 250) return;

    int btn = BTN_NONE;
    if      (gpio_get_level(PIN_LEFT)  == 0) btn = BTN_LEFT;
    else if (gpio_get_level(PIN_RIGHT) == 0) btn = BTN_RIGHT;
    else if (gpio_get_level(PIN_OK)    == 0) btn = BTN_OK;
    if (btn == BTN_NONE) return;
    lastPress = now;

    // Delegasi ke store handler (layar 3, 6, 10, 11, dan konfirmasi PIN 35 —
    // 2, 4, 9 udah ditangani duluan di atas lewat handleMenuNav)
    if (appMode == 3 || appMode == 6 || appMode == 10 || appMode == 11 ||
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
}

// ==========================================================
// HANDLE MENU NAV — khusus layar PILIHAN MENU (0, 2, 4, 9, 13, 18)
// ==========================================================
static void handleMenuNav(JoyEvent ev) {
    // --------------------------------------------------
    // LAYAR 0: MENU UTAMA (carousel kategori + submenu)
    // --------------------------------------------------
    if (appMode == 0) {
        if (!diSubMenu) {
            // Carousel logo — klik kanan/tahan kanan = next | klik kiri =
            // prev | OK = masuk submenu. Ini layar HOME, gak ada "kembali"
            // lagi di atasnya, jadi tahan kiri gak ngapa2in di sini.
            if (IS_NEXT(ev)) {
                katIdx   = (katIdx + 1) % 4;
                katKursor= katIdx;
                katArah  = 1; katAnim = true; katAnimT = ms();
            } else if (ev == JEV_LEFT_CLICK) {
                katIdx   = (katIdx - 1 + 4) % 4;
                katKursor= katIdx;
                katArah  = -1; katAnim = true; katAnimT = ms();
            } else if (ev == JEV_OK_CLICK) {
                diSubMenu = true; katKursor = katIdx;
                subKursor = 0;    atasMenu  = 0;
            }
        } else {
            // List submenu — klik kanan/tahan kanan = geser maju | klik
            // kiri = geser mundur | tahan kiri = balik ke carousel | OK = pilih
            int lim = totalSubKat[katKursor];
            if (ev == JEV_LEFT_HOLD) {
                diSubMenu = false; atasMenu = 0;
            } else if (IS_NEXT(ev)) {
                if (subKursor < lim-1) {
                    subKursor++;
                    if (subKursor >= atasMenu + 5) atasMenu++;
                } else { subKursor = 0; atasMenu = 0; }
            } else if (ev == JEV_LEFT_CLICK) {
                if (subKursor > 0) {
                    subKursor--;
                    if (subKursor < atasMenu) atasMenu--;
                } else {
                    subKursor = lim - 1;
                    atasMenu  = (lim > 5) ? lim - 5 : 0;
                }
            } else if (ev == JEV_OK_CLICK) {
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
        return;
    }

    // --------------------------------------------------
    // LAYAR 2: DAFTAR ITEM
    // klik kanan/tahan kanan = item berikutnya | klik kiri = item
    // sebelumnya | tahan kiri = kembali ke submenu | OK = pilih
    // --------------------------------------------------
    if (appMode == 2) {
        int tot = storeGetTotal(katKursor, subKursor);
        if (IS_NEXT(ev)) {
            int abs = itemScroll + itemKursor;
            if (abs < tot-1) {
                if (itemKursor < 2) itemKursor++;
                else itemScroll++;
            } else { itemKursor=0; itemScroll=0; }
        } else if (ev == JEV_LEFT_CLICK) {
            int abs = itemScroll + itemKursor;
            if (abs > 0) {
                if (itemKursor > 0) itemKursor--;
                else itemScroll--;
            } else if (tot > 0) {
                int last = tot - 1;
                itemKursor = (last < 2) ? last : 2;
                itemScroll = last - itemKursor;
            }
        } else if (ev == JEV_LEFT_HOLD) {
            appMode=0; diSubMenu=true; katKursor=katKursor; subKursor=subKursor;
        } else if (ev == JEV_OK_CLICK && tot > 0) {
            itemDipilih = itemScroll + itemKursor;
            appMode = 3;
        }
        return;
    }

    // --------------------------------------------------
    // LAYAR 4: LIST INPUT FIELD
    // klik kanan/tahan kanan = field berikutnya | klik kiri = field
    // sebelumnya | tahan kiri = kembali ke daftar item | OK = edit/konfirmasi
    // --------------------------------------------------
    if (appMode == 4) {
        int totRow = totalField + 1;
        if (IS_NEXT(ev)) {
            fieldKursor = (fieldKursor + 1) % totRow;
        } else if (ev == JEV_LEFT_CLICK) {
            fieldKursor = (fieldKursor - 1 + totRow) % totRow;
        } else if (ev == JEV_LEFT_HOLD) {
            appMode = 2;
        } else if (ev == JEV_OK_CLICK) {
            if (fieldKursor < totalField) {
                // Edit field → ke input karakter (layar 5, mekanisme ASLI:
                // tap/tahan OK, dll — gak kesentuh sama sekali)
                charIdx    = 0;
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
        return;
    }

    // --------------------------------------------------
    // LAYAR 9: AKSI BAYAR (BAYAR / QRIS) — 2 opsi
    // klik kanan / klik kiri = ganti pilihan | tahan kiri = kembali ke
    // Konfirmasi | OK = eksekusi
    // --------------------------------------------------
    if (appMode == 9) {
        if (IS_NEXT(ev) || ev == JEV_LEFT_CLICK) {
            caraBayar = (caraBayar == 0) ? 1 : 0;
        } else if (ev == JEV_LEFT_HOLD) {
            appMode = 6;
        } else if (ev == JEV_OK_CLICK) {
            if (caraBayar == 1) {
                appMode = 10; // QRIS -> tampilkan langsung, gak perlu PIN
            } else {
                // TF/BAYAR -> wajib PIN dulu (layar 30, mekanisme ASLI)
                memset(pinBuf, 0, sizeof(pinBuf));
                pinPrev = 0;
                appMode = 30;
            }
        }
        return;
    }

    // --------------------------------------------------
    // LAYAR 13: DAFTAR WIFI
    // klik kanan / tahan kanan = AP berikutnya (auto-repeat) | klik kiri =
    // AP sebelumnya | tahan kiri = balik ke Settings | OK = konek
    // --------------------------------------------------
    if (appMode == 13) {
        if (wifiStatus == WIFI_STATUS_SCANNING) return;
        if (ev == JEV_LEFT_HOLD) {
            appMode = 0; diSubMenu = true; katKursor = 3; subKursor = 3;
        } else if (ev == JEV_LEFT_CLICK) {
            if (wifiKursor > 0) {
                wifiKursor--;
                if (wifiKursor < wifiScroll) wifiScroll--;
            }
        } else if (IS_NEXT(ev)) {
            if (wifiKursor < wifiTotal - 1) {
                wifiKursor++;
                if (wifiKursor >= wifiScroll + 3) wifiScroll++;
            }
        } else if (ev == JEV_OK_CLICK && wifiTotal > 0) {
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
                    appMode    = 14;   // layar keyboard password — mekanisme ASLI
                }
            } else {
                wifiPassBuf[0] = '\0';
                wifi_connect_selected();
                appMode = 15;
            }
        }
        return;
    }

    // --------------------------------------------------
    // LAYAR 18: SAVED WIFI LIST
    // klik kanan/tahan kanan = geser | klik kiri = mundur | tahan kiri =
    // kembali ke Settings | OK = lihat detail
    // --------------------------------------------------
    if (appMode == 18) {
        int cnt = wifi_saved_count();
        if (ev == JEV_LEFT_HOLD) {
            appMode = 0; diSubMenu = true; katKursor = 3; subKursor = 4;
        } else if (ev == JEV_LEFT_CLICK) {
            if (savedWifiKursor > 0) {
                savedWifiKursor--;
                if (savedWifiKursor < savedWifiScroll) savedWifiScroll--;
            }
        } else if (IS_NEXT(ev)) {
            if (savedWifiKursor < cnt - 1) {
                savedWifiKursor++;
                if (savedWifiKursor >= savedWifiScroll + 3) savedWifiScroll++;
            }
        } else if (ev == JEV_OK_CLICK && cnt > 0) {
            appMode = 19;
        }
        return;
    }
}

// ==========================================================
// HANDLE INPUT SEMUA LAYAR STORE — layar 3, 5, 6, 10, 11, 35 (mekanisme
// ASLI, klik langsung = aksi). Layar 2, 4, 9 udah dipindah ke
// handleMenuNav() di atas.
// ==========================================================
void handleStoreInput(int btn) {
    static uint32_t tLeft = 0;  // Terakhir tekan Left (buat double-click BATAL, layar 5)
    #define DBL 200              // Window double-click (ms)

    uint32_t now = ms();

    // --------------------------------------------------
    // LAYAR 3: DETAIL ITEM
    // < kembali | OK beli → setup field → layar 4
    // --------------------------------------------------
    if (appMode == 3) {
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
    // LAYAR 10: QRIS SCREEN
    // < kembali ke aksi bayar
    // --------------------------------------------------
    else if (appMode == 10) {
        if (btn == BTN_LEFT) appMode = 9;
    }

    // --------------------------------------------------
    // LAYAR 11: TRX BERHASIL / TIMEOUT / GAGAL (satu layar buat ketiganya,
    // lihat display_system.c appMode 11 — beda tampilan tergantung
    // trxberhasil/trxtimeout, bukan appMode terpisah lagi)
    // < kembali ke home (menu utama) — cuma ini satu-satunya aksi
    // --------------------------------------------------
    else if (appMode == 11) {
        if (btn == BTN_LEFT) {
            appMode = 0; diSubMenu = false;
            katKursor = katIdx = 0;
            subKursor = 0; atasMenu = 0;
            trxberhasil = false; checkstatus = false;
            trxtimeout = false;
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
