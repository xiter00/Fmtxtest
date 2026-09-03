#include <stdio.h>
#include <stdlib.h>
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
#include "ota_sys.h"
#include "pin_system.h"
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
void renderOtaChecking(void);
void renderOtaNoUpdate(void);
void renderOtaAvailable(void);
void renderOtaDownloading(void);
void renderOtaSuccess(void);
void renderOtaFailed(void);

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
// appMode 13-17 → WiFi (list/password/connecting/sukses/gagal)
// appMode 18-19 → Saved WiFi (list/detail)
// appMode 20 → OTA: checking version
// appMode 21 → OTA: no update
// appMode 22 → OTA: update available (konfirmasi)
// appMode 23 → OTA: downloading/flashing
// appMode 24 → OTA: success
// appMode 25 → OTA: failed
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
            case 13: case 14: case 15: case 16: case 17:
            case 18: case 19:
            case 30: case 31: case 32: case 35:
                tampilkanStore(); break;
            case 7: renderAboutScreen();  break;
            case 8: renderRebootScreen(); break;
            case 20: renderOtaChecking();    break;
            case 21: renderOtaNoUpdate();    break;
            case 22: renderOtaAvailable();   break;
            case 23: renderOtaDownloading(); break;
            case 24: renderOtaSuccess();     break;
            case 25: renderOtaFailed();      break;
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
static const char *subSet[]   = {"Brightness", "About", "Reboot", "WiFi", "Saved WiFi", "Cek Update", "Edit PIN"};

const int totalSubKat[] = {4, 4, 4, 7};  // Diakses dari input_system

static const unsigned char *ikonGem[]   = {iconSmall_scan, iconSmall_wifi, iconSmall_sniff, iconSmall_spam};
static const unsigned char *ikonPulsa[] = {iconSmall_apple, iconSmall_android, iconSmall_conn, iconSmall_scan};
static const unsigned char *ikonMoney[] = {iconSmall_apple, iconSmall_android, iconSmall_conn, iconSmall_scan};
static const unsigned char *ikonSet[]   = {iconSmall_bright, iconSmall_info, iconSmall_repeat, iconSmall_wifi, iconSmall_saved, iconSmall_repeat, iconSmall_scan};

// ==========================================================
// DATA PRODUK
// ==========================================================
static const StoreProduk itemML[] = {
    {"5 Diamond",    "ML5",   1500}, {"11 Diamond",   "ML11",   3000},
    {"22 Diamond",   "ML22",   6000}, {"56 Diamond",   "ML56",  14000},
    {"86 Diamond",   "ML86",  22000}, {"172 Diamond",  "ML172",  44000},
    {"257 Diamond",  "ML257",  65000}, {"514 Diamond",  "ML514", 128000},
};
static const StoreProduk itemFF[] = {
    {"5 Diamond",    "FF5",   1500}, {"70 Diamond",   "FF070",  10500},
    {"140 Diamond",  "FF140",  21000}, {"355 Diamond",  "FF355",  52000},
    {"720 Diamond",  "FF720", 104000}, {"1450 Diamond", "FF1450",205000},
};
static const StoreProduk itemPUBG[] = {
    {"60 UC",  "PUBG60",  14000}, {"120 UC",  "PUBG120",  28000},
    {"325 UC", "PUBG325",  75000}, {"660 UC",  "PUBG660", 150000},
    {"1800 UC","PUBG1800",400000},
};
static const StoreProduk itemGI[] = {
    {"60 Primogem",   "GI60",  14000}, {"300+30 Primo",  "GI300",  75000},
    {"980+110 Primo", "GI980", 210000}, {"1980+260 Primo","GI1980",420000},
};

typedef struct {
    const StoreProduk *item;
    const char *sluggame;
    int jumlah;
} SubKategori;

static const SubKategori tabDiamond[] = {
    { itemML,  "mobile-legends", 8 },
    { itemFF,  "free-fire",      6 },
    { itemPUBG,"PUBG",           5 },
    { itemGI,  "genshin",        4 },
};

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
    if (kat == 0) return tabDiamond[sub].jumlah;
    if (kat == 1) return totPulsa[sub];
    if (kat == 2) return totMoney[sub];
    return 0;
}

