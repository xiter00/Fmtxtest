#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "globals.h"
#include "photo_data.h"
#include "ssd1306.h"
#include "i2c.h"
#include <math.h>
#include "driver/gpio.h"
#include "esp_log.h"

#define MAX_STARS 15

// ==========================================================
// FORWARD DECLARATIONS
// ==========================================================
extern void handleJoystick(void);
extern void tampilkanLogoDulu(void);
extern void tampilkanIntroAnime(void);
extern void tampilkanTeksSplash(void);
extern void oled_draw_bitmap(uint8_t id, int16_t x, int16_t y,
                             const uint8_t *bitmap, int16_t w, int16_t h,
                             ssd1306_color_t color);

void tampilkanMenuLogo(void);
void tampilkanMenuUtama(void);
void tampilkanStore(void);
void tampilkanBrightness(void);
void renderAboutScreen(void);
void renderRebootScreen(void);

bool introDone = false;

// ==========================================================
// INISIALISASI JOYSTICK
// ==========================================================
void init_joystick() {
    int pins[] = {PIN_LEFT, PIN_RIGHT, PIN_OK};
    for (int i = 0; i < 3; i++) {
        gpio_set_direction(pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(pins[i], GPIO_PULLUP_ONLY);
    }
}

// ==========================================================
// TASK DISPLAY UTAMA
// ==========================================================
void task_display(void *pvParameters) {
    init_joystick();
    if (ssd1306_init(0, 4, 3)) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ssd1306_select_font(0, 0);
        ssd1306_clear(0);
        ssd1306_refresh(0, true);
        ESP_LOGI("JirStore", "OLED Ready!");
    } else {
        ESP_LOGE("JirStore", "OLED Init Gagal!");
    }

    tampilkanLogoDulu();
    tampilkanIntroAnime();
    tampilkanTeksSplash();
    introDone = true;

    for (;;) {
        handleJoystick();

        switch (appMode) {
            case 0:
                if (!inSubMenu) tampilkanMenuLogo();
                else            tampilkanMenuUtama();
                break;
            case 3:  tampilkanBrightness();  break;
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 18:
            case 19:
                tampilkanStore(); break;
            case 14: renderAboutScreen();  break;
            case 15: renderRebootScreen(); break;
            default: break;
        }

        vTaskDelay(pdMS_TO_TICKS(33)); // ~30 FPS
    }
}

// ==========================================================
// ANIMASI & HELPER GAMBAR
// ==========================================================
typedef struct { float x, y, z; } Star;
Star stars[MAX_STARS];
bool starInit = false;

void initStars() {
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].x = (rand() % 128) - 64;
        stars[i].y = (rand() % 64)  - 32;
        stars[i].z = (rand() % 64)  + 1;
    }
    starInit = true;
}

uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void drawStarfield() {
    if (!starInit) initStars();
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].z -= 0.5f;
        if (stars[i].z <= 1) {
            stars[i].z = 64;
            stars[i].x = (rand() % 128) - 64;
            stars[i].y = (rand() % 64)  - 32;
        }
        int sx = (int)(stars[i].x / stars[i].z * 64 + 64);
        int sy = (int)(stars[i].y / stars[i].z * 32 + 32);
        if (sx >= 0 && sx < 128 && sy >= 10 && sy < 54)
            ssd1306_draw_pixel(0, sx, sy, WHITE);
    }
}

void drawWave() {
    for (int x = 0; x < 128; x++) {
        int y = 60 + (int)(sin((x + (int)millis() / 10) * 0.1) * 3);
        ssd1306_draw_pixel(0, x, y, WHITE);
    }
}

int getBounce(int speed, int range) {
    return (int)(sin(millis() / (float)speed) * range);
}

void drawLoadingBar(int x, int y, int w, int h, int progress) {
    ssd1306_draw_rectangle(0, x, y, w, h, WHITE);
    int fillW = (w * progress) / 100;
    if (fillW > w) fillW = w;
    ssd1306_fill_rectangle(0, x, y, fillW, h, WHITE);
    int offset = (millis() / 50) % 20;
    for (int i = -20; i < fillW; i += 15) {
        int lineX = x + i + offset;
        if (lineX > x && lineX < x + fillW)
            for (int j = 0; j < h; j++)
                ssd1306_draw_pixel(0, lineX, y + j, BLACK);
    }
}

