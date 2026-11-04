// ui.cpp — Pengendali Tampilan E-Paper & Navigasi Menu (Gaya MusicStation v2 / Walkman)

#include "ui.h"
#include "config.h"
#include "display.h"
#include "player.h"
#include "lyrics.h"
#include <string.h>

static UiScreen currentScreen = SCREEN_NOW_PLAYING;
static int mainMenuIdx  = 0;
static int subMenuIdx   = 0;

// Coalescing untuk putaran knob cepat: tunggu INPUT_SETTLE_MS sebelum partial refresh
static bool menuDirty       = false;
static bool menuFullRefresh = false;
static unsigned long lastInputMillis = 0;

// Volume overlay timer pada layar Now Playing
static bool volumeOverlayActive = false;
static bool volumeOverlayDirty  = false;
static unsigned long volumeOverlayUntilMs = 0;

// Pelacak state Now Playing
static uint32_t seenTrackVersion = 0;
static uint32_t lastSec          = UINT32_MAX;
static bool lastPlaying          = false;
static char lastLyric[LYRIC_TEXT_LEN] = "";
static uint8_t coverBuf[COVER_BYTES];

constexpr int MAIN_MENU_COUNT = 7;

static void requestMenuRedraw(bool full)
{
  menuDirty = true;
  if (full) menuFullRefresh = true;
  lastInputMillis = millis();
}

static void renderCurrentMenu(bool fullRefresh)
{
  PlayerState ps;
  playerGetState(ps, nullptr);
  const char *outShort = playerGetOutputShortName(ps.outputMode);

  if (currentScreen == SCREEN_MAIN_MENU) {
    static const char *const ITEMS[MAIN_MENU_COUNT] = {
      "Now Playing",
      "All Tracks",
      "Artists",
      "Albums",
      "Audio Output",
      "Play Mode",
      "About & Controls"
    };
    static char r0[24], r1[24], r2[24], r3[24], r4[24], r5[24], r6[24];
    const int nTracks = playerGetTrackCount();

    snprintf(r0, sizeof(r0), "%s", ps.playing ? "Playing >" : "Paused ||");
    snprintf(r1, sizeof(r1), "%d tracks", nTracks);
    snprintf(r2, sizeof(r2), "%d artists", nTracks);
    snprintf(r3, sizeof(r3), "%d albums", nTracks);
    snprintf(r4, sizeof(r4), "%s", outShort);
    snprintf(r5, sizeof(r5), "%s", playerGetPlayModeName(ps.playMode));
    snprintf(r6, sizeof(r6), "Info");

    const char *const RIGHTS[MAIN_MENU_COUNT] = { r0, r1, r2, r3, r4, r5, r6 };
    displayDrawMenu("WALKMAN MENU", outShort, ITEMS, RIGHTS,
                    MAIN_MENU_COUNT, mainMenuIdx, fullRefresh);
  } else if (currentScreen == SCREEN_TRACK_LIST ||
             currentScreen == SCREEN_ARTIST_LIST ||
             currentScreen == SCREEN_ALBUM_LIST) {
    const int n = playerGetTrackCount();
    static const char *items[16];
    static char rightBufs[16][24];
    static const char *rights[16];

    const char *header = "ALL TRACKS";
    if (currentScreen == SCREEN_ARTIST_LIST) header = "ARTISTS";
    else if (currentScreen == SCREEN_ALBUM_LIST) header = "ALBUMS";

    for (int i = 0; i < n && i < 16; i++) {
      const TrackMetadata &m = playerGetTrackMeta(i);
      if (currentScreen == SCREEN_ARTIST_LIST) {
        items[i] = m.artist;
        snprintf(rightBufs[i], sizeof(rightBufs[i]), "%s", m.title);
      } else if (currentScreen == SCREEN_ALBUM_LIST) {
        items[i] = m.album;
        snprintf(rightBufs[i], sizeof(rightBufs[i]), "%s", m.artist);
      } else {
        items[i] = m.title;
        snprintf(rightBufs[i], sizeof(rightBufs[i]), "%s%lu:%02lu",
                 (i == ps.trackIndex) ? "> " : "",
                 (unsigned long)(m.durationSec / 60),
                 (unsigned long)(m.durationSec % 60));
      }
      rights[i] = rightBufs[i];
    }

    displayDrawMenu(header, outShort, items, rights, n, subMenuIdx, fullRefresh);
  } else if (currentScreen == SCREEN_ABOUT) {
    displayDrawAbout(playerGetOutputFullName(ps.outputMode),
                     playerGetPlayModeName(ps.playMode),
                     ps.volume);
  }
}

