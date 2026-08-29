#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "globals.h"
#include "photo_data.h"

extern void task_display(void *pvParameters);

// ==========================================================
// MENU STATE
// ==========================================================
bool inSubMenu   = false;
int  currentMenu = 0;
int  currentSub  = 0;
int  topMenu     = 0;

// ==========================================================
// APP STATE
// ==========================================================
int appMode         = 0;
int brightnessValue = 150;

// ==========================================================
// CAROUSEL ANIMASI
// ==========================================================
int      carouselCurrentIdx  = 0;
int      carouselDirection   = 0;
bool     carouselAnimating   = false;
uint32_t carouselAnimStart   = 0;

// ==========================================================
// DEKORASI LATAR
// ==========================================================
int starX[5] = {0};
int starY[5] = {0};

// ==========================================================
// STORE STATE
// ==========================================================
int       storeKategori     = 0;
int       storeSubMenuIdx   = 0;
int       storeItemCursor   = 0;
int       storeScrollPos    = 0;
int       storeSelectedItem = -1;
int       storeFieldCursor  = 0;
int       storeCharPos      = 0;
int       storeCharIdx      = 0;
int       storeTotalItems   = 0;
int       storeTotalFields  = 0;
int       storePayMethod    = 0;
StoreField storeFields[4]   = {0};

// ==========================================================
// APP MAIN
// ==========================================================
void app_main(void) {
    ESP_LOGI("JirStore", "Booting JirStore...");
    xTaskCreate(task_display, "DisplayTask", 8192, NULL, 1, NULL);
}
