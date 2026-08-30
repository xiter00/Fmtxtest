#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "globals.h"
#include "wifi_sys.h"
#include "photo_data.h"
#include "ssd1306.h"
#include "i2c.h"
#include <math.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "system.h"

#define MAX_BINTANG 15

// --- EXTERN ---
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
// INIT JOYSTICK
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
// appMode 0  → menu logo + submenu
// appMode 1  → brightness
// appMode 2-6, 9-12 → store (satu fungsi)
// appMode 7  → about
// appMode 8  → reboot
// ==========================================================
void task_display(void *pvParameters) {
    init_joystick();
    if (ssd1306_init(0, 4, 3)) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ssd1306_select_font(0, 0);
        ssd1306_clear(0);
        ssd1306_refresh(0, true);
        ESP_LOGI("JirStore", "OLED OK");
    } else {
        ESP_LOGE("JirStore", "OLED Gagal");
    }

    tampilkanLogoDulu();
    tampilkanIntroAnime();
    tampilkanTeksSplash();
    introDone = true;

    for (;;) {
        handleJoystick();
        switch (appMode) {
            case 0:
                if (!diSubMenu) tampilkanMenuLogo();
                else            tampilkanMenuUtama();
                break;
            case 1:  tampilkanBrightness(); break;
            case 2: case 3: case 4: case 5:
            case 6: case 9: case 10: case 11: case 12:
                tampilkanStore(); break;
            case 7: renderAboutScreen();  break;
            case 8: renderRebootScreen(); break;
            default: break;
        }
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

// ==========================================================
// ANIMASI BINTANG & HELPER
// ==========================================================
typedef struct { float x, y, z; } Bintang;
Bintang bintang[MAX_BINTANG];
bool bintangInit = false;

void initBintang() {
    for (int i = 0; i < MAX_BINTANG; i++) {
        bintang[i].x = (rand() % 128) - 64;
        bintang[i].y = (rand() % 64)  - 32;
        bintang[i].z = (rand() % 64)  + 1;
    }
    bintangInit = true;
}

uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void drawStarfield() {
    if (!bintangInit) initBintang();
    for (int i = 0; i < MAX_BINTANG; i++) {
        bintang[i].z -= 0.5f;
        if (bintang[i].z <= 1) {
            bintang[i].z = 64;
            bintang[i].x = (rand() % 128) - 64;
            bintang[i].y = (rand() % 64)  - 32;
        }
        int sx = (int)(bintang[i].x / bintang[i].z * 64 + 64);
        int sy = (int)(bintang[i].y / bintang[i].z * 32 + 32);
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
        int lx = x + i + offset;
        if (lx > x && lx < x + fillW)
            for (int j = 0; j < h; j++)
                ssd1306_draw_pixel(0, lx, y+j, BLACK);
    }
}

// --- Format harga: 14.000 / 1.400.000 ---
void formatHarga(int harga, char *buf, int maxLen) {
    if (harga < 1000)
        snprintf(buf, maxLen, "%d", harga);
    else if (harga < 1000000)
        snprintf(buf, maxLen, "%d.%03d", harga/1000, harga%1000);
    else
        snprintf(buf, maxLen, "%d.%03d.%03d",
                 harga/1000000, (harga/1000)%1000, harga%1000);
}

// --- Scroll teks jika terlalu panjang ---
// out: isi maxChar karakter dari src, scroll otomatis jika aktif
void scrollTeks(const char *src, char *out, int maxChar, bool aktif) {
    int len = strlen(src);
    if (!aktif || len <= maxChar) {
        strncpy(out, src, maxChar);
        out[maxChar] = '\0';
        return;
    }
    int lebih  = len - maxChar;
    int offset = (millis() / 280) % (lebih + 5);
    if (offset > lebih) offset = lebih;
    strncpy(out, src + offset, maxChar);
    out[maxChar] = '\0';
}



// ==========================================================
// DATA MENU
// ==========================================================
static const char *katHeader[] = {
    "#> Game",
    "#> Pulsa ",
    "#> E-Money ",
    "#> Setting"
};

static const char *subGem[]   = {"Mobile Legends", "Free Fire", "PUBG Mobile", "Genshin Impact"};
static const char *subPulsa[] = {"Telkomsel", "XL Axiata", "Indosat", "Tri"};
static const char *subMoney[] = {"DANA", "GoPay", "OVO", "ShopeePay"};
static const char *subSet[]   = {"Brightness", "About", "Reboot", "WiFi"};

const int totalSubKat[] = {4, 4, 4, 4};  // Diakses dari input_system

static const unsigned char *ikonGem[]   = {iconSmall_scan, iconSmall_wifi, iconSmall_sniff, iconSmall_spam};
static const unsigned char *ikonPulsa[] = {iconSmall_apple, iconSmall_android, iconSmall_conn, iconSmall_scan};
static const unsigned char *ikonMoney[] = {iconSmall_apple, iconSmall_android, iconSmall_conn, iconSmall_scan};
static const unsigned char *ikonSet[]   = {iconSmall_bright, iconSmall_info, iconSmall_repeat};

// ==========================================================
// DATA PRODUK
// ==========================================================
static const StoreProduk itemML[] = {
    {"5 Diamond",    "ML005",   1500}, {"11 Diamond",   "ML011",   3000},
    {"22 Diamond",   "ML022",   6000}, {"56 Diamond",   "ML056",  14000},
    {"86 Diamond",   "ML086",  22000}, {"172 Diamond",  "ML172",  44000},
    {"257 Diamond",  "ML257",  65000}, {"514 Diamond",  "ML514", 128000},
};
static const StoreProduk itemFF[] = {
    {"5 Diamond",    "FF005",   1500}, {"70 Diamond",   "FF070",  10500},
    {"140 Diamond",  "FF140",  21000}, {"355 Diamond",  "FF355",  52000},
    {"720 Diamond",  "FF720", 104000}, {"1450 Diamond", "FF1450",205000},
};
static const StoreProduk itemPUBG[] = {
    {"60 UC",  "PUBG060",  14000}, {"120 UC",  "PUBG120",  28000},
    {"325 UC", "PUBG325",  75000}, {"660 UC",  "PUBG660", 150000},
    {"1800 UC","PUBG1800",400000},
};
static const StoreProduk itemGI[] = {
    {"60 Primogem",   "GI060",  14000}, {"300+30 Primo",  "GI300",  75000},
    {"980+110 Primo", "GI980", 210000}, {"1980+260 Primo","GI1980",420000},
};
static const int           totDiamond[] = {8, 6, 5, 4};
static const StoreProduk  *tabDiamond[] = {itemML, itemFF, itemPUBG, itemGI};

static const StoreProduk itemTsel[] = {
    {"Pulsa 5rb","TSEL5",5000},  {"Pulsa 10rb","TSEL10",10000},
    {"Pulsa 20rb","TSEL20",20000},{"Pulsa 50rb","TSEL50",50000},
    {"Pulsa 100rb","TSEL100",100000},
};
static const StoreProduk itemXL[] = {
    {"Pulsa 5rb","XL5",5000},  {"Pulsa 10rb","XL10",10000},
    {"Pulsa 20rb","XL20",20000},{"Pulsa 50rb","XL50",50000},
    {"Pulsa 100rb","XL100",100000},
};
static const StoreProduk itemIsel[] = {
    {"Pulsa 5rb","ISEL5",5000},  {"Pulsa 10rb","ISEL10",10000},
    {"Pulsa 20rb","ISEL20",20000},{"Pulsa 50rb","ISEL50",50000},
    {"Pulsa 100rb","ISEL100",100000},
};
static const StoreProduk itemTri[] = {
    {"Pulsa 5rb","TRI5",5000},  {"Pulsa 10rb","TRI10",10000},
    {"Pulsa 20rb","TRI20",20000},{"Pulsa 50rb","TRI50",50000},
};
static const int          totPulsa[] = {5, 5, 5, 4};
static const StoreProduk *tabPulsa[] = {itemTsel, itemXL, itemIsel, itemTri};

static const StoreProduk itemDANA[]  = {
    {"DANA 10rb","DANA10",11000},  {"DANA 20rb","DANA20",21500},
    {"DANA 50rb","DANA50",52000},  {"DANA 100rb","DANA100",103000},
    {"DANA 200rb","DANA200",205000},
};
static const StoreProduk itemGoPay[] = {
    {"GoPay 10rb","GP10",11000},   {"GoPay 20rb","GP20",21500},
    {"GoPay 50rb","GP50",52000},   {"GoPay 100rb","GP100",103000},
    {"GoPay 200rb","GP200",205000},
};
static const StoreProduk itemOVO[]   = {
    {"OVO 10rb","OVO10",11000},    {"OVO 20rb","OVO20",21500},
    {"OVO 50rb","OVO50",52000},    {"OVO 100rb","OVO100",103000},
    {"OVO 200rb","OVO200",205000},
};
static const StoreProduk itemSPay[]  = {
    {"SPay 10rb","SPAY10",11000},  {"SPay 20rb","SPAY20",21500},
    {"SPay 50rb","SPAY50",52000},  {"SPay 100rb","SPAY100",103000},
    {"SPay 200rb","SPAY200",205000},
};
static const int          totMoney[] = {5, 5, 5, 5};
static const StoreProduk *tabMoney[] = {itemDANA, itemGoPay, itemOVO, itemSPay};

// ==========================================================
// HELPER AKSES DATA
// ==========================================================
int storeGetTotal(int kat, int sub) {
    if (kat == 0) return totDiamond[sub];
    if (kat == 1) return totPulsa[sub];
    if (kat == 2) return totMoney[sub];
    return 0;
}

const StoreProduk *storeGetItem(int kat, int sub, int idx) {
    if (kat == 0) return &tabDiamond[sub][idx];
    if (kat == 1) return &tabPulsa[sub][idx];
    if (kat == 2) return &tabMoney[sub][idx];
    return NULL;
}

// ==========================================================
// KONFIGURASI FIELD INPUT PER PRODUK
//
// Mau ganti field suatu game? Ubah 1 nilai di tabel configField.
// Mau tambah tipe field baru? Tambah enum + 1 case di switch.
//
// Tipe yang tersedia:
//   F_HP      = Nomor HP
//   F_ID      = User ID
//   F_ID_ZONE = User ID + Zone ID
//   F_EMAIL   = Email
//   F_ID_MAIL = User ID + Email
// ==========================================================
typedef enum {
    F_HP,        // Nomor HP saja
    F_ID,        // User ID saja
    F_ID_ZONE,   // User ID + Zone ID
    F_EMAIL,     // Email saja
    F_ID_MAIL,   // User ID + Email
} TipeField;

// [kategori][sub] → tipe field yang dipakai
// Kategori: 0=Diamond  1=Pulsa  2=EMoney
// Sub Diamond:  0=ML  1=FF  2=PUBG  3=Genshin
// Sub Pulsa:    0=Tsel 1=XL  2=Indosat 3=Tri
// Sub EMoney:   0=DANA 1=GoPay 2=OVO 3=SPay
//
// ┌─ Ubah di sini untuk ganti tipe field suatu game ─┐
static const TipeField configField[3][4] = {
    { F_ID_ZONE, F_ID,    F_ID,    F_ID    }, // Diamond: ML, FF, PUBG, Genshin
    { F_HP,      F_HP,    F_HP,    F_HP    }, // Pulsa
    { F_HP,      F_HP,    F_HP,    F_HP    }, // EMoney
};
// └──────────────────────────────────────────────────┘

void storeSetupField(int kat, int sub) {
    memset(field, 0, sizeof(field));
    TipeField t = configField[kat][sub];
    switch (t) {
        case F_HP:
            strcpy(field[0].label, "NomorHP"); totalField = 1; break;
        case F_ID:
            strcpy(field[0].label, "UserID");  totalField = 1; break;
        case F_ID_ZONE:
            strcpy(field[0].label, "UserID");
            strcpy(field[1].label, "ZoneID");  totalField = 2; break;
        case F_EMAIL:
            strcpy(field[0].label, "Email");    totalField = 1; break;
        case F_ID_MAIL:
            strcpy(field[0].label, "UserID");
            strcpy(field[1].label, "Email");    totalField = 2; break;
    }
}

// ==========================================================
// TAMPILAN MENU LOGO (CAROUSEL KATEGORI)
// ==========================================================
void tampilkanMenuLogo() {
    ssd1306_clear(0);
    drawStarfield();
    drawWave();

    ssd1306_draw_string_adafruit(0, 0, 0, (char *)katHeader[katKursor], WHITE, BLACK);
    ssd1306_draw_hline(0, 0, 9, 128, WHITE);

    const unsigned char *ikon;
    if      (katKursor == 0) ikon = logo_game_32;
    else if (katKursor == 1) ikon = logo_hp_32;
    else if (katKursor == 2) ikon = logo_emoney_32;
    else                     ikon = logo_settings_32;

    oled_draw_bitmap(0, 47, 20 + getBounce(300, 2), ikon, 32, 32, WHITE);

    ssd1306_draw_string_adafruit(0, 18, 30, "<",         WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 102, 30, ">",        WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 38, 56, ">SELECT<",  WHITE, BLACK);
    ssd1306_refresh(0, true);
}

// ==========================================================
// TAMPILAN SUBMENU LIST
// ==========================================================
void tampilkanMenuUtama() {
    ssd1306_clear(0);
    drawStarfield();
    drawWave();

    ssd1306_draw_string_adafruit(0, 0, 0, (char *)katHeader[katKursor], WHITE, BLACK);
    ssd1306_draw_hline(0, 0, 9, 128, WHITE);

    int tot = totalSubKat[katKursor];
    for (int i = 0; i < 5; i++) {
        int idx = atasMenu + i;
        if (idx >= tot) break;

        int y  = 13 + (i * 10);
        int tc = WHITE, bc = BLACK;
        int ib = 0, xp = 0;

        if (idx == subKursor) {
            ssd1306_fill_rectangle(0, 0, y-1, 128, 10, WHITE);
            int sl = (millis()/40) % 20;
            ssd1306_fill_rectangle(0, 125-sl,   y-1, 2, 10, BLACK);
            ssd1306_fill_rectangle(0, 131-sl,   y-1, 4, 10, BLACK);
            ib = getBounce(200, 2); xp = 4; tc = BLACK; bc = WHITE;
        }

        const unsigned char *ikon;
        if      (katKursor == 0) ikon = ikonGem[idx];
        else if (katKursor == 1) ikon = ikonPulsa[idx];
        else if (katKursor == 2) ikon = ikonMoney[idx];
        else                     ikon = ikonSet[idx];
        oled_draw_bitmap(0, 2+xp, (y-1)+ib, ikon, 10, 10, tc);

        const char *teks;
        if      (katKursor == 0) teks = subGem[idx];
        else if (katKursor == 1) teks = subPulsa[idx];
        else if (katKursor == 2) teks = subMoney[idx];
        else                     teks = subSet[idx];
        ssd1306_draw_string_adafruit(0, 18+xp, y, (char *)teks, tc, bc);
    }
    ssd1306_refresh(0, true);
}

// ==========================================================
// TAMPILAN SEMUA LAYAR STORE
// appMode 2  = Daftar Item
// appMode 3  = Detail Item
// appMode 4  = Input Field List
// appMode 5  = Input Karakter
// appMode 6  = Konfirmasi
// appMode 9  = Aksi Bayar
// appMode 10 = QRIS
// appMode 11 = TRX Berhasil
// appMode 12 = TRX Gagal
// ==========================================================
void tampilkanStore() {
    ssd1306_clear(0);
    char buf[48];
    char tmp[24];

    // ======================================================
    // LAYAR 2: DAFTAR ITEM PRODUK
    // > geser bawah | < kembali | OK pilih
    // ======================================================
    if (appMode == 2) {
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        const char *subName =
            (katKursor==0) ? subGem[subKursor] :
            (katKursor==1) ? subPulsa[subKursor] : subMoney[subKursor];
        ssd1306_draw_string_adafruit(0, 2, 1, (char *)subName, BLACK, WHITE);

        int tot = storeGetTotal(katKursor, subKursor);
        for (int i = 0; i < 3; i++) {
            int idx  = itemScroll + i;
            if (idx >= tot) break;
            const StoreProduk *p = storeGetItem(katKursor, subKursor, idx);
            int y    = 12 + (i * 14);
            int tc   = WHITE, bc = BLACK;
            bool sel = (i == itemKursor);

            if (sel) { ssd1306_fill_rectangle(0, 0, y-1, 128, 13, WHITE); tc=BLACK; bc=WHITE; }

            snprintf(buf, sizeof(buf), "%d.", idx+1);
            ssd1306_draw_string_adafruit(0, 1, y+1, buf, tc, bc);

            scrollTeks(p->nama, tmp, 11, sel);
            ssd1306_draw_string_adafruit(0, 16, y+1, tmp, tc, bc);

            char hBuf[12];
            formatHarga(p->harga, hBuf, sizeof(hBuf));
            ssd1306_draw_string_adafruit(0, 84, y+1, hBuf, tc, bc);
        }
        ssd1306_fill_rectangle(0, 0, 55, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 56, "< BACK", BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 104, 56, "[OK]",   BLACK, WHITE);
    }

    // ======================================================
    // LAYAR 3: DETAIL ITEM (full screen)
    // < kembali | OK beli
    // ======================================================
    else if (appMode == 3) {
        const StoreProduk *p = storeGetItem(katKursor, subKursor, itemDipilih);
        
        
        char isibody[200];
        snprintf(isibody, sizeof(isibody), "api_key=%s&type=prabayar&code=%s", apiKeyH2H, p->kode);
        
 if (checkstatus == false) {
 ssd1306_draw_string_adafruit(0, 16, 28, "Checking Product", WHITE, BLACK);
HttpReq req = {
    .url = "https://atlantich2h.com/layanan/price_list",
    .method = SYS_POST,
    .body = isibody,
    .content_type = "application/x-www-form-urlencoded",
};

// Eksekusi request-nya
HttpResp *res = http_request(&req);

if (res && res->ok) {
    const char* tersedia = resp_str(res, "status");
    if (tersedia != NULL && strcmp(tersedia, "available") == 0) {
    itemtersedia = true;
   } else {
        itemtersedia = false;
        
        }
        
}
fetch_free(res);

checkstatus = true;
}

if (itemtersedia == true) {
    ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 22, 1, "DETAIL PRODUK", BLACK, WHITE);

        scrollTeks(p->nama, tmp, 20, true);
        ssd1306_draw_string_adafruit(0, 5, 14, "Produk:", WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 5, 24, tmp,       WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 75, 14, "tersedia", WHITE, BLACK);

        char hBuf[14];
        formatHarga(p->harga, hBuf, sizeof(hBuf));
        snprintf(buf, sizeof(buf), "Rp %s", hBuf);
        ssd1306_draw_string_adafruit(0, 5, 34, "Harga:", WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 5, 44, buf,      WHITE, BLACK);

        ssd1306_fill_rectangle(0, 0, 55, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 56, "< BACK", BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 92, 56, "[BELI]", BLACK, WHITE);
} else {
ssd1306_draw_string_adafruit(0, 1, 28, "Produk Tidak Tersedia", WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 2, 56, "< BACK", WHITE, BLACK);
        }


            }

    // ======================================================
    // LAYAR 4: INPUT FIELD LIST
    // > ganti field | < kembali | OK edit / konfirmasi
    // ======================================================
    else if (appMode == 4) {
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 12, 1, "DATA PEMBELIAN", BLACK, WHITE);

        bool semuaIsi = true;
        for (int f = 0; f < totalField; f++)
            if (!strlen(field[f].value)) { semuaIsi = false; break; }

        int totRow = totalField + 1; // field + baris KONFIRMASI
        for (int i = 0; i < totRow; i++) {
            int y   = 13 + (i * 13);
            int tc  = WHITE, bc = BLACK;
            bool ak = (i == fieldKursor);

            if (ak) {
                ssd1306_fill_rectangle(0, 0, y-1, 128, 12, WHITE);
                tc = BLACK; bc = WHITE;
                ssd1306_draw_string_adafruit(0, 2, y, ">", tc, bc);
            }

            if (i < totalField) {
                snprintf(buf, sizeof(buf), "%s:", field[i].label);
                ssd1306_draw_string_adafruit(0, 10, y, buf, tc, bc);

                // Value: scroll jika field ini aktif & teks panjang
                const char *vs = strlen(field[i].value) ? field[i].value : "---";
                scrollTeks(vs, tmp, 9, ak);
                ssd1306_draw_string_adafruit(0, 73, y, tmp, tc, bc);
            } else {
                // Baris tombol KONFIRMASI
                const char *kl = semuaIsi ? "[]> KONFIRMASI" : "[]> ISI DULU! ";
                if (ak)
                    ssd1306_draw_string_adafruit(0, 16, y+3, kl, BLACK, WHITE);
                else
                    ssd1306_draw_string_adafruit(0, 16, y+3, kl, WHITE, BLACK);
            }
        }
        ssd1306_fill_rectangle(0, 0, 55, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 56, "< BACK", BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 92, 56, "[EDIT]", BLACK, WHITE);
    }

    // ======================================================
    // LAYAR 5: INPUT KARAKTER — 2 MODE KEYBOARD
    //
    // [ANGKA] 0-9 (default) — max 9 klik buat semua digit
    // [HURUF] A-Z + simbol  — buat email, nama, dll
    //
    // > = ganti karakter (tahan = makin cepat)
    // OK     = tambah karakter  |  2x OK  = SELESAI field
    // < = hapus karakter        |  2x <   = BATAL (hapus semua)
    // < saat buffer kosong      = GANTI MODE (ANGKA↔HURUF)
    // ======================================================
    else if (appMode == 5) {
        // Pilih charset sesuai mode
        static const char CS_ANGKA[] = "0123456789";
        static const char CS_HURUF[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@ _-.#";
        const char *cs    = inputAngka ? CS_ANGKA : CS_HURUF;
        int         csLen = inputAngka ? 10 : 59;
        int         ci    = charIdx % csLen; // Safety clamp

        // Header: nama field
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        snprintf(buf, sizeof(buf), "INPUT: %s", field[fieldKursor].label);
        ssd1306_draw_string_adafruit(0, 2, 1, buf, BLACK, WHITE);

        // Buffer + kursor underscore
        char disBuf[32] = {0};
        strcpy(disBuf, field[fieldKursor].value);
        if (strlen(disBuf) < 27) strcat(disBuf, "_");
        ssd1306_draw_string_adafruit(0, 2, 12, disBuf, WHITE, BLACK);
        ssd1306_draw_hline(0, 0, 21, 128, WHITE);

        // Badge MODE (kiri) — putih = aktif, outline = tidak
        if (inputAngka) {
            ssd1306_fill_rectangle(0, 2, 23, 33, 10, WHITE);
            ssd1306_draw_string_adafruit(0, 4, 24, "ANGKA", BLACK, WHITE);
            ssd1306_draw_rectangle(0, 38, 23, 33, 10, WHITE);
            ssd1306_draw_string_adafruit(0, 39, 24, "HURUF", WHITE, BLACK);
        } else {
            ssd1306_draw_rectangle(0, 2, 23, 33, 10, WHITE);
            ssd1306_draw_string_adafruit(0, 4, 24, "ANGKA", WHITE, BLACK);
            ssd1306_fill_rectangle(0, 38, 23, 33, 10, WHITE);
            ssd1306_draw_string_adafruit(0, 39, 24, "HURUF", BLACK, WHITE);
        }

        // Kotak karakter aktif (tengah-kanan)
        ssd1306_fill_rectangle(0, 91, 22, 20, 20, WHITE);
        char csShow[2] = { cs[ci], '\0' };
        ssd1306_draw_string_adafruit(0, 98, 30, csShow, BLACK, WHITE);

        // Panah kiri-kanan
        ssd1306_draw_string_adafruit(0, 82, 28, "<", WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 114, 28, ">", WHITE, BLACK);

        // Posisi karakter (kanan atas)
        snprintf(buf, sizeof(buf), "%d/%d", ci+1, csLen);
        ssd1306_draw_string_adafruit(0, 90, 47, buf, WHITE, BLACK);

        
}

    // ======================================================
    // LAYAR 6: KONFIRMASI (review sebelum bayar)
    // < kembali | OK lanjut ke aksi bayar
    // ======================================================
    else if (appMode == 6) {
        const StoreProduk *p = storeGetItem(katKursor, subKursor, itemDipilih);

        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 28, 1, "KONFIRMASI", BLACK, WHITE);

        // Nama produk scroll
        scrollTeks(p->nama, tmp, 20, true);
        ssd1306_draw_string_adafruit(0, 2, 12, tmp, WHITE, BLACK);
        ssd1306_draw_hline(0, 0, 21, 128, WHITE);

        // Harga
        char hBuf[14];
        formatHarga(p->harga, hBuf, sizeof(hBuf));
        snprintf(buf, sizeof(buf), "Harga: Rp %s", hBuf);
        ssd1306_draw_string_adafruit(0, 2, 24, buf, WHITE, BLACK);

        // Field values — scroll jika panjang
        for (int i = 0; i < totalField && i < 2; i++) {
            int yLine = 34 + (i * 10);
            snprintf(buf, sizeof(buf), "%s:", field[i].label);
            int lw = strlen(buf) * 6;
            ssd1306_draw_string_adafruit(0, 2, yLine, buf, WHITE, BLACK);
            scrollTeks(field[i].value, tmp, 13, true);
            ssd1306_draw_string_adafruit(0, 2+lw, yLine, tmp, WHITE, BLACK);
        }

        ssd1306_fill_rectangle(0, 0, 55, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 56, "< BACK", BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 104, 56, "[OK]",   BLACK, WHITE);
    }

    // ======================================================
    // LAYAR 9: AKSI BAYAR (BAYAR / QRIS)
    // > toggle | < kembali | OK eksekusi
    // ======================================================
    else if (appMode == 9) {
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 12, 1, "PILIH PEMBAYARAN", BLACK, WHITE);
        ssd1306_fill_rectangle(0, 0, 24, 128, 16, WHITE);

        static const char         *menuP[]  = {"TF/BAYAR", "QRIS"};
        static const unsigned char *ikonP[] = {iconSmall_conn, iconSmall_scan};

        for (int i = 0; i < 2; i++) {
            int diff = i - caraBayar;
            int yP   = 27 + (diff * 16);
            if (yP <= 10 || yP >= 46) continue;
            if (diff == 0) {
                oled_draw_bitmap(0, 26, yP-1, ikonP[i], 10, 10, BLACK);
                ssd1306_draw_string_adafruit(0, 10,  yP, ">",          BLACK, WHITE);
                ssd1306_draw_string_adafruit(0, 42,  yP, (char*)menuP[i], BLACK, WHITE);
                ssd1306_draw_string_adafruit(0, 112, yP, "<",          BLACK, WHITE);
            } else {
                oled_draw_bitmap(0, 30, yP, ikonP[i], 10, 10, WHITE);
                ssd1306_draw_string_adafruit(0, 46, yP+1, (char*)menuP[i], WHITE, BLACK);
            }
        }
        ssd1306_fill_rectangle(0, 0, 55, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 56, "< BACK", BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 104, 56, "[OK]",   BLACK, WHITE);
    }

    // ======================================================
    // LAYAR 10: QRIS SCREEN
    // QR 58x58 di kiri, info di kanan
    // < kembali
    // ======================================================
    else if (appMode == 10) {
        const StoreProduk *p = storeGetItem(katKursor, subKursor, itemDipilih);

        oled_draw_bitmap(0, 0, 0, qrisku, 58, 58, WHITE);
      

        char hBuf[14];
        formatHarga(p->harga, hBuf, sizeof(hBuf));
        ssd1306_draw_string_adafruit(0, 61, 1,  "HARGA:",  WHITE, BLACK);
        snprintf(buf, sizeof(buf), "Rp%s", hBuf);
        ssd1306_draw_string_adafruit(0, 61, 11, buf,       WHITE, BLACK);

        scrollTeks(p->nama, tmp, 11, true);
        ssd1306_draw_string_adafruit(0, 61, 23, tmp, WHITE, BLACK);

        // targetID (scroll jika panjang)
        scrollTeks(targetID, tmp, 11, true);
        ssd1306_draw_string_adafruit(0, 61, 34, tmp, WHITE, BLACK);

        
        ssd1306_draw_string_adafruit(0, 91, 57, "< BACK",   WHITE, BLACK);
        
    }

    // ======================================================
    // LAYAR 11: TRX BERHASIL
    // Ikon centang 32x32 pojok kiri-bawah + info di kanan
    // < HOME
    // ======================================================
    else if (appMode == 11) {
        const StoreProduk *p = storeGetItem(katKursor, subKursor, itemDipilih);

        // Header
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 14, 1, "TRX BERHASIL", BLACK, WHITE);

        // Ikon centang 32x32 di pojok kiri-bawah area konten
        oled_draw_bitmap(0,2,16,icon_centang_32,32,32,WHITE);
        

        // Panel kanan (x=35)
        scrollTeks(p->nama, tmp, 13, true);
        ssd1306_draw_string_adafruit(0, 37, 12, tmp, WHITE, BLACK);

        char hBuf[14];
        formatHarga(p->harga, hBuf, sizeof(hBuf));
        snprintf(buf, sizeof(buf), "Rp %s", hBuf);
        ssd1306_draw_string_adafruit(0, 37, 23, buf, WHITE, BLACK);

        scrollTeks(targetID, tmp, 13, true);
        ssd1306_draw_string_adafruit(0, 37, 34, tmp, WHITE, BLACK);

        ssd1306_draw_string_adafruit(0, 37, 45, p->kode, WHITE, BLACK);

        // Footer
        ssd1306_fill_rectangle(0, 0, 55, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 56, "< HOME", BLACK, WHITE);
    }

    // ======================================================
    // LAYAR 12: TRX GAGAL
    // Ikon silang 32x32 pojok kiri-bawah + info di kanan
    // < HOME
    // ======================================================
    else if (appMode == 12) {
        const StoreProduk *p = storeGetItem(katKursor, subKursor, itemDipilih);

        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 22, 1, "TRX GAGAL", BLACK, WHITE);

        // Ikon silang 32x32 pojok kiri-bawah
        oled_draw_bitmap(0,2,16,icon_silang_32,32,32,WHITE);
        

        // Panel kanan (x=35)
        scrollTeks(p->nama, tmp, 13, true);
        ssd1306_draw_string_adafruit(0,37, 12, tmp, WHITE, BLACK);

        char hBuf[14];
        formatHarga(p->harga, hBuf, sizeof(hBuf));
        snprintf(buf, sizeof(buf), "Rp %s", hBuf);
        ssd1306_draw_string_adafruit(0,37, 23, buf, WHITE, BLACK);

        scrollTeks(targetID, tmp, 13, true);
        ssd1306_draw_string_adafruit(0,37, 34, tmp, WHITE, BLACK);

        ssd1306_draw_string_adafruit(0,37, 45, "Coba lagi!", WHITE, BLACK);

        ssd1306_fill_rectangle(0, 0, 55, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 56, "< HOME", BLACK, WHITE);
    }

    
