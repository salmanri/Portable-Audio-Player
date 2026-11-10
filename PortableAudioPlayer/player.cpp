// player.cpp — Simulated Playhead & Katalog Lagu Demo (Fase 1)
// Dirancang dengan Mutex sehingga siap dihubungkan ke Audio Task I2S/Bluetooth di Fase 4-5.

#include "player.h"
#include "config.h"
#include "pins.h"
#include "lyrics.h"
#include "sdcard.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include <string.h>

// ---------------------------------------------------------------------------
// 5 Lagu Demo dengan Lirik LRC Tersinkronisasi untuk Pengujian Fase 1
// ---------------------------------------------------------------------------
static const char LRC_TRACK_0[] =
  "[00:00.50]Memutar kaset di sore hari\n"
  "[00:04.50]Suara hangat dari Walkman klasik\n"
  "[00:08.50]Layar tinta hitam putih tanpa silau\n"
  "[00:12.50]Putar knob aluminium di jemari\n"
  "[00:16.50]Menemani langkah di sepanjang jalan\n"
  "[00:21.00]\xE2\x99\xAA\n"
  "[00:26.00]Tanpa kuota, tanpa koneksi awan\n"
  "[00:30.00]Semua lagu tersimpan di saku\n"
  "[00:34.00]Irama analog dalam jiwa digital\n"
  "[00:38.50]Musik terus mengalun tanpa henti\n";

static const char LRC_TRACK_1[] =
  "[00:00.50]Look at the stars, look how they shine for you\n"
  "[00:05.00]And everything you do\n"
  "[00:09.00]Yeah, they were all yellow\n"
  "[00:13.50]I came along, I wrote a song for you\n"
  "[00:18.00]And all the things you do\n"
  "[00:22.00]And it was called Yellow\n"
  "[00:26.50]So then I took my turn\n"
  "[00:31.00]Oh, what a thing to have done\n"
  "[00:35.50]And it was all yellow\n";

static const char LRC_TRACK_2[] =
  "[00:00.50]Midnight city lights on the dashboard\n"
  "[00:04.50]Driving through the neon boulevard\n"
  "[00:08.50]Synthesizer waves in stereo\n"
  "[00:12.50]32-bit DAC flowing through the line\n"
  "[00:16.50]\xE2\x99\xAA\n"
  "[00:22.00]Rewind the tape back to 1989\n"
  "[00:26.50]Every beat locked in millisecond time\n"
  "[00:31.00]Pure monochrome paper and ink\n";

static const char LRC_TRACK_3[] =
  "[00:00.50]Jarum piringan hitam mulai menyentuh tepi\n"
  "[00:05.00]Dentuman bass akustik memenuhi ruang\n"
  "[00:09.50]Kopi hangat di sudut meja belajar\n"
  "[00:14.00]Merangkai skema elektronika dan kode\n"
  "[00:18.50]Dua inti prosesor bekerja selaras\n"
  "[00:23.00]Satu menjaga suara, satu melukis layar\n";

static const char LRC_TRACK_4[] =
  "[00:00.50] Take me back to the analog days\n"
  "[00:04.50]Click of the button, spin of the wheel\n"
  "[00:08.50]No notifications, just the music and me\n"
  "[00:13.00]Pocket-sized soundstage wherever I go\n"
  "[00:17.50]From headphones to speaker to wireless air\n"
  "[00:22.00]Walkman reborn in the modern age\n";

static const TrackMetadata DEMO_TRACKS[] = {
  { "Cassette Dreams",     "Retro Walkman",   "Side A - 1985",    45, 0, LRC_TRACK_0 },
  { "Yellow (Acoustic)",   "Coldplay",        "Parachutes",       42, 1, LRC_TRACK_1 },
  { "Neon Highway 84",     "The Midnight",    "Endless Summer",   38, 2, LRC_TRACK_2 },
  { "Senja di Laboratorium", "Sore & Kawan",  "Catatan Elektro",  32, 3, LRC_TRACK_3 },
  { "Analog Heartbeat",    "Daft Punk",       "Random Access",    30, 4, LRC_TRACK_4 },
};

