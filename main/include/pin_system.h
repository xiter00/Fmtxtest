// pin_system.h — Manager PIN transaksi buat JirStore
//
// PIN dipakai buat:
//   1) Konfirmasi sebelum eksekusi TF/BAYAR (layar 9 -> OK, pilihan
//      "TF/BAYAR"). QRIS TIDAK butuh PIN — begitu OK ditekan di layar 9
//      dengan pilihan QRIS, langsung tampil QR-nya tanpa lewat PIN.
//   2) Verifikasi PIN lama sebelum boleh ganti PIN baru (Settings > Edit PIN)
//
// Disimpan di NVS (namespace "pinstore", key "pin") sebagai string,
// jadi PIN TETAP tersimpan walau device di-reboot / mati listrik.
// Kalau NVS belum ada isinya (baru pertama kali flash), otomatis
// ke-isi PIN_DEFAULT (hardcoded) sekali di awal — itu yang langsung
// bisa dipakai abis flash, sampai user ganti sendiri lewat Settings.
//
// Proteksi brute-force: 5x salah berturut-turut -> LOCKOUT
// PIN_LOCK_MS (30 detik). Selama lockout, percobaan verifikasi
// ditolak otomatis (gak ngurangin/nambah hitungan salah lagi).
// Begitu waktu lockout abis, hitungan salah balik ke 0 (dapat
// kesempatan 5x lagi).
#ifndef PIN_SYSTEM_H
#define PIN_SYSTEM_H

#include <stdbool.h>
#include <stdint.h>

// ============================================================
// KONFIGURASI
// ============================================================
#define PIN_LEN         6         // Panjang PIN (digit 0-9)
#define PIN_MAX_FAILS   5         // Batas salah berturut-turut
#define PIN_LOCK_MS     30000     // Lama lockout (30 detik)
#define PIN_DEFAULT     "123456"  // PIN awal hardcoded, dipakai kalau NVS masih kosong

// ============================================================
// INIT — panggil sekali di boot (sebelum PIN dipakai / ditampilkan)
// Baca PIN dari NVS; kalau belum ada, tulis PIN_DEFAULT dulu.
// ============================================================
void pin_system_init(void);

// ============================================================
// VERIFIKASI — cocokkan `input` (harus PIN_LEN digit, null-terminated)
// sama PIN yang lagi aktif.
// - Kalau BENAR  : reset hitungan salah, return true.
// - Kalau SALAH  : nambah hitungan salah, kalau nyampe PIN_MAX_FAILS
//                  langsung masuk lockout PIN_LOCK_MS, return false.
// - Kalau lagi LOCKOUT: langsung return false, TIDAK nambah hitungan
//                  salah (biar gak keitung dobel sama UI yang manggil
//                  berulang selagi nunggu).
// ============================================================
bool pin_verify(const char *input);

// ============================================================
// GANTI PIN — simpan `newpin` (harus PIN_LEN digit) jadi PIN aktif,
// langsung persist ke NVS. Panggil ini HANYA setelah pin_verify()
// PIN LAMA berhasil (true) — fungsi ini sendiri tidak cek PIN lama.
// ============================================================
void pin_change(const char *newpin);

// ============================================================
// STATUS LOCKOUT — true kalau lagi dalam masa tunggu 30 detik.
// Kalau ms_left != NULL, diisi sisa waktu lockout dalam ms.
// Otomatis clear sendiri (hitungan salah balik 0) begitu waktunya abis.
// ============================================================
bool pin_is_locked(uint32_t *ms_left);

// ============================================================
// Sisa percobaan sebelum lockout (buat ditampilin di layar,
// misal "3 percobaan lagi"). Selalu antara 0..PIN_MAX_FAILS.
// ============================================================
int pin_attempts_left(void);

#endif // PIN_SYSTEM_H
