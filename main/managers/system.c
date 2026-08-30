// system.c — HTTP Fetch / Axios-style API buat ESP-IDF
// Implementasi fungsi fetch() dan http_request() ala Node.js
// Requires: esp_http_client, json (cJSON) di CMakeLists.txt

#include "system.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_err.h"
#include <cjson/cJSON.h>

static const char *TAG = "system";

// ============================================================
// INTERNAL: Buffer context buat event handler
// Pake struct biar thread-safe (ga pakai static global)
// ============================================================
typedef struct {
    char *buf;      // Buffer tampung response body
    int   len;      // Panjang data yang udah masuk
    int   max;      // Ukuran max buffer
} _BufCtx;

// ============================================================
// INTERNAL: Event handler — dipanggil ESP HTTP client
// ============================================================
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    _BufCtx *ctx = (_BufCtx *)evt->user_data;
    if (!ctx) return ESP_OK;

    switch (evt->event_id) {

        case HTTP_EVENT_ON_DATA:
            // Terima chunk data, tambahin ke buffer
            if (evt->data_len > 0 && ctx->len < ctx->max - 1) {
                int sisa  = ctx->max - 1 - ctx->len;
                int copy  = (evt->data_len < sisa) ? evt->data_len : sisa;
                memcpy(ctx->buf + ctx->len, evt->data, copy);
                ctx->len += copy;
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            // Null-terminate supaya jadi string
            ctx->buf[ctx->len] = '\0';
            break;

        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP event error");
            break;

        default:
            break;
    }

    return ESP_OK;
}

// ============================================================
// INTERNAL: Core request function — semua fetch/post/dll lewat sini
// ============================================================
static HttpResp *_do_request(
    const char             *url,
    esp_http_client_method_t method,
    const char             *body,
    const char             *content_type,
    const char             *auth_token,
    const char             *api_key
) {
    if (!url) {
        ESP_LOGE(TAG, "URL tidak boleh NULL");
        return NULL;
    }

    // Alokasi buffer response di heap (bukan stack, aman buat task kecil)
    char *buf = calloc(HTTP_MAX_BODY, 1);
    if (!buf) {
        ESP_LOGE(TAG, "Gagal alokasi buffer response");
        return NULL;
    }

    _BufCtx ctx = {
        .buf = buf,
        .len = 0,
        .max = HTTP_MAX_BODY,
    };

    // Config HTTP client
    esp_http_client_config_t config = {
        .url            = url,
        .event_handler  = _http_event_handler,
        .user_data      = &ctx,
        .method         = method,
        .timeout_ms     = HTTP_TIMEOUT_MS,
        .buffer_size    = 512,
        .buffer_size_tx = 512,
        // Kalau pakai HTTPS, tambahin:
        // .skip_cert_common_name_check = true,
        // .transport_type = HTTP_TRANSPORT_OVER_SSL,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Gagal init HTTP client");
        free(buf);
        return NULL;
    }

    // Set headers default
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Content-Type",
        content_type ? content_type : "application/json");

    // Bearer token (kalau ada)
    if (auth_token) {
        char auth_hdr[256];
        snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", auth_token);
        esp_http_client_set_header(client, "Authorization", auth_hdr);
    }

    // API Key (kalau ada)
    if (api_key) {
        esp_http_client_set_header(client, "X-Api-Key", api_key);
    }

    // Set body untuk POST / PUT / PATCH
    if (body && (
        method == HTTP_METHOD_POST  ||
        method == HTTP_METHOD_PUT   ||
        method == HTTP_METHOD_PATCH
    )) {
        esp_http_client_set_post_field(client, body, (int)strlen(body));
    }

    // Kirim request
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request gagal: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(buf);
        return NULL;
    }

    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Response [%d] %d bytes dari %s", status_code, ctx.len, url);

    // --------------------------------------------------------
    // Bangun HttpResp
    // --------------------------------------------------------
    HttpResp *resp = calloc(1, sizeof(HttpResp));
    if (!resp) {
        free(buf);
        return NULL;
    }

    resp->status = status_code;
    resp->ok     = (status_code >= 200 && status_code < 300);
    resp->body   = buf;                     // Transfer ownership buffer ke resp
    resp->_json  = cJSON_Parse(buf);        // Parse JSON (NULL kalau bukan JSON)

    if (!resp->_json) {
        ESP_LOGW(TAG, "Response bukan JSON atau gagal parse: %.80s...", buf);
    }

    return resp;
}

// ============================================================
// PUBLIC API — fungsi yang dipanggil dari luar
// ============================================================

HttpResp *fetch(const char *url) {
    return _do_request(url, HTTP_METHOD_GET, NULL, NULL, NULL, NULL);
}

HttpResp *fetch_post(const char *url, const char *json_body) {
    return _do_request(url, HTTP_METHOD_POST, json_body, "application/json", NULL, NULL);
}

HttpResp *http_request(HttpReq *req) {
    if (!req) return NULL;

    esp_http_client_method_t method;
    switch (req->method) {
        case SYS_POST:   method = HTTP_METHOD_POST;   break;
        case SYS_PUT:    method = HTTP_METHOD_PUT;    break;
        case SYS_DELETE: method = HTTP_METHOD_DELETE; break;
        case SYS_PATCH:  method = HTTP_METHOD_PATCH;  break;
        default:         method = HTTP_METHOD_GET;    break;
    }

    return _do_request(
        req->url,
        method,
        req->body,
        req->content_type,
        req->auth_token,
        req->api_key
    );
}

// ============================================================
// PUBLIC: Baca field dari JSON response
// ============================================================

const char *resp_str(HttpResp *resp, const char *key) {
    if (!resp || !resp->_json || !key) return NULL;
    cJSON *item = cJSON_GetObjectItem(resp->_json, key);
    if (!item || !cJSON_IsString(item)) return NULL;
    return item->valuestring;
}

int resp_int(HttpResp *resp, const char *key) {
    if (!resp || !resp->_json || !key) return 0;
    cJSON *item = cJSON_GetObjectItem(resp->_json, key);
    if (!item || !cJSON_IsNumber(item)) return 0;
    return (int)item->valuedouble;
}

bool resp_bool(HttpResp *resp, const char *key) {
    if (!resp || !resp->_json || !key) return false;
    cJSON *item = cJSON_GetObjectItem(resp->_json, key);
    if (!item) return false;
    return cJSON_IsTrue(item);
}

cJSON *resp_obj(HttpResp *resp, const char *key) {
    if (!resp || !resp->_json || !key) return NULL;
    return cJSON_GetObjectItem(resp->_json, key);
}

// ============================================================
// PUBLIC: Bebasin memory — WAJIB dipanggil setelah selesai
// ============================================================
void fetch_free(HttpResp *resp) {
    if (!resp) return;
    if (resp->_json) cJSON_Delete(resp->_json);
    if (resp->body)  free(resp->body);
    free(resp);
}
