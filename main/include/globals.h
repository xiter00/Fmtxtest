#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"

// ==========================================================
// PIN JOYSTICK (3 TOMBOL)
// ==========================================================
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
// APP MODE MAP
// 0  = Main Menu (Carousel Logo + SubMenu)
// 3  = Brightness
// 9  = Store: Daftar Item Produk
// 10 = Store: Detail Item (full screen)
// 11 = Store: List Input Field (User ID, Zone ID, dst)
// 12 = Store: Char Input (input 1 field per huruf)
// 13 = Store: Konfirmasi Detail (review sebelum bayar)
// 14 = About
// 15 = Reboot
// 18 = Store: Action Menu (BAYAR / QRIS)
// 19 = Store: QRIS Screen
// ==========================================================

// ==========================================================
// STORE DATA STRUCTS
// ==========================================================
typedef struct {
    char nama[28];   // Nama produk tampil di layar
    char kode[12];   // Kode produk buat API H2H
    int  harga;      // Harga dalam rupiah
} StoreProduk;

typedef struct {
    char label[14];  // "User ID", "Zone ID", "Nomor HP", dll
    char value[28];  // Nilai yang diinput user
} StoreField;

// ==========================================================
// STORE STATE EXTERN
// ==========================================================
extern int       storeKategori;       // 0=Diamond, 1=Pulsa, 2=EMoney
extern int       storeSubMenuIdx;     // Brand/operator aktif
extern int       storeItemCursor;     // Kursor item di list (0-2, visible row)
extern int       storeScrollPos;      // Scroll offset item list
extern int       storeSelectedItem;   // Index item yang dipilih
extern int       storeFieldCursor;    // Field yang dipilih di input list
extern int       storeCharPos;        // Posisi karakter di buffer
extern int       storeCharIdx;        // Index di CHARSET (cycling)
extern int       storeTotalItems;     // Total item submenu aktif
extern int       storeTotalFields;    // Jumlah field input produk ini
extern int       storePayMethod;      // 0=BAYAR, 1=QRIS
extern StoreField storeFields[4];     // Max 4 field input

// ==========================================================
// MENU STATE EXTERN
// ==========================================================
extern bool inSubMenu;
extern int  currentMenu;      // Kategori aktif (0-3)
extern int  currentSub;       // Item submenu aktif
extern int  topMenu;          // Scroll offset submenu
extern int  brightnessValue;
extern int  appMode;

// ==========================================================
// CAROUSEL ANIMASI
// ==========================================================
extern int       carouselCurrentIdx;
extern int       carouselDirection;
extern bool      carouselAnimating;
extern uint32_t  carouselAnimStart;

// ==========================================================
// DEKORASI LATAR
// ==========================================================
extern int starX[5];
extern int starY[5];

#endif // GLOBALS_H
