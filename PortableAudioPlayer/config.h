// config.h — Pengaturan umum Portable Audio Player
#pragma once

#define FW_NAME        "Portable Audio Player"
#define FW_VERSION     "0.4.0"

// Kapasitas daftar lagu dari MicroSD di RAM
#define MAX_SD_TRACKS  48

// Identitas perangkat yang tampil di layar splash & About (boleh diganti sesuka hati)
#define DEVICE_MODEL   "PA-01"
#define DEVICE_TAGLINE "PORTABLE AUDIO PLAYER"

// Volume (0..100)
#define VOLUME_DEFAULT      50
#define VOLUME_STEP         1
#define VOLUME_OVERLAY_MS   1500   // lama bar volume tampil setelah knob diputar

// E-paper: full refresh berkala untuk membersihkan ghosting
#define FULL_REFRESH_AFTER_PARTIALS 120

// Tunggu input "tenang" sebelum menggambar ulang, supaya putaran knob yang
// cepat (misal scroll birkaç baris) cukup digambar 1x di posisi akhir.
#define INPUT_SETTLE_MS     45

// Geser timing lirik (ms). Positif = lirik muncul lebih awal.
#define LYRIC_LEAD_MS       0

// Tekan "kembali ke awal lagu" jika Prev ditekan setelah detik ke-3
#define PREV_RESTART_MS     3000