const StoreProduk *storeGetItem(int kat, int sub, int idx) {
    if (kat == 0) return &tabDiamond[sub].item[idx];
    if (kat == 1) return &tabPulsa[sub][idx];
    if (kat == 2) return &tabMoney[sub][idx];
    return NULL;
}

const char *getGameSlug(int sub) {
    return tabDiamond[sub].sluggame;
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
// CEK PRODUK KE SERVER — dijalanin di TASK TERPISAH
// Jangan pernah manggil http_request() langsung di tampilkanStore()!
// task_display jalan di loop yang sama buat gambar layar & baca joystick;
// kalau HTTP-nya dipanggil langsung disitu, begitu koneksi lambat/gak ada
// WiFi, esp_http_client_perform() bakal nge-block sampai HTTP_TIMEOUT_MS
// (10 detik) dan SELURUH layar + tombol freeze total selama itu.
// Makanya dipisah ke task sendiri: layar tetap jalan nampilin
// "Checking Product...", task ini yang nunggu response di background.
// ==========================================================
static const char *TAG_PRODUK = "API";



typedef enum {
    CEK_PRODUK,
    CEK_NICKNAME,
    SEND_ITEM
} TipeCek;

typedef struct {
    TipeCek tipe;
    char kode[12];
    char userid[20];
    char zoneid[20];
    char gameslug[20];
    
} CekProdukParam;

static volatile bool s_cekProdukJalan = false;


static void task_cek_produk(void *param) {
    CekProdukParam *cp = (CekProdukParam *)param;
    char buff[200];
    if (cp->tipe == CEK_PRODUK) {
    
    snprintf(buff, sizeof(buff), "api_key=%s&type=prabayar&code=%s", apiKeyH2H, cp->kode);

    HttpReq req = {
        .url = "https://atlantich2h.com/layanan/price_list",
        .method = SYS_POST,
        .body = buff,
        .content_type = "application/x-www-form-urlencoded",
        
    };

    HttpResp *res = http_request(&req);
    bool tersedia = false;

    if (!res) {
        ESP_LOGE(TAG_PRODUK, "kode=%s: request gagal total (cek koneksi WiFi / server)", cp->kode);
    } else {
        // Log ini yang paling penting: kalau "Produk Tidak Tersedia" muncul lagi,
        // buka Serial Monitor dan copy baris ini, itu yang dipake buat diagnosa
        // beneran (bukan nebak-nebak lagi).
        ESP_LOGI(TAG_PRODUK, "kode=%s http_status=%d ok=%d body=%s",
                 cp->kode, res->status, res->ok, res->body ? res->body : "(kosong)");
cJSON *data = resp_obj(res, "data");
const char *status = obj_str(data, "status");
tersedia = (status != NULL && strcmp(status, "available") == 0);
        
    }

    if (res) fetch_free(res);

    itemtersedia     = tersedia;
    checkstatus      = true;
    s_cekProdukJalan = false;
} else if (cp->tipe == CEK_NICKNAME) {
snprintf(buff, sizeof(buff), "https://cek.topupgaming.com/api/game/%s?id=%s&zone=%s", cp->gameslug, cp->userid, cp->zoneid);

    HttpResp *res = fetch(buff);

    if (!res) {
        ESP_LOGE(TAG_PRODUK, "GET NICKNAME GAGAL");
        ceknickgagal = true;
    } else {
      bool status = resp_bool(res, "status");
       if (status == true) {
cJSON *data = resp_obj(res, "data");
const char *uname = obj_str(data, "username");
if (uname) {
    strncpy(nickname, uname, sizeof(nickname) - 1);  
    nickname[sizeof(nickname) - 1] = '\0';
}
     } else {
     ceknickgagal = true;
     }   
    }

    if (res) fetch_free(res);
    checkstatus      = true;
    s_cekProdukJalan = false;
} else if (cp->tipe == SEND_ITEM) {
 snprintf(buff, sizeof(buff), "api_key=%s&code=%s&reff_id=TESTINGJUH12&target=%s", apiKeyH2H, cp->kode, targetID);

    HttpReq req = {
        .url = "https://atlantich2h.com/transaksi/create",
        .method = SYS_POST,
        .body = buff,
        .content_type = "application/x-www-form-urlencoded",
        
    };

    HttpResp *res = http_request(&req);
    bool send = false;

    if (!res) {
        ESP_LOGE(TAG_PRODUK, "kode=%s: request gagal total (cek koneksi WiFi / server)", cp->kode);
    } else {
        
        ESP_LOGI(TAG_PRODUK, "kode=%s http_status=%d ok=%d body=%s",
                 cp->kode, res->status, res->ok, res->body ? res->body : "(kosong)");
cJSON *data = resp_obj(res, "data");
const char *status = obj_str(data, "status");
send = (status != NULL && strcmp(status, "pending") == 0);
        
    }

    if (res) fetch_free(res);

    trxberhasil     = send;
    checkstatus      = true;
    s_cekProdukJalan = false;
}
    free(cp);
    vTaskDelete(NULL);
}

// ==========================================================
// CAROUSEL KARAKTER — dipake bareng di LAYAR 5 (input ID/huruf)
// & LAYAR 14 (password wifi).
//
// Nunjukkin karakter SEKARANG + 2 karakter SEBELUM & 2 SESUDAHNYA
// (wrap-around), biar user bisa liat huruf apa yang bakal DATENG
// sebelum nge-scroll sampe kesitu — gak perlu nebak2 lagi kayak
// model 1-karakter yang lama.
//
//   [prev2] [prev1] [ SEKARANG ] [next1] [next2]
//
// id     = layar OLED target
// yBox   = y posisi baris (karakter SEKARANG digambar sedikit lebih
//          gede lewat kotak highlight, yang lain cuma teks biasa)
// cs/len = charset aktif & panjangnya
// ci     = index karakter yang lagi kepilih (sudah di-mod ke csLen)
// ==========================================================
static void drawCharCarousel(uint8_t id, uint8_t yBox, const char *cs, int len, int ci) {
    // Jarak antar slot 13px (muat 5 slot dari x=60 s/d x=112, sisa buat margin)
    const int slotW = 13;
    const int xCenter = 92; // slot tengah (karakter SEKARANG)

    int offs[5]   = { -2, -1, 0, 1, 2 };
    for (int i = 0; i < 5; i++) {
        int idx = ((ci + offs[i]) % len + len) % len; // wrap-around aman buat negatif
        int x   = xCenter + offs[i] * slotW;
        char s[2] = { cs[idx], '\0' };

        if (offs[i] == 0) {
            // Karakter aktif — kotak highlight solid, teks gede-gedean kesan
            ssd1306_fill_rectangle(id, x - 7, yBox - 1, 16, 16, WHITE);
            ssd1306_draw_string_adafruit(id, x - 2, yBox + 3, s, BLACK, WHITE);
        } else {
            // Preview — teks biasa, sedikit lebih redup kesannya krn gak dikotakin
            ssd1306_draw_string_adafruit(id, x - 2, yBox + 3, s, WHITE, BLACK);
        }
    }
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

        if (checkstatus == false) {
            ssd1306_draw_string_adafruit(0, 16, 28, "Checking Product", WHITE, BLACK);
            ssd1306_draw_string_adafruit(0, 12, 40, "Mohon tunggu...", WHITE, BLACK);

            // Cek ke server dilempar ke task terpisah — layar & tombol
            // TETAP jalan normal sambil nunggu, gak ada blocking sama sekali.
            if (!s_cekProdukJalan) {
                s_cekProdukJalan = true;
                CekProdukParam *cp = calloc(1, sizeof(CekProdukParam));
                if (cp) {
                    cp->tipe = CEK_PRODUK;
                    strncpy(cp->kode, p->kode, sizeof(cp->kode) - 1);
                    cp->kode[sizeof(cp->kode) - 1] = '\0';
                    xTaskCreate(task_cek_produk, "cek_produk", 8192, cp, 5, NULL);
                } else {
                    // Malloc gagal (harusnya jarang) — anggap gak tersedia
                    itemtersedia    = false;
                    checkstatus     = true;
                    s_cekProdukJalan = false;
                }
            }
        }

if (checkstatus == true) {
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
        // Charset dari globals.h (satu sumber, sinkron sama input_system.c —
        // dulu di sini ada salinan charset sendiri yang KETINGGALAN dan beda
        // panjang/urutan dari yang dipakai buat nentuin karakter kepilih,
        // jadi apa yang KELIATAN di layar bisa beda sama yang KEPILIH pas OK)
        const char *cs    = inputAngka ? CS_ANGKA : CS_HURUF;
        int         csLen = inputAngka ? CS_ANGKA_LEN : CS_HURUF_LEN;
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

        // Carousel: [prev2][prev1][SEKARANG][next1][next2] — bisa keliatan
        // huruf yang mau dateng sebelum beneran discroll ke situ
        drawCharCarousel(0, 26, cs, csLen, ci);

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

    // Cek nickname cuma buat kategori Diamond (game)
    if (katKursor == 0 && checkstatus == false) {
        ssd1306_draw_string_adafruit(0, 16, 28, "Checking Nickname", WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 12, 40, "Mohon tunggu...", WHITE, BLACK);
        ssd1306_refresh(0, true);

        if (!s_cekProdukJalan) {
            s_cekProdukJalan = true;
            CekProdukParam *cp = calloc(1, sizeof(CekProdukParam));
            if (cp) {
                cp->tipe = CEK_NICKNAME;

                strncpy(cp->userid, field[0].value, sizeof(cp->userid) - 1);
                cp->userid[sizeof(cp->userid) - 1] = '\0';

                // ZoneID cuma ada kalau totalField == 2 (F_ID_ZONE)
                if (totalField == 2) {
                    strncpy(cp->zoneid, field[1].value, sizeof(cp->zoneid) - 1);
                    cp->zoneid[sizeof(cp->zoneid) - 1] = '\0';
                }

                strncpy(cp->gameslug, getGameSlug(subKursor), sizeof(cp->gameslug) - 1);
                cp->gameslug[sizeof(cp->gameslug) - 1] = '\0';

                xTaskCreate(task_cek_produk, "cek_nickname", 8192, cp, 5, NULL);
            } else {
                ceknickgagal     = true;
                checkstatus      = true;
                s_cekProdukJalan = false;
            }
        }
        return;   // jangan gambar konten konfirmasi dulu selagi nunggu
    } 
     



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
        if (katKursor != 0) {
        for (int i = 0; i < totalField && i < 2; i++) {
            int yLine = 34 + (i * 10);
            snprintf(buf, sizeof(buf), "%s:", field[i].label);
            int lw = strlen(buf) * 6;
            ssd1306_draw_string_adafruit(0, 2, yLine, buf, WHITE, BLACK);
            scrollTeks(field[i].value, tmp, 13, true);
            ssd1306_draw_string_adafruit(0, 2+lw, yLine, tmp, WHITE, BLACK);
        }
        }else{

        ssd1306_draw_string_adafruit(0, 2, 34, "UserID: ", WHITE, BLACK);
        scrollTeks(targetID, tmp, 13, true);
        ssd1306_draw_string_adafruit(0, 50, 34, tmp, WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 2, 44, "Nick: ", WHITE, BLACK);
        if (ceknickgagal == false) {
        scrollTeks(nickname, tmp, 13, true);
        ssd1306_draw_string_adafruit(0, 38, 44, tmp, WHITE, BLACK);
        } else {
        ssd1306_draw_string_adafruit(0, 38, 44, "Tidak Ditemukan", WHITE, BLACK);
        }
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
        

       if (checkstatus == false) {
        ssd1306_draw_string_adafruit(0, 16, 28, "Mengirim Pesanan", WHITE, BLACK);
        ssd1306_draw_string_adafruit(0, 12, 40, "Mohon tunggu...", WHITE, BLACK);
        ssd1306_refresh(0, true);

        if (!s_cekProdukJalan) {
            s_cekProdukJalan = true;
            CekProdukParam *cp = calloc(1, sizeof(CekProdukParam));
            if (cp) {
                cp->tipe = SEND_ITEM;

                
                strncpy(cp->kode, p->kode, sizeof(cp->kode) - 1);
                    cp->kode[sizeof(cp->kode) - 1] = '\0';
                xTaskCreate(task_cek_produk, "send_item", 8192, cp, 5, NULL);
            } else {
                trxberhasil     = false;
                checkstatus      = true;
                s_cekProdukJalan = false;
            }
        }
        return;   // jangan gambar konten konfirmasi dulu selagi nunggu
    } 
        
        if (trxberhasil == true) {
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
        } else {
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

    // ======================================================
    // LAYAR 30/31/32: INPUT PIN (transaksi / lama / baru)
    // Layout SAMA kayak LAYAR 5 (input ID) / LAYAR 14 (password WiFi):
    // buffer + kursor di atas, carousel angka di bawahnya — bedanya
    // buffer di-mask jadi "*" (PIN gak boleh keliatan di layar), dan
    // charset dikunci ANGKA doang (CS_ANGKA) karena PIN cuma digit.
    // Kalau lockout aktif (5x salah), carousel diganti hitung mundur —
    // sengaja, gak ngasih celah tebak2 selama nunggu.
    // ======================================================
    else if (appMode == 30 || appMode == 31 || appMode == 32) {
        const char *judul =
            (appMode == 30) ? "PIN TRANSAKSI" :
            (appMode == 31) ? "MASUKKAN PIN LAMA" : "MASUKKAN PIN BARU";

        int   len    = strlen(pinBuf);
        bool  cekPin = (appMode == 30 || appMode == 31); // 32 gak ada lockout
        uint32_t sisaMs = 0;
        bool  locked = cekPin && pin_is_locked(&sisaMs);
        int   ci     = pinPrev % CS_ANGKA_LEN;

        // Header
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 1, (char *)judul, BLACK, WHITE);

        // Buffer termask (bintang) + kursor underscore — persis pola
        // disBuf di LAYAR 5, cuma isinya diganti '*' biar PIN gak bocor
        char disBuf[PIN_LEN + 2] = {0};
        for (int i = 0; i < len; i++) disBuf[i] = '*';
        if (len < PIN_LEN) disBuf[len] = '_';
        ssd1306_draw_string_adafruit(0, 2, 12, disBuf, WHITE, BLACK);
        ssd1306_draw_hline(0, 0, 21, 128, WHITE);

        if (locked) {
            snprintf(buf, sizeof(buf), "Coba lagi %lus", (unsigned long)((sisaMs / 1000) + 1));
            ssd1306_draw_string_adafruit(0, 4, 30, "Terlalu banyak salah", WHITE, BLACK);
            ssd1306_draw_string_adafruit(0, 30, 42, buf, WHITE, BLACK);
        } else {
            // Carousel angka — sama persis kayak LAYAR 5/14
            drawCharCarousel(0, 26, CS_ANGKA, CS_ANGKA_LEN, ci);

            if (cekPin) {
                snprintf(buf, sizeof(buf), "Sisa: %dx", pin_attempts_left());
                ssd1306_draw_string_adafruit(0, 4, 47, buf, WHITE, BLACK);
            } else {
                snprintf(buf, sizeof(buf), "%d/%d", ci + 1, CS_ANGKA_LEN);
                ssd1306_draw_string_adafruit(0, 90, 47, buf, WHITE, BLACK);
            }
        }

        ssd1306_fill_rectangle(0, 0, 55, 128, 10, WHITE);
        const char *lbl = (len > 0) ? "< HAPUS" : "< BATAL";
        ssd1306_draw_string_adafruit(0, 2, 56, (char *)lbl, BLACK, WHITE);
        if (!locked) ssd1306_draw_string_adafruit(0, 78, 56, "OK isi/tahan", BLACK, WHITE);
    }

    // ======================================================
    // LAYAR 35: PIN BERHASIL DIUBAH (konfirmasi)
    // ======================================================
    else if (appMode == 35) {
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 14, 1, "PIN DIUBAH", BLACK, WHITE);

        oled_draw_bitmap(0, 48, 14, icon_centang_32, 32, 32, WHITE);
        ssd1306_draw_string_adafruit(0, 10, 48, "PIN baru tersimpan", WHITE, BLACK);

        ssd1306_fill_rectangle(0, 0, 55, 128, 10, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 56, "< KEMBALI", BLACK, WHITE);
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
    }

    // ======================================================
    // LAYAR 14: Input Password WiFi (keyboard)
    // ======================================================
    else if (appMode == 14) {
        // Sama persis kayak LAYAR 5: 2 mode keyboard (ANGKA/HURUF),
        // charset dari globals.h — satu sumber sama input_system.c.
        const char *cs14   = inputAngka ? CS_ANGKA : CS_HURUF;
        int         csLen14 = inputAngka ? CS_ANGKA_LEN : CS_HURUF_LEN;
        int         ci14    = charIdx % csLen14;

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

        // Badge MODE (kiri) — sama kayak LAYAR 5
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

        // Carousel: [prev2][prev1][SEKARANG][next1][next2]
        drawCharCarousel(0, 26, cs14, csLen14, ci14);

        char posStr[30];
        snprintf(posStr, sizeof(posStr), "%d/%d", ci14+1, csLen14);
        ssd1306_draw_string_adafruit(0, 90, 47, posStr, WHITE, BLACK);

        ssd1306_draw_string_adafruit(0, 0, 56, "OK=+  TahanOK=Connect", WHITE, BLACK);
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
    }

    // ======================================================
    // LAYAR 18: SAVED WIFI — daftar SSID yang password-nya tersimpan
    // ======================================================
    else if (appMode == 18) {
        ssd1306_clear(0);
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 1, "Saved WiFi", BLACK, WHITE);

        int cnt = wifi_saved_count();
        if (cnt == 0) {
            ssd1306_draw_string_adafruit(0, 4, 22, "Belum ada wifi", WHITE, BLACK);
            ssd1306_draw_string_adafruit(0, 4, 34, "yang tersimpan.", WHITE, BLACK);
            ssd1306_draw_string_adafruit(0, 0, 54, "< Kembali", WHITE, BLACK);
        } else {
            for (int i = 0; i < 3; i++) {
                int idx = savedWifiScroll + i;
                if (idx >= cnt) break;
                int y = 12 + i * 17;
                bool sel = (idx == savedWifiKursor);
                if (sel) ssd1306_fill_rectangle(0, 0, y-1, 128, 15, WHITE);
                char baris[36];
                snprintf(baris, sizeof(baris), "%.32s", wifi_saved_get_ssid(idx));
                ssd1306_draw_string_adafruit(0, 2, y+1, baris,
                    sel ? BLACK : WHITE, sel ? WHITE : BLACK);
            }
            ssd1306_draw_string_adafruit(0, 0, 56, "< Kembali OK Lihat", WHITE, BLACK);
        }
    }

    // ======================================================
    // LAYAR 19: DETAIL SAVED WIFI — lihat password + hapus
    // ======================================================
    else if (appMode == 19) {
        ssd1306_clear(0);
        ssd1306_fill_rectangle(0, 0, 0, 128, 9, WHITE);
        ssd1306_draw_string_adafruit(0, 2, 1, "Detail WiFi", BLACK, WHITE);

        const char *ssidS = wifi_saved_get_ssid(savedWifiKursor);
        const char *passS = wifi_saved_get_pass(savedWifiKursor);

        ssd1306_draw_string_adafruit(0, 2, 14, "SSID:", WHITE, BLACK);
        scrollTeks(ssidS, tmp, 20, true);
        ssd1306_draw_string_adafruit(0, 2, 24, tmp, WHITE, BLACK);

        ssd1306_draw_string_adafruit(0, 2, 36, "Password:", WHITE, BLACK);
        scrollTeks(passS, tmp, 20, true);
        ssd1306_draw_string_adafruit(0, 2, 46, tmp, WHITE, BLACK);

        ssd1306_draw_string_adafruit(0, 0, 56, "<Kembali  OK=Hapus", WHITE, BLACK);
    }

    // Satu-satunya ssd1306_refresh buat SEMUA layar toko (2-6, 9-19).
    // Tiap cabang appMode di atas cukup gambar ke framebuffer aja, refresh
    // ke OLED (yang paling lambat, ngirim seluruh buffer lewat I2C)
    // dilakuin sekali di sini. Dulu layar 13-19 (WiFi) punya refresh
    // sendiri-sendiri DI DALEM cabangnya + kena refresh ini lagi = 2x per
    // loop, bikin 1 putaran for(;;) molor dari 33ms, dan itu yang bikin
    // auto-repeat tombol kanan di layar password WiFi (14) kerasa lebih
    // lambat/patah-patah dibanding layar input ID (5).
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

// ==========================================================
// CEK UPDATE (OTA) — layar 20-25
// ==========================================================
void renderOtaChecking() {
    ssd1306_clear(0);
    ssd1306_draw_rectangle(0, 5, 5, 118, 54, WHITE);
    ssd1306_draw_string_adafruit(0, 14, 22, "Cek update...", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 10, 34, "Mohon tunggu", WHITE, BLACK);
    ssd1306_refresh(0, true);
}

void renderOtaNoUpdate() {
    ssd1306_clear(0);
    ssd1306_draw_rectangle(0, 5, 5, 118, 54, WHITE);
    ssd1306_draw_string_adafruit(0, 8, 18, "Sudah versi", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 8, 28, "terbaru.", WHITE, BLACK);
    char buf[32];
    snprintf(buf, sizeof(buf), "Ver: %s", ota_get_current_version());
    ssd1306_draw_string_adafruit(0, 8, 40, buf, WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 2, 56, "< OK", WHITE, BLACK);
    ssd1306_refresh(0, true);
}

void renderOtaAvailable() {
    ssd1306_clear(0);
    ssd1306_draw_rectangle(0, 5, 5, 118, 54, WHITE);
    ssd1306_draw_string_adafruit(0, 8, 15, "Update tersedia!", WHITE, BLACK);
    char buf[32];
    snprintf(buf, sizeof(buf), "Now : %s", ota_get_current_version());
    ssd1306_draw_string_adafruit(0, 8, 27, buf, WHITE, BLACK);
    snprintf(buf, sizeof(buf), "Baru: %s", otaServerVersion);
    ssd1306_draw_string_adafruit(0, 8, 37, buf, WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 2, 56, "< NO",  WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 104, 56, "[OK]",  WHITE, BLACK);
    ssd1306_refresh(0, true);
}

void renderOtaDownloading() {
    ssd1306_clear(0);
    ssd1306_draw_rectangle(0, 5, 5, 118, 54, WHITE);
    ssd1306_draw_string_adafruit(0, 10, 18, "Mengupdate...", WHITE, BLACK);
    int p = otaProgress;
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    drawLoadingBar(14, 32, 100, 10, p);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", p);
    ssd1306_draw_string_adafruit(0, 54, 46, buf, WHITE, BLACK);
    ssd1306_refresh(0, true);
}

void renderOtaSuccess() {
    ssd1306_clear(0);
    ssd1306_draw_rectangle(0, 5, 5, 118, 54, WHITE);
    ssd1306_draw_string_adafruit(0, 16, 22, "Berhasil!", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 8, 34, "Reboot...", WHITE, BLACK);
    ssd1306_refresh(0, true);
}

void renderOtaFailed() {
    ssd1306_clear(0);
    ssd1306_draw_rectangle(0, 5, 5, 118, 54, WHITE);
    ssd1306_draw_string_adafruit(0, 12, 18, "Update gagal", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 4, 28, "Cek WiFi/coba", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 20, 38, "lagi.", WHITE, BLACK);
    ssd1306_draw_string_adafruit(0, 2, 56, "< OK", WHITE, BLACK);
    ssd1306_refresh(0, true);
}