static void switchToNowPlaying()
{
  currentScreen = SCREEN_NOW_PLAYING;
  volumeOverlayActive = false;
  volumeOverlayDirty  = false;
  seenTrackVersion    = 0; // Paksa Full Refresh layar Now Playing
}

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

  // Tombol fisik Prev & Next selalu berfungsi di layar mana saja
  if (evt == EVT_BTN_NEXT) {
    playerNext();
    if (currentScreen != SCREEN_NOW_PLAYING) {
      requestMenuRedraw(false);
    }
    return;
  }
  if (evt == EVT_BTN_PREV) {
    playerPrev();
    if (currentScreen != SCREEN_NOW_PLAYING) {
      requestMenuRedraw(false);
    }
    return;
  }

  // Perilaku berdasarkan layar yang sedang aktif
  if (currentScreen == SCREEN_NOW_PLAYING) {
    if (evt == EVT_ENC_CW) {
      playerChangeVolume(+VOLUME_STEP);
      volumeOverlayActive  = true;
      volumeOverlayDirty   = true;
      volumeOverlayUntilMs = millis() + VOLUME_OVERLAY_MS;
      lastInputMillis      = millis();
    } else if (evt == EVT_ENC_CCW) {
      playerChangeVolume(-VOLUME_STEP);
      volumeOverlayActive  = true;
      volumeOverlayDirty   = true;
      volumeOverlayUntilMs = millis() + VOLUME_OVERLAY_MS;
      lastInputMillis      = millis();
    } else if (evt == EVT_ENC_CLICK) {
      playerTogglePause();
    } else if (evt == EVT_BTN_MENU) {
      currentScreen = SCREEN_MAIN_MENU;
      requestMenuRedraw(true);
      Serial.println("[UI] Buka WALKMAN MENU");
    }
  } else if (currentScreen == SCREEN_MAIN_MENU) {
    if (evt == EVT_ENC_CW) {
      mainMenuIdx = (mainMenuIdx + 1) % MAIN_MENU_COUNT;
      requestMenuRedraw(false);
    } else if (evt == EVT_ENC_CCW) {
      mainMenuIdx = (mainMenuIdx - 1 + MAIN_MENU_COUNT) % MAIN_MENU_COUNT;
      requestMenuRedraw(false);
    } else if (evt == EVT_BTN_MENU) {
      switchToNowPlaying();
      Serial.println("[UI] Kembali ke Now Playing");
    } else if (evt == EVT_ENC_CLICK) {
      switch (mainMenuIdx) {
        case 0: // Now Playing
          switchToNowPlaying();
          break;
        case 1: // All Tracks
          currentScreen = SCREEN_TRACK_LIST;
          subMenuIdx = 0;
          requestMenuRedraw(true);
          break;
        case 2: // Artists
          currentScreen = SCREEN_ARTIST_LIST;
          subMenuIdx = 0;
          requestMenuRedraw(true);
          break;
        case 3: // Albums
          currentScreen = SCREEN_ALBUM_LIST;
          subMenuIdx = 0;
          requestMenuRedraw(true);
          break;
        case 4: // Audio Output (3.5mm / Speaker / Bluetooth)
          playerCycleOutputMode();
          requestMenuRedraw(false);
          break;
        case 5: // Play Mode (Repeat All / Repeat One / Shuffle)
          playerCyclePlayMode();
          requestMenuRedraw(false);
          break;
        case 6: // About & Controls
          currentScreen = SCREEN_ABOUT;
          requestMenuRedraw(true);
          break;
      }
    }
  } else if (currentScreen == SCREEN_TRACK_LIST ||
             currentScreen == SCREEN_ARTIST_LIST ||
             currentScreen == SCREEN_ALBUM_LIST) {
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
      currentScreen = SCREEN_MAIN_MENU;
      requestMenuRedraw(true);
    }
  } else if (currentScreen == SCREEN_ABOUT) {
    if (evt == EVT_ENC_CLICK || evt == EVT_BTN_MENU) {
      currentScreen = SCREEN_MAIN_MENU;
      requestMenuRedraw(true);
    }
  }
}

