// input.cpp — Pembacaan Input Keyboard Serial Monitor (Fase 1) +
//             Interrupt Rotary Encoder KY-040 & 3 Push Button (Fase 2).

#include "input.h"
#include "pins.h"
#include "config.h"

// Aktifkan pembacaan hardware Rotary Encoder KY-040 & 3 Push Button (Fase 2)
#ifndef ENABLE_HARDWARE_INPUT
#define ENABLE_HARDWARE_INPUT 1
#endif

static volatile int32_t encDelta = 0;

#if ENABLE_HARDWARE_INPUT
// Tabel state transisi quadrature encoder (anti-bounce tanpa delay)
static volatile uint8_t encState = 0;
static void IRAM_ATTR onEncoderISR()
{
  static const int8_t TRANS[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };
  uint8_t clk = (uint8_t)digitalRead(ENC_CLK);
  uint8_t dt  = (uint8_t)digitalRead(ENC_DT);
  encState = ((encState << 2) | (clk << 1) | dt) & 0x0F;
  encDelta += TRANS[encState];
}

struct DebouncedBtn {
  uint8_t pin;
  InputEvent evt;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeMs;
};

static DebouncedBtn buttons[] = {
  { ENC_SW,   EVT_ENC_CLICK, HIGH, HIGH, 0 },
  { BTN_PREV, EVT_BTN_PREV,  HIGH, HIGH, 0 },
  { BTN_NEXT, EVT_BTN_NEXT,  HIGH, HIGH, 0 },
  { BTN_MENU, EVT_BTN_MENU,  HIGH, HIGH, 0 },
};
#endif

void inputPrintHelp()
{
  Serial.println("\n=======================================================");
  Serial.println("  KONTROL SIMULASI SERIAL MONITOR (Ketik lalu Enter)");
  Serial.println("=======================================================");
  Serial.println("  s / + / ]   : Putar Knob KANAN (Scroll Bawah / Vol+)");
  Serial.println("  w / - / [   : Putar Knob KIRI  (Scroll Atas  / Vol-)");
  Serial.println("  e / Spasi   : Klik Tengah Knob (Play/Pause / Pilih OK)");
  Serial.println("  n           : Tombol [ NEXT >> ]");
  Serial.println("  p           : Tombol [ << PREV ]");
  Serial.println("  m / b       : Tombol [ MENU / BACK ]");
  Serial.println("  h / ?       : Tampilkan panduan tombol ini");
  Serial.println("=======================================================\n");
}

void inputBegin()
{
#if ENABLE_HARDWARE_INPUT
  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT, INPUT);
  pinMode(ENC_SW, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_MENU, INPUT_PULLUP);

  encState = ((digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT)) & 0x03;
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), onEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT), onEncoderISR, CHANGE);
#endif
  inputPrintHelp();
}

InputEvent inputPoll()
{
  // 1. Cek input dari Serial Monitor (aktif di Fase 1 maupun Fase 2 untuk debugging)
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') continue;

    switch (c) {
      case 's': case 'S': case '+': case '=': case ']': case 'd': case 'D':
        return EVT_ENC_CW;
      case 'w': case 'W': case '-': case '_': case '[': case 'a': case 'A':
        return EVT_ENC_CCW;
      case 'e': case 'E': case ' ': case 'o': case 'O':
        return EVT_ENC_CLICK;
      case 'n': case 'N': case '>': case '.':
        return EVT_BTN_NEXT;
      case 'p': case 'P': case '<': case ',':
        return EVT_BTN_PREV;
      case 'm': case 'M': case 'b': case 'B': case 27: // ESC
        return EVT_BTN_MENU;
      case 'h': case 'H': case '?':
        inputPrintHelp();
        break;
      default:
        break;
    }
  }

#if ENABLE_HARDWARE_INPUT
  // 2. Cek putaran Rotary Encoder (1 detent = 2 atau 4 step quadrature)
  noInterrupts();
  int32_t d = encDelta;
  if (d >= 2) {
    encDelta -= 2;
    interrupts();
    Serial.println("[Input] Knob Putar KANAN (CW)");
    return EVT_ENC_CW;
  } else if (d <= -2) {
    encDelta += 2;
    interrupts();
    Serial.println("[Input] Knob Putar KIRI (CCW)");
    return EVT_ENC_CCW;
  }
  interrupts();

  // 3. Cek klik tombol dengan debounce 35 ms
  unsigned long now = millis();
  for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
    bool r = (bool)digitalRead(buttons[i].pin);
    if (r != buttons[i].lastReading) {
      buttons[i].lastChangeMs = now;
      buttons[i].lastReading = r;
    }
    if ((now - buttons[i].lastChangeMs) >= 35 && r != buttons[i].stableState) {
      buttons[i].stableState = r;
      if (r == LOW) { // Active LOW
        Serial.printf("[Input] Tombol Ditekan (GPIO %u)\n", buttons[i].pin);
        return buttons[i].evt;
      }
    }
  }
#endif

  return EVT_NONE;
}

