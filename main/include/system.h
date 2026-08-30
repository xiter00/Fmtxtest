// system.h — HTTP Fetch / Axios-style API buat ESP-IDF
// Usage mirip Node.js fetch/axios
// Author: dibikin buat project JirStore H2H PPOB

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stddef.h>
#include <stdbool.h>
#include "cJSON.h"

// ============================================================
// KONFIGURASI
// ============================================================
#define HTTP_MAX_BODY       4096    // Max ukuran response body (bytes)
#define HTTP_TIMEOUT_MS     10000   // Timeout request (10 detik)

// ============================================================
// STRUCTS
// ============================================================

// Response — mirip Response object di Node.js fetch
typedef struct {
    int     status;     // HTTP status code (200, 404, 500, dll)
    bool    ok;         // true kalau status 200-299
    char   *body;       // Raw response body (string JSON / teks)
    cJSON  *_json;      // Internal: parsed JSON (jangan akses langsung)
} HttpResp;

// Method enum buat http_request() advanced
typedef enum {
    SYS_GET,
    SYS_POST,
    SYS_PUT,
    SYS_DELETE,
    SYS_PATCH,
} HttpMethod;

// Request options buat http_request() advanced (mirip axios config)
typedef struct {
    const char *url;
    HttpMethod  method;
    const char *body;           // JSON string buat POST/PUT/PATCH (boleh NULL)
    const char *content_type;   // Default: "application/json" (boleh NULL)
    const char *auth_token;     // Bearer token (boleh NULL)
    const char *api_key;        // X-API-Key header (boleh NULL)
} HttpReq;

// ============================================================
// FUNGSI UTAMA — mirip Node.js / axios
// ============================================================

/**
 * fetch(url)  →  GET request, mirip fetch() Node.js
 *
 * Contoh:
 *   HttpResp *m = fetch("https://api.mysite.com/produk");
 *   if (m && m->ok) {
 *       ESP_LOGI("TAG", "Status: %d", m->status);
 *       ESP_LOGI("TAG", "Nama: %s", resp_str(m, "nama"));
 *   }
 *   fetch_free(m);
 */
HttpResp *fetch(const char *url);

/**
 * fetch_post(url, json_body)  →  POST request dengan JSON body
 *
 * Contoh:
 *   HttpResp *m = fetch_post(
 *       "https://api.mysite.com/beli",
 *       "{\"produk\":\"PLN50\",\"id\":\"1234567890\"}"
 *   );
 *   if (m && m->ok) {
 *       ESP_LOGI("TAG", "TRX ID: %s", resp_str(m, "trx_id"));
 *       ESP_LOGI("TAG", "Harga: %d", resp_int(m, "harga"));
 *   }
 *   fetch_free(m);
 */
HttpResp *fetch_post(const char *url, const char *json_body);

/**
 * http_request(&req)  →  Request lengkap (custom method, header, auth)
 *
 * Contoh:
 *   HttpReq req = {
 *       .url        = "https://api.mysite.com/cek",
 *       .method     = SYS_POST,
 *       .body       = "{\"id\":\"123\"}",
 *       .auth_token = "secret-token-lu",
 *   };
 *   HttpResp *m = http_request(&req);
 *   fetch_free(m);
 */
HttpResp *http_request(HttpReq *req);

// ============================================================
// FUNGSI BACA RESPONSE — mirip m.nama di Node.js
// ============================================================

/**
 * resp_str(m, "key")  →  ambil string dari JSON response
 *
 * Mirip:   m.nama    di Node.js
 * Di sini: resp_str(m, "nama")
 *
 * Return NULL kalau key tidak ada atau bukan string.
 */
const char *resp_str(HttpResp *resp, const char *key);

/**
 * resp_int(m, "key")  →  ambil integer dari JSON response
 *
 * Mirip:   m.harga   di Node.js
 * Di sini: resp_int(m, "harga")
 */
int resp_int(HttpResp *resp, const char *key);

/**
 * resp_bool(m, "key")  →  ambil boolean dari JSON response
 *
 * Mirip:   m.sukses  di Node.js
 * Di sini: resp_bool(m, "sukses")
 */
bool resp_bool(HttpResp *resp, const char *key);

/**
 * resp_obj(m, "key")  →  ambil nested JSON object/array
 *
 * Mirip:   m.data    di Node.js (kalau value-nya object/array)
 * Di sini: cJSON *data = resp_obj(m, "data");
 *          const char *nama = cJSON_GetStringValue(cJSON_GetObjectItem(data, "nama"));
 */
cJSON *resp_obj(HttpResp *resp, const char *key);

/**
 * fetch_free(m)  →  wajib dipanggil setelah selesai, biar ga memory leak
 */
void fetch_free(HttpResp *resp);

#endif // SYSTEM_H
