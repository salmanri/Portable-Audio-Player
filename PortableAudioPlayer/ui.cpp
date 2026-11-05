// ui.cpp — Pengendali layar & navigasi (Now Playing, Menu, Songs, Artists, Albums, About)

#include "ui.h"
#include "config.h"
#include "display.h"
#include "player.h"
#include "lyrics.h"
#include <string.h>

static UiScreen currentScreen = SCREEN_NOW_PLAYING;
static int mainMenuIdx = 0;
static int subMenuIdx  = 0;

// Coalescing putaran knob cepat: gambar sekali setelah input tenang
static bool menuDirty = false;
static bool menuFull  = false;
static unsigned long lastInputMillis = 0;

// Panel volume di layar Now Playing
static bool volActive = false;
static bool volDirty  = false;
static unsigned long volUntilMs = 0;

// Pelacak state Now Playing
static uint32_t seenTrackVersion = 0;
static bool npRedraw = false;          // gambar ulang seluruh layar (partial) saat kembali dari menu
static uint32_t lastSec = UINT32_MAX;
static bool lastPlaying = false;
static char lastLyric[LYRIC_TEXT_LEN] = "";
static uint8_t coverBuf[COVER_BYTES];

constexpr int MAIN_MENU_COUNT = 7;

// ---------------------------------------------------------------------------
// Helper pemetaan state -> tampilan
// ---------------------------------------------------------------------------
static UiIcon outputIcon(AudioOutputMode m)
{
  switch (m) {
    case OUT_SPEAKER:   return ICON_SPEAKER;
    case OUT_BLUETOOTH: return ICON_BLUETOOTH;
    default:            return ICON_HEADPHONES;
  }
}

static const char *outputLabel(AudioOutputMode m)
{
  switch (m) {
    case OUT_SPEAKER:   return "Speaker";
    case OUT_BLUETOOTH: return "Bluetooth";
    default:            return "Headphones";
  }
}

static UiIcon modeIcon(PlayMode m)
{
  switch (m) {
    case PLAY_REPEAT_ONE: return ICON_REPEAT_ONE;
    case PLAY_SHUFFLE:    return ICON_SHUFFLE;
    default:              return ICON_REPEAT;
  }
}

static void buildStatus(const PlayerState &s, StatusInfo &st)
{
  st.outputIcon = outputIcon(s.outputMode);
  st.modeIcon   = modeIcon(s.playMode);
  st.playing    = s.playing;
  st.batteryPct = -1;   // Fase 6: diganti pembacaan ADC baterai
}

static void buildTrack(const PlayerState &s, TrackView &t, const uint8_t *cover)
{
  t.title       = s.title;
  t.artist      = s.artist;
  t.album       = s.album;
  t.cover       = cover;
  t.elapsedSec  = s.positionMs / 1000;
  t.durationSec = s.durationMs / 1000;
  t.playing     = s.playing;
}

static int countUnique(bool artist)
{
  const int n = playerGetTrackCount();
  int u = 0;
  for (int i = 0; i < n; i++) {
    const char *a = artist ? playerGetTrackMeta(i).artist : playerGetTrackMeta(i).album;
    bool seen = false;
    for (int j = 0; j < i && !seen; j++) {
      const char *b = artist ? playerGetTrackMeta(j).artist : playerGetTrackMeta(j).album;
      seen = (strcmp(a, b) == 0);
    }
    if (!seen) u++;
  }
  return u;
}

static void requestMenuRedraw(bool full)
{
  menuDirty = true;
  if (full) menuFull = true;
  lastInputMillis = millis();
}

static void openScreen(UiScreen scr)
{
  currentScreen = scr;
  requestMenuRedraw(true);
}

static void switchToNowPlaying()
{
  currentScreen = SCREEN_NOW_PLAYING;
  volActive = false;
  volDirty  = false;
  npRedraw  = true;
}

