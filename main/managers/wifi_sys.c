// wifi_sys.c — WiFi Manager untuk JirStore
// Scan, connect (open/password), disconnect

#include "wifi_sys.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_sys";

// ============================================================
// STATE — definisi variabel extern
// ============================================================
WifiAP  wifiList[WIFI_MAX_AP] = {0};
int     wifiTotal              = 0;
int     wifiKursor             = 0;
int     wifiScroll             = 0;
char    wifiPassBuf[64]        = {0};
int     wifiStatus             = WIFI_STATUS_IDLE;
char    wifiConnectedSSID[33]  = {0};

// ============================================================
// INTERNAL
// ============================================================
static bool          s_wifi_inited = false;
static char          s_ip_str[20]  = {0};
static EventGroupHandle_t s_evt_grp = NULL;

#define BIT_CONNECTED  BIT0
#define BIT_FAILED     BIT1

// ============================================================
// EVENT HANDLER
// ============================================================
static void _wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data) {
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            // WiFi station started — dipanggil setelah esp_wifi_start()
            // Kalau kita mau auto-reconnect bisa esp_wifi_connect() di sini
        }
        else if (id == WIFI_EVENT_SCAN_DONE) {
            // Scan selesai — ambil hasilnya
            uint16_t count = WIFI_MAX_AP;
            wifi_ap_record_t records[WIFI_MAX_AP];
            memset(records, 0, sizeof(records));

            esp_wifi_scan_get_ap_records(&count, records);
            wifiTotal = (int)count;

            for (int i = 0; i < wifiTotal; i++) {
                strncpy(wifiList[i].ssid, (char *)records[i].ssid, 32);
                wifiList[i].ssid[32] = '\0';
                wifiList[i].has_pass = (records[i].authmode != WIFI_AUTH_OPEN);
                wifiList[i].rssi     = records[i].rssi;
            }

            wifiKursor = 0;
            wifiScroll = 0;
            wifiStatus = WIFI_STATUS_IDLE;  // Scan selesai, balik idle
            ESP_LOGI(TAG, "Scan done: %d AP ditemukan", wifiTotal);
        }
        else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "Disconnected, reason: %d", ev->reason);

            memset(s_ip_str, 0, sizeof(s_ip_str));
            memset(wifiConnectedSSID, 0, sizeof(wifiConnectedSSID));

            if (wifiStatus == WIFI_STATUS_CONNECTING) {
                // Gagal konek (timeout / salah password)
                wifiStatus = WIFI_STATUS_FAILED;
                if (s_evt_grp) xEventGroupSetBits(s_evt_grp, BIT_FAILED);
            } else {
                // Disconnect normal atau kehilangan koneksi
                wifiStatus = WIFI_STATUS_IDLE;
            }
        }
    }
    else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
            snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&ev->ip_info.ip));

            wifiStatus = WIFI_STATUS_CONNECTED;

            // Simpan SSID yang konek
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                strncpy(wifiConnectedSSID, (char *)ap.ssid, 32);
            }

            if (s_evt_grp) xEventGroupSetBits(s_evt_grp, BIT_CONNECTED);
            ESP_LOGI(TAG, "Konek! IP: %s", s_ip_str);
        }
    }
}

// ============================================================
// WIFI INIT — panggil 1x di awal
// ============================================================
void wifi_init(void) {
    if (s_wifi_inited) return;

    // NVS wajib diinit sebelum WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // Register event handler
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        _wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        _wifi_event_handler, NULL, NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    s_evt_grp    = xEventGroupCreate();
    s_wifi_inited = true;

    ESP_LOGI(TAG, "WiFi init OK");
}

// ============================================================
// SCAN — non-blocking, hasilnya masuk event SCAN_DONE
// ============================================================
void wifi_scan_start(void) {
    if (!s_wifi_inited) wifi_init();

    wifiTotal  = 0;
    wifiStatus = WIFI_STATUS_SCANNING;
    memset(wifiList, 0, sizeof(wifiList));

    wifi_scan_config_t scan_cfg = {
        .ssid        = NULL,    // Scan semua
        .bssid       = NULL,
        .channel     = 0,       // Semua channel
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_wifi_scan_start(&scan_cfg, false);  // false = non-blocking
    ESP_LOGI(TAG, "Scan mulai...");
}

// ============================================================
// CONNECT — konek ke AP yang dipilih (wifiList[wifiKursor])
// ============================================================
void wifi_connect_selected(void) {
    if (!s_wifi_inited) wifi_init();
    if (wifiTotal == 0) return;

    WifiAP *ap = &wifiList[wifiKursor];

    // Disconnect dulu kalau masih nyambung
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Set config
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    strncpy((char *)cfg.sta.ssid, ap->ssid, sizeof(cfg.sta.ssid) - 1);

    if (ap->has_pass) {
        strncpy((char *)cfg.sta.password, wifiPassBuf, sizeof(cfg.sta.password) - 1);
    }

    cfg.sta.threshold.authmode = ap->has_pass ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable    = true;
    cfg.sta.pmf_cfg.required   = false;

    esp_wifi_set_config(WIFI_IF_STA, &cfg);

    wifiStatus = WIFI_STATUS_CONNECTING;
    if (s_evt_grp) xEventGroupClearBits(s_evt_grp, BIT_CONNECTED | BIT_FAILED);

    esp_wifi_connect();
    ESP_LOGI(TAG, "Nyoba konek ke: %s", ap->ssid);
}

// ============================================================
// DISCONNECT
// ============================================================
void wifi_disconnect(void) {
    esp_wifi_disconnect();
    wifiStatus = WIFI_STATUS_IDLE;
    memset(s_ip_str, 0, sizeof(s_ip_str));
    memset(wifiConnectedSSID, 0, sizeof(wifiConnectedSSID));
    ESP_LOGI(TAG, "Disconnected");
}

// ============================================================
// STATUS HELPERS
// ============================================================
bool wifi_is_connected(void) {
    return (wifiStatus == WIFI_STATUS_CONNECTED);
}

const char *wifi_get_ip(void) {
    return (wifiStatus == WIFI_STATUS_CONNECTED) ? s_ip_str : NULL;
}

// RSSI → bar 0-3 buat ikon di OLED
int wifi_rssi_bar(int rssi) {
    if (rssi >= -55) return 3;  // Kuat
    if (rssi >= -70) return 2;  // Sedang
    if (rssi >= -85) return 1;  // Lemah
    return 0;                    // Sangat lemah
}
