// weblog_sys.c — Web Log Viewer buat JirStore
// Lihat penjelasan lengkap di weblog_sys.h

#include "weblog_sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_http_server.h"

#include "wifi_sys.h"   // buat nampilin IP di log pas server nyala

static const char *TAG = "weblog";

// ============================================================
// RING BUFFER — nyimpen teks log terakhir di RAM
// ============================================================
static char               s_buf[WEBLOG_BUF_SIZE];
static size_t             s_head  = 0;   // posisi tulis berikutnya
static size_t             s_count = 0;   // jumlah byte valid (<= WEBLOG_BUF_SIZE)
static SemaphoreHandle_t  s_mutex = NULL;

// Tulis `len` byte dari `data` ke ring buffer, sambil BUANG kode
// warna ANSI (\x1b[...m) yang biasa nempel di log ESP-IDF — kalau
// gak dibuang, bakal keliatan berantakan pas ditampilin di browser.
static void _ring_append_stripped(const char *data, int len) {
    for (int i = 0; i < len; i++) {
        char c = data[i];
        if (c == 0x1B) {                 // ESC — awal kode ANSI
            i++;
            while (i < len && data[i] != 'm') i++;  // skip sampe 'm'
            continue;                    // 'm' ikut ke-skip di iterasi for
        }
        s_buf[s_head] = c;
        s_head = (s_head + 1) % WEBLOG_BUF_SIZE;
        if (s_count < WEBLOG_BUF_SIZE) s_count++;
    }
}

// Ambil snapshot isi buffer (urut dari yang paling lama ke paling
// baru) ke satu blok memori linear yang di-malloc — biar aman
// dikirim lewat jaringan TANPA nahan mutex kelamaan (kalau client
// lambat, penulisan log dari task lain gak ikut ketahan).
// Caller WAJIB free() hasilnya.
static void _snapshot(char **out_buf, size_t *out_len) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    size_t len = s_count;
    char *out = malloc(len > 0 ? len : 1);
    if (!out) {
        xSemaphoreGive(s_mutex);
        *out_buf = NULL;
        *out_len = 0;
        return;
    }

    if (len > 0) {
        size_t start = (s_count < WEBLOG_BUF_SIZE) ? 0 : s_head;
        size_t first_chunk = WEBLOG_BUF_SIZE - start;
        if (first_chunk > len) first_chunk = len;
        memcpy(out, &s_buf[start], first_chunk);
        if (first_chunk < len) {
            memcpy(out + first_chunk, &s_buf[0], len - first_chunk);
        }
    }

    xSemaphoreGive(s_mutex);
    *out_buf = out;
    *out_len = len;
}

// ============================================================
// HOOK LOGGING — nangkep semua ESP_LOG tanpa ubah kode di file lain
// ============================================================
static vprintf_like_t s_orig_vprintf = NULL;

static int _weblog_vprintf(const char *fmt, va_list args) {
    char line[WEBLOG_LINE_MAX];

    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);

    if (n > 0) {
        int len = n;
        if (len >= (int)sizeof(line)) len = sizeof(line) - 1;  // kepotong, gpp
        if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            _ring_append_stripped(line, len);
            xSemaphoreGive(s_mutex);
        }
    }

    // Tetap keluarin ke serial/USB kayak biasa — fungsi kabel gak ilang,
    // cuma sekarang ADA alternatif tambahan lewat WiFi.
    return s_orig_vprintf ? s_orig_vprintf(fmt, args) : vprintf(fmt, args);
}

void weblog_hook_install(void) {
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (s_orig_vprintf == NULL) {
        s_orig_vprintf = esp_log_set_vprintf(_weblog_vprintf);
    }
}

