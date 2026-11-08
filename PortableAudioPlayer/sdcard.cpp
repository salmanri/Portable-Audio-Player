// sdcard.cpp — Implementasi MicroSD Library Scanner, Parser ID3v2/ID3v1 + VBR/CBR Duration,
//              Loader Lirik .lrc, dan Dekoder Cover Art JPEG + Floyd-Steinberg Dithering 96x96.

#include "sdcard.h"
#include "pins.h"
#include "lyrics.h"

#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <JPEGDEC.h>
#include <driver/gpio.h>
#include <string.h>
#include <strings.h>

static SdTrackEntry tracks[MAX_SD_TRACKS];
static int trackCount = 0;
static bool mounted = false;

// ---------------------------------------------------------------------------
// Helper String & Path
// ---------------------------------------------------------------------------
static bool endsWithIgnoreCase(const char *str, const char *suffix)
{
  size_t lenS = strlen(str);
  size_t lenX = strlen(suffix);
  if (lenX > lenS) return false;
  return strcasecmp(str + lenS - lenX, suffix) == 0;
}

static bool isHiddenOrSystemName(const char *name)
{
  if (!name || !name[0]) return true;
  if (name[0] == '.') return true; // ".", "..", "._file", ".Trash"
  if (strcasecmp(name, "System Volume Information") == 0) return true;
  if (strcasecmp(name, "LOST.DIR") == 0) return true;
  if (strcasecmp(name, "FOUND.000") == 0) return true;
  return false;
}

static const char *baseNameOf(const char *path)
{
  const char *slash = strrchr(path, '/');
  return slash ? (slash + 1) : path;
}

static void trimTrailingSpaces(char *s)
{
  int n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == '\t')) {
    s[--n] = '\0';
  }
}

static void replaceExtension(const char *path, const char *newExt, char *out, size_t outSize)
{
  strlcpy(out, path, outSize);
  char *dot = strrchr(out, '.');
  char *slash = strrchr(out, '/');
  if (dot && (!slash || dot > slash)) {
    *dot = '\0';
  }
  strlcat(out, newExt, outSize);
}

static void parentDirOf(const char *path, char *out, size_t outSize)
{
  strlcpy(out, path, outSize);
  char *slash = strrchr(out, '/');
  if (slash && slash != out) {
    *slash = '\0';
  } else {
    strlcpy(out, "/", outSize);
  }
}

// ---------------------------------------------------------------------------
// Parser ID3v2 (v2.2 / v2.3 / v2.4), ID3v1, Embedded APIC JPEG, & Durasi MP3
// ---------------------------------------------------------------------------
static uint32_t readSynchsafe32(const uint8_t *b)
{
  return ((uint32_t)(b[0] & 0x7F) << 21) |
         ((uint32_t)(b[1] & 0x7F) << 14) |
         ((uint32_t)(b[2] & 0x7F) << 7)  |
         ((uint32_t)(b[3] & 0x7F));
}

static uint32_t readBE32(const uint8_t *b)
{
  return ((uint32_t)b[0] << 24) |
         ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8)  |
         ((uint32_t)b[3]);
}

static uint32_t readBE24(const uint8_t *b)
{
  return ((uint32_t)b[0] << 16) |
         ((uint32_t)b[1] << 8)  |
         ((uint32_t)b[2]);
}