// ==========================================================
// FORMAT HARGA (Indonesian style: 14.000 / 1.400.000)
// ==========================================================
void formatHarga(int harga, char *buf, int maxLen) {
    if (harga < 1000)
        snprintf(buf, maxLen, "%d", harga);
    else if (harga < 1000000)
        snprintf(buf, maxLen, "%d.%03d", harga / 1000, harga % 1000);
    else
        snprintf(buf, maxLen, "%d.%03d.%03d",
                 harga / 1000000, (harga / 1000) % 1000, harga % 1000);
}

// ==========================================================
// DATA MENU — KATEGORI & SUBMENU
// Kategori: 0=Diamond, 1=Pulsa, 2=E-Money, 3=Settings
// ==========================================================

static const char *katHeader[] = {
    "#> JirStore: GEM",
    "#> JirStore: HP ",
    "#> JirStore: $$$ ",
    "#> JirStore: SET"
};

// --- Submenu Labels ---
static const char *subMenuGem[]   = {"Mobile Legends", "Free Fire", "PUBG Mobile", "Genshin Impact"};
static const char *subMenuPulsa[] = {"Telkomsel", "XL Axiata", "Indosat", "Tri"};
static const char *subMenuMoney[] = {"DANA", "GoPay", "OVO", "ShopeePay"};
static const char *subMenuSet[]   = {"Brightness", "About", "Reboot"};

// --- Total submenu per kategori (DIAKSES DARI input_system.c) ---
const int totalSubPerKat[] = {4, 4, 4, 3};

// --- Icon small per submenu (10x10) ---
static const unsigned char *iconListGem[]   = {iconSmall_scan, iconSmall_wifi, iconSmall_sniff, iconSmall_spam};
static const unsigned char *iconListPulsa[] = {iconSmall_apple, iconSmall_android, iconSmall_conn, iconSmall_scan};
static const unsigned char *iconListMoney[] = {iconSmall_apple, iconSmall_android, iconSmall_conn, iconSmall_scan};
static const unsigned char *iconListSet[]   = {iconSmall_bright, iconSmall_info, iconSmall_repeat};

// ==========================================================
// DATA PRODUK
// ==========================================================

// --- DIAMOND: Mobile Legends ---
static const StoreProduk itemML[] = {
    {"5 Diamond",    "ML005",   1500},
    {"11 Diamond",   "ML011",   3000},
    {"22 Diamond",   "ML022",   6000},
    {"56 Diamond",   "ML056",  14000},
    {"86 Diamond",   "ML086",  22000},
    {"172 Diamond",  "ML172",  44000},
    {"257 Diamond",  "ML257",  65000},
    {"514 Diamond",  "ML514", 128000},
};

// --- DIAMOND: Free Fire ---
static const StoreProduk itemFF[] = {
    {"5 Diamond",    "FF005",   1500},
    {"70 Diamond",   "FF070",  10500},
    {"140 Diamond",  "FF140",  21000},
    {"355 Diamond",  "FF355",  52000},
    {"720 Diamond",  "FF720", 104000},
    {"1450 Diamond", "FF1450",205000},
};

// --- DIAMOND: PUBG Mobile ---
static const StoreProduk itemPUBG[] = {
    {"60 UC",    "PUBG060",  14000},
    {"120 UC",   "PUBG120",  28000},
    {"325 UC",   "PUBG325",  75000},
    {"660 UC",   "PUBG660", 150000},
    {"1800 UC",  "PUBG1800",400000},
};

// --- DIAMOND: Genshin Impact ---
static const StoreProduk itemGI[] = {
    {"60 Primogem",   "GI060",  14000},
    {"300+30 Primo",  "GI300",  75000},
    {"980+110 Primo", "GI980", 210000},
    {"1980+260 Primo","GI1980",420000},
};

static const int           totalItemDiamond[] = {8, 6, 5, 4};
static const StoreProduk  *tabelDiamond[]     = {itemML, itemFF, itemPUBG, itemGI};

