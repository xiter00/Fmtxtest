#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "globals.h"
#include "wifi_sys.h"
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

void app_main(void) {
    ESP_LOGI("JirStore", "Booting...");
    wifi_init();
    xTaskCreate(task_display, "Display", 8192, NULL, 1, NULL);
}