// Konversi payload teks frame ID3v2 (ISO-8859-1 / UTF-16 / UTF-8) ke UTF-8 bersih
static void decodeId3Text(const uint8_t *buf, size_t len, char *out, size_t outSize)
{
  out[0] = '\0';
  if (len <= 1 || outSize == 0) return;

  uint8_t enc = buf[0];
  const uint8_t *p = buf + 1;
  size_t rem = len - 1;
  size_t w = 0;

  if (enc == 0 || enc == 3) {
    // 0 = ISO-8859-1, 3 = UTF-8
    for (size_t i = 0; i < rem && w + 1 < outSize; i++) {
      uint8_t c = p[i];
      if (c == 0) break;
      if (enc == 0 && c >= 0x80) {
        // Konversi Latin-1 ke UTF-8 2-byte
        if (w + 2 >= outSize) break;
        out[w++] = (char)(0xC0 | (c >> 6));
        out[w++] = (char)(0x80 | (c & 0x3F));
      } else {
        out[w++] = (char)c;
      }
    }
  } else if (enc == 1 || enc == 2) {
    // 1 = UTF-16 dengan BOM, 2 = UTF-16BE tanpa BOM
    bool littleEndian = true;
    if (enc == 2) {
      littleEndian = false;
    } else if (rem >= 2) {
      if (p[0] == 0xFE && p[1] == 0xFF) { littleEndian = false; p += 2; rem -= 2; }
      else if (p[0] == 0xFF && p[1] == 0xFE) { littleEndian = true; p += 2; rem -= 2; }
    }
    for (size_t i = 0; i + 1 < rem && w + 1 < outSize; i += 2) {
      uint16_t ch = littleEndian ? (uint16_t)(p[i] | (p[i + 1] << 8))
                                 : (uint16_t)((p[i] << 8) | p[i + 1]);
      if (ch == 0) break;
      if (ch < 0x80) {
        out[w++] = (char)ch;
      } else if (ch < 0x800) {
        if (w + 2 >= outSize) break;
        out[w++] = (char)(0xC0 | (ch >> 6));
        out[w++] = (char)(0x80 | (ch & 0x3F));
      } else {
        if (w + 3 >= outSize) break;
        out[w++] = (char)(0xE0 | (ch >> 12));
        out[w++] = (char)(0x80 | ((ch >> 6) & 0x3F));
        out[w++] = (char)(0x80 | (ch & 0x3F));
      }
    }
  }
  out[w] = '\0';
  trimTrailingSpaces(out);
}

// Estimasi durasi MP3 (detik) dari header Xing/Info (VBR) atau frame MPEG pertama (CBR)
static uint32_t estimateMp3DurationSec(File &f, uint32_t audioOffset, uint32_t fileSize)
{
  if (fileSize <= audioOffset + 128) return 0;
  uint32_t audioBytes = fileSize - audioOffset;

  static const uint16_t bitratesV1L3[16] = {
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
  };
  static const uint16_t bitratesV2L3[16] = {
    0,  8, 16, 24, 32, 40, 48, 56,  64,  80,  96, 112, 128, 144, 160, 0
  };
  static const uint16_t sampleRatesV1[4] = { 44100, 48000, 32000, 0 };
  static const uint16_t sampleRatesV2[4] = { 22050, 24000, 16000, 0 };
  static const uint16_t sampleRatesV25[4] = { 11025, 12000, 8000, 0 };

  uint8_t buf[512];
  f.seek(audioOffset);
  size_t n = f.read(buf, sizeof(buf));
  if (n < 48) return audioBytes / 16000;

  for (size_t i = 0; i + 40 < n; i++) {
    if (buf[i] != 0xFF || (buf[i + 1] & 0xE0) != 0xE0) continue;

    uint8_t mpegVerBits = (buf[i + 1] >> 3) & 0x03; // 3=MPEG1, 2=MPEG2, 0=MPEG2.5
    uint8_t layerBits   = (buf[i + 1] >> 1) & 0x03; // 1=Layer III
    uint8_t brIdx       = (buf[i + 2] >> 4) & 0x0F;
    uint8_t srIdx       = (buf[i + 2] >> 2) & 0x03;
    if (mpegVerBits == 1 || layerBits != 1 || brIdx == 0 || brIdx == 15 || srIdx == 3) continue;

    uint32_t sr = (mpegVerBits == 3) ? sampleRatesV1[srIdx]
                : (mpegVerBits == 2) ? sampleRatesV2[srIdx]
                                     : sampleRatesV25[srIdx];
    uint32_t kbps = (mpegVerBits == 3) ? bitratesV1L3[brIdx] : bitratesV2L3[brIdx];
    uint32_t samplesPerFrame = (mpegVerBits == 3) ? 1152 : 576;

    // Cek apakah ada header VBR "Xing" atau "Info" di dalam frame pertama
    for (size_t k = i + 4; k + 16 <= n && k <= i + 40; k++) {
      if ((memcmp(&buf[k], "Xing", 4) == 0 || memcmp(&buf[k], "Info", 4) == 0)) {
        uint32_t flags = readBE32(&buf[k + 4]);
        if (flags & 0x01) { // Frames field present
          uint32_t totalFrames = readBE32(&buf[k + 8]);
          if (totalFrames > 0 && sr > 0) {
            return (uint32_t)((uint64_t)totalFrames * samplesPerFrame / sr);
          }
        }
      }
    }

    if (kbps > 0) {
      return (uint32_t)(((uint64_t)audioBytes * 8ULL) / (kbps * 1000ULL));
    }
    break;
  }

  return max((uint32_t)1, audioBytes / 16000);
}

