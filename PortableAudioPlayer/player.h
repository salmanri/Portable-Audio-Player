// player.h — Mesin Pemutar Audio & Manajemen Queue Lagu
// Fase 1: Simulated Playhead dengan 5 lagu demo + lirik LRC tersinkronisasi
//         & cover art 96x96 retro (sebelum modul MicroSD & I2S DAC dipasang di Fase 3-4).

#pragma once
#include <Arduino.h>
#include "display.h"

enum AudioOutputMode : uint8_t {
  OUT_HEADPHONE_35MM = 0,
  OUT_SPEAKER        = 1,
  OUT_BLUETOOTH      = 2,
  OUT_MODE_COUNT     = 3
};

enum PlayMode : uint8_t {
  PLAY_REPEAT_ALL = 0,
  PLAY_REPEAT_ONE = 1,
  PLAY_SHUFFLE    = 2,
  PLAY_MODE_COUNT = 3
};

struct TrackMetadata {
  const char *title;
  const char *artist;
  const char *album;
  uint32_t durationSec;
  uint8_t artStyle;        // 0=Cassette, 1=Vinyl, 2=SynthSun, 3=Equalizer, 4=CheckerWave
  const char *lrcContent;
};

struct PlayerState {
  bool active;
  bool playing;
  int trackIndex;
  char title[96];
  char artist[96];
  char album[96];
  uint32_t durationMs;
  uint32_t positionMs;
  int volume;              // 0..100
  AudioOutputMode outputMode;
  PlayMode playMode;
  bool hasCover;
  uint32_t trackVersion;   // naik setiap ganti lagu / ganti mode header
};

void playerBegin();
void playerTick();         // dipanggil berkala untuk update playhead & auto-next

// Kontrol Pemutaran
void playerPlayTrack(int index);
void playerTogglePause();
void playerNext();
void playerPrev();
void playerSeekRelativeMs(int32_t deltaMs);

// Kontrol Volume & Mode
void playerChangeVolume(int delta);
int  playerGetVolume();
void playerCycleOutputMode();
void playerSetOutputMode(AudioOutputMode mode);
AudioOutputMode playerGetOutputMode();
const char *playerGetOutputShortName(AudioOutputMode mode);
const char *playerGetOutputFullName(AudioOutputMode mode);

void playerCyclePlayMode();
PlayMode playerGetPlayMode();
const char *playerGetPlayModeName(PlayMode mode);

// Akses Database Lagu (Fase 1: Demo Katalog, Fase 3: Indeks MicroSD)
int  playerGetTrackCount();
const TrackMetadata &playerGetTrackMeta(int index);

// Salin state terkini & bitmap cover art (thread-safe)
void playerGetState(PlayerState &out, uint8_t *coverOut = nullptr);

