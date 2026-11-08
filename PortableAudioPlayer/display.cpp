// display.cpp — Desain UI E-Paper bergaya perangkat pabrikan / Walkman retro
//
// Prinsip desain:
//   - Margin & grid konsisten, garis rambut 1px, tanpa kotak/bingkai berlebihan
//   - Ikon vektor 12x12 digambar dengan primitif (tajam di layar 1-bit)
//   - Tipografi Helvetica: Bold untuk judul, Regular untuk info sekunder
//   - Selection bar membulat + scrollbar tipis pada daftar

#include "display.h"
#include "pins.h"
#include "config.h"

#include <SPI.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

// WeAct 2.9" B/W = panel DEPG0290BS, controller SSD1680.
GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> epd(
    GxEPD2_290_BS(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------
static const uint8_t *const F_STATUS   = u8g2_font_helvB08_tf;
static const uint8_t *const F_SMALL    = u8g2_font_helvR08_tf;
static const uint8_t *const F_TITLE    = u8g2_font_helvB12_tf;
static const uint8_t *const F_LYRIC_L  = u8g2_font_helvB12_tf;
static const uint8_t *const F_LYRIC_S  = u8g2_font_helvB10_tf;
static const uint8_t *const F_ROW      = u8g2_font_helvR10_tf;
static const uint8_t *const F_ROW_SEL  = u8g2_font_helvB10_tf;
static const uint8_t *const F_BIG      = u8g2_font_helvB18_tf;
static const uint8_t *const F_HERO     = u8g2_font_helvB24_tf;

// ---------------------------------------------------------------------------
// Grid layout
// ---------------------------------------------------------------------------
constexpr int STATUS_LINE_Y = 14;   // garis rambut di bawah status bar

constexpr int COVER_X  = 4;
constexpr int COVER_Y  = 23;

constexpr int COL_X = 110;          // kolom kanan
constexpr int COL_W = 182;          // 110 + 182 = 292 (margin kanan 4px)

constexpr int TITLE_BASE = 34;
constexpr int SUB_BASE   = 47;
constexpr int SEP_Y      = 53;

constexpr int LYR_Y = 55;
constexpr int LYR_H = 46;           // 55..100

constexpr int TR_Y  = 102;
constexpr int TR_H  = 26;           // 102..127

constexpr int LIST_Y = 15;
constexpr int ROW_H  = 18;
constexpr int ROWS   = 6;

static uint16_t partialCount = 0;

// ---------------------------------------------------------------------------
// Helper teks
// ---------------------------------------------------------------------------
static void textColor(uint16_t fg, uint16_t bg)
{
  u8g2.setForegroundColor(fg);
  u8g2.setBackgroundColor(bg);
}

static int textWidth(const char *t, const uint8_t *font)
{
  if (!t || !t[0]) return 0;
  u8g2.setFont(font);
  return u8g2.getUTF8Width(t);
}

// Potong teks dengan "..." agar muat dalam maxW piksel
static void fitText(const char *in, char *out, size_t outLen, int maxW, const uint8_t *font)
{
  u8g2.setFont(font);
  strlcpy(out, in ? in : "", outLen);
  if (u8g2.getUTF8Width(out) <= maxW) return;
  int len = strlen(out);
  while (len > 0) {
    len--;
    // jangan memotong di tengah karakter UTF-8 multi-byte
    while (len > 0 && ((uint8_t)out[len] & 0xC0) == 0x80) len--;
    out[len] = '\0';
    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s...", out);
    if (u8g2.getUTF8Width(tmp) <= maxW) {
      strlcpy(out, tmp, outLen);
      return;
    }
  }
  strlcpy(out, "...", outLen);
}

static void drawLeft(int x, int base, const char *t, const uint8_t *font, int maxW = 0)
{
  if (!t || !t[0]) return;
  char buf[160];
  if (maxW > 0) fitText(t, buf, sizeof(buf), maxW, font);
  else strlcpy(buf, t, sizeof(buf));
  u8g2.setFont(font);
  u8g2.setCursor(x, base);
  u8g2.print(buf);
}

static void drawRight(int rightX, int base, const char *t, const uint8_t *font, int maxW = 0)
{
  if (!t || !t[0]) return;
  char buf[160];
  if (maxW > 0) fitText(t, buf, sizeof(buf), maxW, font);
  else strlcpy(buf, t, sizeof(buf));
  u8g2.setFont(font);
  u8g2.setCursor(rightX - u8g2.getUTF8Width(buf), base);
  u8g2.print(buf);
}

static void drawCenter(int cx, int base, const char *t, const uint8_t *font, int maxW = 0)
{
  if (!t || !t[0]) return;
  char buf[160];
  if (maxW > 0) fitText(t, buf, sizeof(buf), maxW, font);
  else strlcpy(buf, t, sizeof(buf));
  u8g2.setFont(font);
  u8g2.setCursor(cx - u8g2.getUTF8Width(buf) / 2, base);
  u8g2.print(buf);
}

// Teks ASCII dengan jarak antar huruf (letter-spacing) — kesan label hardware
static void drawSpacedCenter(int cx, int base, const char *t, const uint8_t *font, int spacing)
{
  u8g2.setFont(font);
  char ch[2] = {0, 0};
  int total = 0;
  for (const char *p = t; *p; p++) {
    ch[0] = *p;
    total += u8g2.getUTF8Width(ch) + spacing;
  }
  total -= spacing;
  int x = cx - total / 2;
  for (const char *p = t; *p; p++) {
    ch[0] = *p;
    u8g2.setCursor(x, base);
    u8g2.print(ch);
    x += u8g2.getUTF8Width(ch) + spacing;
  }
}

static void formatTime(uint32_t s, char *out, size_t n)
{
  snprintf(out, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

static void dottedHLine(int x, int y, int w, uint16_t c)
{
  for (int i = 0; i < w; i += 3) epd.drawPixel(x + i, y, c);
}

// ---------------------------------------------------------------------------
// Ikon vektor 12x12 (x,y = pojok kiri atas)
// ---------------------------------------------------------------------------
static void drawIcon(UiIcon icon, int x, int y, uint16_t c)
{
  switch (icon) {
    case ICON_NOTE:
      epd.fillCircle(x + 3, y + 9, 2, c);
      epd.fillCircle(x + 9, y + 8, 2, c);
      epd.drawFastVLine(x + 5, y + 1, 9, c);
      epd.drawFastVLine(x + 11, y + 0, 9, c);
      epd.fillRect(x + 5, y + 0, 7, 2, c);
      break;

    case ICON_LIST:
      for (int i = 0; i < 3; i++) {
        epd.fillRect(x + 0, y + 2 + i * 4, 2, 2, c);
        epd.fillRect(x + 4, y + 2 + i * 4, 8, 2, c);
      }
      break;

    case ICON_ARTIST:
      epd.fillCircle(x + 6, y + 3, 3, c);
      epd.fillRoundRect(x + 1, y + 8, 11, 4, 2, c);
      break;

    case ICON_ALBUM:
      epd.drawCircle(x + 6, y + 6, 5, c);
      epd.drawCircle(x + 6, y + 6, 4, c);
      epd.fillCircle(x + 6, y + 6, 1, c);
      break;

    case ICON_HEADPHONES:
      epd.drawCircleHelper(x + 6, y + 7, 5, 0x1 | 0x2, c);
      epd.drawCircleHelper(x + 6, y + 7, 4, 0x1 | 0x2, c);
      epd.fillRoundRect(x + 0, y + 6, 3, 6, 1, c);
      epd.fillRoundRect(x + 9, y + 6, 3, 6, 1, c);
      break;

    case ICON_SPEAKER:
      epd.fillRect(x + 0, y + 4, 3, 4, c);
      epd.fillTriangle(x + 2, y + 6, x + 6, y + 1, x + 6, y + 11, c);
      epd.drawCircleHelper(x + 6, y + 6, 3, 0x2 | 0x4, c);
      epd.drawCircleHelper(x + 6, y + 6, 5, 0x2 | 0x4, c);
      break;

    case ICON_BLUETOOTH:
      epd.drawFastVLine(x + 6, y + 0, 12, c);
      epd.drawLine(x + 6, y + 0, x + 9, y + 3, c);
      epd.drawLine(x + 9, y + 3, x + 3, y + 8, c);
      epd.drawLine(x + 6, y + 11, x + 9, y + 8, c);
      epd.drawLine(x + 9, y + 8, x + 3, y + 3, c);
      break;

    case ICON_REPEAT:
    case ICON_REPEAT_ONE:
      epd.drawRoundRect(x + 0, y + 3, 11, 7, 3, c);
      epd.fillTriangle(x + 6, y + 1, x + 9, y + 3, x + 6, y + 5, c);
      epd.fillTriangle(x + 5, y + 7, x + 2, y + 9, x + 5, y + 11, c);
      if (icon == ICON_REPEAT_ONE) epd.drawFastVLine(x + 5, y + 5, 3, c);
      break;

    case ICON_SHUFFLE:
      epd.drawLine(x + 0, y + 2, x + 3, y + 2, c);
      epd.drawLine(x + 3, y + 2, x + 7, y + 9, c);
      epd.drawLine(x + 7, y + 9, x + 9, y + 9, c);
      epd.drawLine(x + 0, y + 9, x + 3, y + 9, c);
      epd.drawLine(x + 3, y + 9, x + 7, y + 2, c);
      epd.drawLine(x + 7, y + 2, x + 9, y + 2, c);
      epd.fillTriangle(x + 9, y + 0, x + 11, y + 2, x + 9, y + 4, c);
      epd.fillTriangle(x + 9, y + 7, x + 11, y + 9, x + 9, y + 11, c);
      break;

    case ICON_INFO:
      epd.drawCircle(x + 6, y + 6, 5, c);
      epd.fillRect(x + 5, y + 5, 2, 4, c);
      epd.fillRect(x + 5, y + 3, 2, 1, c);
      break;

    case ICON_PLAY:
      epd.fillTriangle(x + 3, y + 1, x + 3, y + 11, x + 10, y + 6, c);
      break;

    case ICON_PAUSE:
      epd.fillRect(x + 2, y + 1, 3, 10, c);
      epd.fillRect(x + 7, y + 1, 3, 10, c);
      break;

    default:
      break;
  }
}

// Baterai 17x8. pct < 0 -> ikon petir (ditenagai USB)
static void drawBattery(int x, int y, int pct, uint16_t c)
{
  epd.drawRect(x, y, 15, 9, c);
  epd.fillRect(x + 15, y + 3, 2, 3, c);
  if (pct >= 0) {
    int w = (11 * constrain(pct, 0, 100) + 50) / 100;
    if (w > 0) epd.fillRect(x + 2, y + 2, w, 5, c);
  } else {
    epd.drawLine(x + 8, y + 1, x + 5, y + 4, c);
    epd.drawFastHLine(x + 5, y + 4, 5, c);
    epd.drawLine(x + 9, y + 4, x + 6, y + 7, c);
  }
}

static void drawChevron(int x, int y, uint16_t c)
{
  // ">" tebal 2px, tinggi 7px
  for (int k = 0; k < 2; k++) {
    epd.drawLine(x + k, y, x + 3 + k, y + 3, c);
    epd.drawLine(x + 3 + k, y + 3, x + k, y + 6, c);
  }
}

// ---------------------------------------------------------------------------
// Status bar (semua layar)
// ---------------------------------------------------------------------------
static void drawStatusBar(const char *title, const StatusInfo &st)
{
  epd.fillRect(0, 0, SCREEN_W, STATUS_LINE_Y + 1, GxEPD_WHITE);
  textColor(GxEPD_BLACK, GxEPD_WHITE);
  drawLeft(4, 11, title, F_STATUS, 215);

  int x = SCREEN_W - 4 - 17;
  drawBattery(x, 2, st.batteryPct, GxEPD_BLACK);
  x -= 18;
  drawIcon(st.outputIcon, x, 1, GxEPD_BLACK);
  x -= 17;
  drawIcon(st.modeIcon, x, 1, GxEPD_BLACK);

  epd.drawFastHLine(0, STATUS_LINE_Y, SCREEN_W, GxEPD_BLACK);
}

// ---------------------------------------------------------------------------
// Bagian layar Now Playing
// ---------------------------------------------------------------------------
static const uint8_t NOTE_32[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x01, 0xF0,
  0x00, 0x00, 0x1F, 0xF0, 0x00, 0x01, 0xFF, 0xF0,
  0x00, 0x1F, 0xFF, 0xF0, 0x00, 0x1F, 0xFF, 0x70,
  0x00, 0x1F, 0xF1, 0xF0, 0x00, 0x1F, 0x1F, 0xF0,
  0x00, 0x1D, 0xFF, 0xF0, 0x00, 0x1F, 0xFF, 0x70,
  0x00, 0x1F, 0xF0, 0x70, 0x00, 0x1F, 0x00, 0x70,
  0x00, 0x1C, 0x07, 0xF0, 0x00, 0x1C, 0x0F, 0xF0,
  0x00, 0x1C, 0x1F, 0xF0, 0x00, 0x1C, 0x3F, 0xF0,
  0x00, 0x1C, 0x3F, 0xF0, 0x01, 0xFC, 0x3F, 0xF0,
  0x03, 0xFC, 0x1F, 0xE0, 0x07, 0xFC, 0x0F, 0xC0,
  0x0F, 0xFC, 0x07, 0x80, 0x0F, 0xFC, 0x00, 0x00,
  0x0F, 0xFC, 0x00, 0x00, 0x07, 0xF8, 0x00, 0x00,
  0x03, 0xF0, 0x00, 0x00, 0x01, 0xE0, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void drawCover(const uint8_t *cover)
{
  epd.fillRect(COVER_X - 1, COVER_Y - 1, COVER_SIZE + 2, COVER_SIZE + 2, GxEPD_WHITE);
  if (cover) {
    epd.drawBitmap(COVER_X, COVER_Y, cover, COVER_SIZE, COVER_SIZE, GxEPD_BLACK, GxEPD_WHITE);
  } else {
    epd.drawBitmap(COVER_X + 32, COVER_Y + 24, NOTE_32, 32, 32, GxEPD_BLACK, GxEPD_WHITE);
    textColor(GxEPD_BLACK, GxEPD_WHITE);
    drawCenter(COVER_X + COVER_SIZE / 2, COVER_Y + 74, "No Artwork", F_SMALL);
  }
  epd.drawRect(COVER_X - 1, COVER_Y - 1, COVER_SIZE + 2, COVER_SIZE + 2, GxEPD_BLACK);
}

static void drawTrackHeader(const TrackView &t)
{
  epd.fillRect(COL_X, STATUS_LINE_Y + 1, COL_W, LYR_Y - STATUS_LINE_Y - 1, GxEPD_WHITE);
  textColor(GxEPD_BLACK, GxEPD_WHITE);
  drawLeft(COL_X, TITLE_BASE, t.title, F_TITLE, COL_W);

  char sub[200];
  const bool hasArtist = t.artist && t.artist[0];
  const bool hasAlbum  = t.album && t.album[0];
  if (hasArtist && hasAlbum) snprintf(sub, sizeof(sub), "%s  \xC2\xB7  %s", t.artist, t.album);
  else snprintf(sub, sizeof(sub), "%s", hasArtist ? t.artist : (hasAlbum ? t.album : ""));
  drawLeft(COL_X, SUB_BASE, sub, F_SMALL, COL_W);

  dottedHLine(COL_X, SEP_Y, COL_W, GxEPD_BLACK);
}

static void drawTransport(const TrackView &t)
{
  epd.fillRect(COL_X, TR_Y, COL_W, TR_H, GxEPD_WHITE);

  // Progress bar: garis rambut + isi 3px + kenop bulat
  const int barY = TR_Y + 5;
  epd.drawFastHLine(COL_X, barY, COL_W, GxEPD_BLACK);
  int fill = 0;
  if (t.durationSec > 0) {
    fill = (int)((uint64_t)COL_W * min(t.elapsedSec, t.durationSec) / t.durationSec);
  }
  if (fill > 0) epd.fillRect(COL_X, barY - 1, fill, 3, GxEPD_BLACK);
  int knobX = constrain(COL_X + fill, COL_X + 3, COL_X + COL_W - 4);
  epd.fillCircle(knobX, barY, 3, GxEPD_BLACK);

  char a[12], b[12];
  formatTime(t.elapsedSec, a, sizeof(a));
  formatTime(t.durationSec, b, sizeof(b));

  textColor(GxEPD_BLACK, GxEPD_WHITE);
  const int base = TR_Y + 20;
  drawLeft(COL_X, base, a, F_SMALL);
  drawRight(COL_X + COL_W, base, b, F_SMALL);

  // Ikon tombol Play / Pause tepat di tengah bawah progress bar
  // (saat lagu berjalan tampilkan "||", saat dijeda tampilkan "|>")
  drawIcon(t.playing ? ICON_PAUSE : ICON_PLAY,
           COL_X + COL_W / 2 - 6, TR_Y + 10, GxEPD_BLACK);
}

// Bungkus kata ke beberapa baris. Return jumlah baris, atau maxLines+1 jika tidak muat.
static int wrapLines(const char *text, int maxW, char out[][72], int maxLines)
{
  char buf[160];
  strlcpy(buf, text, sizeof(buf));
  int n = 0;
  char cur[72] = "";
  for (char *w = strtok(buf, " "); w; w = strtok(nullptr, " ")) {
    char test[160];
    if (cur[0]) snprintf(test, sizeof(test), "%s %s", cur, w);
    else strlcpy(test, w, sizeof(test));

    if (!cur[0] || u8g2.getUTF8Width(test) <= maxW) {
      strlcpy(cur, test, sizeof(cur));
    } else {
      if (n >= maxLines) return maxLines + 1;
      strlcpy(out[n++], cur, 72);
      strlcpy(cur, w, sizeof(cur));
    }
  }
  if (cur[0]) {
    if (n >= maxLines) return maxLines + 1;
    strlcpy(out[n++], cur, 72);
  }
  return n;
}

static void drawLyrics(const char *text, bool hasLyrics)
{
  epd.fillRect(COL_X, LYR_Y, COL_W, LYR_H, GxEPD_WHITE);
  textColor(GxEPD_BLACK, GxEPD_WHITE);
  const int cx = COL_X + COL_W / 2;
  const int cy = LYR_Y + LYR_H / 2;

  if (!hasLyrics) {
    drawCenter(cx, cy + 4, "No lyrics available", F_SMALL);
    return;
  }
  if (!text || !text[0]) {
    // Jeda instrumental: tiga titik tenang
    epd.fillCircle(cx - 12, cy, 2, GxEPD_BLACK);
    epd.fillCircle(cx,      cy, 2, GxEPD_BLACK);
    epd.fillCircle(cx + 12, cy, 2, GxEPD_BLACK);
    return;
  }

  const int maxW = COL_W - 6;
  char lines[3][72];
  const uint8_t *font = F_LYRIC_L;
  int asc = 12, lh = 18;

  u8g2.setFont(F_LYRIC_L);
  int n = wrapLines(text, maxW, lines, 2);
  if (n > 2) {
    font = F_LYRIC_S;
    asc = 10;
    lh = 15;
    u8g2.setFont(font);
    n = wrapLines(text, maxW, lines, 3);
    if (n > 3) {
      n = 3;
      strlcat(lines[2], " ...", sizeof(lines[2]));
    }
  }

  const int block = asc + (n - 1) * lh;
  const int top = LYR_Y + (LYR_H - block) / 2;
  for (int i = 0; i < n; i++) {
    drawCenter(cx, top + asc + i * lh, lines[i], font, maxW);
  }
}

static void refreshWindow(int x, int y, int w, int h)
{
  digitalWrite(SD_CS, HIGH);
  if (++partialCount >= FULL_REFRESH_AFTER_PARTIALS) {
    epd.display(false);   // full refresh berkala untuk membersihkan ghosting
    partialCount = 0;
  } else {
    epd.displayWindow(x, y, w, h);
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void displayBegin()
{
  pinMode(EPD_CS, OUTPUT);
  digitalWrite(EPD_CS, HIGH);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  SPI.end();
  SPI.begin(EPD_SCK, SD_MISO, EPD_MOSI, -1);
  epd.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));

  epd.init(115200, true, 2, false);
  epd.setRotation(1);  // landscape 296x128
  epd.setTextWrap(false);

  u8g2.begin(epd);
  u8g2.setFontMode(1);
  u8g2.setFontDirection(0);
  textColor(GxEPD_BLACK, GxEPD_WHITE);
}

void displaySplash()
{
  epd.setFullWindow();
  epd.fillScreen(GxEPD_WHITE);
  textColor(GxEPD_BLACK, GxEPD_WHITE);

  const int cx = SCREEN_W / 2;
  drawSpacedCenter(cx, 30, DEVICE_TAGLINE, F_STATUS, 3);
  drawCenter(cx, 72, DEVICE_MODEL, F_HERO);
  epd.drawFastHLine(cx - 60, 84, 120, GxEPD_BLACK);
  drawCenter(cx, 100, "Hi-Fi Digital Audio  \xC2\xB7  E-Paper Display", F_SMALL);
  drawCenter(cx, 121, "Firmware " FW_VERSION, F_SMALL);

  epd.display(false);
  partialCount = 0;
}

void displayNowPlaying(const StatusInfo &st, const TrackView &t,
                       const char *lyric, bool hasLyrics, bool fullRefresh)
{
  epd.setFullWindow();
  epd.fillScreen(GxEPD_WHITE);

  drawStatusBar("Now Playing", st);
  drawCover(t.cover);
  drawTrackHeader(t);
  drawLyrics(lyric, hasLyrics);
  drawTransport(t);

  if (fullRefresh) {
    epd.display(false);
    partialCount = 0;
  } else {
    refreshWindow(0, 0, SCREEN_W, SCREEN_H);
  }
}

void displayUpdateTransport(const TrackView &t)
{
  drawTransport(t);
  refreshWindow(COL_X, TR_Y, COL_W, TR_H);
}

void displayUpdateLyrics(const TrackView &t, const char *lyric, bool hasLyrics)
{
  drawLyrics(lyric, hasLyrics);
  drawTransport(t);
  refreshWindow(COL_X, LYR_Y, COL_W, TR_Y + TR_H - LYR_Y);
}

void displayShowVolume(const TrackView &t, int volume, UiIcon outputIcon, const char *outputName)
{
  volume = constrain(volume, 0, 100);
  epd.fillRect(COL_X, LYR_Y, COL_W, LYR_H, GxEPD_WHITE);
  textColor(GxEPD_BLACK, GxEPD_WHITE);

  // Baris 1: ikon + nama output
  drawIcon(outputIcon, COL_X, LYR_Y + 2, GxEPD_BLACK);
  drawLeft(COL_X + 17, LYR_Y + 12, outputName, F_SMALL, COL_W - 60);

  // Baris 2: label VOLUME + angka besar
  u8g2.setFont(F_STATUS);
  drawLeft(COL_X, LYR_Y + 24, "VOLUME", F_STATUS);
  char num[8];
  snprintf(num, sizeof(num), "%d", volume);
  drawRight(COL_X + COL_W, LYR_Y + 24, num, F_BIG);

  // Baris 3: meter segmen bergaya VU
  const int segs = 26, segW = 6, gap = 1;
  const int segY = LYR_Y + 30;
  int active = (volume * segs + 50) / 100;
  for (int i = 0; i < segs; i++) {
    int x = COL_X + i * (segW + gap);
    if (i < active) epd.fillRect(x, segY, segW, 10, GxEPD_BLACK);
    else epd.fillRect(x, segY + 8, segW, 2, GxEPD_BLACK);
  }

  drawTransport(t);
  refreshWindow(COL_X, LYR_Y, COL_W, TR_Y + TR_H - LYR_Y);
}

void displayMenu(const char *title, const StatusInfo &st,
                 const MenuRow *rows, int count, int selected, bool fullRefresh)
{
  epd.setFullWindow();
  epd.fillScreen(GxEPD_WHITE);
  drawStatusBar(title, st);

  // Jendela gulir: pastikan baris terpilih selalu terlihat
  int first = 0;
  if (selected >= ROWS) first = selected - (ROWS - 1);
  if (count > ROWS && first > count - ROWS) first = count - ROWS;
  if (first < 0) first = 0;

  const bool scroll = count > ROWS;
  const int barRight = scroll ? 287 : SCREEN_W - 3;   // tepi kanan selection bar
  const int contentRight = barRight - 4;

  for (int r = 0; r < ROWS; r++) {
    int idx = first + r;
    if (idx >= count) break;
    const MenuRow &row = rows[idx];
    const int top = LIST_Y + r * ROW_H;
    const bool sel = (idx == selected);
    const uint16_t fg = sel ? GxEPD_WHITE : GxEPD_BLACK;
    const uint16_t bg = sel ? GxEPD_BLACK : GxEPD_WHITE;

    if (sel) epd.fillRoundRect(2, top, barRight - 2, ROW_H, 3, GxEPD_BLACK);
    textColor(fg, bg);

    int x = 8;
    if (row.icon != ICON_NONE) {
      drawIcon(row.icon, x, top + 3, fg);
      x += 18;
    }

    int right = contentRight;
    if (row.hasChildren) {
      drawChevron(right - 4, top + 6, fg);
      right -= 12;
    }

    int valueW = 0;
    if (row.value && row.value[0]) {
      valueW = min(textWidth(row.value, F_SMALL), 110);
      drawRight(right, top + 13, row.value, F_SMALL, 110);
      valueW += 10;
    }

    drawLeft(x, top + 13, row.label, sel ? F_ROW_SEL : F_ROW, right - valueW - x);
  }

  textColor(GxEPD_BLACK, GxEPD_WHITE);

  // Scrollbar tipis
  if (scroll) {
    const int trackTop = LIST_Y + 2, trackH = ROWS * ROW_H - 4;
    for (int yy = trackTop; yy < trackTop + trackH; yy += 2) epd.drawPixel(291, yy, GxEPD_BLACK);
    int thumbH = max(8, trackH * ROWS / count);
    int thumbY = trackTop + (trackH - thumbH) * first / (count - ROWS);
    epd.fillRect(290, thumbY, 3, thumbH, GxEPD_BLACK);
  }

  digitalWrite(SD_CS, HIGH);
  if (fullRefresh) {
    epd.display(false);
    partialCount = 0;
  } else {
    refreshWindow(0, 0, SCREEN_W, SCREEN_H);   // partial cepat tanpa kedip (~280 ms)
  }
}
