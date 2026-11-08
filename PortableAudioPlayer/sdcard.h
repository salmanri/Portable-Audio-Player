// sdcard.h — Manajer Kartu MicroSD, Pemindai Library MP3, Parser ID3v2/ID3v1,
//            Loader Lirik .lrc, & Dekoder Cover Art JPEG + Floyd-Steinberg Dithering 96x96.
//
// Fase 3: Membaca file lagu asli (.mp3), lirik (.lrc), dan sampul album (.jpg atau
//         embedded APIC di dalam MP3) langsung dari kartu MicroSD (FAT32).

#pragma once
#include <Arduino.h>
#include "config.h"
#include "display.h"

struct SdTrackEntry {
  char path[128];        // Path lengkap di MicroSD, mis. "/music/Coldplay - Yellow.mp3"
  char title[64];        // Judul dari tag ID3v2 (TIT2) / ID3v1 / nama file
  char artist[48];       // Artis dari tag ID3v2 (TPE1) / ID3v1 / nama folder
  char album[48];        // Album dari tag ID3v2 (TALB) / ID3v1 / nama folder
  uint32_t durationSec;  // Durasi lagu (detik) dari TLEN / bitrate frame MP3
  uint32_t apicOffset;   // Offset byte gambar JPEG embedded (APIC) di dalam file MP3 (0 = tidak ada)
  uint32_t apicSize;     // Ukuran byte gambar JPEG embedded (APIC)
};

// Inisialisasi modul MicroSD pada bus VSPI (CS = GPIO 15) dan pindai seluruh file .mp3.
// Return true jika MicroSD terbaca.
bool sdBegin();

// Pindai ulang isi kartu MicroSD. Return jumlah lagu .mp3 yang ditemukan.
int sdScanLibrary();

// Status kartu MicroSD
bool sdIsMounted();
uint64_t sdGetTotalMB();
uint64_t sdGetUsedMB();

// Akses daftar lagu yang ditemukan di MicroSD
int sdGetTrackCount();
const SdTrackEntry &sdGetTrack(int index);

// Muat file lirik .lrc yang berpasangan dengan lagu (mis. "/music/Lagu.lrc").
// Return true jika file .lrc ditemukan dan berhasil di-parse.
bool sdLoadLyrics(const SdTrackEntry &track);

// Muat cover art 96x96 1-bit (Floyd-Steinberg dithered) untuk lagu:
//   1. File "<nama_lagu>.jpg" di sebelahnya
//   2. Gambar JPEG embedded (ID3v2 APIC) di dalam file .mp3
//   3. File "cover.jpg" / "folder.jpg" di dalam folder lagu tersebut
// Return true jika cover berhasil dimuat ke coverOut (1152 byte).
bool sdLoadCover(const SdTrackEntry &track, uint8_t *coverOut);

