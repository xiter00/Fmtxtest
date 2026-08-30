#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "globals.h"
#include "wifi_sys.h"

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
// HANDLE JOYSTICK — ENTRY POINT
// ==========================================================
void handleJoystick() {
    static uint32_t lastPress = 0;
    uint32_t now = ms();

    // ---- LAYAR 5: AUTO-REPEAT RIGHT + debounce terpisah OK/LEFT ----
    // Tahan RIGHT → cycling makin cepat (600ms=100ms/step, 1200ms=50ms/step)
    // OK/LEFT punya debounce sendiri supaya auto-repeat tidak ganggu
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
                uint32_t d  = now - tHold;
                uint32_t iv = (d > 1200) ? 50 : (d > 600) ? 100 : 0;
                if (iv > 0 && now - tRep >= iv) {
                    tRep = now;
                    handleStoreInput(BTN_RIGHT);
                }
            }
            return;
        } else {
            hold = false;
        }

        // --- OK / LEFT: hanya trigger sekali per penekanan (rising edge) ---
        bool oPressed = oDown && !oPrev;
        bool lPressed = lDown && !lPrev;
        oPrev = oDown;
        lPrev = lDown;

        if (!oPressed && !lPressed) return;
        if (now - lastOL < 150) return;  // redam noise mekanik
        lastOL = now;

        if      (lPressed) handleStoreInput(BTN_LEFT);
        else if (oPressed) handleStoreInput(BTN_OK);
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

    // Delegasi ke store handler (layar 2-6 dan 9-12)
    if ((appMode >= 2 && appMode <= 6) || (appMode >= 9 && appMode <= 12)) {
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
                memset(wifiPassBuf, 0, sizeof(wifiPassBuf));
                charIdx = 0;
                appMode = 14;
            } else {
                wifiPassBuf[0] = '\0';
                wifi_connect_selected();
                appMode = 15;
            }
        }
        return;
    }

    // ---- WIFI PASSWORD (mode 14) — keyboard khusus WiFi ----
    if (appMode == 14) {
        static const char CS_WIFI[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@ _-.#!";
        int csWLen = 69;
        static uint32_t tOK14 = 0, tL14 = 0;

        if (btn == BTN_RIGHT) {
            charIdx = (charIdx + 1) % csWLen;
        } else if (btn == BTN_OK) {
            bool dbl = (now - tOK14 < 400) && tOK14;
            tOK14 = now;
            if (dbl) {
                wifi_connect_selected();
                charIdx = 0;
                appMode = 15;
            } else {
                int l = strlen(wifiPassBuf);
                if (l < 63) {
                    wifiPassBuf[l]   = CS_WIFI[charIdx % csWLen];
                    wifiPassBuf[l+1] = '\0';
                    charIdx = 0;
                }
            }
        } else if (btn == BTN_LEFT) {
            bool dbl = (now - tL14 < 400) && tL14;
            tL14 = now;
            if (dbl) {
                memset(wifiPassBuf, 0, sizeof(wifiPassBuf));
                charIdx = 0;
                appMode = 13;
            } else {
                int l = strlen(wifiPassBuf);
                if (l > 0) wifiPassBuf[l-1] = '\0';
            }
        }
        return;
    }

    // ---- CONNECTING (mode 15) — auto-transition, cek status ----
    if (appMode == 15) {
        if (wifiStatus == WIFI_STATUS_CONNECTED) appMode = 16;
        if (wifiStatus == WIFI_STATUS_FAILED)    appMode = 17;
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
    static uint32_t tOK   = 0;  // Terakhir tekan OK  (buat double-click)
    static uint32_t tLeft = 0;  // Terakhir tekan Left (buat double-click)
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

    // --------------------------------------------------
    // LAYAR 4: LIST INPUT FIELD
    // > ganti field | < kembali | OK edit/konfirmasi
    // --------------------------------------------------
    else if (appMode == 4) {
        int totRow = totalField + 1;
        if (btn == BTN_RIGHT) {
            fieldKursor = (fieldKursor + 1) % totRow;
        } else if (btn == BTN_LEFT) {
            appMode = 3;
        } else if (btn == BTN_OK) {
            if (fieldKursor < totalField) {
                // Edit field → ke input karakter
                charIdx    = 0; tOK = 0; tLeft = 0;
                inputAngka = true; // Reset ke mode ANGKA setiap mulai field baru
                appMode    = 5;
            } else {
                // Tombol KONFIRMASI — cek semua field terisi
                bool ok = true;
                for (int f = 0; f < totalField; f++)
                    if (!strlen(field[f].value)) { ok=false; break; }
                if (ok) appMode = 6;
            }
        }
    }

    // --------------------------------------------------
    // LAYAR 5: INPUT KARAKTER — 2 MODE KEYBOARD
    //
    // [ANGKA] default: cycling 0-9 saja (10 karakter, super cepat!)
    // [HURUF]        : cycling A-Z + simbol (58 karakter)
    //
    // > = next char (tahan = auto-repeat makin cepat)
    // OK (1x)  = tambah char ke buffer
    // OK (2x)  = SELESAI — kembali ke field list
    // < (1x)   = hapus char terakhir
    //            ATAU jika buffer KOSONG = GANTI MODE (ANGKA↔HURUF)
    // < (2x)   = BATAL — hapus semua, kembali ke field list
    // --------------------------------------------------
    else if (appMode == 5) {
        static const char CS_ANGKA[] = "0123456789";
        static const char CS_HURUF[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@ _-.#";
        const char *cs    = inputAngka ? CS_ANGKA : CS_HURUF;
        int         csLen = inputAngka ? 10 : 59;

        if (btn == BTN_RIGHT) {
            charIdx = (charIdx + 1) % csLen;
        }
        else if (btn == BTN_OK) {
            bool dbl = (now - tOK < 400) && tOK;
            tOK = now;
            if (dbl) {
                // 2x OK = SELESAI input field ini
                charIdx = 0; appMode = 4;
            } else {
                // 1x OK = tambah karakter ke buffer
                int l = strlen(field[fieldKursor].value);
                if (l < 27) {
                    field[fieldKursor].value[l]   = cs[charIdx % csLen];
                    field[fieldKursor].value[l+1] = '\0';
                    charIdx = 0;
                }
            }
        }
        else if (btn == BTN_LEFT) {
            bool dbl = (now - tLeft < 400) && tLeft;
            tLeft = now;
            if (dbl) {
                // 2x LEFT = BATAL: hapus semua, balik ke field list
                memset(field[fieldKursor].value, 0, sizeof(field[fieldKursor].value));
                charIdx = 0; tOK = 0; appMode = 4;
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
        } else if (btn == BTN_OK) {
            buildTargetID();    // Bangun "idGame|zoneID" atau "nomorHP"
            caraBayar = 0;
            appMode   = 9;
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
                appMode = 10; // Tampilkan QRIS
            } else {
                // BAYAR → TODO: panggil API H2H di sini
                // Hasil API → appMode 11 (berhasil) atau 12 (gagal)
                // Untuk placeholder, langsung ke TRX Berhasil:
                appMode = 11;
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
        }
        else if (btn == BTN_RIGHT) {
            appMode = 12;
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
}