void uiTick()
{
  unsigned long now = millis();

  // 1. Jika sedang di layar Menu / Library
  if (currentScreen != SCREEN_NOW_PLAYING) {
    if (menuDirty && (now - lastInputMillis >= INPUT_SETTLE_MS)) {
      bool full = menuFullRefresh;
      menuDirty = false;
      menuFullRefresh = false;
      renderCurrentMenu(full);
    }
    return;
  }

  // 2. Layar Now Playing
  PlayerState s;
  bool needCover = (seenTrackVersion == 0);
  playerGetState(s, needCover ? coverBuf : nullptr);
  if (s.trackVersion != seenTrackVersion) {
    playerGetState(s, coverBuf);
  }

  uint32_t posMs  = s.positionMs;
  uint32_t sec    = posMs / 1000;
  uint32_t durSec = s.durationMs / 1000;

  char lyric[LYRIC_TEXT_LEN];
  lyricsGetCurrent(posMs, LYRIC_LEAD_MS, lyric, sizeof(lyric));

  // Badge kanan header: gabungkan nama Album + Output Mode (contoh: "Parachutes [3.5mm]")
  char badgeRight[64];
  snprintf(badgeRight, sizeof(badgeRight), "%s %s",
           s.album, playerGetOutputShortName(s.outputMode));

  // Apakah perlu Full Refresh layar lagu baru?
  if (s.trackVersion != seenTrackVersion) {
    seenTrackVersion = s.trackVersion;
    volumeOverlayActive = false;
    volumeOverlayDirty  = false;

    displayDrawTrack(s.title, s.artist, badgeRight,
                     s.hasCover ? coverBuf : nullptr,
                     sec, durSec, s.playing, lyric);

    // Hitung ulang detik terkini seusai full refresh (~1.8s)
    playerTick();
    playerGetState(s, nullptr);
    lastSec     = s.positionMs / 1000;
    lastPlaying = s.playing;
    strlcpy(lastLyric, lyric, sizeof(lastLyric));
    return;
  }

  // Apakah pop-up Volume sedang aktif?
  if (volumeOverlayActive) {
    if (volumeOverlayDirty && (now - lastInputMillis >= INPUT_SETTLE_MS)) {
      volumeOverlayDirty = false;
      displayUpdateVolumePopup(s.volume, playerGetOutputFullName(s.outputMode),
                               sec, durSec, s.playing);
      lastSec     = sec;
      lastPlaying = s.playing;
    } else if ((long)(now - volumeOverlayUntilMs) >= 0) {
      // Waktu pop-up volume habis -> kembalikan tampilan lirik aktif
      volumeOverlayActive = false;
      displayUpdateLyricsAndTransport(lyric, sec, durSec, s.playing);
      strlcpy(lastLyric, lyric, sizeof(lastLyric));
      lastSec     = sec;
      lastPlaying = s.playing;
    }
    return;
  }

  // Update reguler Lirik & Progress Bar
  bool lyricChanged = (strcmp(lyric, lastLyric) != 0);
  if (lyricChanged) {
    displayUpdateLyricsAndTransport(lyric, sec, durSec, s.playing);
    strlcpy(lastLyric, lyric, sizeof(lastLyric));
    lastSec     = sec;
    lastPlaying = s.playing;
  } else if (sec != lastSec || s.playing != lastPlaying) {
    // Tunda maksimal 220ms jika baris lirik baru akan masuk tepat di awal detik ini
    uint32_t nextMs = lyricsGetNextChangeMs(posMs, LYRIC_LEAD_MS);
    bool lyricRightNow = (nextMs != UINT32_MAX && nextMs > posMs && (nextMs - posMs) <= 220);

    if (!lyricRightNow || s.playing != lastPlaying) {
      displayUpdateTransport(sec, durSec, s.playing);
      lastSec     = sec;
      lastPlaying = s.playing;
    }
  }
}

