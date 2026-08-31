// system.c — HTTP Fetch / Axios-style API buat ESP-IDF
// Implementasi fungsi fetch() dan http_request() ala Node.js
// Requires: esp_http_client, json (cJSON) di CMakeLists.txt

#include "system.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>     // buat strerror() pas nge-log errno socket/TLS

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"
#include "server_cert.h"

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
//
// Di-log SEMUA tahapan request (bukan cuma pas data masuk / error
// generik) — biar kalau macet/gagal, keliatan PERSIS di tahap mana:
// belum konek sama sekali? konek tapi header gak pernah balik?
// header balik tapi body kosong? dll. Ini yang dipake buat diagnosa
// "kok gagal" tanpa nebak2 lagi.
// ============================================================
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    _BufCtx *ctx = (_BufCtx *)evt->user_data;

    switch (evt->event_id) {

        case HTTP_EVENT_ERROR:
            // Event ini KOSONGAN infonya (esp_http_client emang gitu),
            // detail errornya baru keliatan dari return value
            // esp_http_client_perform() di _do_request — tapi tetep
            // di-log biar kelihatan URUTAN kejadiannya di log.
            ESP_LOGE(TAG, "[event] HTTP_EVENT_ERROR (detail error ada di baris 'perform gagal' di bawah)");
            break;

        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "[event] konek ke server berhasil (TCP+TLS handshake OK)");
            break;

        case HTTP_EVENT_HEADERS_SENT:
            ESP_LOGI(TAG, "[event] request headers+body udah dikirim ke server");
            break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "[event] header balik dari server: %s: %s", evt->header_key, evt->header_value);
            break;

        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "[event] terima %d byte data body", evt->data_len);
            if (ctx && evt->data_len > 0 && ctx->len < ctx->max - 1) {
                int sisa  = ctx->max - 1 - ctx->len;
                int copy  = (evt->data_len < sisa) ? evt->data_len : sisa;
                memcpy(ctx->buf + ctx->len, evt->data, copy);
                ctx->len += copy;
            } else if (ctx && ctx->len >= ctx->max - 1) {
                ESP_LOGW(TAG, "[event] buffer response udah penuh (HTTP_MAX_BODY=%d), sisa data dibuang", ctx->max);
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "[event] response selesai diterima semua");
            if (ctx) ctx->buf[ctx->len] = '\0';
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "[event] koneksi ke server terputus (HTTP_EVENT_DISCONNECTED)");
            break;

        default:
            ESP_LOGI(TAG, "[event] event_id=%d (gak ada handler khusus)", evt->event_id);
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

    ESP_LOGI(TAG, "=== MULAI REQUEST === url=%s method=%d body=%s",
             url, method, body ? body : "(tidak ada body)");

    // Alokasi buffer response di heap (bukan stack, aman buat task kecil)
    char *buf = calloc(HTTP_MAX_BODY, 1);
    if (!buf) {
        ESP_LOGE(TAG, "Gagal alokasi buffer response (HTTP_MAX_BODY=%d, cek heap free)", HTTP_MAX_BODY);
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
    .cert_pem       = atlanticcert, 
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Gagal init HTTP client (esp_http_client_init return NULL)");
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

    // Kirim request — sepanjang ini jalan, _http_event_handler bakal
    // ngeluarin log per-tahap (konek → kirim header → terima data → selesai)
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        // INI YANG PALING PENTING kalau requestnya gagal total (bukan
        // soal parsing JSON): TLS/cert gak cocok, DNS gagal, timeout,
        // server nolak konek, dll — semua ketangkep di sini.
        int sock_errno = errno;
        ESP_LOGE(TAG, "=== REQUEST GAGAL === esp_err=%s (0x%x) | errno=%d (%s) | url=%s",
                 esp_err_to_name(err), err, sock_errno, strerror(sock_errno), url);
        esp_http_client_cleanup(client);
        free(buf);
        return NULL;
    }

    int status_code = esp_http_client_get_status_code(client);
    int64_t content_len = esp_http_client_get_content_length(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "=== REQUEST SELESAI === status=%d content_length=%lld byte_diterima=%d url=%s",
             status_code, content_len, ctx.len, url);

    // --------------------------------------------------------
    // Bangun HttpResp
    // --------------------------------------------------------
    HttpResp *resp = calloc(1, sizeof(HttpResp));
    if (!resp) {
        ESP_LOGE(TAG, "Gagal alokasi HttpResp (cek heap free)");
        free(buf);
        return NULL;
    }

    resp->status = status_code;
    resp->ok     = (status_code >= 200 && status_code < 300);
    resp->body   = buf;                     // Transfer ownership buffer ke resp

    if (ctx.len == 0) {
        ESP_LOGW(TAG, "Body response KOSONG (0 byte) — server balas tapi gak ngirim apa2");
    }

    resp->_json  = cJSON_Parse(buf);        // Parse JSON (NULL kalau bukan JSON)

    if (!resp->_json) {
        const char *errPtr = cJSON_GetErrorPtr();
        ESP_LOGW(TAG, "Response BUKAN JSON valid / gagal di-parse. Body asli: %.200s", buf);
        if (errPtr) {
            ESP_LOGW(TAG, "cJSON berhenti parse di sekitar sini: ...%.60s", errPtr);
        }
    }

    if (!resp->ok) {
        ESP_LOGW(TAG, "HTTP status BUKAN 2xx (%d) — body: %.200s", status_code, buf);
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
