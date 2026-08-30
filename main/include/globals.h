#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"

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
// 11 = TRX Berhasil
// 12 = TRX Gagal
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

#endif // GLOBALS_H