// ---------------------------------------------------------------------------
// Render layar daftar
// ---------------------------------------------------------------------------
static void renderMenu(bool full)
{
  PlayerState s;
  playerGetState(s, nullptr);
  StatusInfo st;
  buildStatus(s, st);

  static MenuRow rows[32];
  static char values[32][24];
  const int n = playerGetTrackCount();

  if (currentScreen == SCREEN_MAIN_MENU) {
    snprintf(values[0], sizeof(values[0]), "%s", s.playing ? "Playing" : "Paused");
    snprintf(values[1], sizeof(values[1]), "%d", n);
    snprintf(values[2], sizeof(values[2]), "%d", countUnique(true));
    snprintf(values[3], sizeof(values[3]), "%d", countUnique(false));

    rows[0] = { "Now Playing", values[0], s.playing ? ICON_PLAY : ICON_PAUSE, false };
    rows[1] = { "Songs",       values[1], ICON_LIST,   true };
    rows[2] = { "Artists",     values[2], ICON_ARTIST, true };
    rows[3] = { "Albums",      values[3], ICON_ALBUM,  true };
    rows[4] = { "Output",      outputLabel(s.outputMode), outputIcon(s.outputMode), false };
    rows[5] = { "Play Mode",   playerGetPlayModeName(s.playMode), modeIcon(s.playMode), false };
    rows[6] = { "About",       nullptr, ICON_INFO, true };
    displayMenu("Menu", st, rows, MAIN_MENU_COUNT, mainMenuIdx, full);

  } else if (currentScreen == SCREEN_TRACK_LIST) {
    for (int i = 0; i < n && i < 32; i++) {
      const TrackMetadata &m = playerGetTrackMeta(i);
      snprintf(values[i], sizeof(values[i]), "%lu:%02lu",
               (unsigned long)(m.durationSec / 60), (unsigned long)(m.durationSec % 60));
      rows[i] = { m.title, values[i], (i == s.trackIndex) ? ICON_SPEAKER : ICON_NOTE, false };
    }
    displayMenu("Songs", st, rows, n, subMenuIdx, full);

  } else if (currentScreen == SCREEN_ARTIST_LIST) {
    for (int i = 0; i < n && i < 32; i++) {
      const TrackMetadata &m = playerGetTrackMeta(i);
      rows[i] = { m.artist, m.title, ICON_ARTIST, false };
    }
    displayMenu("Artists", st, rows, n, subMenuIdx, full);

  } else if (currentScreen == SCREEN_ALBUM_LIST) {
    for (int i = 0; i < n && i < 32; i++) {
      const TrackMetadata &m = playerGetTrackMeta(i);
      rows[i] = { m.album, m.artist, ICON_ALBUM, false };
    }
    displayMenu("Albums", st, rows, n, subMenuIdx, full);

  } else if (currentScreen == SCREEN_ABOUT) {
    snprintf(values[0], sizeof(values[0]), "%d%%", s.volume);
    snprintf(values[1], sizeof(values[1]), "%u KB", (unsigned)(ESP.getFreeHeap() / 1024));
    rows[0] = { "Model",     DEVICE_MODEL, ICON_NONE, false };
    rows[1] = { "Firmware",  FW_VERSION,   ICON_NONE, false };
    rows[2] = { "Output",    outputLabel(s.outputMode), ICON_NONE, false };
    rows[3] = { "Play Mode", playerGetPlayModeName(s.playMode), ICON_NONE, false };
    rows[4] = { "Volume",    values[0], ICON_NONE, false };
    rows[5] = { "Free RAM",  values[1], ICON_NONE, false };
    displayMenu("About", st, rows, 6, -1, full);
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void uiBegin()
{
  currentScreen = SCREEN_NOW_PLAYING;
  seenTrackVersion = 0;
}

UiScreen uiGetScreen()
{
  return currentScreen;
}

void uiHandleInput(InputEvent evt)
{
  if (evt == EVT_NONE) return;

  // Prev / Next berfungsi di semua layar
  if (evt == EVT_BTN_NEXT || evt == EVT_BTN_PREV) {
    if (evt == EVT_BTN_NEXT) playerNext();
    else playerPrev();
    if (currentScreen != SCREEN_NOW_PLAYING) requestMenuRedraw(false);
    return;
  }

  switch (currentScreen) {
    case SCREEN_NOW_PLAYING:
      if (evt == EVT_ENC_CW || evt == EVT_ENC_CCW) {
        playerChangeVolume(evt == EVT_ENC_CW ? +VOLUME_STEP : -VOLUME_STEP);
        volActive  = true;
        volDirty   = true;
        volUntilMs = millis() + VOLUME_OVERLAY_MS;
        lastInputMillis = millis();
      } else if (evt == EVT_ENC_CLICK) {
        playerTogglePause();
      } else if (evt == EVT_BTN_MENU) {
        openScreen(SCREEN_MAIN_MENU);
      }
      break;

    case SCREEN_MAIN_MENU:
      if (evt == EVT_ENC_CW) {
        mainMenuIdx = (mainMenuIdx + 1) % MAIN_MENU_COUNT;
        requestMenuRedraw(false);
      } else if (evt == EVT_ENC_CCW) {
        mainMenuIdx = (mainMenuIdx - 1 + MAIN_MENU_COUNT) % MAIN_MENU_COUNT;
        requestMenuRedraw(false);
      } else if (evt == EVT_BTN_MENU) {
        switchToNowPlaying();
      } else if (evt == EVT_ENC_CLICK) {
        switch (mainMenuIdx) {
          case 0: switchToNowPlaying(); break;
          case 1: subMenuIdx = 0; openScreen(SCREEN_TRACK_LIST); break;
          case 2: subMenuIdx = 0; openScreen(SCREEN_ARTIST_LIST); break;
          case 3: subMenuIdx = 0; openScreen(SCREEN_ALBUM_LIST); break;
          case 4: playerCycleOutputMode(); requestMenuRedraw(false); break;
          case 5: playerCyclePlayMode();   requestMenuRedraw(false); break;
          case 6: openScreen(SCREEN_ABOUT); break;
        }
      }
      break;

    case SCREEN_TRACK_LIST:
    case SCREEN_ARTIST_LIST:
    case SCREEN_ALBUM_LIST: {
      const int n = playerGetTrackCount();
      if (evt == EVT_ENC_CW) {
        subMenuIdx = (subMenuIdx + 1) % n;
        requestMenuRedraw(false);
      } else if (evt == EVT_ENC_CCW) {
        subMenuIdx = (subMenuIdx - 1 + n) % n;
        requestMenuRedraw(false);
      } else if (evt == EVT_ENC_CLICK) {
        playerPlayTrack(subMenuIdx);
        switchToNowPlaying();
      } else if (evt == EVT_BTN_MENU) {
        openScreen(SCREEN_MAIN_MENU);
      }
      break;
    }

    case SCREEN_ABOUT:
      if (evt == EVT_ENC_CLICK || evt == EVT_BTN_MENU) openScreen(SCREEN_MAIN_MENU);
      break;
  }
}

void uiTick()
{
  const unsigned long now = millis();

  // 1. Layar daftar / menu
  if (currentScreen != SCREEN_NOW_PLAYING) {
    if (menuDirty && (now - lastInputMillis >= INPUT_SETTLE_MS)) {
      bool full = menuFull;
      menuDirty = false;
      menuFull  = false;
      renderMenu(full);
    }
    return;
  }

  // 2. Layar Now Playing
  PlayerState s;
  playerGetState(s, coverBuf);
  TrackView tv;
  buildTrack(s, tv, s.hasCover ? coverBuf : nullptr);
  StatusInfo st;
  buildStatus(s, st);

  const uint32_t posMs = s.positionMs;
  const uint32_t sec = posMs / 1000;
  const bool hasLyrics = lyricsGetLineCount() > 0;
  char lyric[LYRIC_TEXT_LEN];
  lyricsGetCurrent(posMs, LYRIC_LEAD_MS, lyric, sizeof(lyric));

  // a. Lagu baru (full refresh) atau kembali dari menu (partial seluruh layar)
  if (s.trackVersion != seenTrackVersion || npRedraw) {
    const bool full = (s.trackVersion != seenTrackVersion);
    seenTrackVersion = s.trackVersion;
    npRedraw  = false;
    volActive = false;
    volDirty  = false;

    displayNowPlaying(st, tv, lyric, hasLyrics, full);

    // Sinkronkan ulang detik setelah refresh (~0.3-1.8 detik)
    playerTick();
    playerGetState(s, nullptr);
    lastSec     = s.positionMs / 1000;
    lastPlaying = s.playing;
    strlcpy(lastLyric, lyric, sizeof(lastLyric));
    return;
  }

  // b. Play/Pause berubah -> cukup partial refresh area transport di bawah bar
  if (s.playing != lastPlaying) {
    displayUpdateTransport(tv);
    lastSec     = sec;
    lastPlaying = s.playing;
    return;
  }

  // c. Panel volume
  if (volActive) {
    if (volDirty && (now - lastInputMillis >= INPUT_SETTLE_MS)) {
      volDirty = false;
      displayShowVolume(tv, s.volume, st.outputIcon, outputLabel(s.outputMode));
      lastSec = sec;
    } else if (!volDirty && (long)(now - volUntilMs) >= 0) {
      volActive = false;
      displayUpdateLyrics(tv, lyric, hasLyrics);
      strlcpy(lastLyric, lyric, sizeof(lastLyric));
      lastSec = sec;
    }
    return;
  }

  // d. Update reguler lirik & progress bar
  if (strcmp(lyric, lastLyric) != 0) {
    displayUpdateLyrics(tv, lyric, hasLyrics);
    strlcpy(lastLyric, lyric, sizeof(lastLyric));
    lastSec = sec;
  } else if (sec != lastSec) {
    // Tunda maks. 220 ms jika baris lirik baru akan masuk, agar digabung dalam 1 refresh
    uint32_t nextMs = lyricsGetNextChangeMs(posMs, LYRIC_LEAD_MS);
    bool lyricRightNow = (nextMs != UINT32_MAX && nextMs > posMs && (nextMs - posMs) <= 220);
    if (!lyricRightNow) {
      displayUpdateTransport(tv);
      lastSec = sec;
    }
  }
}
