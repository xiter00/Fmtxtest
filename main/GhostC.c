#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "globals.h"
#include "wifi_sys.h"
#include "weblog_sys.h"
#include "photo_data.h"

extern void task_display(void *pvParameters);

// --- NAVIGASI MENU ---
int  katKursor = 0;
int  subKursor = 0;
int  atasMenu  = 0;
bool diSubMenu = false;

// --- CAROUSEL LOGO ---
int      katIdx   = 0;
int      katArah  = 0;
bool     katAnim  = false;
uint32_t katAnimT = 0;

// --- APP STATE ---
int appMode   = 0;
int kecerahan = 150;

// --- BINTANG DEKORASI ---
int bintangX[5] = {0};
int bintangY[5] = {0};

// --- STATE STORE ---
int       itemKursor  = 0;
int       itemScroll  = 0;
int       itemDipilih = -1;
int       itemTotal   = 0;
int       fieldKursor = 0;
int       charIdx     = 0;
int       totalField  = 0;
int       caraBayar   = 0;
StoreField field[4]   = {0};
char      targetID[64]= {0};
bool      inputAngka  = true;   // Default: mode angka (0-9)

// -- SYSTEM API --
bool itemtersedia = false;
bool checkstatus = false;
const char* apiKeyH2H = "n69ZrluCowPuGGnJ9nP8cQHlHAp21WHGKE1O66eHz3BVEUYbwPPmXgLevypIOLsNezoC3vsLo5IOCkKsPFKs5tUCA1t4TjYiiU3f";
char nickname[64];
bool ceknickgagal = false;

// --- KEYBOARD CHARSETS (satu sumber buat input_system.c & display_system.c) ---
const char CS_ANGKA[] = "0123456789";
const char CS_HURUF[] = "abcdefghijklmnopqrstuvwxyz0123456789@._-ABCDEFGHIJKLMNOPQRSTUVWXYZ #";
const char CS_WIFI[]  = "abcdefghijklmnopqrstuvwxyz0123456789@_-.#! ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const int  CS_ANGKA_LEN = sizeof(CS_ANGKA) - 1;
const int  CS_HURUF_LEN = sizeof(CS_HURUF) - 1;
const int  CS_WIFI_LEN  = sizeof(CS_WIFI)  - 1;

void app_main(void) {
    // Pasang hook log SEBELUM log lain — biar log paling awal juga
    // ke-capture ke web viewer (lihat weblog_sys.h buat penjelasan
    // kenapa ini ada: 1 kabel USB-C aja, kepake buat ngetes).
    weblog_hook_install();

    // Tandain partition yang lagi jalan ini "valid" — kalau abis OTA
    // firmware baru ternyata gagal boot / crash-loop, bootloader bakal
    // otomatis rollback balik ke firmware lama (butuh baris ini biar
    // firmware yang SEKARANG juga ke-mark valid, bukan cuma yang baru).
    esp_ota_mark_app_valid_cancel_rollback();

    ESP_LOGI("JirStore", "Booting...");
    wifi_init();
    xTaskCreate(task_display, "Display", 8192, NULL, 1, NULL);
}
