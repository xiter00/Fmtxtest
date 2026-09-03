// ota_sys.c — OTA Update Manager buat JirStore
//
// Alur:
//  1. ota_check_start()  -> GET https://api.github.com/repos/OWNER/REPO/contents/version.txt
//     pakai header "Accept: application/vnd.github.raw+json" biar GitHub
//     balikin ISI FILE APA ADANYA (bukan JSON+base64) — langsung dari
//     domain api.github.com, BUKAN raw.githubusercontent.com, jadi gak
//     numpang limit rate raw.
//  2. Angka hasil dibandingin ke OTA_FW_VERSION (versi yang ditanam di
//     firmware ini). Kalau beda (lebih besar) -> ada update.
//  3. ota_update_start() -> download firmware.bin (endpoint sama, path
//     beda) lewat esp_https_ota, ditulis ke partition OTA yang gak lagi
//     dipake (next update partition, dialokasiin otomatis sama
//     esp_ota_get_next_update_partition() di dalam esp_https_ota),
//     lalu esp_restart().
//
// Semua kerja jaringan jalan di task terpisah (pola sama kayak
// task_cek_produk di display_system.c) biar layar+tombol gak freeze.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "ota_sys.h"
#include "wifi_sys.h"

static const char *TAG = "ota_sys";

volatile OtaState   otaState        = OTA_ST_IDLE;
char                otaServerVersion[16] = {0};
volatile int        otaProgress     = 0;

// User-Agent WAJIB diisi buat request ke GitHub API, kalau kosong
// GitHub bakal nolak dengan 403.
#define OTA_USER_AGENT "JirStore-ESP32-OTA"

const char *ota_get_current_version(void) {
    return OTA_FW_VERSION;
}

// ============================================================
// INTERNAL: buffer kecil buat nampung isi version.txt (raw text,
// bukan JSON, jadi cukup puluhan byte).
// ============================================================
typedef struct {
    char buf[64];
    int  len;
} _VerCtx;

static esp_err_t _ver_event_handler(esp_http_client_event_t *evt) {
    _VerCtx *ctx = (_VerCtx *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        int sisa = (int)sizeof(ctx->buf) - 1 - ctx->len;
        if (sisa > 0) {
            int copy = (evt->data_len < sisa) ? evt->data_len : sisa;
            memcpy(ctx->buf + ctx->len, evt->data, copy);
            ctx->len += copy;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

// Buang whitespace/newline di ujung (isi version.txt biasanya kepotong "\n")
static void _trim(char *s) {
    int l = (int)strlen(s);
    while (l > 0 && (s[l-1] == '\n' || s[l-1] == '\r' || s[l-1] == ' ' || s[l-1] == '\t')) {
        s[--l] = '\0';
    }
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i > 0) memmove(s, s + i, strlen(s + i) + 1);
}

// ============================================================
// TASK: CEK VERSI
// ============================================================
static void _task_ota_check(void *arg) {
    (void)arg;

    // Jam device harus sync sebelum HTTPS pertama, sama kayak di system.c
    wifi_wait_time_synced(5000);

    char url[192];
    snprintf(url, sizeof(url),
             "https://api.github.com/repos/%s/%s/contents/version.txt?ref=%s",
             OTA_GH_OWNER, OTA_GH_REPO, OTA_GH_BRANCH);

    _VerCtx ctx = {0};

    esp_http_client_config_t config = {
        .url             = url,
        .event_handler   = _ver_event_handler,
        .user_data       = &ctx,
        .method          = HTTP_METHOD_GET,
        .timeout_ms      = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "gagal init http client buat cek versi");
        otaState = OTA_ST_CHECK_FAILED;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_set_header(client, "Accept", "application/vnd.github.raw+json");
    esp_http_client_set_header(client, "User-Agent", OTA_USER_AGENT);
    esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "cek versi gagal: err=%s status=%d", esp_err_to_name(err), status);
        otaState = OTA_ST_CHECK_FAILED;
        vTaskDelete(NULL);
        return;
    }

    _trim(ctx.buf);
    strncpy(otaServerVersion, ctx.buf, sizeof(otaServerVersion) - 1);
    otaServerVersion[sizeof(otaServerVersion) - 1] = '\0';

    ESP_LOGI(TAG, "versi sekarang=%s versi github=%s", OTA_FW_VERSION, otaServerVersion);

    int vNow    = atoi(OTA_FW_VERSION);
    int vServer = atoi(otaServerVersion);

    otaState = (vServer > vNow) ? OTA_ST_UPDATE_AVAILABLE : OTA_ST_NO_UPDATE;

    vTaskDelete(NULL);
}

void ota_check_start(void) {
    otaState          = OTA_ST_CHECKING;
    otaServerVersion[0]= '\0';
    xTaskCreate(_task_ota_check, "ota_check", 6144, NULL, 5, NULL);
}

// ============================================================
// TASK: DOWNLOAD + FLASH FIRMWARE
// ============================================================

// Dipanggil sama esp_https_ota SEBELUM request firmware.bin dikirim —
// dipake buat nambahin header yang GitHub API butuhin (Accept +
// User-Agent), sama kayak alasannya di _task_ota_check di atas.
static esp_err_t _https_ota_header_cb(esp_http_client_handle_t client) {
    esp_http_client_set_header(client, "Accept", "application/vnd.github.raw+json");
    esp_http_client_set_header(client, "User-Agent", OTA_USER_AGENT);
    esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
    return ESP_OK;
}

static void _task_ota_update(void *arg) {
    (void)arg;

    wifi_wait_time_synced(5000);

    char url[192];
    snprintf(url, sizeof(url),
              "https://api.github.com/repos/%s/%s/contents/firmware.bin?ref=%s",
              OTA_GH_OWNER, OTA_GH_REPO, OTA_GH_BRANCH);

    otaProgress = 0;
    otaState    = OTA_ST_DOWNLOADING;

    esp_http_client_config_t http_config = {
        .url               = url,
        .timeout_ms        = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config          = &http_config,
        .http_client_init_cb  = _https_ota_header_cb,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK || !https_ota_handle) {
        ESP_LOGE(TAG, "esp_https_ota_begin gagal: %s", esp_err_to_name(err));
        otaState = OTA_ST_FAILED;
        vTaskDelete(NULL);
        return;
    }

    int totalSize = esp_https_ota_get_image_size(https_ota_handle);

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;

        int sudah = esp_https_ota_get_image_len_read(https_ota_handle);
        if (totalSize > 0) {
            otaProgress = (sudah * 100) / totalSize;
            if (otaProgress > 100) otaProgress = 100;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_perform gagal: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        otaState = OTA_ST_FAILED;
        vTaskDelete(NULL);
        return;
    }

    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "data firmware belum lengkap keterima semua");
        esp_https_ota_abort(https_ota_handle);
        otaState = OTA_ST_FAILED;
        vTaskDelete(NULL);
        return;
    }

    err = esp_https_ota_finish(https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_finish gagal: %s", esp_err_to_name(err));
        otaState = OTA_ST_FAILED;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA sukses, reboot ke firmware baru...");
    otaProgress = 100;
    otaState    = OTA_ST_SUCCESS;

    vTaskDelay(pdMS_TO_TICKS(1500)); // biar layar "Berhasil!" sempet keliatan
    esp_restart();
}

void ota_update_start(void) {
    xTaskCreate(_task_ota_update, "ota_update", 8192, NULL, 5, NULL);
}
