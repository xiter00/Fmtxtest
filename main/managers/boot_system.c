#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "globals.h"
#include "photo_data.h"
#include "ssd1306.h" 

#ifndef WHITE
#define WHITE 1
#endif
#ifndef BLACK
#define BLACK 0
#endif

// ========================================================
// Pembaca Bitmap gaya Adafruit buat library Baoshi
// ========================================================
void oled_draw_bitmap(uint8_t id, int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, ssd1306_color_t color) {
    int16_t byteWidth = (w + 7) / 8; 
    uint8_t byte = 0;
    for (int16_t j = 0; j < h; j++, y++) {
        for (int16_t i = 0; i < w; i++) {
            if (i & 7) byte <<= 1;
            else       byte   = bitmap[j * byteWidth + i / 8];
            
            if (byte & 0x80) ssd1306_draw_pixel(id, x + i, y, color);
        }
    }
}

// ========================================================
// 1. Fungsi Tampilan Logo Saja
// ========================================================
void tampilkanLogoDulu() {
    ssd1306_clear(0); // Pake aturan Baoshi
    
    // (id, x, y, bitmap, w, h, color)
    oled_draw_bitmap(0, 0, 0, my_photo_bmp, 128, 64, WHITE);
    
    ssd1306_refresh(0, true); // Pake aturan Baoshi
    vTaskDelay(pdMS_TO_TICKS(1500));
}

// ========================================================
// 2. Fungsi Intro Anime & Info Firmware
// ========================================================
void tampilkanIntroAnime() {
    for (int i = 0; i < 8; i++) {
        ssd1306_clear(0);
        
        for (int j = 0; j < 10; j++) {
            int y = esp_random() % 64; 
            ssd1306_draw_hline(0, 0, y, 128, WHITE); // Baoshi punya fungsi garis!
        }
        ssd1306_refresh(0, true);
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    ssd1306_clear(0);
    
    // Foto Anime di Kiri
    oled_draw_bitmap(0, 0, 0, foto_welcome, 128, 64, WHITE);
    

    ssd1306_refresh(0, true);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ========================================================
// Fungsi Bantu Efek Ngetik 
// ========================================================
void ketikTeks(const char* teks, int x, int y) {
    char hurufTemp[2] = {0, 0}; 
    int currentX = x;
    int panjang = strlen(teks);

    for (int i = 0; i < panjang; i++) {
        hurufTemp[0] = teks[i]; 
        
        // Pake aturan Baoshi
        ssd1306_draw_string_adafruit(0, currentX, y, hurufTemp, WHITE, BLACK);
        ssd1306_refresh(0, true);
        
        currentX += 6; 
        vTaskDelay(pdMS_TO_TICKS(25)); 
    }
}

// ========================================================
// 3. Fungsi Teks Splash (Hacker Booting)
// ========================================================
void tampilkanTeksSplash() {
    ssd1306_clear(0);

    ketikTeks(">> Created by: Andyy.", 0, 10);
    vTaskDelay(pdMS_TO_TICKS(150));
    vTaskDelay(pdMS_TO_TICKS(400));
}
