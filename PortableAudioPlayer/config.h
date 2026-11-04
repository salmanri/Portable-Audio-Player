// config.h — Pengaturan umum Portable Audio Player
#pragma once

#define FW_NAME     "Portable Audio Player"
#define FW_VERSION  "v0.1 (Fase 1 - UI Simulasi)"

// Volume (0..100)
#define VOLUME_DEFAULT      50
#define VOLUME_STEP         5
#define VOLUME_OVERLAY_MS   1500   // lama bar volume tampil setelah knob diputar

// E-paper: full refresh berkala untuk membersihkan ghosting
#define FULL_REFRESH_AFTER_PARTIALS 120

// Tunggu input "tenang" sebelum menggambar ulang, supaya putaran knob yang
// cepat (misal scroll 5 baris) cukup digambar 1x di posisi akhir.
#define INPUT_SETTLE_MS     90

// Geser timing lirik (ms). Positif = lirik muncul lebih awal.
#define LYRIC_LEAD_MS       0

// Tekan "kembali ke awal lagu" jika Prev ditekan setelah detik ke-3
#define PREV_RESTART_MS     3000