// ============================================================
// HALAMAN WEB — auto-refresh tiap 1 detik, dark theme, monospace
// ============================================================
static const char INDEX_HTML[] =
"<!DOCTYPE html><html lang=\"id\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<title>ESP Log Viewer</title>"
"<style>"
"*{box-sizing:border-box}"
"body{margin:0;background:#0d1117;color:#c9d1d9;font-family:ui-monospace,Consolas,Menlo,monospace}"
"header{position:sticky;top:0;background:#161b22;padding:10px 14px;display:flex;gap:10px;"
"align-items:center;border-bottom:1px solid #30363d;flex-wrap:wrap;z-index:1}"
"h1{font-size:14px;margin:0;color:#58a6ff;flex:1;white-space:nowrap}"
"button{background:#21262d;color:#c9d1d9;border:1px solid #30363d;border-radius:6px;"
"padding:6px 12px;cursor:pointer;font-size:12px}"
"button:hover{background:#30363d}"
"label{font-size:12px;display:flex;align-items:center;gap:4px;color:#8b949e;white-space:nowrap}"
"#status{font-size:11px;color:#8b949e;white-space:nowrap}"
"#dot{width:8px;height:8px;border-radius:50%;background:#3fb950;display:inline-block;margin-right:4px}"
"#dot.off{background:#f85149}"
"pre{margin:0;padding:14px;white-space:pre-wrap;word-break:break-all;font-size:12.5px;line-height:1.45}"
"</style></head><body>"
"<header>"
"<h1>ESP Log Viewer</h1>"
"<span id=\"status\"><span id=\"dot\"></span>live</span>"
"<label><input type=\"checkbox\" id=\"autoscroll\" checked>autoscroll</label>"
"<label><input type=\"checkbox\" id=\"paused\">pause</label>"
"<button onclick=\"downloadLog()\">Download</button>"
"<button onclick=\"clearLog()\">Clear</button>"
"</header>"
"<pre id=\"log\">memuat log...</pre>"
"<script>"
"const logEl=document.getElementById('log');"
"const dot=document.getElementById('dot');"
"const auto=document.getElementById('autoscroll');"
"const paused=document.getElementById('paused');"
"function nearBottom(){return (window.innerHeight+window.scrollY)>=(document.body.scrollHeight-60);}"
"async function refresh(){"
"if(paused.checked)return;"
"try{"
"const r=await fetch('/log.txt',{cache:'no-store'});"
"const t=await r.text();"
"const stick=auto.checked&&nearBottom();"
"if(t!==logEl.textContent){logEl.textContent=t;if(stick)window.scrollTo(0,document.body.scrollHeight);}"
"dot.classList.remove('off');"
"}catch(e){dot.classList.add('off');}"
"}"
"function clearLog(){fetch('/clear',{method:'POST'}).then(refresh);}"
"function downloadLog(){window.location='/download';}"
"refresh();"
"setInterval(refresh,1000);"
"</script>"
"</body></html>";

// ============================================================
// HTTP HANDLERS
// ============================================================
static esp_err_t _h_index(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t _h_logtxt(httpd_req_t *req) {
    char *snap; size_t len;
    _snapshot(&snap, &len);

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, snap, len);

    free(snap);
    return r;
}

static esp_err_t _h_download(httpd_req_t *req) {
    char *snap; size_t len;
    _snapshot(&snap, &len);

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"esp_log.txt\"");
    esp_err_t r = httpd_resp_send(req, snap, len);

    free(snap);
    return r;
}

static esp_err_t _h_clear(httpd_req_t *req) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_head  = 0;
    s_count = 0;
    xSemaphoreGive(s_mutex);

    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "OK", 2);
}

// ============================================================
// START / STOP SERVER
// ============================================================
static httpd_handle_t s_server = NULL;

void weblog_start(void) {
    if (s_server != NULL) return;  // udah jalan, gak usah start lagi
    if (s_mutex == NULL) s_mutex = xSemaphoreCreateMutex();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = WEBLOG_PORT;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 6;
    // Stack dinaikin dikit — snapshot log bisa lumayan gede (belasan KB)
    config.stack_size = 6144;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Gagal start HTTP server log viewer di port %d", WEBLOG_PORT);
        s_server = NULL;
        return;
    }

    httpd_uri_t u_index    = { .uri = "/",        .method = HTTP_GET,  .handler = _h_index };
    httpd_uri_t u_logtxt   = { .uri = "/log.txt",  .method = HTTP_GET,  .handler = _h_logtxt };
    httpd_uri_t u_download = { .uri = "/download", .method = HTTP_GET,  .handler = _h_download };
    httpd_uri_t u_clear    = { .uri = "/clear",    .method = HTTP_POST, .handler = _h_clear };

    httpd_register_uri_handler(s_server, &u_index);
    httpd_register_uri_handler(s_server, &u_logtxt);
    httpd_register_uri_handler(s_server, &u_download);
    httpd_register_uri_handler(s_server, &u_clear);

    const char *ip = wifi_get_ip();
    ESP_LOGI(TAG, "Log viewer siap -> buka http://%s/ dari HP/laptop (WiFi sama)",
             ip ? ip : "?");
}

void weblog_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
