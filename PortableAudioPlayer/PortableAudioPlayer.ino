// PortableAudioPlayer.ino — Firmware Utama Portable Audio Player (Walkman E-Paper)
//
// FASE 1 (UI & Simulated Playhead):
//   - Berjalan di ESP32 DOIT DevKit V1 maupun ESP32-WROVER-DEV + E-Paper WeAct 2.9" SSD1680.
//   - Menampilkan Now Playing (Cover Art 96x96 + Synced Lyrics LRC + Progress Bar),
//     Library Menu 6 baris (All Tracks, Artists, Albums, Audio Output, Play Mode, About),
//     dan Volume Overlay Popup.
//   - Dikendalikan via Serial Monitor (115200 baud) selama menunggu komponen
//     Rotary Encoder KY-040, Push Button, MicroSD, dan DAC Audio tiba.
//
// Pin Kabel E-Paper 2.9" SSD1680 (lihat pins.h):
//   VCC  -> 3V3
//   GND  -> GND
//   SCL  -> GPIO 18
//   SDA  -> GPIO 23
//   CS   -> GPIO 5
//   DC   -> GPIO 21  (dipindah dari 17 agar aman untuk PSRAM WROVER)
//   RES  -> GPIO 22  (dipindah dari 16 agar aman untuk PSRAM WROVER)
//   BUSY -> GPIO 34  (dipindah dari 4  agar GPIO 4 dipakai Shutdown Speaker Amp)

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "pins.h"
#include "display.h"
#include "lyrics.h"
#include "player.h"
#include "input.h"
#include "ui.h"

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=======================================================");
  Serial.printf("  %s — %s\n", FW_NAME, FW_VERSION);
  Serial.println("=======================================================");

  // Matikan radio Wi-Fi di mode offline untuk menghemat daya & suhu LDO
  WiFi.mode(WIFI_OFF);

  // 1. Inisialisasi MicroSD terlebih dahulu (persis seperti saat berhasil membaca 3 lagu)
  playerBegin();

  // 2. Inisialisasi layar E-Paper & tampilkan splash screen
  displayBegin();
  displaySplash();

  inputBegin();
  uiBegin();
}

void loop()
{
  // 1. Update posisi playhead lagu & auto-advance
  playerTick();

  // 2. Baca semua antrean event input (supaya beberapa klik cepat langsung digabung)
  while (true) {
    InputEvent evt = inputPoll();
    if (evt == EVT_NONE) break;
    uiHandleInput(evt);
  }

  // 3. Render perubahan ke layar E-Paper
  uiTick();

  delay(2);
}

