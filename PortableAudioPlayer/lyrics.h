// lyrics.h — Parser & sinkronisasi lirik LRC lokal (Offline)
// Diadaptasi dari SpotifyDesk V2 untuk membaca file .lrc dari MicroSD
// maupun string LRC di memori (mode simulasi Fase 1).

#pragma once
#include <Arduino.h>
#include <Stream.h>

constexpr int MAX_LYRIC_LINES = 160;
constexpr int LYRIC_TEXT_LEN  = 72;

struct LyricLine {
  uint32_t time_ms;
  char text[LYRIC_TEXT_LEN];
};

// Kosongkan seluruh baris lirik di memori (thread-safe).
void lyricsReset();

// Muat lirik dari teks string LRC di RAM/Flash (untuk simulasi Fase 1 atau ID3 tag).
// Return jumlah baris yang berhasil dimuat.
int lyricsLoadFromText(const char *lrcText);

// Muat lirik dari Stream (misal objek File dari MicroSD pada Fase 3).
// Return jumlah baris yang berhasil dimuat.
int lyricsLoadFromStream(Stream &stream);

// Jumlah baris lirik yang sedang dimuat.
int lyricsGetLineCount();

// Ambil baris lirik yang aktif pada posisi waktu positionMs (dengan Binary Search O(log N)).
// Jika sedang jeda instrumental panjang, out akan berisi string kosong "" (layar menggambar ♪).
void lyricsGetCurrent(uint32_t positionMs, int leadMs, char *out, size_t outLen);

// Ambil waktu (ms) pergantian baris lirik berikutnya (untuk lookahead partial refresh).
uint32_t lyricsGetNextChangeMs(uint32_t positionMs, int leadMs);