static void parseMp3Metadata(const char *path, SdTrackEntry &entry)
{
  memset(&entry, 0, sizeof(entry));
  strlcpy(entry.path, path, sizeof(entry.path));

  File f = SD.open(path, FILE_READ);
  if (!f) return;

  uint32_t fileSize = f.size();
  uint32_t audioOffset = 0;

  // 1. Cek header ID3v2 di awal file
  uint8_t hdr[10];
  if (fileSize >= 10 && f.read(hdr, 10) == 10 &&
      hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
    uint8_t ver = hdr[3];
    uint8_t flags = hdr[5];
    uint32_t tagSize = readSynchsafe32(&hdr[6]);
    uint32_t tagEnd = min(fileSize, 10U + tagSize);
    audioOffset = tagEnd;

    uint32_t pos = 10;
    // Lewati Extended Header jika ada
    if ((flags & 0x40) && pos + 4 <= tagEnd) {
      uint8_t ext[4];
      f.seek(pos);
      if (f.read(ext, 4) == 4) {
        uint32_t extSize = (ver == 4) ? readSynchsafe32(ext) : readBE32(ext);
        pos += (ver == 4) ? extSize : (4 + extSize);
      }
    }

    const uint32_t frameHdrLen = (ver == 2) ? 6 : 10;
    while (pos + frameHdrLen <= tagEnd) {
      uint8_t fh[10];
      f.seek(pos);
      if (f.read(fh, frameHdrLen) != frameHdrLen) break;
      if (fh[0] == 0) break; // Padding nol

      char id[5] = {0};
      uint32_t fSize = 0;
      if (ver == 2) {
        memcpy(id, fh, 3);
        fSize = readBE24(&fh[3]);
      } else {
        memcpy(id, fh, 4);
        fSize = (ver == 4) ? readSynchsafe32(&fh[4]) : readBE32(&fh[4]);
      }

      uint32_t dataPos = pos + frameHdrLen;
      if (fSize == 0 || dataPos + fSize > tagEnd) break;

      if (strcmp(id, "TIT2") == 0 || strcmp(id, "TT2") == 0) {
        uint8_t buf[160];
        size_t rd = f.read(buf, min((uint32_t)sizeof(buf), fSize));
        decodeId3Text(buf, rd, entry.title, sizeof(entry.title));
      } else if (strcmp(id, "TPE1") == 0 || strcmp(id, "TP1") == 0) {
        uint8_t buf[128];
        size_t rd = f.read(buf, min((uint32_t)sizeof(buf), fSize));
        decodeId3Text(buf, rd, entry.artist, sizeof(entry.artist));
      } else if (strcmp(id, "TALB") == 0 || strcmp(id, "TAL") == 0) {
        uint8_t buf[128];
        size_t rd = f.read(buf, min((uint32_t)sizeof(buf), fSize));
        decodeId3Text(buf, rd, entry.album, sizeof(entry.album));
      } else if (strcmp(id, "TLEN") == 0 || strcmp(id, "TLE") == 0) {
        uint8_t buf[32];
        size_t rd = f.read(buf, min((uint32_t)sizeof(buf), fSize));
        char numStr[24];
        decodeId3Text(buf, rd, numStr, sizeof(numStr));
        uint32_t ms = (uint32_t)atol(numStr);
        if (ms >= 1000) entry.durationSec = ms / 1000;
      } else if ((strcmp(id, "APIC") == 0 || strcmp(id, "PIC") == 0) &&
                 entry.apicOffset == 0 && fSize > 64) {
        // Cari signature awal JPEG (FF D8 FF) di 128 byte pertama frame APIC
        uint8_t probe[128];
        size_t rd = f.read(probe, min((uint32_t)sizeof(probe), fSize));
        for (size_t k = 0; k + 3 < rd; k++) {
          if (probe[k] == 0xFF && probe[k + 1] == 0xD8 && probe[k + 2] == 0xFF) {
            entry.apicOffset = dataPos + k;
            entry.apicSize   = fSize - k;
            break;
          }
        }
      }

      pos = dataPos + fSize;
    }
  }

  // 2. Fallback ke ID3v1 di 128 byte terakhir jika ada field yang masih kosong
  if ((!entry.title[0] || !entry.artist[0] || !entry.album[0]) && fileSize >= 128) {
    uint8_t v1[128];
    f.seek(fileSize - 128);
    if (f.read(v1, 128) == 128 && v1[0] == 'T' && v1[1] == 'A' && v1[2] == 'G') {
      char tmp[32];
      if (!entry.title[0]) {
        memcpy(tmp, &v1[3], 30); tmp[30] = '\0'; trimTrailingSpaces(tmp);
        strlcpy(entry.title, tmp, sizeof(entry.title));
      }
      if (!entry.artist[0]) {
        memcpy(tmp, &v1[33], 30); tmp[30] = '\0'; trimTrailingSpaces(tmp);
        strlcpy(entry.artist, tmp, sizeof(entry.artist));
      }
      if (!entry.album[0]) {
        memcpy(tmp, &v1[63], 30); tmp[30] = '\0'; trimTrailingSpaces(tmp);
        strlcpy(entry.album, tmp, sizeof(entry.album));
      }
    }
  }

  // 3. Fallback ke nama file jika tag Title kosong (mis. "Coldplay - Yellow.mp3")
  if (!entry.title[0]) {
    char base[96];
    strlcpy(base, baseNameOf(path), sizeof(base));
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';

    // Jika nama file berformat "Artis - Judul", pisahkan otomatis
    char *sep = strstr(base, " - ");
    if (sep && !entry.artist[0]) {
      *sep = '\0';
      strlcpy(entry.artist, base, sizeof(entry.artist));
      strlcpy(entry.title, sep + 3, sizeof(entry.title));
    } else {
      strlcpy(entry.title, base, sizeof(entry.title));
    }
  }

  if (!entry.artist[0]) strlcpy(entry.artist, "Unknown Artist", sizeof(entry.artist));
  if (!entry.album[0]) {
    char dir[64];
    parentDirOf(path, dir, sizeof(dir));
    const char *folder = baseNameOf(dir);
    if (folder && folder[0] && strcasecmp(folder, "music") != 0) {
      strlcpy(entry.album, folder, sizeof(entry.album));
    } else {
      strlcpy(entry.album, "MicroSD Library", sizeof(entry.album));
    }
  }

  // 4. Estimasi durasi dari bitrate / header VBR jika TLEN tidak ada
  if (entry.durationSec == 0) {
    entry.durationSec = estimateMp3DurationSec(f, audioOffset, fileSize);
  }

  f.close();
}