// ==========================================================
// BRIGHTNESS
// ==========================================================
    // ======================================================
    // LAYAR 13: WiFi List (hasil scan)
    // ======================================================
    else if (appMode == 13) {
        ssd1306_clear(0);
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 1, "Jaringan WiFi", BLACK, WHITE);

        if (wifiStatus == WIFI_STATUS_SCANNING) {
            ssd1306_draw_string_adafruit(0, 10, 28, "Scanning...", WHITE, BLACK);
        } else if (wifiTotal == 0) {
            ssd1306_draw_string_adafruit(0, 10, 20, "Tidak ada", WHITE, BLACK);
            ssd1306_draw_string_adafruit(0, 10, 34, "jaringan.", WHITE, BLACK);
            ssd1306_draw_string_adafruit(0, 0, 54, "< Kembali", WHITE, BLACK);
        } else {
            for (int i = 0; i < 3; i++) {
                int idx = wifiScroll + i;
                if (idx >= wifiTotal) break;
                int y = 12 + i * 17;
                if (idx == wifiKursor) ssd1306_fill_rectangle(0, 0, y-1, 128, 15, WHITE);
                char baris[40];
                snprintf(baris, sizeof(baris), "%s%.32s",
                    wifiList[idx].has_pass ? "[*]" : "[O]",
                    wifiList[idx].ssid);
                ssd1306_draw_string_adafruit(0, 2, y+1, baris,
                    (idx == wifiKursor) ? BLACK : WHITE,
                    (idx == wifiKursor) ? WHITE : BLACK);
            }
            ssd1306_draw_string_adafruit(0, 0, 56, "< Batal  OK Pilih", WHITE, BLACK);
        }
        ssd1306_refresh(0, true);
    }

    // ======================================================
    // LAYAR 14: Input Password WiFi (keyboard)
    // ======================================================
    else if (appMode == 14) {
        static const char CS_WIFI[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@ _-.#!";
        int csLen14 = 69;
        int ci14    = charIdx % csLen14;

        ssd1306_clear(0);
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 1, "Password WiFi:", BLACK, WHITE);

        // Preview password (masking)
        char masked[65] = {0};
        int pl = strlen(wifiPassBuf);
        for (int m = 0; m < pl && m < 21; m++) masked[m] = '*';
        if (pl < 64) strcat(masked, "_");
        ssd1306_draw_string_adafruit(0, 2, 12, masked, WHITE, BLACK);
        ssd1306_draw_hline(0, 0, 21, 128, WHITE);

        // Kotak karakter
        ssd1306_fill_rectangle(0, 91, 22, 20, 20, WHITE);
        char csShow[2] = { CS_WIFI[ci14], '\0' };
        ssd1306_draw_string_adafruit(0, 98, 30, csShow, BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 82, 28, "<", WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 114, 28, ">", WHITE, BLACK);

        char posStr[10];
        snprintf(posStr, sizeof(posStr), "%d/%d", ci14+1, csLen14);
        ssd1306_draw_string_adafruit(0, 90, 47, posStr, WHITE, BLACK);

        ssd1306_draw_string_adafruit(0, 0, 56, "OK=Tambah 2xOK=Selesai", WHITE, BLACK);
        ssd1306_refresh(0, true);
    }

    // ======================================================
    // LAYAR 15: Connecting WiFi
    // ======================================================
    else if (appMode == 15) {
        ssd1306_clear(0);
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 1, "Menghubungkan...", BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 4, 18, wifiList[wifiKursor].ssid, WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 10, 40, "Mohon tunggu...", WHITE, BLACK);
        ssd1306_refresh(0, true);
    }

    // ======================================================
    // LAYAR 16: WiFi Terhubung
    // ======================================================
    else if (appMode == 16) {
        ssd1306_clear(0);
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 1, "WiFi Terhubung!", BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 4, 14, wifiConnectedSSID, WHITE, BLACK);
        const char *ip = wifi_get_ip();
        ssd1306_draw_string_adafruit(0, 4, 30, ip ? ip : "-", WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 0, 54, "< Kembali", WHITE, BLACK);
        ssd1306_refresh(0, true);
    }

    // ======================================================
    // LAYAR 17: WiFi Gagal
    // ======================================================
    else if (appMode == 17) {
        ssd1306_clear(0);
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 1, "Gagal Konek!", BLACK, WHITE);
        ssd1306_draw_string_adafruit(0, 10, 18, wifiList[wifiKursor].ssid, WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 4, 34, "Salah password?", WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 0, 54, "< Coba lagi", WHITE, BLACK);
        ssd1306_refresh(0, true);
    }
    ssd1306_refresh(0, true);
}

    

