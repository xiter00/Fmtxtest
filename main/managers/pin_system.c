// pin_system.c — Manager PIN transaksi buat JirStore (lihat pin_system.h)

#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "pin_system.h"

static const char *TAG = "pin_system";

#define PIN_NVS_NS  "pinstore"
#define PIN_NVS_KEY "pin"

static char     s_currentPin[PIN_LEN + 1] = {0};
static int      s_fails                   = 0;
static uint32_t s_lockUntil                = 0;   // 0 = gak lockout
static bool     s_loaded                   = false;

static uint32_t _now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ============================================================
// INIT — baca PIN dari NVS, kalau belum ada tulis PIN_DEFAULT dulu
// ============================================================
void pin_system_init(void) {
    if (s_loaded) return;

    nvs_handle_t h;
    esp_err_t err = nvs_open(PIN_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Gagal buka NVS namespace '%s' (err=%s) — pakai PIN default di RAM aja",
                 PIN_NVS_NS, esp_err_to_name(err));
        strncpy(s_currentPin, PIN_DEFAULT, PIN_LEN);
        s_currentPin[PIN_LEN] = '\0';
        s_loaded = true;
        return;
    }

    char buf[PIN_LEN + 1] = {0};
    size_t len = sizeof(buf);
    err = nvs_get_str(h, PIN_NVS_KEY, buf, &len);

    if (err == ESP_OK && strlen(buf) == PIN_LEN) {
        // PIN sudah pernah disimpan (misal abis diedit user) — pakai itu
        strncpy(s_currentPin, buf, PIN_LEN);
        s_currentPin[PIN_LEN] = '\0';
        ESP_LOGI(TAG, "PIN dimuat dari NVS");
    } else {
        // Belum ada / rusak / beda panjang -> tulis PIN default sekali
        strncpy(s_currentPin, PIN_DEFAULT, PIN_LEN);
        s_currentPin[PIN_LEN] = '\0';
        nvs_set_str(h, PIN_NVS_KEY, s_currentPin);
        nvs_commit(h);
        ESP_LOGI(TAG, "PIN belum ada di NVS -> ditulis PIN default");
    }

    nvs_close(h);
    s_fails     = 0;
    s_lockUntil = 0;
    s_loaded    = true;
}

// ============================================================
// LOCKOUT STATUS — juga yang bertugas auto-clear kalau udah lewat
// ============================================================
bool pin_is_locked(uint32_t *ms_left) {
    uint32_t now = _now_ms();
    if (s_lockUntil != 0) {
        if (now < s_lockUntil) {
            if (ms_left) *ms_left = s_lockUntil - now;
            return true;
        }
        // Lockout udah lewat -> reset, kasih kesempatan lagi
        s_lockUntil = 0;
        s_fails     = 0;
    }
    if (ms_left) *ms_left = 0;
    return false;
}

int pin_attempts_left(void) {
    int left = PIN_MAX_FAILS - s_fails;
    if (left < 0) left = 0;
    return left;
}

// ============================================================
// VERIFIKASI
// ============================================================
bool pin_verify(const char *input) {
    if (!s_loaded) pin_system_init();
    if (!input) return false;

    // Masih lockout? Tolak langsung, jangan ngitung salah lagi.
    if (pin_is_locked(NULL)) return false;

    if (strncmp(input, s_currentPin, PIN_LEN) == 0 && strlen(input) == PIN_LEN) {
        s_fails = 0;
        return true;
    }

    s_fails++;
    if (s_fails >= PIN_MAX_FAILS) {
        s_lockUntil = _now_ms() + PIN_LOCK_MS;
        ESP_LOGW(TAG, "PIN salah %d kali berturut-turut -> lockout %d detik",
                 s_fails, PIN_LOCK_MS / 1000);
    }
    return false;
}

// ============================================================
// GANTI PIN — panggil cuma setelah PIN lama diverifikasi benar
// ============================================================
void pin_change(const char *newpin) {
    if (!newpin || strlen(newpin) != PIN_LEN) {
        ESP_LOGE(TAG, "pin_change ditolak: panjang PIN baru tidak %d digit", PIN_LEN);
        return;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(PIN_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Gagal buka NVS buat simpan PIN baru (err=%s)", esp_err_to_name(err));
        return;
    }

    nvs_set_str(h, PIN_NVS_KEY, newpin);
    nvs_commit(h);
    nvs_close(h);

    strncpy(s_currentPin, newpin, PIN_LEN);
    s_currentPin[PIN_LEN] = '\0';

    // PIN baru aktif -> mulai bersih, gak bawa2 hitungan salah lama
    s_fails     = 0;
    s_lockUntil = 0;

    ESP_LOGI(TAG, "PIN berhasil diganti & disimpan ke NVS");
}
