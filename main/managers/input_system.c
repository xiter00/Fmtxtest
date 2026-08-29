#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "globals.h"

// ==========================================================
// FUNGSI DARI display_system.c (extern)
// ==========================================================
extern int storeGetTotalItems(int kat, int sub);
extern int storeGetTotalSubs(int kat);
extern void storeSetupFields(int kat, int sub);
extern void setOledBrightness(uint8_t level);
extern const int totalSubPerKat[];

// ==========================================================
// CHARSET UNTUK INPUT KARAKTER
// Urutan: angka dulu (paling sering dipakai buat user ID),
// lalu huruf besar, kecil, simbol
// ==========================================================
#define CHARSET_LEN 68
static const char CHARSET[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz _-.";

// ==========================================================
// HELPER MILLIS
// ==========================================================
uint32_t input_millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ==========================================================
// FORWARD DECLARATION
// ==========================================================
void handleStoreInput(int btn);

// ==========================================================
// HANDLE JOYSTICK — ENTRY POINT UTAMA
// Dipanggil tiap frame dari task_display
// ==========================================================
void handleJoystick() {
    static uint32_t lastPress = 0;
    uint32_t now = input_millis();

    // Debounce adaptif:
    // - Mode char input: lebih cepat supaya bisa cycling huruf
    // - Mode lain: normal 250ms
    uint32_t debounce = (appMode == 12) ? 110 : 250;
    if (now - lastPress < debounce) return;

    int btn = BTN_NONE;
    if      (gpio_get_level(PIN_LEFT)  == 0) btn = BTN_LEFT;
    else if (gpio_get_level(PIN_RIGHT) == 0) btn = BTN_RIGHT;
    else if (gpio_get_level(PIN_OK)    == 0) btn = BTN_OK;

    if (btn == BTN_NONE) return;
    lastPress = now;

    // ======================================================
    // STORE MODES: Delegasi ke handleStoreInput
    // ======================================================
    if ((appMode >= 9 && appMode <= 13) || appMode == 18 || appMode == 19) {
        handleStoreInput(btn);
        return;
    }

    // ======================================================
    // BRIGHTNESS (appMode 3)
    // < = kembali | > = naik +20 | OK = turun -20
    // ======================================================
    if (appMode == 3) {
        if (btn == BTN_LEFT) {
            appMode   = 0;
            inSubMenu = true;
            currentMenu = 3;
            currentSub  = 0;
        } else if (btn == BTN_RIGHT) {
            brightnessValue = (brightnessValue + 20 > 255) ? 0 : brightnessValue + 20;
            setOledBrightness((uint8_t)brightnessValue);
        } else if (btn == BTN_OK) {
            brightnessValue = (brightnessValue - 20 < 0) ? 255 : brightnessValue - 20;
            setOledBrightness((uint8_t)brightnessValue);
        }
        return;
    }

    // ======================================================
    // ABOUT (appMode 14)
    // < = kembali ke settings submenu
    // ======================================================
    if (appMode == 14) {
        if (btn == BTN_LEFT) {
            appMode   = 0;
            inSubMenu = true;
            currentMenu = 3;
            currentSub  = 1;
        }
        return;
    }

    // ======================================================
    // REBOOT (appMode 15)
    // < = kembali | OK = reboot sekarang
    // ======================================================
    if (appMode == 15) {
        if (btn == BTN_LEFT) {
            appMode   = 0;
            inSubMenu = true;
            currentMenu = 3;
            currentSub  = 2;
        } else if (btn == BTN_OK) {
            esp_restart();
        }
        return;
    }

    // ======================================================
    // MAIN MENU (appMode 0)
    // ======================================================
    if (appMode == 0) {
        if (!inSubMenu) {
            // --- CAROUSEL LOGO ---
            // > = next kategori | < = prev kategori | OK = masuk submenu
            if (btn == BTN_RIGHT) {
                carouselCurrentIdx = (carouselCurrentIdx + 1) % 4;
                currentMenu        = carouselCurrentIdx;
                carouselDirection  = 1;
                carouselAnimating  = true;
                carouselAnimStart  = now;
            } else if (btn == BTN_LEFT) {
                carouselCurrentIdx = (carouselCurrentIdx - 1 + 4) % 4;
                currentMenu        = carouselCurrentIdx;
                carouselDirection  = -1;
                carouselAnimating  = true;
                carouselAnimStart  = now;
            } else if (btn == BTN_OK) {
                inSubMenu   = true;
                currentMenu = carouselCurrentIdx;
                currentSub  = 0;
                topMenu     = 0;
            }
        } else {
            // --- LIST SUBMENU ---
            // > = geser ke bawah | < = kembali ke carousel | OK = pilih item
            int limitMenu = totalSubPerKat[currentMenu];

            if (btn == BTN_RIGHT) {
                if (currentSub < limitMenu - 1) {
                    currentSub++;
                    // Scroll otomatis kalau keluar dari jendela 5 baris
                    if (currentSub >= topMenu + 5) topMenu++;
                } else {
                    // Wrap balik ke atas
                    currentSub = 0;
                    topMenu    = 0;
                }
            } else if (btn == BTN_LEFT) {
                inSubMenu = false;
                topMenu   = 0;
            } else if (btn == BTN_OK) {
                if (currentMenu == 3) {
                    // Kategori Settings — langsung ke sub-screen
                    if      (currentSub == 0) appMode = 3;   // Brightness
                    else if (currentSub == 1) appMode = 14;  // About
                    else if (currentSub == 2) appMode = 15;  // Reboot
                } else {
                    // Kategori toko (Diamond / Pulsa / E-Money) — ke list produk
                    storeKategori   = currentMenu;
                    storeSubMenuIdx = currentSub;
                    storeItemCursor = 0;
                    storeScrollPos  = 0;
                    storeTotalItems = storeGetTotalItems(storeKategori, storeSubMenuIdx);
                    appMode = 9;
                }
            }
        }
        return;
    }
}

// ==========================================================
// HANDLE SEMUA INPUT STORE
// Satu fungsi untuk semua appMode store (9-13, 18, 19)
//
// DOUBLE-CLICK LOGIC:
// - Hanya aktif di appMode 12 (char input)
// - Double-OK  (2x dalam 450ms) = selesai input field
// - Double-LEFT (2x dalam 450ms) = batal, hapus & kembali
// ==========================================================
void handleStoreInput(int btn) {
    static uint32_t lastOkTime   = 0;
    static uint32_t lastLeftTime = 0;
    #define DOUBLE_MS 450

    uint32_t now = input_millis();

    // --------------------------------------------------
    // MODE 9: NAVIGASI LIST ITEM PRODUK
    // > = scroll bawah | < = kembali | OK = pilih item
    // --------------------------------------------------
    if (appMode == 9) {
        int total = storeGetTotalItems(storeKategori, storeSubMenuIdx);

        if (btn == BTN_RIGHT) {
            // Gerak kursor ke bawah
            int absoluteIdx = storeScrollPos + storeItemCursor;
            if (absoluteIdx < total - 1) {
                if (storeItemCursor < 2) {
                    storeItemCursor++;  // Geser kursor (belum perlu scroll)
                } else {
                    storeScrollPos++;   // Kursor sudah di baris 3, scroll konten
                }
            } else {
                // Wrap ke atas
                storeItemCursor = 0;
                storeScrollPos  = 0;
            }
        } else if (btn == BTN_LEFT) {
            // Kembali ke submenu
            appMode   = 0;
            inSubMenu = true;
            currentMenu = storeKategori;
            currentSub  = storeSubMenuIdx;
        } else if (btn == BTN_OK) {
            if (total > 0) {
                storeSelectedItem = storeScrollPos + storeItemCursor;
                appMode = 10; // Ke detail item
            }
        }
    }

    // --------------------------------------------------
    // MODE 10: DETAIL ITEM
    // < = kembali ke list | OK = lanjut ke input data
    // --------------------------------------------------
    else if (appMode == 10) {
        if (btn == BTN_LEFT) {
            appMode = 9;
        } else if (btn == BTN_OK) {
            storeSetupFields(storeKategori, storeSubMenuIdx);
            storeFieldCursor = 0;
            storeCharIdx     = 0;
            storeCharPos     = 0;
            appMode = 11;
        }
    }

    // --------------------------------------------------
    // MODE 11: LIST INPUT FIELD
    // > = geser ke field berikut | < = kembali | OK = edit / konfirmasi
    // --------------------------------------------------
    else if (appMode == 11) {
        int totalRows = storeTotalFields + 1; // Fields + baris KONFIRMASI

        if (btn == BTN_RIGHT) {
            storeFieldCursor = (storeFieldCursor + 1) % totalRows;
        } else if (btn == BTN_LEFT) {
            appMode = 10;
        } else if (btn == BTN_OK) {
            if (storeFieldCursor < storeTotalFields) {
                // Edit field ini: lanjut ke mode char input
                storeCharPos = strlen(storeFields[storeFieldCursor].value);
                storeCharIdx = 0;
                // Reset double-click timer supaya tidak misfiring
                lastOkTime   = 0;
                lastLeftTime = 0;
                appMode = 12;
            } else {
                // Tombol KONFIRMASI — cek semua field sudah terisi
                bool semuaIsi = true;
                for (int f = 0; f < storeTotalFields; f++)
                    if (strlen(storeFields[f].value) == 0) { semuaIsi = false; break; }

                if (semuaIsi) appMode = 13;
                // Kalau belum isi, tidak pindah (layar sudah tunjukkan "ISI DULU!")
            }
        }
    }

    // --------------------------------------------------
    // MODE 12: INPUT KARAKTER (KETIK PER HURUF)
    //
    // BTN_RIGHT       = ganti karakter (cycling CHARSET)
    // BTN_OK (1x)     = tambah karakter ke buffer
    // BTN_OK (2x)     = SELESAI — kembali ke field list
    // BTN_LEFT (1x)   = hapus karakter terakhir
    // BTN_LEFT (2x)   = BATAL — hapus semua value, kembali ke field list
    // --------------------------------------------------
    else if (appMode == 12) {
        if (btn == BTN_RIGHT) {
            // Cycling karakter
            storeCharIdx = (storeCharIdx + 1) % CHARSET_LEN;
        }
        else if (btn == BTN_OK) {
            bool isDouble = (now - lastOkTime < DOUBLE_MS) && (lastOkTime != 0);
            lastOkTime = now;

            if (isDouble) {
                // DOUBLE OK = selesai input field ini
                storeCharIdx = 0;
                appMode = 11;
            } else {
                // SINGLE OK = tambah karakter ke buffer
                int curLen = strlen(storeFields[storeFieldCursor].value);
                if (curLen < 27) {
                    storeFields[storeFieldCursor].value[curLen]     = CHARSET[storeCharIdx];
                    storeFields[storeFieldCursor].value[curLen + 1] = '\0';
                    storeCharIdx = 0; // Reset ke '0' setelah tambah
                }
            }
        }
        else if (btn == BTN_LEFT) {
            bool isDouble = (now - lastLeftTime < DOUBLE_MS) && (lastLeftTime != 0);
            lastLeftTime = now;

            if (isDouble) {
                // DOUBLE LEFT = batal, hapus value field ini
                memset(storeFields[storeFieldCursor].value, 0,
                       sizeof(storeFields[storeFieldCursor].value));
                storeCharPos = 0;
                storeCharIdx = 0;
                lastOkTime   = 0; // Reset juga OK timer
                appMode = 11;
            } else {
                // SINGLE LEFT = hapus karakter terakhir
                int curLen = strlen(storeFields[storeFieldCursor].value);
                if (curLen > 0) {
                    storeFields[storeFieldCursor].value[curLen - 1] = '\0';
                    storeCharIdx = 0;
                }
            }
        }
    }

    // --------------------------------------------------
    // MODE 13: KONFIRMASI DETAIL
    // < = kembali ke input | OK = lanjut ke action menu
    // --------------------------------------------------
    else if (appMode == 13) {
        if (btn == BTN_LEFT) {
            appMode = 11;
        } else if (btn == BTN_OK) {
            storePayMethod = 0; // Default BAYAR
            appMode = 18;
        }
    }

    // --------------------------------------------------
    // MODE 18: ACTION MENU (BAYAR / QRIS)
    // > = toggle pilihan | < = kembali | OK = eksekusi
    // --------------------------------------------------
    else if (appMode == 18) {
        if (btn == BTN_RIGHT) {
            storePayMethod = (storePayMethod == 0) ? 1 : 0;
        } else if (btn == BTN_LEFT) {
            appMode = 13;
        } else if (btn == BTN_OK) {
            if (storePayMethod == 1) {
                appMode = 19; // Tampilkan QRIS
            } else {
                // BAYAR — TODO: panggil API H2H di sini
                // Sementara kembali ke home sebagai placeholder
                appMode     = 0;
                inSubMenu   = false;
                currentMenu = 0;
                currentSub  = 0;
                topMenu     = 0;
            }
        }
    }

    // --------------------------------------------------
    // MODE 19: QRIS SCREEN
    // < = kembali ke action menu
    // --------------------------------------------------
    else if (appMode == 19) {
        if (btn == BTN_LEFT) {
            appMode = 18;
        }
    }
}