// ---------------------------------------------------------------------------
// Pemindaian Direktori MicroSD Rekursif (Maks. Kedalaman 3 Level)
// ---------------------------------------------------------------------------
static bool isAlreadyIndexed(const char *path)
{
  for (int i = 0; i < trackCount; i++) {
    if (strcasecmp(tracks[i].path, path) == 0) return true;
  }
  return false;
}

static void scanDirectory(const char *dirPath, int depth)
{
  if (depth > 3 || trackCount >= MAX_SD_TRACKS) return;

  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  while (trackCount < MAX_SD_TRACKS) {
    File f = dir.openNextFile();
    if (!f) break;

    const char *rawName = f.name();
    bool isDir = f.isDirectory();

    char fullPath[128];
    if (rawName[0] == '/') {
      strlcpy(fullPath, rawName, sizeof(fullPath));
    } else if (strcmp(dirPath, "/") == 0) {
      snprintf(fullPath, sizeof(fullPath), "/%s", rawName);
    } else {
      snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, rawName);
    }
    f.close();

    // PENTING: ambil pointer base dari fullPath (di stack), BUKAN dari rawName yang sudah di-free oleh f.close()!
    const char *base = baseNameOf(fullPath);
    if (isHiddenOrSystemName(base)) continue;

    if (isDir) {
      scanDirectory(fullPath, depth + 1);
    } else if (endsWithIgnoreCase(base, ".mp3") && !isAlreadyIndexed(fullPath)) {
      parseMp3Metadata(fullPath, tracks[trackCount]);
      trackCount++;
    }
  }

  dir.close();
}

static void sortTracksByTitle()
{
  for (int i = 1; i < trackCount; i++) {
    SdTrackEntry tmp = tracks[i];
    int j = i - 1;
    while (j >= 0 && strcasecmp(tracks[j].title, tmp.title) > 0) {
      tracks[j + 1] = tracks[j];
      j--;
    }
    tracks[j + 1] = tmp;
  }
}

// ---------------------------------------------------------------------------
// Dekoder Cover Art JPEG + Area Averaging + Kontras 1.25x + Floyd-Steinberg
// ---------------------------------------------------------------------------
struct JpegFileSlice {
  File file;
  uint32_t offset;
  uint32_t size;
};

