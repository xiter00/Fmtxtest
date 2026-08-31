// weblog_sys.h — Web Log Viewer buat JirStore
//
// Masalah yang diselesain: pas testing cuma ada 1 kabel USB-C dan
// itu lagi kepake buat ngetes device (charging/keperluan lain),
// jadi gak bisa sambil liat log lewat serial monitor.
//
// Solusi: semua ESP_LOGx (I/W/E/D/V) di-capture ke buffer di RAM,
// terus dilayani lewat HTTP server lokal di jaringan WiFi yang lagi
// dikonek ESP. Tinggal buka http://<ip-esp>/ dari HP/laptop yang
// satu WiFi buat liat log real-time — kabel USB bebas dipake buat
// yang lain.
//
// Log tetep KELUAR juga ke serial/USB kayak biasa (gak "dicuri"),
// jadi kalau kabel available lagi, serial monitor tetep jalan normal.
#ifndef WEBLOG_SYS_H
#define WEBLOG_SYS_H

// ============================================================
// KONFIGURASI
// ============================================================
// Ukuran buffer log yang disimpen di RAM (ring buffer — log paling
// lama otomatis kegusur kalau limit ini kelampauan). 12KB cukup buat
// nyimpen beberapa ribu baris log terakhir, aman buat RAM ESP32-C3.
#define WEBLOG_BUF_SIZE   12288

// Port HTTP server-nya. Dibiarin 80 biar tinggal buka http://<ip>/
// tanpa perlu nulis port di browser.
#define WEBLOG_PORT       80

// Panjang max 1 baris log yang ditampung (baris lebih panjang dari
// ini bakal kepotong pas masuk buffer, gak crash).
#define WEBLOG_LINE_MAX   256

// ============================================================
// FUNGSI PUBLIK
// ============================================================

// Pasang hook ke sistem logging ESP-IDF biar SEMUA ESP_LOG (dari
// file mana pun di project) ke-capture ke buffer web log.
// Panggil ini SEKALI, di awal-awal app_main() — sebelum log-log
// penting lain supaya gak ke-miss dari buffer.
// Log tetep jalan normal ke serial/USB seperti biasa.
void weblog_hook_install(void);

// Nyalain HTTP server log viewer. Aman dipanggil berkali-kali
// (no-op kalau server udah jalan) — jadi bisa dipanggil ulang tiap
// kali WiFi konek/reconnect tanpa was-was.
// Panggil ini SETELAH ESP dapet IP (misal di event IP_EVENT_STA_GOT_IP).
void weblog_start(void);

// Matiin HTTP server log viewer (opsional — biasanya gak perlu
// dipanggil manual, server dibiarin nyala terus).
void weblog_stop(void);

#endif // WEBLOG_SYS_H
