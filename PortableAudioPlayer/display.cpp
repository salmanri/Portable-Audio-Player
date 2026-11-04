// display.cpp — Driver E-Paper WeAct 2.9" SSD1680 + UI Now Playing & Library Menu
// Diadaptasi dari SpotifyDesk V2 & referensi MusicStation v2 oleh Roman Likhachev.

#include "display.h"
#include "pins.h"
#include "config.h"

#include <SPI.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

// WeAct 2.9" B/W = panel DEPG0290BS, controller SSD1680 (296x128).
GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> epd(
    GxEPD2_290_BS(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
U8G2_FOR_ADAFRUIT_GFX u8g2;

// Font proporsional Helvetica
static const uint8_t *const TITLE_FONT    = u8g2_font_helvB14_tf;
static const uint8_t *const ARTIST_FONT   = u8g2_font_helvR08_tf;
static const uint8_t *const SUBTITLE_FONT = u8g2_font_helvB08_tf;
static const uint8_t *const LYRIC_FONT    = u8g2_font_helvB12_tf;
static const uint8_t *const TIME_FONT     = u8g2_font_helvB08_tf;
static const uint8_t *const MENU_FONT     = u8g2_font_helvB10_tf;
static const uint8_t *const MENU_SUB_FONT = u8g2_font_helvR08_tf;

static uint16_t partialCount = 0;

// ---------------------------------------------------------------------------
// Simbol Not Balok Musik 32x32 px (tampil saat jeda instrumental)
// ---------------------------------------------------------------------------
static const uint8_t MUSIC_NOTE_32x32[] = {
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

static void drawMusicIcon(int cx, int cy)
{
  epd.drawBitmap(cx - 16, cy - 16, MUSIC_NOTE_32x32, 32, 32, GxEPD_BLACK, GxEPD_WHITE);
}

// ---------------------------------------------------------------------------
// Helper Gambar Teks
// ---------------------------------------------------------------------------
static void setTextColors(uint16_t fg, uint16_t bg)
{
  u8g2.setForegroundColor(fg);
  u8g2.setBackgroundColor(bg);
}

static void drawText(int x, int baselineY, const char *text, const uint8_t *font)
{
  if (!text || !text[0]) return;
  u8g2.setFont(font);
  u8g2.setCursor(x, baselineY);
  u8g2.print(text);
}

static void drawTextRight(int rightX, int baselineY, const char *text, const uint8_t *font)
{
  if (!text || !text[0]) return;
  u8g2.setFont(font);
  int w = u8g2.getUTF8Width(text);
  u8g2.setCursor(rightX - w, baselineY);
  u8g2.print(text);
}

static void drawTextCenter(int centerX, int baselineY, const char *text, const uint8_t *font)
{
  if (!text || !text[0]) return;
  u8g2.setFont(font);
  int w = u8g2.getUTF8Width(text);
  u8g2.setCursor(centerX - w / 2, baselineY);
  u8g2.print(text);
}

static void drawTextFitted(int x, int baselineY, const char *text, int maxW, const uint8_t *font)
{
  if (!text || !text[0]) return;
  u8g2.setFont(font);
  int w = u8g2.getUTF8Width(text);
  if (w <= maxW) {
    u8g2.setCursor(x, baselineY);
    u8g2.print(text);
    return;
  }
  char buf[128];
  strncpy(buf, text, sizeof(buf) - 4);
  buf[sizeof(buf) - 4] = '\0';
  int len = strlen(buf);
  while (len > 0) {
    buf[len] = '\0';
    strcpy(buf + len, "...");
    if (u8g2.getUTF8Width(buf) <= maxW) break;
    len--;
  }
  u8g2.setCursor(x, baselineY);
  u8g2.print(buf);
}

static void drawTextRightFitted(int rightX, int baselineY, const char *text, int maxW,
                                const uint8_t *font)
{
  if (!text || !text[0]) return;
  u8g2.setFont(font);
  int w = u8g2.getUTF8Width(text);
  if (w <= maxW) {
    u8g2.setCursor(rightX - w, baselineY);
    u8g2.print(text);
    return;
  }
  char buf[128];
  strncpy(buf, text, sizeof(buf) - 4);
  buf[sizeof(buf) - 4] = '\0';
  int len = strlen(buf);
  while (len > 0) {
    buf[len] = '\0';
    strcpy(buf + len, "...");
    if (u8g2.getUTF8Width(buf) <= maxW) break;
    len--;
  }
  int fitW = u8g2.getUTF8Width(buf);
  u8g2.setCursor(rightX - fitW, baselineY);
  u8g2.print(buf);
}

static void formatTime(uint32_t seconds, char *out, size_t size)
{
  snprintf(out, size, "%lu:%02lu", (unsigned long)(seconds / 60),
           (unsigned long)(seconds % 60));
}

// Gambar grafis kaset retro jika lagu belum punya bitmap cover art
static void drawDefaultCassetteArt()
{
  epd.fillRect(COVER_X, COVER_Y, COVER_SIZE, COVER_SIZE, GxEPD_WHITE);
  epd.drawRect(COVER_X, COVER_Y, COVER_SIZE, COVER_SIZE, GxEPD_BLACK);
  epd.drawRect(COVER_X + 3, COVER_Y + 3, COVER_SIZE - 6, COVER_SIZE - 6, GxEPD_BLACK);

  // Badan kaset di tengah
  int cx = COVER_X + 8;
  int cy = COVER_Y + 18;
  int cw = COVER_SIZE - 16; // 80
  int ch = 56;
  epd.drawRoundRect(cx, cy, cw, ch, 4, GxEPD_BLACK);
  epd.drawRect(cx + 6, cy + 6, cw - 12, 28, GxEPD_BLACK);

  // Dua roda pita kaset
  int r1x = cx + 22, r2x = cx + cw - 22, ry = cy + 20;
  epd.drawCircle(r1x, ry, 9, GxEPD_BLACK);
  epd.drawCircle(r1x, ry, 4, GxEPD_BLACK);
  epd.drawCircle(r2x, ry, 9, GxEPD_BLACK);
  epd.drawCircle(r2x, ry, 4, GxEPD_BLACK);
  epd.drawRect(r1x + 10, ry - 4, r2x - r1x - 20, 8, GxEPD_BLACK);

  // Trapesium bawah kaset
  epd.drawLine(cx + 12, cy + ch, cx + 18, cy + ch - 12, GxEPD_BLACK);
  epd.drawLine(cx + cw - 12, cy + ch, cx + cw - 18, cy + ch - 12, GxEPD_BLACK);
  epd.drawFastHLine(cx + 18, cy + ch - 12, cw - 36, GxEPD_BLACK);

  setTextColors(GxEPD_BLACK, GxEPD_WHITE);
  drawTextCenter(COVER_X + COVER_SIZE / 2, COVER_Y + 14, "WALKMAN DAP", ARTIST_FONT);
  drawTextCenter(COVER_X + COVER_SIZE / 2, COVER_Y + COVER_SIZE - 6, "HI-FI AUDIO", ARTIST_FONT);
}

static void drawPlayPauseIcon(int cx, int cy, bool playing)
{
  if (playing) {
    epd.fillRect(cx - 5, cy - 5, 3, 10, GxEPD_BLACK);
    epd.fillRect(cx + 2, cy - 5, 3, 10, GxEPD_BLACK);
  } else {
    epd.fillTriangle(cx - 4, cy - 5, cx - 4, cy + 5, cx + 5, cy, GxEPD_BLACK);
  }
}

// ---------------------------------------------------------------------------
// Header Now Playing: Judul Lagu (Baris 1) & Artis + Badge Output/Album (Baris 2)
// ---------------------------------------------------------------------------
static void drawHeader(const char *title, const char *artist, const char *badgeRight)
{
  setTextColors(GxEPD_BLACK, GxEPD_WHITE);
  if (title && title[0]) {
    drawTextFitted(4, 15, title, SCREEN_W - 8, TITLE_FONT);
  }

  const bool hasArtist = (artist && artist[0]);
  const bool hasRight  = (badgeRight && badgeRight[0]);

  if (hasArtist && hasRight) {
    u8g2.setFont(SUBTITLE_FONT);
    int wRight = u8g2.getUTF8Width(badgeRight);
    int maxArtW = SCREEN_W - 8 - wRight - 10;
    if (maxArtW < 80) maxArtW = 80;

    drawTextFitted(4, 28, artist, maxArtW, ARTIST_FONT);
    drawTextRightFitted(SCREEN_W - 4, 28, badgeRight, SCREEN_W - 8 - maxArtW, SUBTITLE_FONT);
  } else if (hasArtist) {
    drawTextFitted(4, 28, artist, SCREEN_W - 8, ARTIST_FONT);
  } else if (hasRight) {
    drawTextRightFitted(SCREEN_W - 4, 28, badgeRight, SCREEN_W - 8, SUBTITLE_FONT);
  }

  epd.drawFastHLine(0, 31, SCREEN_W, GxEPD_BLACK);
}

// ---------------------------------------------------------------------------
// Area Transport: Waktu & Progress Bar
// ---------------------------------------------------------------------------
static void drawTransport(uint32_t elapsedSec, uint32_t durationSec, bool playing)
{
  epd.fillRect(RIGHT_X, TRANSPORT_Y, RIGHT_W, TRANSPORT_H, GxEPD_WHITE);
  setTextColors(GxEPD_BLACK, GxEPD_WHITE);

  char elapsedText[12];
  char durationText[12];
  formatTime(elapsedSec, elapsedText, sizeof(elapsedText));
  formatTime(durationSec, durationText, sizeof(durationText));

  drawText(RIGHT_X + 4, TRANSPORT_Y + 11, elapsedText, TIME_FONT);
  drawTextRight(RIGHT_X + RIGHT_W - 4, TRANSPORT_Y + 11, durationText, TIME_FONT);
  drawPlayPauseIcon(RIGHT_X + RIGHT_W / 2, TRANSPORT_Y + 7, playing);

  const int barX = RIGHT_X + 4;
  const int barW = RIGHT_W - 8;
  const int barY = TRANSPORT_Y + 16;
  const int barH = 5;

  epd.drawRect(barX, barY, barW, barH, GxEPD_BLACK);
  if (durationSec > 0) {
    int fill = (int)((uint64_t)(barW - 2) * min(elapsedSec, durationSec) / durationSec);
    if (fill > 0) {
      epd.fillRect(barX + 1, barY + 1, fill, barH - 2, GxEPD_BLACK);
    }
  }
}

// ---------------------------------------------------------------------------
// Area Lirik: Auto-wrap & Center Aligned (atau simbol ♪ saat jeda)
// ---------------------------------------------------------------------------
static void drawLyrics(const char *text)
{
  epd.fillRect(RIGHT_X, LYRIC_AREA_Y, RIGHT_W, LYRIC_AREA_H, GxEPD_WHITE);

  const int centerX = RIGHT_X + (RIGHT_W / 2);
  const int centerY = LYRIC_AREA_Y + (LYRIC_AREA_H / 2);

  if (!text || !text[0]) {
    drawMusicIcon(centerX, centerY);
    return;
  }

  u8g2.setFont(LYRIC_FONT);
  setTextColors(GxEPD_BLACK, GxEPD_WHITE);

  const int maxW = RIGHT_W - 16;
  char lines[4][64];
  int lineCount = 0;
  lines[0][0] = '\0';

  char textCopy[256];
  strncpy(textCopy, text, sizeof(textCopy) - 1);
  textCopy[sizeof(textCopy) - 1] = '\0';

  char *word = strtok(textCopy, " ");
  char currentLine[64] = "";

  while (word && lineCount < 4) {
    char testLine[64];
    if (currentLine[0] == '\0') {
      strncpy(testLine, word, sizeof(testLine) - 1);
      testLine[sizeof(testLine) - 1] = '\0';
    } else {
      snprintf(testLine, sizeof(testLine), "%s %s", currentLine, word);
    }

    if (u8g2.getUTF8Width(testLine) <= maxW) {
      strncpy(currentLine, testLine, sizeof(currentLine) - 1);
      currentLine[sizeof(currentLine) - 1] = '\0';
      word = strtok(nullptr, " ");
    } else {
      if (currentLine[0] != '\0') {
        strncpy(lines[lineCount++], currentLine, sizeof(lines[0]) - 1);
        lines[lineCount - 1][sizeof(lines[0]) - 1] = '\0';
        currentLine[0] = '\0';
      } else {
        strncpy(lines[lineCount++], word, sizeof(lines[0]) - 1);
        lines[lineCount - 1][sizeof(lines[0]) - 1] = '\0';
        word = strtok(nullptr, " ");
      }
    }
  }

  if (currentLine[0] != '\0' && lineCount < 4) {
    strncpy(lines[lineCount++], currentLine, sizeof(lines[0]) - 1);
    lines[lineCount - 1][sizeof(lines[0]) - 1] = '\0';
  }

  int baseHeights[4];
  if (lineCount == 1) {
    baseHeights[0] = LYRIC_AREA_Y + 41;
  } else if (lineCount == 2) {
    baseHeights[0] = LYRIC_AREA_Y + 31;
    baseHeights[1] = LYRIC_AREA_Y + 54;
  } else if (lineCount == 3) {
    baseHeights[0] = LYRIC_AREA_Y + 22;
    baseHeights[1] = LYRIC_AREA_Y + 42;
    baseHeights[2] = LYRIC_AREA_Y + 63;
  } else {
    baseHeights[0] = LYRIC_AREA_Y + 16;
    baseHeights[1] = LYRIC_AREA_Y + 33;
    baseHeights[2] = LYRIC_AREA_Y + 50;
    baseHeights[3] = LYRIC_AREA_Y + 67;
  }

  for (int i = 0; i < lineCount; i++) {
    int w = u8g2.getUTF8Width(lines[i]);
    int lineX = centerX - (w / 2);
    if (lineX < RIGHT_X + 4) lineX = RIGHT_X + 4;
    u8g2.setCursor(lineX, baseHeights[i]);
    u8g2.print(lines[i]);
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void displayBegin()
{
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  epd.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));

  epd.init(115200, true, 2, false);
  epd.setRotation(1);  // Landscape 296x128
  epd.setTextWrap(false);

  u8g2.begin(epd);
  u8g2.setFontMode(1);
  u8g2.setFontDirection(0);
  setTextColors(GxEPD_BLACK, GxEPD_WHITE);
}

void displayDrawSplash()
{
  epd.setFullWindow();
  epd.fillScreen(GxEPD_WHITE);

  drawDefaultCassetteArt();
  drawHeader("WALKMAN DAP", "Portable Audio Player", FW_VERSION);

  setTextColors(GxEPD_BLACK, GxEPD_WHITE);
  drawTextCenter(RIGHT_X + RIGHT_W / 2, 62, "RETRO E-INK PLAYER", MENU_FONT);
  drawTextCenter(RIGHT_X + RIGHT_W / 2, 82, "3.5mm + Speaker + BT", ARTIST_FONT);
  drawTextCenter(RIGHT_X + RIGHT_W / 2, 112, "Memuat perpustakaan...", ARTIST_FONT);

  epd.display(false);
  partialCount = 0;
}

void displayDrawTrack(const char *title, const char *artist, const char *badgeRight,
                      const uint8_t *coverBitmap,
                      uint32_t elapsedSec, uint32_t durationSec, bool playing,
                      const char *lyricText)
{
  epd.setFullWindow();
  epd.fillScreen(GxEPD_WHITE);

  drawHeader(title, artist, badgeRight);

  if (coverBitmap) {
    epd.drawBitmap(COVER_X, COVER_Y, coverBitmap, COVER_SIZE, COVER_SIZE,
                   GxEPD_BLACK, GxEPD_WHITE);
    epd.drawRect(COVER_X, COVER_Y, COVER_SIZE, COVER_SIZE, GxEPD_BLACK);
  } else {
    drawDefaultCassetteArt();
  }

  drawLyrics(lyricText);
  drawTransport(elapsedSec, durationSec, playing);

  epd.display(false);
  partialCount = 0;
}

void displayUpdateTransport(uint32_t elapsedSec, uint32_t durationSec, bool playing)
{
  drawTransport(elapsedSec, durationSec, playing);

  if (++partialCount >= FULL_REFRESH_AFTER_PARTIALS) {
    epd.display(false);
    partialCount = 0;
  } else {
    epd.displayWindow(RIGHT_X, TRANSPORT_Y, RIGHT_W, TRANSPORT_H);
  }
}

void displayUpdateLyrics(const char *lyricText)
{
  drawLyrics(lyricText);

  if (++partialCount >= FULL_REFRESH_AFTER_PARTIALS) {
    epd.display(false);
    partialCount = 0;
  } else {
    epd.displayWindow(RIGHT_X, LYRIC_AREA_Y, RIGHT_W, LYRIC_AREA_H);
  }
}

void displayUpdateLyricsAndTransport(const char *lyricText, uint32_t elapsedSec,
                                     uint32_t durationSec, bool playing)
{
  drawLyrics(lyricText);
  drawTransport(elapsedSec, durationSec, playing);

  if (++partialCount >= FULL_REFRESH_AFTER_PARTIALS) {
    epd.display(false);
    partialCount = 0;
  } else {
    epd.displayWindow(RIGHT_X, LYRIC_AREA_Y, RIGHT_W, LYRIC_AREA_H + TRANSPORT_H);
  }
}

void displayUpdateVolumePopup(int volumePercent, const char *outputName,
                              uint32_t elapsedSec, uint32_t durationSec, bool playing)
{
  epd.fillRect(RIGHT_X, LYRIC_AREA_Y, RIGHT_W, LYRIC_AREA_H, GxEPD_WHITE);

  // Kotak pop-up retro di tengah area lirik
  const int boxX = RIGHT_X + 10;
  const int boxY = LYRIC_AREA_Y + 8;
  const int boxW = RIGHT_W - 20; // 172
  const int boxH = LYRIC_AREA_H - 16; // 56

  epd.drawRect(boxX, boxY, boxW, boxH, GxEPD_BLACK);
  epd.drawRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, GxEPD_BLACK);

  setTextColors(GxEPD_BLACK, GxEPD_WHITE);
  char volTitle[40];
  snprintf(volTitle, sizeof(volTitle), "VOLUME  %d%%", constrain(volumePercent, 0, 100));
  drawTextCenter(RIGHT_X + RIGHT_W / 2, boxY + 20, volTitle, LYRIC_FONT);

  if (outputName && outputName[0]) {
    drawTextCenter(RIGHT_X + RIGHT_W / 2, boxY + 33, outputName, ARTIST_FONT);
  }

  // Bar volume bergaya segmen retro
  const int barX = boxX + 12;
  const int barY = boxY + 39;
  const int barW = boxW - 24;
  const int barH = 10;
  epd.drawRect(barX, barY, barW, barH, GxEPD_BLACK);

  int fillW = (barW - 4) * constrain(volumePercent, 0, 100) / 100;
  if (fillW > 0) {
    epd.fillRect(barX + 2, barY + 2, fillW, barH - 4, GxEPD_BLACK);
  }

  drawTransport(elapsedSec, durationSec, playing);
  epd.displayWindow(RIGHT_X, LYRIC_AREA_Y, RIGHT_W, LYRIC_AREA_H + TRANSPORT_H);
}

// ---------------------------------------------------------------------------
// Layar Menu / Library (Gaya MusicStation v2 — 6 baris dengan Inverted Bar)
// ---------------------------------------------------------------------------
void displayDrawMenu(const char *headerTitle, const char *headerRight,
                     const char *const *items, const char *const *rightLabels,
                     int itemCount, int selectedIndex, bool fullRefresh)
{
  if (fullRefresh) {
    epd.setFullWindow();
  }
  epd.fillScreen(GxEPD_WHITE);

  // Header Bar (Y = 0..19)
  setTextColors(GxEPD_BLACK, GxEPD_WHITE);
  drawTextFitted(6, 14, headerTitle ? headerTitle : "LIBRARY", 200, MENU_FONT);

  char posBuf[32];
  if (headerRight && headerRight[0]) {
    snprintf(posBuf, sizeof(posBuf), "%s  %d/%d", headerRight,
             itemCount > 0 ? (selectedIndex + 1) : 0, itemCount);
  } else {
    snprintf(posBuf, sizeof(posBuf), "%d/%d",
             itemCount > 0 ? (selectedIndex + 1) : 0, itemCount);
  }
  drawTextRight(SCREEN_W - 6, 14, posBuf, TIME_FONT);
  epd.drawFastHLine(0, MENU_HEADER_H - 1, SCREEN_W, GxEPD_BLACK);

  // Hitung window scroll supaya selectedIndex selalu terlihat di antara 6 baris
  int firstIdx = 0;
  if (selectedIndex >= MENU_VISIBLE_ROWS) {
    firstIdx = selectedIndex - (MENU_VISIBLE_ROWS - 1);
  }
  if (firstIdx + MENU_VISIBLE_ROWS > itemCount && itemCount > MENU_VISIBLE_ROWS) {
    firstIdx = itemCount - MENU_VISIBLE_ROWS;
  }
  if (firstIdx < 0) firstIdx = 0;

  for (int row = 0; row < MENU_VISIBLE_ROWS; row++) {
    int idx = firstIdx + row;
    if (idx >= itemCount) break;

    int rowY = MENU_LIST_Y + row * MENU_ROW_H;
    bool isSelected = (idx == selectedIndex);

    if (isSelected) {
      // Baris aktif: blok hitam pekat, teks putih (Inverted Selection Band)
      epd.fillRect(0, rowY, SCREEN_W, MENU_ROW_H, GxEPD_BLACK);
      setTextColors(GxEPD_WHITE, GxEPD_BLACK);
    } else {
      setTextColors(GxEPD_BLACK, GxEPD_WHITE);
    }

    int rightW = 0;
    if (rightLabels && rightLabels[idx] && rightLabels[idx][0]) {
      u8g2.setFont(MENU_SUB_FONT);
      rightW = u8g2.getUTF8Width(rightLabels[idx]) + 10;
      drawTextRight(SCREEN_W - 6, rowY + 13, rightLabels[idx], MENU_SUB_FONT);
    }

    int maxItemW = SCREEN_W - 16 - rightW;
    drawTextFitted(8, rowY + 13, items[idx], maxItemW, MENU_FONT);
  }

  setTextColors(GxEPD_BLACK, GxEPD_WHITE);

  if (fullRefresh || ++partialCount >= FULL_REFRESH_AFTER_PARTIALS) {
    epd.display(false);
    partialCount = 0;
  } else {
    epd.displayWindow(0, 0, SCREEN_W, SCREEN_H);
  }
}

void displayDrawAbout(const char *outputName, const char *playModeName, int volumePercent)
{
  epd.setFullWindow();
  epd.fillScreen(GxEPD_WHITE);

  setTextColors(GxEPD_BLACK, GxEPD_WHITE);
  drawText(6, 14, "ABOUT WALKMAN DAP", MENU_FONT);
  drawTextRight(SCREEN_W - 6, 14, FW_VERSION, TIME_FONT);
  epd.drawFastHLine(0, 19, SCREEN_W, GxEPD_BLACK);

  char line1[64], line2[64], line3[64];
  snprintf(line1, sizeof(line1), "Output : %s   |   Vol : %d%%", outputName, volumePercent);
  snprintf(line2, sizeof(line2), "Mode   : %s", playModeName);
  snprintf(line3, sizeof(line3), "RAM    : %u KB free  |  PSRAM : %u KB",
           (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));

  drawText(8, 38, line1, ARTIST_FONT);
  drawText(8, 52, line2, ARTIST_FONT);
  drawText(8, 66, line3, ARTIST_FONT);

  epd.drawFastHLine(0, 74, SCREEN_W, GxEPD_BLACK);
  drawText(8, 89, "Kontrol Knob : Putar = Scroll/Vol, Klik = Play/OK", ARTIST_FONT);
  drawText(8, 103, "Tombol Fisik : [Prev]  [Next]  [Menu/Back]", ARTIST_FONT);
  drawText(8, 117, "Serial Test  : w/s = Putar, Space = Klik, n/p/m", SUBTITLE_FONT);

  epd.display(false);
  partialCount = 0;
}