struct JpegDecodeWorkspace {
  JPEGDEC  jpeg;
  uint16_t accSum[COVER_SIZE * COVER_SIZE]; // dipakai ulang sebagai int16_t errBuf saat dithering
  uint8_t  accCnt[COVER_SIZE * COVER_SIZE];
  int      cropX;
  int      cropY;
  int      cropS;
};

static JpegDecodeWorkspace *gWs = nullptr;

static void *jpegOpenSlice(JpegFileSlice *slice, int32_t *pFileSize)
{
  *pFileSize = (int32_t)slice->size;
  slice->file.seek(slice->offset);
  return slice;
}

static void jpegCloseCb(void *pHandle)
{
  (void)pHandle;
}

static int32_t jpegReadCb(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
  JpegFileSlice *s = (JpegFileSlice *)pFile->fHandle;
  if (!s || !s->file) return 0;
  int32_t rem = (int32_t)s->size - pFile->iPos;
  if (rem <= 0) return 0;
  if (iLen > rem) iLen = rem;
  size_t r = s->file.read(pBuf, (size_t)iLen);
  pFile->iPos += (int32_t)r;
  return (int32_t)r;
}

static int32_t jpegSeekCb(JPEGFILE *pFile, int32_t iPosition)
{
  JpegFileSlice *s = (JpegFileSlice *)pFile->fHandle;
  if (!s || !s->file) return 0;
  if (iPosition < 0) iPosition = 0;
  if (iPosition > (int32_t)s->size) iPosition = (int32_t)s->size;
  s->file.seek(s->offset + (uint32_t)iPosition);
  pFile->iPos = iPosition;
  return iPosition;
}

static int jpegDrawCb(JPEGDRAW *pDraw)
{
  if (!gWs) return 0;
  const uint16_t *px = pDraw->pPixels;
  const int w = pDraw->iWidth;
  const int wUsed = (pDraw->iWidthUsed > 0) ? pDraw->iWidthUsed : w;
  const int h = pDraw->iHeight;
  const int cropX = gWs->cropX;
  const int cropY = gWs->cropY;
  const int cropS = gWs->cropS;

  for (int j = 0; j < h; j++) {
    int sy = pDraw->y + j;
    if (sy < cropY || sy >= cropY + cropS) continue;

    int dy0 = (sy - cropY) * COVER_SIZE / cropS;
    int dy1 = (sy - cropY + 1) * COVER_SIZE / cropS;
    if (dy1 <= dy0) dy1 = dy0 + 1;
    if (dy1 > COVER_SIZE) dy1 = COVER_SIZE;

    const uint16_t *row = px + j * w;
    for (int i = 0; i < wUsed; i++) {
      int sx = pDraw->x + i;
      if (sx < cropX || sx >= cropX + cropS) continue;

      // Ekstrak RGB565 -> 8-bit R, G, B -> Luminansi ITU-R BT.601
      uint16_t p = row[i];
      uint32_t r = ((p >> 11) & 0x1F) * 255 / 31;
      uint32_t g = ((p >> 5)  & 0x3F) * 255 / 63;
      uint32_t b = ( p        & 0x1F) * 255 / 31;
      uint8_t yLuma = (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);

      int dx0 = (sx - cropX) * COVER_SIZE / cropS;
      int dx1 = (sx - cropX + 1) * COVER_SIZE / cropS;
      if (dx1 <= dx0) dx1 = dx0 + 1;
      if (dx1 > COVER_SIZE) dx1 = COVER_SIZE;

      for (int dy = dy0; dy < dy1; dy++) {
        int baseIdx = dy * COVER_SIZE;
        for (int dx = dx0; dx < dx1; dx++) {
          int idx = baseIdx + dx;
          if (gWs->accCnt[idx] < 250) {
            gWs->accSum[idx] += yLuma;
            gWs->accCnt[idx]++;
          }
        }
      }
    }
  }
  return 1;
}