constexpr int DEMO_TRACK_COUNT = sizeof(DEMO_TRACKS) / sizeof(DEMO_TRACKS[0]);

static SemaphoreHandle_t playerMutex = nullptr;
static PlayerState state = {};
static uint8_t currentCover[COVER_BYTES];
static unsigned long lastTickMillis = 0;
static bool usingSdLibrary = false;

// ---------------------------------------------------------------------------
// Generator Cover Art 96x96 1-Bit (5 Gaya Retro Berbeda untuk Lagu Demo)
// ---------------------------------------------------------------------------
static inline void setPixelBlack(uint8_t *bmp, int x, int y)
{
  if (x < 0 || x >= COVER_SIZE || y < 0 || y >= COVER_SIZE) return;
  int idx = y * COVER_SIZE + x;
  bmp[idx >> 3] |= (0x80 >> (x & 7));
}

static inline void clearPixelWhite(uint8_t *bmp, int x, int y)
{
  if (x < 0 || x >= COVER_SIZE || y < 0 || y >= COVER_SIZE) return;
  int idx = y * COVER_SIZE + x;
  bmp[idx >> 3] &= ~(0x80 >> (x & 7));
}

static void generateDemoCover(uint8_t style, uint8_t *bmp)
{
  memset(bmp, 0, COVER_BYTES); // Putih semua (bit 0 = putih, bit 1 = hitam di GxEPD2 drawBitmap)
  static const uint8_t bayer[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5}
  };

  const int cx = COVER_SIZE / 2;
  const int cy = COVER_SIZE / 2;

  for (int y = 0; y < COVER_SIZE; y++) {
    for (int x = 0; x < COVER_SIZE; x++) {
      int dx = x - cx;
      int dy = y - cy;
      float dist = sqrtf((float)(dx * dx + dy * dy));

      if (style == 0) {
        // Gaya 0: Kaset Pita Retro dengan latar dithered
        if ((x < 6 || x >= COVER_SIZE - 6 || y < 14 || y >= COVER_SIZE - 14)) {
          if (((x + y) & 3) == 0) setPixelBlack(bmp, x, y);
        } else if (y == 14 || y == COVER_SIZE - 15 || x == 6 || x == COVER_SIZE - 7) {
          setPixelBlack(bmp, x, y);
        } else {
          // Dua roda kaset
          int d1 = (x - 30) * (x - 30) + (y - 46) * (y - 46);
          int d2 = (x - 66) * (x - 66) + (y - 46) * (y - 46);
          if ((d1 >= 25 && d1 <= 100) || (d2 >= 25 && d2 <= 100)) {
            setPixelBlack(bmp, x, y);
          } else if (y >= 24 && y <= 32 && x >= 14 && x <= 82 && ((y & 1) == 0)) {
            setPixelBlack(bmp, x, y);
          } else if (y >= 66 && y <= 78 && x >= 22 && x <= 74) {
            if (y == 66 || x == 22 || x == 74) setPixelBlack(bmp, x, y);
          }
        }
      } else if (style == 1) {
        // Gaya 1: Piringan Hitam (Vinyl Record)
        if (dist <= 43.0f && dist >= 14.0f) {
          int ring = (int)dist;
          // Efek pantulan cahaya diagonal pada piringan hitam
          bool shine = (abs(dx + dy) < 8) || (abs(dx - dy) < 5);
          if (!shine || (ring % 3 == 0)) setPixelBlack(bmp, x, y);
        } else if (dist < 14.0f && dist >= 11.5f) {
          setPixelBlack(bmp, x, y);
        } else if (dist < 4.0f) {
          setPixelBlack(bmp, x, y);
        } else if (dist > 43.0f) {
          uint8_t lvl = (uint8_t)((x + y) * 8 / COVER_SIZE);
          if (lvl > bayer[y & 3][x & 3]) setPixelBlack(bmp, x, y);
        }
      } else if (style == 2) {
        // Gaya 2: Synthwave Sunset & Grid Perspektif
        if (y < 58) {
          // Matahari bergaris horizontal di tengah
          if (dist < 28.0f) {
            int gap = (y > 34) ? ((y - 30) / 4) : 0;
            if (gap == 0 || (y % (gap + 2) != 0)) setPixelBlack(bmp, x, y);
          } else {
            uint8_t sky = (uint8_t)((58 - y) * 12 / 58);
            if (sky > bayer[y & 3][x & 3]) setPixelBlack(bmp, x, y);
          }
        } else {
          // Garis horizon & grid bawah
          if (y == 58 || y == 59) setPixelBlack(bmp, x, y);
          else if ((y - 58) % 7 == 0) setPixelBlack(bmp, x, y);
          else {
            int dyGrid = y - 58;
            if (dyGrid > 0 && abs(dx * 18 / dyGrid) % 12 == 0) setPixelBlack(bmp, x, y);
          }
        }
      } else if (style == 3) {
        // Gaya 3: Spektrum Gelombang Audio / Equalizer
        int col = x / 8;
        int localX = x % 8;
        int heights[12] = {24, 42, 64, 50, 78, 84, 68, 54, 72, 46, 34, 20};
        int barH = heights[col % 12];
        int topY = COVER_SIZE - 8 - barH;
        if (localX >= 1 && localX <= 6 && y >= topY && y < COVER_SIZE - 8) {
          if ((y % 4) != 0) setPixelBlack(bmp, x, y);
        } else if (y < topY) {
          if (bayer[y & 3][x & 3] < 2) setPixelBlack(bmp, x, y);
        }
      } else {
        // Gaya 4: Gelombang Konsentris Op-Art Dithered
        float wave = (sinf(dist * 0.35f) + 1.0f) * 8.0f;
        if ((uint8_t)wave > bayer[y & 3][x & 3]) {
          setPixelBlack(bmp, x, y);
        }
      }
    }
  }
}