void tampilkanBrightness() {
    ssd1306_clear(0);
    char buf[16];

    ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
    ssd1306_draw_string_adafruit(0, 35, 1, "BRIGHTNESS", BLACK, WHITE);

    ssd1306_draw_rectangle(0, 14, 28, 100, 12, WHITE);
    int bw = map(kecerahan, 0, 255, 0, 96);
    ssd1306_fill_rectangle(0, 16, 30, bw, 8, WHITE);

    snprintf(buf, sizeof(buf), "%d%%", (int)map(kecerahan, 0, 255, 0, 100));
    ssd1306_draw_string_adafruit(0, 55, 45, buf, WHITE, BLACK);

    ssd1306_fill_rectangle(0, 0, 55, 128, 10, WHITE);
    ssd1306_draw_string_adafruit(0, 2, 56, "[<]BACK",    BLACK, WHITE);
    ssd1306_draw_string_adafruit(0, 62, 56, "[>]+[OK]-",  BLACK, WHITE);
    ssd1306_refresh(0, true);
}

void setOledBrightness(uint8_t level) {
    i2c_start(); i2c_write(0x78); i2c_write(0x00);
    i2c_write(0x81); i2c_write(level); i2c_stop();
}

// ==========================================================
// ABOUT & REBOOT
// ==========================================================
void renderAboutScreen() {
    ssd1306_clear(0);
    ssd1306_draw_rectangle(0, 0, 0, 128, 64, WHITE);
    ssd1306_draw_rectangle(0, 2, 2, 124, 60, WHITE);
    ssd1306_draw_string_adafruit(0, 30,  8,  "JIR STORE",   WHITE, BLACK);
    ssd1306_draw_hline(0, 25, 18, 78, WHITE);
    ssd1306_draw_string_adafruit(0, 10, 25,  "Ver : 1.0.0", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 10, 35,  "Core: ESP32C3",WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 10, 45,  "By  : Andyy", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 90, 45,  "[<]",         WHITE, BLACK);
    ssd1306_refresh(0, true);
}

void renderRebootScreen() {
    ssd1306_clear(0);
    ssd1306_draw_rectangle(0, 5, 5, 118, 54, WHITE);
    ssd1306_draw_string_adafruit(0, 20, 20, "Reboot sekarang?", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 2, 56, "< NO",  WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 104, 56, "[OK]",  WHITE, BLACK);
    ssd1306_refresh(0, true);
}
