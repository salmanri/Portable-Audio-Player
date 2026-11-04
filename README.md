# 🎧 Portable Audio Player (Walkman E-Paper DAP)

Standalone Offline Digital Audio Player (DAP) inspired by the classic **Sony Walkman** and **MusicStation v2**, built with **ESP32 / ESP32-WROVER (8MB PSRAM)** and a **2.9" WeAct Studio E-Paper Display (`SSD1680`, 296×128)**.

---

## ✨ Key Features

- **Retro E-Paper Now Playing UI**:
  - Crisp `96×96` 1-bit monochrome album cover art with Floyd-Steinberg error diffusion dithering.
  - Real-time millisecond-synced `.lrc` lyrics (auto-wrapped, center-aligned, with `♪` musical note indicator during instrumental solos).
  - Smooth 1-second transport progress bar & retro **Volume Overlay Popup**.
- **MusicStation-Style Library Browser**:
  - 6-row inverted selection bar menu (`Now Playing`, `All Tracks`, `Artists`, `Albums`, `Audio Output`, `Play Mode`, `About`).
  - Fast partial refresh (`~250 ms`) with input coalescing (`INPUT_SETTLE_MS`) for responsive Rotary Encoder scrolling.
- **Triple Audio Output Architecture**:
  1. 🎧 **3.5mm Stereo Headphone Jack** via 32-bit I2S Hi-Fi DAC (`PCM5102A`).
  2. 🔊 **Internal Mini Speaker** via 3W Class-D I2S Amplifier (`MAX98357A` with hardware shutdown control).
  3. 📡 **Bluetooth Wireless Audio (`A2DP Source`)** for TWS earbuds and Bluetooth speakers.
- **Tactile Hardware Controls**:
  - **1× Rotary Encoder Knob (`KY-040` / `EC11`)**: Rotate for menu scrolling / volume control, click for Play/Pause/OK.
  - **3× Physical Push Buttons**: `[ ⏮ Prev ]`, `[ ⏭ Next ]`, and `[ ☰ Menu / Back ]`.

---

## 🔌 Pin Map (`pins.h`)

Designed to be **100% compatible** with both **ESP32 DOIT DevKit V1** and **ESP32-WROVER-DEV (8MB PSRAM)** (`GPIO 16` & `GPIO 17` are kept free for WROVER PSRAM):

| Peripheral | Pin Name | ESP32 GPIO | Notes |
| :--- | :--- | :---: | :--- |
| **E-Paper 2.9" (VSPI)** | `VCC` / `GND` | `3V3` / `GND` | **3.3V only!** Never connect to 5V |
| | `SCL` (SCK) | `GPIO 18` | Hardware VSPI Clock |
| | `SDA` (MOSI) | `GPIO 23` | Hardware VSPI MOSI |
| | `CS` | `GPIO 5` | Chip Select |
| | `DC` | `GPIO 21` | Moved from GPIO 17 (PSRAM-safe) |
| | `RES` (RST) | `GPIO 22` | Moved from GPIO 16 (PSRAM-safe) |
| | `BUSY` | `GPIO 34` | Input-only pin (driven by SSD1680) |
| **MicroSD Card (HSPI)** | `SCK` / `MOSI` / `MISO` / `CS` | `14` / `13` / `19` / `15` | Dedicated SPI bus (zero display contention) |
| **I2S Audio (`PCM5102A` + `MAX98357A`)** | `BCLK` / `LRCK` / `DOUT` | `26` / `25` / `27` | Shared digital I2S bus |
| | `AMP_SD` | `GPIO 4` | `MAX98357A` Shutdown (`HIGH` = Speaker ON) |
| **Rotary Encoder (`KY-040`)** | `CLK` / `DT` / `SW` | `35` / `39` / `32` | `KY-040` `+` pin must connect to `3V3` |
| **Push Buttons** | `PREV` / `NEXT` / `MENU` | `33` / `12` / `2` | Active-LOW (`INPUT_PULLUP`) |
| **Battery Monitor** | `BAT_ADC` | `GPIO 36` | Voltage divider (Phase 6) |

---

## 🚀 Phase 1 Quick Start (UI & Simulated Playhead on ESP32 DOIT)

Before the audio DAC, MicroSD module, and buttons arrive, you can run and test the complete UI on your existing **ESP32 DOIT DevKit V1 + 2.9" E-Paper**:

1. **Move 3 E-Paper Wires**:
   - `DC` → **`GPIO 21`**
   - `RES` → **`GPIO 22`**
   - `BUSY` → **`GPIO 34`**
   *(Keep `SCL → 18`, `SDA → 23`, `CS → 5`, `VCC → 3V3`, `GND → GND`).*
2. **Open & Upload in Arduino IDE**:
   - Open [`PortableAudioPlayer/PortableAudioPlayer.ino`](PortableAudioPlayer/PortableAudioPlayer.ino).
   - Select Board: **DOIT ESP32 DEVKIT V1** and click **Upload**.
3. **Test Controls via Serial Monitor (`115200 baud`)**:
   - `s` or `+` : Rotate Knob Right (Scroll Down / Volume +)
   - `w` or `-` : Rotate Knob Left (Scroll Up / Volume -)
   - `e` or `Space` : Click Knob (Play/Pause or Select Menu Item)
   - `n` : Next Track (`⏭`)
   - `p` : Previous Track (`⏮`)
   - `m` : Open / Close Walkman Menu (`☰`)