static void lockPlayer()   { xSemaphoreTake(playerMutex, portMAX_DELAY); }
static void unlockPlayer() { xSemaphoreGive(playerMutex); }

static void selectTrack(int index, bool autoPlay)
{
  const int total = playerGetTrackCount();
  if (total <= 0) return;
  if (index < 0) index = total - 1;
  if (index >= total) index = 0;

  if (usingSdLibrary) {
    const SdTrackEntry &sd = sdGetTrack(index);
    uint8_t tempCover[COVER_BYTES];
    bool gotCover = sdLoadCover(sd, tempCover);
    if (!gotCover) {
      // Fallback ke seni vektor Walkman retro jika lagu MP3 tidak memiliki gambar sampul
      generateDemoCover(index % 5, tempCover);
      gotCover = true;
    }
    bool gotLyrics = sdLoadLyrics(sd);

    lockPlayer();
    state.active = true;
    state.playing = autoPlay;
    state.trackIndex = index;
    strlcpy(state.title, sd.title, sizeof(state.title));
    strlcpy(state.artist, sd.artist, sizeof(state.artist));
    strlcpy(state.album, sd.album, sizeof(state.album));
    state.durationMs = max((uint32_t)1, sd.durationSec) * 1000UL;
    state.positionMs = 0;
    state.hasCover = gotCover;
    memcpy(currentCover, tempCover, COVER_BYTES);
    state.trackVersion++;
    lastTickMillis = millis();
    unlockPlayer();

    Serial.printf("[Player] SD Track [%d/%d]: %s - %s (%lus) [Cover:%s, LRC:%d baris]\n",
                  index + 1, total, sd.title, sd.artist, (unsigned long)sd.durationSec,
                  sd.coverBmp ? "JPEG" : "Retro", gotLyrics ? lyricsGetLineCount() : 0);
  } else {
    const TrackMetadata &m = DEMO_TRACKS[index];
    lockPlayer();
    state.active = true;
    state.playing = autoPlay;
    state.trackIndex = index;
    strlcpy(state.title, m.title, sizeof(state.title));
    strlcpy(state.artist, m.artist, sizeof(state.artist));
    strlcpy(state.album, m.album, sizeof(state.album));
    state.durationMs = m.durationSec * 1000UL;
    state.positionMs = 0;
    state.hasCover = true;
    state.trackVersion++;
    generateDemoCover(m.artStyle, currentCover);
    lastTickMillis = millis();
    unlockPlayer();

    lyricsLoadFromText(m.lrcContent);
    Serial.printf("[Player] Demo Track [%d/%d]: %s - %s\n",
                  index + 1, total, m.title, m.artist);
  }
}

