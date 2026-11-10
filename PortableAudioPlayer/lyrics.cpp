// lyrics.cpp — Parser LRC lokal dengan dukungan multi-timestamp, tag [offset],
// normalisasi tanda baca UTF-8, deteksi jeda instrumental, & Binary Search O(log N).

#include "lyrics.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ctype.h>
#include <string.h>

static LyricLine lines[MAX_LYRIC_LINES];
static int lineCount = 0;
static int stagingCount = 0;
static int32_t lrcOffsetMs = 0;
static SemaphoreHandle_t lyrMutex = nullptr;

static void ensureMutex()
{
  if (!lyrMutex) lyrMutex = xSemaphoreCreateMutex();
}

void lyricsReset()
{
  ensureMutex();
  xSemaphoreTake(lyrMutex, portMAX_DELAY);
  lineCount = 0;
  stagingCount = 0;
  lrcOffsetMs = 0;
  xSemaphoreGive(lyrMutex);
}

int lyricsGetLineCount()
{
  ensureMutex();
  xSemaphoreTake(lyrMutex, portMAX_DELAY);
  int n = lineCount;
  xSemaphoreGive(lyrMutex);
  return n;
}

// Ubah kutip lengkung & dash UTF-8 menjadi ASCII standar agar ter-render rapi di font U8g2
static void normalizeText(char *s)
{
  const char *r = s;
  char *wr = s;
  while (*r) {
    if ((uint8_t)*r == 0xE2 && (uint8_t)*(r + 1) == 0x80) {
      uint8_t d = (uint8_t)*(r + 2);
      if (d == 0x98 || d == 0x99) { *wr++ = '\''; r += 3; continue; }
      if (d == 0x9C || d == 0x9D) { *wr++ = '"';  r += 3; continue; }
      if (d == 0x93 || d == 0x94) { *wr++ = '-';  r += 3; continue; }
      if (d == 0xA6) { *wr++ = '.'; *wr++ = '.'; *wr++ = '.'; r += 3; continue; }
    }
    *wr++ = *r++;
  }
  *wr = '\0';
}

static bool parseUInt(const char *&p, int &val, int &digits)
{
  val = 0;
  digits = 0;
  while (*p >= '0' && *p <= '9') {
    val = val * 10 + (*p - '0');
    p++;
    digits++;
  }
  return digits > 0;
}

// Parse satu baris LRC: "[mm:ss.xx][mm:ss.xx] teks" atau "[offset:+/-ms]"
static void parseLrcLine(char *line)
{
  // Lewati UTF-8 BOM jika ada di awal file
  if ((uint8_t)line[0] == 0xEF && (uint8_t)line[1] == 0xBB && (uint8_t)line[2] == 0xBF) {
    line += 3;
  }

  if (strncmp(line, "[offset:", 8) == 0 || strncmp(line, "[OFFSET:", 8) == 0) {
    lrcOffsetMs = atoi(line + 8);
    return;
  }

  const char *p = line;
  uint32_t times[4];
  int nt = 0;

  while (*p == '[') {
    const char *q = p + 1;
    int mm, ss, frac = 0, d;
    if (!parseUInt(q, mm, d) || *q != ':') break;
    q++;
    if (!parseUInt(q, ss, d)) break;
    int fracMs = 0;
    if (*q == '.' || *q == ':') {
      q++;
      if (parseUInt(q, frac, d)) {
        if (d == 1) fracMs = frac * 100;
        else if (d == 2) fracMs = frac * 10;
        else {
          while (d > 3) { frac /= 10; d--; }
          fracMs = frac;
        }
      }
    }
    if (*q != ']') break;
    if (nt < 4) times[nt++] = (uint32_t)(mm * 60 + ss) * 1000UL + fracMs;
    p = q + 1;
  }
  if (nt == 0) return;

  while (*p == ' ') p++;
  char text[LYRIC_TEXT_LEN];
  strlcpy(text, p, sizeof(text));

  int len = strlen(text);
  while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\r' || text[len - 1] == '\n')) {
    text[--len] = '\0';
  }
  normalizeText(text);

  // Baris simbol musik dianggap jeda instrumental (string kosong -> layar gambar ♪)
  if (strcmp(text, "\xE2\x99\xAA") == 0 || strcmp(text, "\xE2\x99\xAB") == 0 ||
      strcmp(text, "~") == 0) {
    text[0] = '\0';
  }

  for (int i = 0; i < nt && stagingCount < MAX_LYRIC_LINES; i++) {
    lines[stagingCount].time_ms = times[i];
    strlcpy(lines[stagingCount].text, text, LYRIC_TEXT_LEN);
    stagingCount++;
  }
}

static int publishStaging()
{
  if (lrcOffsetMs != 0) {
    for (int i = 0; i < stagingCount; i++) {
      int32_t t = (int32_t)lines[i].time_ms - lrcOffsetMs;
      lines[i].time_ms = (t > 0) ? (uint32_t)t : 0;
    }
  }

  // Insertion sort berdasarkan time_ms (menangani multi-timestamp chorus)
  for (int i = 1; i < stagingCount; i++) {
    LyricLine tmp = lines[i];
    int j = i - 1;
    while (j >= 0 && lines[j].time_ms > tmp.time_ms) {
      lines[j + 1] = lines[j];
      j--;
    }
    lines[j + 1] = tmp;
  }

  ensureMutex();
  xSemaphoreTake(lyrMutex, portMAX_DELAY);
  lineCount = stagingCount;
  xSemaphoreGive(lyrMutex);

  return stagingCount;
}

