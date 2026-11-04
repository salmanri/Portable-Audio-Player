// pins.h — Pin map Portable Audio Player
//
// Dirancang AMAN untuk dua board sekaligus (kode & kabel tidak perlu diubah
// saat pindah board):
//   - ESP32 DOIT DevKit V1 (ESP-WROOM-32)      -> board prototipe saat ini
//   - ESP32-WROVER-DEV (ESP32-WROVER-E, 8MB PSRAM) -> board final
//
// Pin yang SENGAJA TIDAK DIPAKAI:
//   GPIO 6-11  : terhubung ke SPI Flash internal
//   GPIO 16-17 : dipakai PSRAM pada modul WROVER
//   GPIO 1, 3  : UART0 (upload & Serial Monitor)
//   GPIO 0     : tombol BOOT
//
// Catatan GPIO 34-39: INPUT ONLY & tanpa pull-up internal.
//   - BUSY e-paper digerakkan aktif oleh panel -> aman.
//   - CLK/DT KY-040 sudah punya resistor pull-up 10k di modulnya -> aman
//     (pin "+" KY-040 WAJIB ke 3V3, bukan 5V).

#pragma once

// ---------------------------------------------------------------------------
// E-Paper WeAct 2.9" SSD1680 — bus VSPI
// ---------------------------------------------------------------------------
#define EPD_SCK    18  // -> SCL
#define EPD_MOSI   23  // -> SDA
#define EPD_CS      5  // -> CS
#define EPD_DC     21  // -> DC   (dulu GPIO 17 di SpotifyDesk)
#define EPD_RST    22  // -> RES  (dulu GPIO 16 di SpotifyDesk)
#define EPD_BUSY   34  // -> BUSY (dulu GPIO 4 di SpotifyDesk)

// ---------------------------------------------------------------------------
// MicroSD — berbagi bus VSPI dengan E-Paper (dipakai mulai Fase 3)
// ---------------------------------------------------------------------------
#define SD_SCK     18  // berbagi VSPI SCK
#define SD_MOSI    23  // berbagi VSPI MOSI
#define SD_MISO    19  // VSPI MISO
#define SD_CS      15  // Chip Select khusus MicroSD

// ---------------------------------------------------------------------------
// Audio I2S -> PCM5102A (jack 3.5mm) & MAX98357A (speaker) paralel (Fase 4)
// ---------------------------------------------------------------------------
#define I2S_BCLK   26
#define I2S_LRCK   25
#define I2S_DOUT   27
#define AMP_SD      4  // pin SD MAX98357A: LOW = speaker mati, HIGH = speaker nyala

// ---------------------------------------------------------------------------
// Kontrol fisik (Fase 2) — Semua tombol memakai pin bebas dengan INPUT_PULLUP
// ---------------------------------------------------------------------------
#define ENC_CLK    35  // KY-040 CLK (pull-up 10k bawaan modul KY-040)
#define ENC_DT     39  // KY-040 DT  (label VN di board, pull-up 10k bawaan KY-040)
#define ENC_SW     32  // KY-040 SW  (INPUT_PULLUP internal ESP32)

#define BTN_PREV   33  // Tombol [ << PREV ]      (INPUT_PULLUP, kaki lain ke GND)
#define BTN_NEXT   14  // Tombol [ NEXT >> ]      (INPUT_PULLUP, kaki lain ke GND)
#define BTN_MENU   13  // Tombol [ MENU / BACK ]  (INPUT_PULLUP, kaki lain ke GND)

// ---------------------------------------------------------------------------
// Baterai (Fase 6) — via pembagi tegangan 100k/100k
// ---------------------------------------------------------------------------
#define BAT_ADC    36  // label VP di board