void playerBegin()
{
  if (!playerMutex) playerMutex = xSemaphoreCreateMutex();

  // Matikan speaker amp secara default sampai mode speaker dipilih
  pinMode(AMP_SD, OUTPUT);
  digitalWrite(AMP_SD, LOW);

  // Fase 3: Inisialisasi MicroSD & pindai lagu .mp3
  sdBegin();
  usingSdLibrary = (sdGetTrackCount() > 0);

  lockPlayer();
  state.volume = VOLUME_DEFAULT;
  state.outputMode = OUT_HEADPHONE_35MM;
  state.playMode = PLAY_REPEAT_ALL;
  unlockPlayer();

  selectTrack(0, true);
}

void playerTick()
{
  bool needNext = false;

  lockPlayer();
  unsigned long now = millis();
  unsigned long dt = now - lastTickMillis;
  lastTickMillis = now;

  if (state.active && state.playing) {
    state.positionMs += (uint32_t)dt;
    if (state.durationMs > 0 && state.positionMs >= state.durationMs) {
      if (state.playMode == PLAY_REPEAT_ONE) {
        state.positionMs = 0;
      } else {
        needNext = true;
      }
    }
  }
  unlockPlayer();

  if (needNext) {
    playerNext();
  }
}

void playerPlayTrack(int index)
{
  selectTrack(index, true);
}

void playerTogglePause()
{
  lockPlayer();
  state.playing = !state.playing;
  lastTickMillis = millis();
  bool pl = state.playing;
  unlockPlayer();

  Serial.printf("[Player] %s\n", pl ? "PLAY" : "PAUSE");
}

void playerNext()
{
  const int total = playerGetTrackCount();
  lockPlayer();
  int nextIdx = state.trackIndex + 1;
  if (state.playMode == PLAY_SHUFFLE && total > 1) {
    int r = random(0, total - 1);
    if (r >= state.trackIndex) r++;
    nextIdx = r;
  } else if (nextIdx >= total) {
    nextIdx = 0;
  }
  unlockPlayer();

  selectTrack(nextIdx, true);
}

void playerPrev()
{
  const int total = playerGetTrackCount();
  lockPlayer();
  if (state.positionMs > PREV_RESTART_MS) {
    // Jika lagu sudah jalan > 3 detik, tombol Prev mengulang dari detik 0:00
    state.positionMs = 0;
    lastTickMillis = millis();
    unlockPlayer();
    Serial.println("[Player] Prev -> Ulang dari 0:00");
    return;
  }

  int prevIdx = state.trackIndex - 1;
  if (prevIdx < 0) prevIdx = total - 1;
  unlockPlayer();

  selectTrack(prevIdx, true);
}

void playerSeekRelativeMs(int32_t deltaMs)
{
  lockPlayer();
  int64_t np = (int64_t)state.positionMs + deltaMs;
  if (np < 0) np = 0;
  if (state.durationMs > 0 && np > (int64_t)state.durationMs) np = state.durationMs;
  state.positionMs = (uint32_t)np;
  lastTickMillis = millis();
  uint32_t pos = state.positionMs;
  unlockPlayer();

  Serial.printf("[Player] Seek -> %lu.%lus\n", (unsigned long)(pos / 1000),
                (unsigned long)((pos % 1000) / 100));
}