int lyricsLoadFromText(const char *lrcText)
{
  lyricsReset();
  if (!lrcText || !lrcText[0]) return 0;

  char line[160];
  int li = 0;
  const char *p = lrcText;

  while (*p) {
    char c = *p++;
    if (c == '\n') {
      line[li] = '\0';
      parseLrcLine(line);
      li = 0;
    } else if (c != '\r') {
      if (li < (int)sizeof(line) - 1) line[li++] = c;
    }
  }
  if (li > 0) {
    line[li] = '\0';
    parseLrcLine(line);
  }

  // Fallback jika lirik berupa teks biasa tanpa timestamp [mm:ss.xx] (mis. tag ID3 USLT polos):
  // bagi per baris dengan interval 4 detik agar tetap tampil di layar Now Playing.
  if (stagingCount == 0) {
    p = lrcText;
    li = 0;
    uint32_t synthTimeMs = 0;
    auto pushPlainLine = [&](char *ln) {
      while (*ln == ' ' || *ln == '\t') ln++;
      int len = strlen(ln);
      while (len > 0 && (ln[len - 1] == ' ' || ln[len - 1] == '\r' || ln[len - 1] == '\n')) {
        ln[--len] = '\0';
      }
      if (len == 0 || ln[0] == '[') return;
      normalizeText(ln);
      if (stagingCount < MAX_LYRIC_LINES) {
        lines[stagingCount].time_ms = synthTimeMs;
        strlcpy(lines[stagingCount].text, ln, LYRIC_TEXT_LEN);
        stagingCount++;
        synthTimeMs += 4000;
      }
    };
    while (*p && stagingCount < MAX_LYRIC_LINES) {
      char c = *p++;
      if (c == '\n') {
        line[li] = '\0';
        pushPlainLine(line);
        li = 0;
      } else if (c != '\r') {
        if (li < (int)sizeof(line) - 1) line[li++] = c;
      }
    }
    if (li > 0 && stagingCount < MAX_LYRIC_LINES) {
      line[li] = '\0';
      pushPlainLine(line);
    }
  }

  return publishStaging();
}

int lyricsLoadFromStream(Stream &stream)
{
  lyricsReset();
  char line[160];
  int li = 0;

  while (stream.available()) {
    int c = stream.read();
    if (c < 0) break;
    if (c == '\n') {
      line[li] = '\0';
      parseLrcLine(line);
      li = 0;
    } else if (c != '\r') {
      if (li < (int)sizeof(line) - 1) line[li++] = (char)c;
    }
  }
  if (li > 0) {
    line[li] = '\0';
    parseLrcLine(line);
  }

  return publishStaging();
}

void lyricsGetCurrent(uint32_t positionMs, int leadMs, char *out, size_t outLen)
{
  out[0] = '\0';
  ensureMutex();
  xSemaphoreTake(lyrMutex, portMAX_DELAY);
  int n = lineCount;
  if (n > 0) {
    int32_t p = (int32_t)positionMs + leadMs;
    uint32_t pos = (p > 0) ? (uint32_t)p : 0;
    int idx = -1;

    // Binary search O(log N): cari baris terakhir dengan time_ms <= pos
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      if (lines[mid].time_ms <= pos) {
        idx = mid;
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }

    if (idx >= 0) {
      uint32_t inLine = pos - lines[idx].time_ms;
      bool show = true;
      if (idx + 1 < n) {
        uint32_t gap = lines[idx + 1].time_ms - lines[idx].time_ms;
        // Jeda instrumental panjang (>= 18 detik) -> kembali ke simbol ♪ setelah baris selesai dibaca
        if (gap >= 18000) {
          uint32_t dur = max((uint32_t)7000, (uint32_t)strlen(lines[idx].text) * 320);
          if (inLine >= dur) show = false;
        }
      } else if (inLine >= 10000) {
        show = false;
      }
      if (show) strlcpy(out, lines[idx].text, outLen);
    } else {
      // Saat intro sebelum timestamp pertama (mis. [00:13.80] di Leave The Door Open),
      // tampilkan langsung baris lirik pembuka agar area lirik tidak kosong di awal lagu.
      for (int k = 0; k < n; k++) {
        if (lines[k].text[0] != '\0') {
          strlcpy(out, lines[k].text, outLen);
          break;
        }
      }
    }
  }
  xSemaphoreGive(lyrMutex);
}

uint32_t lyricsGetNextChangeMs(uint32_t positionMs, int leadMs)
{
  uint32_t nextChange = UINT32_MAX;
  ensureMutex();
  xSemaphoreTake(lyrMutex, portMAX_DELAY);
  int n = lineCount;
  if (n > 0) {
    int32_t p = (int32_t)positionMs + leadMs;
    uint32_t pos = (p > 0) ? (uint32_t)p : 0;
    int idx = -1;
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      if (lines[mid].time_ms <= pos) {
        idx = mid;
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }
    if (idx < 0) {
      nextChange = lines[0].time_ms;
    } else if (idx + 1 < n) {
      nextChange = lines[idx + 1].time_ms;
    }
  }
  xSemaphoreGive(lyrMutex);
  return nextChange;
}

