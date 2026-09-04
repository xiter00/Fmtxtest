#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "pin_system.h"

// --- PIN TOMBOL ---
#define PIN_LEFT  5
#define PIN_RIGHT 6
#define PIN_OK    7
#define WHITE 1
#define BLACK 0
#define BTN_NONE  0
#define BTN_LEFT  1
#define BTN_RIGHT 2
#define BTN_OK    3

// ==========================================================
// PETA LAYAR (appMode) — URUT 0-12
// 0  = Menu Utama (Logo kategori + SubMenu)
// 1  = Brightness
// 2  = Daftar Item Produk
// 3  = Detail Item (full screen)
// 4  = Input Field (User ID / Nomor HP / dll)
// 5  = Input Karakter (ketik per huruf)
// 6  = Konfirmasi (review sebelum bayar)
// 7  = About
// 8  = Reboot
// 9  = Aksi Bayar (BAYAR / QRIS)
// 10 = Layar QRIS
// 11 = TRX Berhasil / Timeout / Gagal (satu layar, beda tampilan
//      tergantung trxberhasil/trxtimeout — cuma bisa "kembali" ke home)
// 30 = Input PIN Transaksi (sebelum eksekusi TF/BAYAR dari layar 9 —
//      QRIS TIDAK butuh PIN, langsung ke layar 10)
// 31 = Input PIN Lama (Settings > Edit PIN, verifikasi dulu)
// 32 = Input PIN Baru (Settings > Edit PIN, setelah PIN lama benar)
// 35 = PIN Berhasil Diubah (konfirmasi)
// ==========================================================

// --- STRUCT PRODUK ---
typedef struct {
    char nama[28];   // Nama produk (tampil layar)
    char kode[12];   // Kode buat API H2H
    int  harga;      // Harga rupiah
} StoreProduk;

// --- STRUCT FIELD INPUT ---
typedef struct {
    char label[14];  // "User ID", "Zone ID", "Nomor HP", dll
    char value[28];  // Nilai yang diketik user
} StoreField;

// --- STATE STORE ---
extern int       katKursor;         // Kategori aktif (0-3)
extern int       subKursor;         // Submenu aktif
extern int       atasMenu;          // Scroll offset submenu
extern bool      diSubMenu;         // True = di dalam submenu

extern int       katIdx;            // Index kategori di logo carousel
extern int       katArah;           // Arah geser carousel (-1/0/1)
extern bool      katAnim;           // Lagi animasi carousel?
extern uint32_t  katAnimT;          // Timestamp mulai animasi

extern int       itemKursor;        // Kursor baris item (0-2)
extern int       itemScroll;        // Scroll offset daftar item
extern int       itemDipilih;       // Index item yang dipilih
extern int       itemTotal;         // Total item submenu aktif

extern int       fieldKursor;       // Field yang aktif di input list
extern int       charIdx;           // Index karakter di CHARSET
extern int       totalField;        // Jumlah field input produk ini
extern int       caraBayar;         // 0=BAYAR, 1=QRIS

extern StoreField field[4];         // Max 4 field input
extern char      targetID[64];      // ID target: "idGame|zoneID" atau "nomorHP"
extern bool      inputAngka;        // true=mode angka(0-9) | false=mode huruf(A-Z+)

// --- APP STATE ---
extern int  appMode;
extern int  kecerahan;

// --- BINTANG DEKORASI ---
extern int bintangX[5];
extern int bintangY[5];

// -- SYSTEM API
extern bool itemtersedia;
extern bool checkstatus;
extern const char* apiKeyH2H;
extern char nickname[64];
extern bool ceknickgagal;
extern bool trxberhasil;
extern bool trxtimeout;
// -- PIN ENTRY (layar 30/31/32) --
// pinBuf   : digit yang udah "dikonfirmasi" (ditekan OK), dipakai gantian
//            buat entri PIN lama & PIN baru (mode 31 lalu 32) — dikosongin
//            tiap pindah antar layar PIN biar gak nyampur.
// pinPrev  : angka 0-9 yang lagi digeser (belum ditekan OK), sebelum
//            ditambahin ke pinBuf.
extern char pinBuf[PIN_LEN + 1];
extern int  pinPrev;
// ==========================================================
// KEYBOARD CHARSETS — SATU SUMBER (dipakai input_system.c
// buat logic DAN display_system.c buat gambar layar).
// Dulu tiap file punya salinan sendiri2 yang gak sinkron
// (charset yang KELIATAN di layar beda sama yang KEPILIH pas
// OK ditekan) — sekarang cuma ada 1 definisi biar gak nyimpang lagi.
// Urutan karakter dioptimasi biar rata-rata pencet tombol > lebih
// dikit buat kasus yang paling sering dipakai:
//   CS_HURUF -> huruf kecil dulu, baru angka, simbol email, huruf besar
//               (buat ID/username/email di layar 5)
//   CS_WIFI  -> huruf kecil + angka dulu (paling umum di password wifi),
//               baru simbol, huruf besar paling belakang (paling jarang)
// ==========================================================
extern const char CS_ANGKA[];
extern const char CS_HURUF[];
extern const char CS_WIFI[];
extern const int  CS_ANGKA_LEN;
extern const int  CS_HURUF_LEN;
extern const int  CS_WIFI_LEN;

#endif // GLOBALS_H