static bool decodeJpegSliceToCover(const char *path, uint32_t offset, uint32_t size, uint8_t *coverOut)
{
  JpegFileSlice slice;
  slice.file = SD.open(path, FILE_READ);
  if (!slice.file) return false;

  uint32_t fileSize = slice.file.size();
  if (size == 0 || offset + size > fileSize) {
    if (offset >= fileSize) { slice.file.close(); return false; }
    size = fileSize - offset;
  }
  slice.offset = offset;
  slice.size   = size;

  JpegDecodeWorkspace *ws = (JpegDecodeWorkspace *)malloc(sizeof(JpegDecodeWorkspace));
  if (!ws) {
    slice.file.close();
    return false;
  }
  gWs = ws;

  int32_t dummySize = 0;
  jpegOpenSlice(&slice, &dummySize);

  if (!ws->jpeg.open(&slice, (int)slice.size, jpegCloseCb, jpegReadCb, jpegSeekCb, jpegDrawCb)) {
    gWs = nullptr;
    free(ws);
    slice.file.close();
    return false;
  }

  int srcW = ws->jpeg.getWidth();
  int srcH = ws->jpeg.getHeight();
  if (srcW <= 0 || srcH <= 0) {
    ws->jpeg.close();
    gWs = nullptr;
    free(ws);
    slice.file.close();
    return false;
  }

  // Pilih skala dekode DCT perangkat keras agar ukuran mendekati >= 96x96
  int scaleOpt = 0;
  int div = 1;
  int minDim = min(srcW, srcH);
  if (minDim >= COVER_SIZE * 8)      { scaleOpt = JPEG_SCALE_EIGHTH;  div = 8; }
  else if (minDim >= COVER_SIZE * 4) { scaleOpt = JPEG_SCALE_QUARTER; div = 4; }
  else if (minDim >= COVER_SIZE * 2) { scaleOpt = JPEG_SCALE_HALF;    div = 2; }

  int decW = srcW / div;
  int decH = srcH / div;
  ws->cropS = max(1, min(decW, decH));
  ws->cropX = (decW - ws->cropS) / 2;
  ws->cropY = (decH - ws->cropS) / 2;

  memset(ws->accSum, 0, sizeof(ws->accSum));
  memset(ws->accCnt, 0, sizeof(ws->accCnt));

  ws->jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  bool ok = (ws->jpeg.decode(0, 0, scaleOpt) == 1);
  ws->jpeg.close();
  slice.file.close();

  if (!ok) {
    gWs = nullptr;
    free(ws);
    return false;
  }

  // Hitung rata-rata piksel 96x96 & mean luminansi untuk kontras 1.25x
  // Gunakan ulang buffer accSum sebagai int16_t errBuf
  int16_t *errBuf = (int16_t *)ws->accSum;
  uint32_t totalLuma = 0;
  for (int i = 0; i < COVER_SIZE * COVER_SIZE; i++) {
    uint8_t v = ws->accCnt[i] ? (uint8_t)(ws->accSum[i] / ws->accCnt[i]) : 255;
    errBuf[i] = v;
    totalLuma += v;
  }
  int mean = (int)(totalLuma / (COVER_SIZE * COVER_SIZE));

  for (int i = 0; i < COVER_SIZE * COVER_SIZE; i++) {
    int v = mean + ((errBuf[i] - mean) * 5) / 4;
    errBuf[i] = (int16_t)constrain(v, 0, 255);
  }

  // Floyd-Steinberg Error Diffusion Dithering (bit 1 = hitam, bit 0 = putih)
  memset(coverOut, 0, COVER_BYTES);
  for (int y = 0; y < COVER_SIZE; y++) {
    for (int x = 0; x < COVER_SIZE; x++) {
      int idx = y * COVER_SIZE + x;
      int oldPx = constrain((int)errBuf[idx], 0, 255);
      int newPx = (oldPx < 128) ? 0 : 255;
      int err   = oldPx - newPx;

      if (newPx == 0) {
        coverOut[idx >> 3] |= (0x80 >> (x & 7));
      }

      if (x + 1 < COVER_SIZE)
        errBuf[idx + 1] += (err * 7) >> 4;
      if (y + 1 < COVER_SIZE) {
        if (x > 0)
          errBuf[idx + COVER_SIZE - 1] += (err * 3) >> 4;
        errBuf[idx + COVER_SIZE] += (err * 5) >> 4;
        if (x + 1 < COVER_SIZE)
          errBuf[idx + COVER_SIZE + 1] += (err * 1) >> 4;
      }
    }
  }

  gWs = nullptr;
  free(ws);
  return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
// Bangunkan & sinkronkan ulang kartu MicroSD ke mode SPI Idle (0x01) menggunakan
// hardware SPI (tanpa pernah memanggil pinMode pada SCK/MOSI/MISO agar E-Paper tetap aman).
static uint8_t spiWakeSd()
{
  digitalWrite(EPD_CS, HIGH);
  digitalWrite(SD_CS, HIGH);

  uint8_t r1 = 0xFF;
  SPI.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));

  // 1. Flush hingga >512 byte (600x 0xFF) dengan CS HIGH untuk menyelesaikan
  //    sisa transfer blok jika ESP32 baru saja di-reset di tengah pembacaan.
  for (int i = 0; i < 600; i++) {
    SPI.transfer(0xFF);
  }

  for (int attempt = 0; attempt < 5 && r1 != 0x01; attempt++) {
    digitalWrite(SD_CS, HIGH);
    for (int i = 0; i < 32; i++) SPI.transfer(0xFF);

    digitalWrite(SD_CS, LOW);
    // Tunggu sampai jalur MISO lepas dari status sibuk (0x00 -> 0xFF)
    for (int w = 0; w < 64; w++) {
      if (SPI.transfer(0xFF) == 0xFF) break;
    }

    // Jika pada percobaan sebelumnya kartu masih di Transfer State, kirim CMD12 (STOP_TRANSMISSION)
    if (attempt == 1) {
      SPI.transfer(0x40 | 12);
      SPI.transfer(0x00);
      SPI.transfer(0x00);
      SPI.transfer(0x00);
      SPI.transfer(0x00);
      SPI.transfer(0x61);
      for (int i = 0; i < 32; i++) SPI.transfer(0xFF);
      digitalWrite(SD_CS, HIGH);
      for (int i = 0; i < 16; i++) SPI.transfer(0xFF);
      digitalWrite(SD_CS, LOW);
      for (int w = 0; w < 32; w++) {
        if (SPI.transfer(0xFF) == 0xFF) break;
      }
    }

    // Kirim CMD0 (GO_IDLE_STATE = 0x40 00 00 00 00 95)
    SPI.transfer(0x40);
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0x95);

    // PENTING: Abaikan byte 0xFF dan 0x00 (busy); tunggu sampai byte R1 valid (0x01)
    for (int i = 0; i < 64; i++) {
      uint8_t b = SPI.transfer(0xFF);
      if (b == 0x01) {
        r1 = 0x01;
        break;
      } else if (b != 0xFF && b != 0x00 && (b & 0x80) == 0) {
        r1 = b;
      }
    }

    digitalWrite(SD_CS, HIGH);
    SPI.transfer(0xFF);
    delay(20);
  }

  SPI.endTransaction();
  return r1;
}