void playerChangeVolume(int delta)
{
  lockPlayer();
  state.volume = constrain(state.volume + delta, 0, 100);
  int v = state.volume;
  unlockPlayer();

  Serial.printf("[Player] Volume: %d%%\n", v);
}

int playerGetVolume()
{
  lockPlayer();
  int v = state.volume;
  unlockPlayer();
  return v;
}

void playerSetOutputMode(AudioOutputMode mode)
{
  lockPlayer();
  state.outputMode = (AudioOutputMode)(mode % OUT_MODE_COUNT);
  state.trackVersion++;
  AudioOutputMode m = state.outputMode;
  unlockPlayer();

  // Aktifkan pin Shutdown MAX98357A hanya ketika mode Speaker dipilih
  digitalWrite(AMP_SD, (m == OUT_SPEAKER) ? HIGH : LOW);
  Serial.printf("[Player] Output Mode: %s\n", playerGetOutputFullName(m));
}

void playerCycleOutputMode()
{
  AudioOutputMode cur = playerGetOutputMode();
  playerSetOutputMode((AudioOutputMode)((cur + 1) % OUT_MODE_COUNT));
}

AudioOutputMode playerGetOutputMode()
{
  lockPlayer();
  AudioOutputMode m = state.outputMode;
  unlockPlayer();
  return m;
}

const char *playerGetOutputShortName(AudioOutputMode mode)
{
  switch (mode) {
    case OUT_HEADPHONE_35MM: return "[3.5mm]";
    case OUT_SPEAKER:        return "[SPK]";
    case OUT_BLUETOOTH:      return "[BT]";
    default:                 return "[AUX]";
  }
}

const char *playerGetOutputFullName(AudioOutputMode mode)
{
  switch (mode) {
    case OUT_HEADPHONE_35MM: return "Jack 3.5mm (PCM5102A)";
    case OUT_SPEAKER:        return "Speaker Internal (MAX98357A)";
    case OUT_BLUETOOTH:      return "Bluetooth TWS (A2DP)";
    default:                 return "Unknown";
  }
}

void playerCyclePlayMode()
{
  lockPlayer();
  state.playMode = (PlayMode)((state.playMode + 1) % PLAY_MODE_COUNT);
  PlayMode pm = state.playMode;
  unlockPlayer();

  Serial.printf("[Player] Play Mode: %s\n", playerGetPlayModeName(pm));
}

PlayMode playerGetPlayMode()
{
  lockPlayer();
  PlayMode pm = state.playMode;
  unlockPlayer();
  return pm;
}

const char *playerGetPlayModeName(PlayMode mode)
{
  switch (mode) {
    case PLAY_REPEAT_ALL: return "Repeat All";
    case PLAY_REPEAT_ONE: return "Repeat One";
    case PLAY_SHUFFLE:    return "Shuffle";
    default:              return "Normal";
  }
}

int playerGetTrackCount()
{
  return usingSdLibrary ? sdGetTrackCount() : DEMO_TRACK_COUNT;
}

const TrackMetadata &playerGetTrackMeta(int index)
{
  if (usingSdLibrary) {
    static TrackMetadata sdMeta;
    const SdTrackEntry &e = sdGetTrack(index);
    sdMeta.title       = e.title;
    sdMeta.artist      = e.artist;
    sdMeta.album       = e.album;
    sdMeta.durationSec = e.durationSec;
    sdMeta.artStyle    = 0;
    sdMeta.lrcContent  = nullptr;
    return sdMeta;
  }
  if (index < 0 || index >= DEMO_TRACK_COUNT) index = 0;
  return DEMO_TRACKS[index];
}

void playerGetState(PlayerState &out, uint8_t *coverOut)
{
  lockPlayer();
  out = state;
  if (coverOut && state.hasCover) {
    memcpy(coverOut, currentCover, COVER_BYTES);
  }
  unlockPlayer();
}