// --- PULSA: Telkomsel ---
static const StoreProduk itemTsel[] = {
    {"Pulsa 5rb",   "TSEL5",    5000},
    {"Pulsa 10rb",  "TSEL10",  10000},
    {"Pulsa 20rb",  "TSEL20",  20000},
    {"Pulsa 50rb",  "TSEL50",  50000},
    {"Pulsa 100rb", "TSEL100",100000},
};
// --- PULSA: XL ---
static const StoreProduk itemXL[] = {
    {"Pulsa 5rb",   "XL5",    5000},
    {"Pulsa 10rb",  "XL10",  10000},
    {"Pulsa 20rb",  "XL20",  20000},
    {"Pulsa 50rb",  "XL50",  50000},
    {"Pulsa 100rb", "XL100",100000},
};
// --- PULSA: Indosat ---
static const StoreProduk itemIsel[] = {
    {"Pulsa 5rb",   "ISEL5",    5000},
    {"Pulsa 10rb",  "ISEL10",  10000},
    {"Pulsa 20rb",  "ISEL20",  20000},
    {"Pulsa 50rb",  "ISEL50",  50000},
    {"Pulsa 100rb", "ISEL100",100000},
};
// --- PULSA: Tri ---
static const StoreProduk itemTri[] = {
    {"Pulsa 5rb",  "TRI5",   5000},
    {"Pulsa 10rb", "TRI10", 10000},
    {"Pulsa 20rb", "TRI20", 20000},
    {"Pulsa 50rb", "TRI50", 50000},
};

static const int          totalItemPulsa[] = {5, 5, 5, 4};
static const StoreProduk *tabelPulsa[]     = {itemTsel, itemXL, itemIsel, itemTri};

// --- E-MONEY ---
static const StoreProduk itemDANA[] = {
    {"DANA 10rb",   "DANA10",   11000},
    {"DANA 20rb",   "DANA20",   21500},
    {"DANA 50rb",   "DANA50",   52000},
    {"DANA 100rb",  "DANA100", 103000},
    {"DANA 200rb",  "DANA200", 205000},
};
static const StoreProduk itemGoPay[] = {
    {"GoPay 10rb",  "GP10",   11000},
    {"GoPay 20rb",  "GP20",   21500},
    {"GoPay 50rb",  "GP50",   52000},
    {"GoPay 100rb", "GP100", 103000},
    {"GoPay 200rb", "GP200", 205000},
};
static const StoreProduk itemOVO[] = {
    {"OVO 10rb",   "OVO10",   11000},
    {"OVO 20rb",   "OVO20",   21500},
    {"OVO 50rb",   "OVO50",   52000},
    {"OVO 100rb",  "OVO100", 103000},
    {"OVO 200rb",  "OVO200", 205000},
};
static const StoreProduk itemSPay[] = {
    {"SPay 10rb",  "SPAY10",   11000},
    {"SPay 20rb",  "SPAY20",   21500},
    {"SPay 50rb",  "SPAY50",   52000},
    {"SPay 100rb", "SPAY100", 103000},
    {"SPay 200rb", "SPAY200", 205000},
};

static const int          totalItemEMoney[] = {5, 5, 5, 5};
static const StoreProduk *tabelEMoney[]     = {itemDANA, itemGoPay, itemOVO, itemSPay};

// ==========================================================
// HELPER AKSES DATA PRODUK (dipanggil juga dari input_system)
// ==========================================================

int storeGetTotalItems(int kat, int sub) {
    if (kat == 0) return totalItemDiamond[sub];
    if (kat == 1) return totalItemPulsa[sub];
    if (kat == 2) return totalItemEMoney[sub];
    return 0;
}

int storeGetTotalSubs(int kat) {
    return totalSubPerKat[kat];
}

const StoreProduk *storeGetItem(int kat, int sub, int idx) {
    if (kat == 0) return &tabelDiamond[sub][idx];
    if (kat == 1) return &tabelPulsa[sub][idx];
    if (kat == 2) return &tabelEMoney[sub][idx];
    return NULL;
}

// Setup field input berdasarkan kategori + sub
void storeSetupFields(int kat, int sub) {
    memset(storeFields, 0, sizeof(storeFields));
    if (kat == 0) {           // Diamond
        if (sub == 0) {       // Mobile Legends: User ID + Zone ID
            strcpy(storeFields[0].label, "User ID");
            strcpy(storeFields[1].label, "Zone ID");
            storeTotalFields = 2;
        } else {              // FF / PUBG / Genshin: cuma User ID
            strcpy(storeFields[0].label, "User ID");
            storeTotalFields = 1;
        }
    } else {                  // Pulsa & E-Money: Nomor HP
        strcpy(storeFields[0].label, "Nomor HP");
        storeTotalFields = 1;
    }
}