bool sdBegin()
{
  // PENTING: Jangan pernah memanggil SPI.end() atau pinMode() pada SCK/MOSI/MISO
  // karena layar E-Paper sudah menggunakan bus SPI yang sama!
  digitalWrite(EPD_CS, HIGH);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  gpio_pullup_en((gpio_num_t)SD_MISO);
  gpio_pullup_en((gpio_num_t)SD_MOSI);
  gpio_pullup_en((gpio_num_t)SD_CS);
  delay(40);

  // Coba mount hingga 4 kali dengan kecepatan konservatif (1 MHz -> 400 kHz)
  // yang sangat stabil di kabel jumper breadboard paralel.
  const uint32_t freqs[4] = { 1000000, 1000000, 400000, 400000 };
  for (int attempt = 0; attempt < 4 && !mounted; attempt++) {
    uint8_t r1 = spiWakeSd();
    Serial.printf("[SD] Percobaan #%d (Wake CMD0 = 0x%02X, SPI = %lu Hz)\n",
                  attempt + 1, r1, (unsigned long)freqs[attempt]);
    mounted = SD.begin(SD_CS, SPI, freqs[attempt]);
    if (!mounted || SD.cardType() == CARD_NONE) {
      mounted = false;
      SD.end();
      gpio_pullup_en((gpio_num_t)SD_MISO);
      delay(50);
    }
  }

  if (!mounted) {
    trackCount = 0;
    Serial.println("[SD] Kartu MicroSD belum terdeteksi (mode lagu demo aktif).");
    return false;
  }

  Serial.printf("[SD] Kartu MicroSD terhubung (%llu MB total, %llu MB terpakai).\n",
                sdGetTotalMB(), sdGetUsedMB());
  sdScanLibrary();
  return true;
}

int sdScanLibrary()
{
  trackCount = 0;
  if (!mounted) return 0;

  // Prioritaskan folder /music jika ada, lalu pindai root "/" untuk lagu di luar folder
  if (SD.exists("/music")) scanDirectory("/music", 1);
  if (SD.exists("/Music")) scanDirectory("/Music", 1);
  scanDirectory("/", 1);

  sortTracksByTitle();

  Serial.printf("[SD] Ditemukan %d lagu MP3 di kartu MicroSD:\n", trackCount);
  for (int i = 0; i < trackCount; i++) {
    Serial.printf("  %2d. %s — %s [%s] (%lus)%s\n",
                  i + 1,
                  tracks[i].title,
                  tracks[i].artist,
                  tracks[i].album,
                  (unsigned long)tracks[i].durationSec,
                  tracks[i].apicOffset ? " [Cover: APIC]" : "");
  }
  return trackCount;
}

