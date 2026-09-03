// ota_sys.h — OTA Update Manager buat JirStore
// Cek versi.txt di GitHub (via GitHub API, BUKAN raw.githubusercontent
// biar gak kena limit raw) → kalau ada versi lebih baru, download
// firmware.bin (juga lewat GitHub API) → flash ke partition OTA
// sebelah → reboot.

#ifndef OTA_SYS_H
#define OTA_SYS_H

#include <stdbool.h>

// ⚠️ WAJIB DIISI SEBELUM BUILD:
#define OTA_GH_OWNER    "xiter00"   // <-- ganti punya lu
#define OTA_GH_REPO     "updatestore"          // <-- ganti punya lu
#define OTA_GH_BRANCH   "main"                     // branch tempat version.txt & firmware.bin

// Versi firmware yang LAGI JALAN sekarang ini (ditanam di source pas
// compile). Tiap kali mau rilis versi baru: naikin angka ini di source,
// build, upload firmware.bin ke GitHub, terus edit version.txt di repo
// jadi angka yang SAMA. Device bakal bandingin angka ini vs isi
// version.txt di GitHub.
#define OTA_FW_VERSION  "1"

// ============================================================
// STATE MACHINE OTA — dibaca sama display_system.c buat nampilin
// layar yang sesuai, ditulis dari task background di ota_sys.c
// (biar gak nge-block layar/tombol kayak pola task_cek_produk).
// ============================================================
typedef enum {
    OTA_ST_IDLE = 0,
    OTA_ST_CHECKING,        // lagi request ke GitHub API buat cek version.txt
    OTA_ST_CHECK_FAILED,    // request gagal (wifi/server bermasalah)
    OTA_ST_NO_UPDATE,       // versi sekarang == versi di GitHub
    OTA_ST_UPDATE_AVAILABLE,// versi di GitHub lebih baru
    OTA_ST_DOWNLOADING,     // lagi download+flash firmware.bin
    OTA_ST_SUCCESS,         // flash sukses, bentar lagi reboot sendiri
    OTA_ST_FAILED,          // download/flash gagal
} OtaState;

extern volatile OtaState otaState;
extern char    otaServerVersion[16];   // isi version.txt dari GitHub (buat ditampilin)
extern volatile int otaProgress;       // 0-100, progress download firmware (buat progress bar)

// Mulai cek update (non-blocking — spawn task sendiri, hasil masuk ke
// otaState & otaServerVersion). Panggil sekali pas masuk ke layar cek
// update.
void ota_check_start(void);

// Mulai download + flash firmware.bin dari GitHub ke partition OTA
// yang gak lagi dipake, lalu reboot otomatis kalau sukses.
// Non-blocking — spawn task sendiri, progress via otaProgress/otaState.
void ota_update_start(void);

// Versi firmware yang lagi jalan sekarang (dari OTA_FW_VERSION di atas).
const char *ota_get_current_version(void);

#endif // OTA_SYS_H
