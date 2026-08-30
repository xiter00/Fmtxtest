// wifi_sys.h — WiFi Manager untuk JirStore
// Scan, connect (open/password), disconnect, status

#ifndef WIFI_SYS_H
#define WIFI_SYS_H

#include <stdbool.h>

// ============================================================
// KONFIGURASI
// ============================================================
#define WIFI_MAX_AP         15      // Max AP yang ditampilin saat scan
#define WIFI_CONNECT_TIMEOUT_MS  10000  // Timeout konek (10 detik)

// ============================================================
// STATUS KONEKSI
// ============================================================
#define WIFI_STATUS_IDLE        0   // Belum apa-apa
#define WIFI_STATUS_SCANNING    1   // Lagi scan
#define WIFI_STATUS_CONNECTING  2   // Lagi konek
#define WIFI_STATUS_CONNECTED   3   // Konek + dapat IP
#define WIFI_STATUS_FAILED      4   // Gagal konek

// ============================================================
// STRUCT AP (hasil scan)
// ============================================================
typedef struct {
    char ssid[33];      // Nama WiFi
    bool has_pass;      // false = open, true = butuh password
    int  rssi;          // Signal strength (makin gede makin bagus)
} WifiAP;

// ============================================================
// STATE — diakses display & input system
// ============================================================
extern WifiAP  wifiList[WIFI_MAX_AP]; // Hasil scan
extern int     wifiTotal;              // Jumlah AP ditemukan
extern int     wifiKursor;             // Kursor di list WiFi (mode 13)
extern int     wifiScroll;             // Scroll offset list
extern char    wifiPassBuf[64];        // Buffer password yang diketik
extern int     wifiStatus;             // WIFI_STATUS_xxx
extern char    wifiConnectedSSID[33];  // SSID yang lagi konek

// ============================================================
// FUNGSI PUBLIK
// ============================================================

// Inisialisasi WiFi (panggil 1x di app_main / sebelum pakai)
void wifi_init(void);

// Scan AP sekitar (non-blocking, hasil masuk ke wifiList)
void wifi_scan_start(void);

// Konek ke wifiList[wifiKursor]
// - open network: langsung konek (wifiPassBuf diabaikan)
// - password:     pakai wifiPassBuf
void wifi_connect_selected(void);

// Disconnect dari WiFi aktif
void wifi_disconnect(void);

// Cek apakah sedang konek
bool wifi_is_connected(void);

// Ambil IP address (string, e.g. "192.168.1.5"), NULL kalau belum konek
const char *wifi_get_ip(void);

// RSSI icon: 0-3 (0=lemah, 3=kuat) — buat tampil di OLED
int wifi_rssi_bar(int rssi);

#endif // WIFI_SYS_H
