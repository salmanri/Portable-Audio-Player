// display.h — Driver E-Paper WeAct Studio 2.9" SSD1680 (296x128)
// Mendukung 2 tampilan utama (gaya MusicStation v2 / Retro Walkman):
//   1. Now Playing Screen : Header + Cover Art 96x96 + Synced Lyrics + Progress Bar + Volume Popup
//   2. Library Menu Screen: Header + 6 Baris Daftar Menu/Lagu dengan Inverted Selection Bar

#pragma once
#include <Arduino.h>

constexpr int SCREEN_W = 296;
constexpr int SCREEN_H = 128;

// Dimensi Layout Now Playing
constexpr int COVER_X      = 4;
constexpr int COVER_Y      = 32;
constexpr int COVER_SIZE   = 96;
constexpr int COVER_BYTES  = (COVER_SIZE * COVER_SIZE) / 8;
constexpr int RIGHT_X      = 104;
constexpr int RIGHT_W      = 192;

constexpr int LYRIC_AREA_Y = 32;
constexpr int LYRIC_AREA_H = 72;

constexpr int TRANSPORT_Y  = 104;
constexpr int TRANSPORT_H  = 24;

// Dimensi Layout Menu / Library (6 baris @ 18px = 108px, Y=20..127)
constexpr int MENU_HEADER_H     = 20;
constexpr int MENU_LIST_Y       = 20;
constexpr int MENU_ROW_H        = 18;
constexpr int MENU_VISIBLE_ROWS = 6;

// Inisialisasi SPI + layar E-Paper SSD1680. Panggil sekali di setup().
void displayBegin();

// Layar Splash / Booting singkat.
void displayDrawSplash();

// Layar Now Playing lengkap (Full Refresh).
void displayDrawTrack(const char *title, const char *artist, const char *badgeRight,
                      const uint8_t *coverBitmap,
                      uint32_t elapsedSec, uint32_t durationSec, bool playing,
                      const char *lyricText = nullptr);

// Update hanya area transport / progress bar (Partial Refresh).
void displayUpdateTransport(uint32_t elapsedSec, uint32_t durationSec, bool playing);

// Update hanya area lirik (Partial Refresh).
void displayUpdateLyrics(const char *lyricText);

// Update lirik + transport sekaligus dalam 1 Partial Refresh di kolom kanan.
void displayUpdateLyricsAndTransport(const char *lyricText, uint32_t elapsedSec,
                                     uint32_t durationSec, bool playing);

// Tampilkan pop-up bar Volume di area kanan (Partial Refresh).
void displayUpdateVolumePopup(int volumePercent, const char *outputName,
                              uint32_t elapsedSec, uint32_t durationSec, bool playing);

// Gambar layar Menu / Daftar Lagu (bisa Full Refresh saat pindah halaman,
// atau Partial Refresh saat scroll naik/turun dengan Rotary Encoder).
void displayDrawMenu(const char *headerTitle, const char *headerRight,
                     const char *const *items, const char *const *rightLabels,
                     int itemCount, int selectedIndex, bool fullRefresh);

// Gambar layar informasi perangkat / bantuan kontrol.
void displayDrawAbout(const char *outputName, const char *playModeName, int volumePercent);