// ==========================================================
// TAMPILAN: MENU LOGO (CAROUSEL 4 KATEGORI)
// ==========================================================
void tampilkanMenuLogo() {
    ssd1306_clear(0);
    drawStarfield();
    drawWave();

    ssd1306_draw_string_adafruit(0, 0, 0, (char *)katHeader[currentMenu], WHITE, BLACK);
    ssd1306_draw_hline(0, 0, 9, 128, WHITE);

    const unsigned char *bigIcon;
    if      (currentMenu == 0) bigIcon = logo_game_32;
    else if (currentMenu == 1) bigIcon = logo_hp_32;
    else if (currentMenu == 2) bigIcon = logo_emoney_32;
    else                       bigIcon = logo_settings_32;

    int iconBounce = getBounce(300, 2);
    oled_draw_bitmap(0, 47, 20 + iconBounce, bigIcon, 32, 32, WHITE);

    ssd1306_draw_string_adafruit(0, 18, 30, "<", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 102, 30, ">", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 38, 56, ">SELECT<", WHITE, BLACK);

    ssd1306_refresh(0, true);
}

// ==========================================================
// TAMPILAN: SUBMENU LIST
// ==========================================================
void tampilkanMenuUtama() {
    ssd1306_clear(0);
    drawStarfield();
    drawWave();

    ssd1306_draw_string_adafruit(0, 0, 0, (char *)katHeader[currentMenu], WHITE, BLACK);
    ssd1306_draw_hline(0, 0, 9, 128, WHITE);

    int totalSub = totalSubPerKat[currentMenu];

    for (int i = 0; i < 5; i++) {
        int itemIndex = topMenu + i;
        if (itemIndex >= totalSub) break;

        int yPos      = 13 + (i * 10);
        int textColor = WHITE, bgColor = BLACK;
        int iconBounce = 0, xPad = 0;

        if (itemIndex == currentSub) {
            ssd1306_fill_rectangle(0, 0, yPos - 1, 128, 10, WHITE);
            // Efek data-stream (garis ngalir di ujung kanan)
            int slide = (millis() / 40) % 20;
            int animX = 125 - slide;
            ssd1306_fill_rectangle(0, animX,     yPos - 1, 2, 10, BLACK);
            ssd1306_fill_rectangle(0, animX + 6, yPos - 1, 4, 10, BLACK);
            iconBounce = getBounce(200, 2);
            xPad       = 4;
            textColor  = BLACK;
            bgColor    = WHITE;
        }

        const unsigned char *iconSmall;
        if      (currentMenu == 0) iconSmall = iconListGem[itemIndex];
        else if (currentMenu == 1) iconSmall = iconListPulsa[itemIndex];
        else if (currentMenu == 2) iconSmall = iconListMoney[itemIndex];
        else                       iconSmall = iconListSet[itemIndex];

        oled_draw_bitmap(0, 2 + xPad, (yPos - 1) + iconBounce, iconSmall, 10, 10, textColor);

        const char *textToPrint;
        if      (currentMenu == 0) textToPrint = subMenuGem[itemIndex];
        else if (currentMenu == 1) textToPrint = subMenuPulsa[itemIndex];
        else if (currentMenu == 2) textToPrint = subMenuMoney[itemIndex];
        else                       textToPrint = subMenuSet[itemIndex];

        ssd1306_draw_string_adafruit(0, 18 + xPad, yPos, (char *)textToPrint, textColor, bgColor);
    }

    ssd1306_refresh(0, true);
}

