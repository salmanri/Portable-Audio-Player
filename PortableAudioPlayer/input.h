// input.h — Abstraksi Input Kontrol (Serial Monitor di Fase 1 + Rotary Encoder & Tombol di Fase 2)
#pragma once
#include <Arduino.h>

enum InputEvent : uint8_t {
  EVT_NONE = 0,
  EVT_ENC_CW,        // Putar Knob Kanan (Scroll Bawah / Volume Naik)
  EVT_ENC_CCW,       // Putar Knob Kiri  (Scroll Atas  / Volume Turun)
  EVT_ENC_CLICK,     // Klik Tengah Knob (Play/Pause / Pilih Menu OK)
  EVT_BTN_PREV,      // Tombol Fisik [Prev]
  EVT_BTN_NEXT,      // Tombol Fisik [Next]
  EVT_BTN_MENU,      // Tombol Fisik [Menu / Back]
};

void inputBegin();
InputEvent inputPoll();
void inputPrintHelp();