bool sdIsMounted()
{
  return mounted;
}

uint64_t sdGetTotalMB()
{
  return mounted ? (SD.totalBytes() / (1024ULL * 1024ULL)) : 0;
}

uint64_t sdGetUsedMB()
{
  return mounted ? (SD.usedBytes() / (1024ULL * 1024ULL)) : 0;
}

int sdGetTrackCount()
{
  return trackCount;
}

const SdTrackEntry &sdGetTrack(int index)
{
  static const SdTrackEntry empty = {};
  if (index < 0 || index >= trackCount) return empty;
  return tracks[index];
}

bool sdLoadLyrics(const SdTrackEntry &track)
{
  lyricsReset();
  if (!mounted || !track.path[0]) return false;

  const char *exts[] = { ".lrc", ".LRC" };
  char lrcPath[140];

  // 1. Cek file .lrc dengan nama & folder yang sama dengan file .mp3
  for (const char *ext : exts) {
    replaceExtension(track.path, ext, lrcPath, sizeof(lrcPath));
    if (SD.exists(lrcPath)) {
      File f = SD.open(lrcPath, FILE_READ);
      if (f) {
        int n = lyricsLoadFromStream(f);
        f.close();
        Serial.printf("[SD] Lirik dimuat dari %s (%d baris)\n", lrcPath, n);
        return n > 0;
      }
    }
  }

  // 2. Cek di folder "/lyrics/<nama_file>.lrc"
  char baseLrc[96];
  replaceExtension(baseNameOf(track.path), ".lrc", baseLrc, sizeof(baseLrc));
  snprintf(lrcPath, sizeof(lrcPath), "/lyrics/%s", baseLrc);
  if (SD.exists(lrcPath)) {
    File f = SD.open(lrcPath, FILE_READ);
    if (f) {
      int n = lyricsLoadFromStream(f);
      f.close();
      Serial.printf("[SD] Lirik dimuat dari %s (%d baris)\n", lrcPath, n);
      return n > 0;
    }
  }

  return false;
}

bool sdLoadCover(const SdTrackEntry &track, uint8_t *coverOut)
{
  if (!mounted || !track.path[0] || !coverOut) return false;

  // 1. Cek file gambar "<nama_lagu>.jpg" atau "<nama_lagu>.jpeg" di sebelah file .mp3
  const char *imgExts[] = { ".jpg", ".JPG", ".jpeg", ".JPEG" };
  char imgPath[140];
  for (const char *ext : imgExts) {
    replaceExtension(track.path, ext, imgPath, sizeof(imgPath));
    if (SD.exists(imgPath) && decodeJpegSliceToCover(imgPath, 0, 0, coverOut)) {
      Serial.printf("[SD] Cover dimuat dari %s\n", imgPath);
      return true;
    }
  }

  // 2. Cek gambar JPEG embedded (ID3v2 APIC) di dalam file .mp3 itu sendiri
  if (track.apicOffset > 0 && track.apicSize > 64) {
    if (decodeJpegSliceToCover(track.path, track.apicOffset, track.apicSize, coverOut)) {
      Serial.printf("[SD] Cover dimuat dari tag ID3 APIC (%u byte)\n", (unsigned)track.apicSize);
      return true;
    }
  }

  // 3. Cek "cover.jpg" atau "folder.jpg" di folder tempat lagu berada
  char dir[128];
  parentDirOf(track.path, dir, sizeof(dir));
  const char *folderCovers[] = { "cover.jpg", "Cover.jpg", "folder.jpg", "front.jpg" };
  for (const char *fc : folderCovers) {
    if (strcmp(dir, "/") == 0) snprintf(imgPath, sizeof(imgPath), "/%s", fc);
    else snprintf(imgPath, sizeof(imgPath), "%s/%s", dir, fc);
    if (SD.exists(imgPath) && decodeJpegSliceToCover(imgPath, 0, 0, coverOut)) {
      Serial.printf("[SD] Cover dimuat dari %s\n", imgPath);
      return true;
    }
  }

  return false;
}

