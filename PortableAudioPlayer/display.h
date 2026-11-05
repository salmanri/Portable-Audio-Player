// display.h — Driver & Desain UI E-Paper WeAct 2.9" SSD1680 (296x128)
//
// Bahasa desain: bersih ala perangkat pabrikan / Walkman retro —
//   status bar tipis + garis rambut (hairline), ikon vektor 12x12,
//   tipografi Helvetica, selection bar membulat, scrollbar tipis.
//
// Layar Now Playing:
// +--------------------------------------------------------------+
// | Now Playing                                  [rep] [hp] [bat] |  status bar
// |--------------------------------------------------------------|
// | +----------+  Judul Lagu (bold)                              |
// | |          |  Artis · Album                                  |
// | |  COVER   |  · · · · · · · · · · · · · · · · · · · · · · ·  |
// | |  96x96   |        Lirik aktif (auto-size 2-3 baris)        |
// | |          |  ====================o-----------------------   |
// | +----------+  1:23                 |>                  3:45   |
// +--------------------------------------------------------------+

#pragma once
#include <Arduino.h>

constexpr int SCREEN_W    = 296;
constexpr int SCREEN_H    = 128;
constexpr int COVER_SIZE  = 96;
constexpr int COVER_BYTES = (COVER_SIZE * COVER_SIZE) / 8;

enum UiIcon : uint8_t {
  ICON_NONE = 0,
  ICON_NOTE,
  ICON_LIST,
  ICON_ARTIST,
  ICON_ALBUM,
  ICON_HEADPHONES,
  ICON_SPEAKER,
  ICON_BLUETOOTH,
  ICON_REPEAT,
  ICON_REPEAT_ONE,
  ICON_SHUFFLE,
  ICON_INFO,
  ICON_PLAY,
  ICON_PAUSE,
};

// Info status bar (dipakai semua layar)
struct StatusInfo {
  UiIcon outputIcon;   // ICON_HEADPHONES / ICON_SPEAKER / ICON_BLUETOOTH
  UiIcon modeIcon;     // ICON_REPEAT / ICON_REPEAT_ONE / ICON_SHUFFLE
  bool playing;
  int batteryPct;      // 0..100, atau -1 = ditenagai USB (Fase 6 baru aktif)
};

// Data lagu untuk layar Now Playing
struct TrackView {
  const char *title;
  const char *artist;
  const char *album;
  const uint8_t *cover;     // bitmap 96x96 1-bit, atau nullptr
  uint32_t elapsedSec;
  uint32_t durationSec;
  bool playing;
};

// Satu baris pada layar daftar/menu
struct MenuRow {
  const char *label;
  const char *value;        // teks kecil rata kanan (boleh nullptr)
  UiIcon icon;
  bool hasChildren;         // tampilkan chevron ">"
};

void displayBegin();
void displaySplash();

// Gambar seluruh layar Now Playing. fullRefresh=false -> partial seluruh layar (tanpa kedip).
void displayNowPlaying(const StatusInfo &st, const TrackView &t,
                       const char *lyric, bool hasLyrics, bool fullRefresh);

// Partial refresh area progress bar saja.
void displayUpdateTransport(const TrackView &t);

// Partial refresh area lirik + progress bar.
void displayUpdateLyrics(const TrackView &t, const char *lyric, bool hasLyrics);

// Tampilkan panel volume di area lirik (partial refresh).
void displayShowVolume(const TrackView &t, int volume, UiIcon outputIcon, const char *outputName);

// Layar daftar/menu. selected = -1 untuk daftar info tanpa sorotan.
void displayMenu(const char *title, const StatusInfo &st,
                 const MenuRow *rows, int count, int selected, bool fullRefresh);