// ==========================================================
// TAMPILAN: SEMUA SCREEN STORE (1 FUNGSI, BANYAK STATE)
//
// appMode 9  — Daftar item produk
// appMode 10 — Detail item (full screen)
// appMode 11 — List input field (User ID, Zone ID, dst)
// appMode 12 — Char input (ketik 1 huruf per OK)
// appMode 13 — Konfirmasi detail (review + harga)
// appMode 18 — Action menu (BAYAR / QRIS)
// appMode 19 — QRIS screen
// ==========================================================
void tampilkanStore() {
    ssd1306_clear(0);
    char buf[48];

    // Charset buat preview karakter di mode 12
    static const char CHARSET[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz _-.";
    #define CHARSET_LEN 68

    // ======================================================
    // MODE 9: DAFTAR ITEM PRODUK
    // Logika nav: > = geser bawah | OK = pilih | < = kembali
    // ======================================================
    if (appMode == 9) {
        // Header: nama sub-kategori aktif
        ssd1306_fill_rectangle(0, 0, 0, 128, 10, WHITE);
        const char *subName = "";
        if      (storeKategori == 0) subName = subMenuGem[storeSubMenuIdx];
        else if (storeKategori == 1) subName = subMenuPulsa[storeSubMenuIdx];
        else                         subName = subMenuMoney[storeSubMenuIdx];
        ssd1306_draw_string_adafruit(0, 2, 1, (char *)subName, BLACK, WHITE);

        int total = storeGetTotalItems(storeKategori, storeSubMenuIdx);

        // 3 baris item, layout per baris: [No][Nama(scroll)][Harga]
        for (int i = 0; i < 3; i++) {
            int idx = storeScrollPos + i;
            if (idx >= total) break;

            const StoreProduk *p  = storeGetItem(storeKategori, storeSubMenuIdx, idx);
            int yPos   = 12 + (i * 14);
            int txtCol = WHITE, bgCol = BLACK;
            bool isSelected = (i == storeItemCursor);

            if (isSelected) {
                ssd1306_fill_rectangle(0, 0, yPos - 1, 128, 13, WHITE);
                txtCol = BLACK; bgCol = WHITE;
            }

            // Nomor item
            snprintf(buf, sizeof(buf), "%d.", idx + 1);
            ssd1306_draw_string_adafruit(0, 1, yPos + 1, buf, txtCol, bgCol);

            // Nama produk — auto-scroll jika terpilih & nama panjang
            const int nameMaxChar = 11;
            int nameLen = strlen(p->nama);
            char nameShow[16] = {0};
            if (isSelected && nameLen > nameMaxChar) {
                int kelebihan = nameLen - nameMaxChar;
                int offset    = (millis() / 300) % (kelebihan + 4);
                if (offset > kelebihan) offset = kelebihan;
                strncpy(nameShow, p->nama + offset, nameMaxChar);
            } else {
                strncpy(nameShow, p->nama,
                        nameLen > nameMaxChar ? nameMaxChar : nameLen);
            }
            ssd1306_draw_string_adafruit(0, 16, yPos + 1, nameShow, txtCol, bgCol);

            // Harga (align kanan area)
            char hargaBuf[12];
            formatHarga(p->harga, hargaBuf, sizeof(hargaBuf));
            ssd1306_draw_string_adafruit(0, 84, yPos + 1, hargaBuf, txtCol, bgCol);
        }

        // Footer
        ssd1306_fill_rectangle(0, 0, 54, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2,  55, "< BACK",      BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 80, 55, "[OK] PILIH",  BLACK, WHITE);
    }

    // ======================================================
    // MODE 10: DETAIL ITEM (FULL SCREEN)
    // Logika: < = kembali list | OK = lanjut ke input data
    // ======================================================
    else if (appMode == 10) {
        const StoreProduk *p = storeGetItem(storeKategori, storeSubMenuIdx, storeSelectedItem);

        ssd1306_fill_rectangle(0, 0, 0, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 22, 1, "DETAIL PRODUK", BLACK, WHITE);

        // Nama produk — scroll otomatis
        int nameLen = strlen(p->nama);
        char nameShow[24] = {0};
        const int maxChar = 20;
        if (nameLen > maxChar) {
            int kelebihan = nameLen - maxChar;
            int offset    = (millis() / 300) % (kelebihan + 4);
            if (offset > kelebihan) offset = kelebihan;
            strncpy(nameShow, p->nama + offset, maxChar);
        } else {
            strcpy(nameShow, p->nama);
        }
        ssd1306_draw_string_adafruit(0, 5, 14, "Produk:", WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 5, 24, nameShow,  WHITE, BLACK);

        // Harga
        char hargaBuf[14];
        formatHarga(p->harga, hargaBuf, sizeof(hargaBuf));
        snprintf(buf, sizeof(buf), "Rp %s", hargaBuf);
        ssd1306_draw_string_adafruit(0, 5, 36, "Harga:",  WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 5, 46, buf,       WHITE, BLACK);

        ssd1306_fill_rectangle(0, 0, 54, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2,  55, "< BACK",    BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 80, 55, "[OK] BELI", BLACK, WHITE);
    }

    // ======================================================
    // MODE 11: LIST INPUT FIELD
    // Logika: > = geser field | OK = edit/konfirmasi | < = kembali
    // ======================================================
    else if (appMode == 11) {
        ssd1306_fill_rectangle(0, 0, 0, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 12, 1, "DATA PEMBELIAN", BLACK, WHITE);

        // Cek apakah semua field sudah diisi
        bool semuaIsi = true;
        for (int f = 0; f < storeTotalFields; f++)
            if (strlen(storeFields[f].value) == 0) { semuaIsi = false; break; }

        int totalRows = storeTotalFields + 1; // Fields + tombol KONFIRMASI

        for (int i = 0; i < totalRows; i++) {
            int yPos      = 13 + (i * 13);
            int txtCol    = WHITE, bgCol = BLACK;
            bool isActive = (i == storeFieldCursor);

            if (isActive) {
                ssd1306_fill_rectangle(0, 0, yPos - 1, 128, 12, WHITE);
                txtCol = BLACK; bgCol = WHITE;
                // Penanda aktif
                ssd1306_draw_string_adafruit(0, 2, yPos, ">", txtCol, bgCol);
            }

            if (i < storeTotalFields) {
                // Baris field input
                snprintf(buf, sizeof(buf), "%s:", storeFields[i].label);
                ssd1306_draw_string_adafruit(0, 10, yPos, buf, txtCol, bgCol);

                // Value: tampil "---" kalau kosong
                const char *valShow = (strlen(storeFields[i].value) == 0)
                                      ? "---" : storeFields[i].value;
                int vLen = strlen(valShow);
                char vBuf[16] = {0};
                strncpy(vBuf, valShow, vLen > 11 ? 11 : vLen);
                ssd1306_draw_string_adafruit(0, 72, yPos, vBuf, txtCol, bgCol);
            } else {
                // Baris tombol KONFIRMASI
                const char *konfLabel = semuaIsi ? "> [KONFIRMASI]" : "> [ISI DULU!]";
                ssd1306_draw_string_adafruit(0, 14, yPos, konfLabel, txtCol, bgCol);
            }
        }

        ssd1306_fill_rectangle(0, 0, 54, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2,  55, "< BACK",    BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 80, 55, "[OK] EDIT", BLACK, WHITE);
    }

    // ======================================================
    // MODE 12: INPUT KARAKTER (KETIK PER HURUF)
    //
    // BTN_RIGHT = ganti karakter (cycling CHARSET)
    // BTN_OK    = tambah karakter ke buffer  |  2x cepet = SELESAI
    // BTN_LEFT  = hapus char terakhir        |  2x cepet = BATAL
    // ======================================================
    else if (appMode == 12) {
        // Header: nama field yang lagi diisi
        ssd1306_fill_rectangle(0, 0, 0, 128, 10, WHITE);
        snprintf(buf, sizeof(buf), "INPUT: %s", storeFields[storeFieldCursor].label);
        ssd1306_draw_string_adafruit(0, 2, 1, buf, BLACK, WHITE);

        // Buffer yang sudah diketik + underscore kursor
        char displayBuf[32] = {0};
        strcpy(displayBuf, storeFields[storeFieldCursor].value);
        int bufLen = strlen(displayBuf);
        if (bufLen < 27) strcat(displayBuf, "_");
        ssd1306_draw_string_adafruit(0, 2, 13, displayBuf, WHITE, BLACK);

        // Garis batas buffer
        ssd1306_draw_hline(0, 0, 22, 128, WHITE);

        // Kotak besar karakter yang lagi dipilih (tengah layar)
        ssd1306_fill_rectangle(0, 42, 24, 22, 20, WHITE);   // Kotak putih
        char charShow[2] = { CHARSET[storeCharIdx], '\0' };
        ssd1306_draw_string_adafruit(0, 50, 29, charShow, BLACK, WHITE); // Huruf hitam di atas putih

        // Panah kiri-kanan
        ssd1306_draw_string_adafruit(0, 26, 30, "<", WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 68, 30, ">", WHITE, BLACK);

        // Info posisi karakter di charset
        snprintf(buf, sizeof(buf), "%d/%d", storeCharIdx + 1, CHARSET_LEN);
        ssd1306_draw_string_adafruit(0, 84, 27, buf, WHITE, BLACK);

        // Hint singkat
        ssd1306_draw_string_adafruit(0, 2, 46, ">Gnt | OK Tmb | 2x=Done", WHITE, BLACK);

        // Footer minimal (tanpa fill agar hint tetap keliatan)
        ssd1306_draw_hline(0, 0, 53, 128, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 55, "<Hapus | 2x<= Batal", WHITE, BLACK);
    }

    // ======================================================
    // MODE 13: KONFIRMASI DETAIL (REVIEW SEBELUM BAYAR)
    // Logika: < = kembali ke input | OK = lanjut ke action menu
    // ======================================================
    else if (appMode == 13) {
        const StoreProduk *p = storeGetItem(storeKategori, storeSubMenuIdx, storeSelectedItem);

        ssd1306_fill_rectangle(0, 0, 0, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 28, 1, "KONFIRMASI", BLACK, WHITE);

        // Nama produk (scroll otomatis)
        int nameLen = strlen(p->nama);
        char nameShow[22] = {0};
        const int maxNC = 20;
        if (nameLen > maxNC) {
            int kelebihan = nameLen - maxNC;
            int offset    = (millis() / 350) % (kelebihan + 4);
            if (offset > kelebihan) offset = kelebihan;
            strncpy(nameShow, p->nama + offset, maxNC);
        } else {
            strcpy(nameShow, p->nama);
        }
        ssd1306_draw_string_adafruit(0, 2, 13, nameShow, WHITE, BLACK);

        // Garis tipis pembatas
        ssd1306_draw_hline(0, 0, 22, 128, WHITE);

        // Harga
        char hargaBuf[14];
        formatHarga(p->harga, hargaBuf, sizeof(hargaBuf));
        snprintf(buf, sizeof(buf), "Harga : Rp %s", hargaBuf);
        ssd1306_draw_string_adafruit(0, 2, 25, buf, WHITE, BLACK);

        // Field values
        for (int i = 0; i < storeTotalFields && i < 2; i++) {
            snprintf(buf, sizeof(buf), "%-8s: %s",
                     storeFields[i].label, storeFields[i].value);
            ssd1306_draw_string_adafruit(0, 2, 35 + (i * 10), buf, WHITE, BLACK);
        }

        ssd1306_fill_rectangle(0, 0, 54, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2,  55, "< BACK",      BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 72, 55, "[OK] LANJUT", BLACK, WHITE);
    }

    // ======================================================
    // MODE 18: ACTION MENU — PILIH BAYAR ATAU QRIS
    // Logika: > = toggle pilihan | OK = pilih | < = kembali
    // Rolling menu seperti action menu WiFi scanner sebelumnya
    // ======================================================
    else if (appMode == 18) {
        ssd1306_fill_rectangle(0, 0, 0, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 12, 1, "PILIH PEMBAYARAN", BLACK, WHITE);

        // Blok putih statis di tengah (area item terpilih)
        ssd1306_fill_rectangle(0, 0, 24, 128, 16, WHITE);

        static const char         *menuPay[]  = {"TRANSFER/BAYAR", "QRIS"};
        static const unsigned char *iconPay[] = {iconSmall_conn,   iconSmall_scan};

        for (int i = 0; i < 2; i++) {
            int diff = i - storePayMethod;     // Jarak dari pilihan aktif
            int yPos = 27 + (diff * 16);       // Center di y=27

            if (yPos > 10 && yPos < 46) {
                if (diff == 0) {   // Item terpilih (warna terbalik)
                    oled_draw_bitmap(0, 26, yPos - 1, iconPay[i], 10, 10, BLACK);
                    ssd1306_draw_string_adafruit(0, 42, yPos, (char *)menuPay[i], BLACK, WHITE);
                    ssd1306_draw_string_adafruit(0, 10, yPos, ">", BLACK, WHITE);
                    ssd1306_draw_string_adafruit(0, 112, yPos, "<", BLACK, WHITE);
                } else {           // Item tidak terpilih (warna normal)
                    oled_draw_bitmap(0, 30, yPos, iconPay[i], 10, 10, WHITE);
                    ssd1306_draw_string_adafruit(0, 46, yPos + 1, (char *)menuPay[i], WHITE, BLACK);
                }
            }
        }

        ssd1306_fill_rectangle(0, 0, 54, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2,  55, "< BACK",   BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 85, 55, "[OK] GO",  BLACK, WHITE);
    }

    // ======================================================
    // MODE 19: QRIS SCREEN
    // QR Code 58x58 di kiri, info harga + produk di kanan
    // Logika: < = kembali ke action menu
    // ======================================================
    else if (appMode == 19) {
        const StoreProduk *p = storeGetItem(storeKategori, storeSubMenuIdx, storeSelectedItem);

        // Gambar QR statis 58x58 di kiri (x=0, y=0 s/d y=57)
        oled_draw_bitmap(0, 0, 0, qrisku, 58, 58, WHITE);

        // Garis vertikal pembatas kiri-kanan
        ssd1306_draw_vline(0, 59, 0, 57, WHITE);

        // ---- Panel kanan (x=61 s/d x=127) ----

        // Harga
        char hargaBuf[14];
        formatHarga(p->harga, hargaBuf, sizeof(hargaBuf));
        ssd1306_draw_string_adafruit(0, 61, 1,  "HARGA:",  WHITE, BLACK);
        snprintf(buf, sizeof(buf), "Rp%s", hargaBuf);
        ssd1306_draw_string_adafruit(0, 61, 11, buf, WHITE, BLACK);

        // Nama produk (scroll)
        int nameLen = strlen(p->nama);
        char nameShow[13] = {0};
        const int maxNC2 = 11;
        if (nameLen > maxNC2) {
            int kelebihan = nameLen - maxNC2;
            int offset    = (millis() / 300) % (kelebihan + 4);
            if (offset > kelebihan) offset = kelebihan;
            strncpy(nameShow, p->nama + offset, maxNC2);
        } else {
            strcpy(nameShow, p->nama);
        }
        ssd1306_draw_string_adafruit(0, 61, 24, nameShow, WHITE, BLACK);

        // Field values
        for (int i = 0; i < storeTotalFields && i < 2; i++) {
            int fLen = strlen(storeFields[i].value);
            char fBuf[13] = {0};
            strncpy(fBuf, storeFields[i].value, fLen > 11 ? 11 : fLen);
            ssd1306_draw_string_adafruit(0, 61, 37 + (i * 10), fBuf, WHITE, BLACK);
        }

        // Footer tipis (tanpa fill, biar gak nutup QR)
        ssd1306_draw_hline(0, 0, 57, 128, WHITE);
        ssd1306_draw_string_adafruit(0, 2,  58, "< BACK",   WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 61, 58, "SCAN&PAY", WHITE, BLACK);
    }

    ssd1306_refresh(0, true);
}

// ==========================================================
// BRIGHTNESS SCREEN
// BTN_RIGHT = naik +20 | BTN_OK = turun -20 | BTN_LEFT = back
// ==========================================================
void tampilkanBrightness() {
    ssd1306_clear(0);
    char buf[16];

    ssd1306_fill_rectangle(0, 0, 0, 128, 10, WHITE);
    ssd1306_draw_string_adafruit(0, 35, 1, "BRIGHTNESS", BLACK, WHITE);

    ssd1306_draw_rectangle(0, 14, 28, 100, 12, WHITE);
    int barWidth = map(brightnessValue, 0, 255, 0, 96);
    ssd1306_fill_rectangle(0, 16, 30, barWidth, 8, WHITE);

    int persen = map(brightnessValue, 0, 255, 0, 100);
    snprintf(buf, sizeof(buf), "%d%%", persen);
    ssd1306_draw_string_adafruit(0, 55, 45, buf, WHITE, BLACK);

    ssd1306_fill_rectangle(0, 0, 54, 128, 10, WHITE);
    ssd1306_draw_string_adafruit(0, 2, 55,  "[<]BACK", BLACK, WHITE);
    ssd1306_draw_string_adafruit(0, 60, 55, "[>]+ [OK]-", BLACK, WHITE);

    ssd1306_refresh(0, true);
}

void setOledBrightness(uint8_t level) {
    i2c_start();
    i2c_write(0x78);
    i2c_write(0x00);
    i2c_write(0x81);
    i2c_write(level);
    i2c_stop();
}

// ==========================================================
// ABOUT & REBOOT SCREEN
// ==========================================================
void renderAboutScreen() {
    ssd1306_clear(0);
    ssd1306_draw_rectangle(0, 0, 0, 128, 64, WHITE);
    ssd1306_draw_rectangle(0, 2, 2, 124, 60, WHITE);
    ssd1306_draw_string_adafruit(0, 30, 8, "JIR STORE", WHITE, BLACK);
    ssd1306_draw_hline(0, 25, 18, 78, WHITE);
    ssd1306_draw_string_adafruit(0, 10, 25, "Ver : 1.0.0",     WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 10, 35, "Core: ESP32C3",   WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 10, 45, "By  : Andyy",     WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 90, 45, "[<]", WHITE, BLACK);
    ssd1306_refresh(0, true);
}

void renderRebootScreen() {
    ssd1306_clear(0);
    ssd1306_draw_rectangle(0, 5, 5, 118, 54, WHITE);
    ssd1306_draw_string_adafruit(0, 20, 20, "Reboot sekarang?", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 2,  55, "< NO",  WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 95, 55, "OK >",  WHITE, BLACK);
    ssd1306_refresh(0, true);
}
