# Waveshare ESP32-S3-Touch-LCD-7B Tello Drone Bridge & HUD Hardware Guide

**Folder:** `esp32_tello/`  
**Sketch:** `esp32_tello.ino`  
**Hardware:** Waveshare ESP32-S3-Touch-LCD-7B (7.0" 1024×600 High-Definition Capacitive Touchscreen, GT911 Touch Controller, CH422G IO Expander)

---

## 🌟 Overview

The **Waveshare ESP32-S3-Touch-LCD-7B Tello Bridge & HUD** firmware transforms the Waveshare ESP32-S3 7.0-inch 1024×600 HD Touch LCD into an intelligent flight command bridge, dynamic HUD visualizer, and flight telemetry dashboard for the DJI Tello drone.

Instead of the PC communicating over raw UDP sockets directly to the drone, all commands from:
1. **The Thinker Window (PC Tkinter GUI Tab)**
2. **Gemini Multimodal Live Voice & Vision AI**
3. **The 7-Inch Capacitive Touchscreen Dashboard**

are routed through the ESP32-S3 screen. The ESP32 manages the WiFi connection and UDP sockets to the Tello drone, renders real-time dynamic flight animations on the 1024×600 display, and reports live flight telemetry back to the PC.

---

## 🚀 Key Features

### 1. 7.0-Inch 1024×600 HD Touchscreen Dashboard
- **GT911 5-Point Capacitive Touch** interface (I2C: `0x5D`).
- **Onboard CH422G IO Expander**: Backlight (`EXIO2`), LCD Power (`EXIO6`), Touch Reset (`EXIO1`).
- **Flight Essentials**:
  - `[🛫 TAKEOFF]` / `[🛬 LAND]` / `[🚨 EMERGENCY]` / `[📡 CONNECT/SDK]` / `[🔋 BATTERY?]`
- **Directional D-Pad**:
  - `[▲ FORWARD]`, `[▼ BACK]`, `[◄ LEFT]`, `[► RIGHT]`, `[⬆ UP]`, `[⬇ DOWN]`
- **Yaw & Rotation**:
  - `[↺ CCW 90°]`, `[↻ CW 90°]`
- **Acrobatic Stunts**:
  - `[FLIP FWD]`, `[FLIP BCK]`, `[FLIP LEFT]`, `[FLIP RIGHT]`
- **Distance Step Selector**:
  - `[20 cm (8")]`, `[50 cm (20")]`, `[100 cm (40")]`
- **Choreography Flight Routines**:
  - `[🔲 SQUARE PATROL]`, `[🌀 360 SCAN]`, `[🎈 BOUNCE WAVE]`

### 2. Dynamic HUD Animations Engine (~30-50 FPS)
- **Animated Quadcopter Mascot**: Central drone avatar with 4 spinning rotor blades that spin faster during takeoff and maneuvers.
- **Navigation Strobes**: Flashing port (red), starboard (green), and tail (white/blue) beacon lights.
- **Pulsing Energy Shield Aura**: Glows and expands during flight maneuvers.
- **Artificial Horizon**: Dynamic pitch & roll attitude visualizer.
- **Altitude Gauge Tape**: Real-time altitude gauge in centimeters and inches with ascending/descending motion.
- **Battery Gauge**: Real-time percentage meter with color-coded warning alert states.
- **Command & Status Banner**: Live display of incoming commands and status responses from PC, AI, and Touchscreen.

### 3. Dual-Channel Host Serial Protocol
- USB CDC Serial (115200 baud) and Bluetooth Classic / BLE for PC Thinker Window and Gemini AI integration.

### 4. MAX98357A I2S Mono Audio Amplifier Subsystem
- Real-time procedural audio synthesis engine for flight sound effects (Turbine takeoff spool-up, Landing spool-down & touchdown, Aerodynamic flip whooshes, HUD radar pings, Low battery alarms, Emergency sirens, and touch clicks).

---

## 🔌 Hardware Pinout (Waveshare ESP32-S3-Touch-LCD-7B)

### A. Core Interface Pinout

| Function | Pin / GPIO | Description |
| :--- | :--- | :--- |
| **I2C SDA** | `GPIO 8` | Shared I2C Data (PH2.0 4-Pin Header) |
| **I2C SCL** | `GPIO 9` | Shared I2C Clock (PH2.0 4-Pin Header) |
| **TP_INT** | `GPIO 4` | GT911 Touch Interrupt |
| **TP_RST** | `EXIO1` (CH422G) | Touch Reset Line (Active High) |
| **Backlight (DISP)** | `EXIO2` (CH422G) | LCD Backlight Enable (Active High) |
| **LCD Power (LCD_VDD_EN)** | `EXIO6` (CH422G) | LCD Power Rail Enable (Active High) |
| **LCD RGB** | `RGB565` | 1024×600 7.0-inch 16-bit parallel interface |
| **I2S BCLK** | `GPIO 19` | I2S Bit Clock to MAX98357A |
| **I2S LRC** | `GPIO 20` | I2S Word Select (WS/LRC) to MAX98357A |
| **I2S DOUT** | `GPIO 21` | I2S Serial Data Out (DIN) to MAX98357A |
| **USB CDC** | `USB Native / Type-C` | Host Serial PC Link (115200 baud) |

### B. MAX98357A I2S Audio Amplifier Wiring

```
[ Waveshare ESP32-S3-Touch-LCD-7B ]            [ MAX98357A I2S Amp ]
  ├── 5V (or 3.3V) ───────────────────────────────> VIN (5V recommended for 3.2W)
  ├── GND ────────────────────────────────────────> GND
  ├── GPIO 19 (I2S BCLK) ─────────────────────────> BCLK
  ├── GPIO 20 (I2S LRC / WS) ─────────────────────> LRC
  └── GPIO 21 (I2S DOUT) ─────────────────────────> DIN
                                                    GAIN ───> Open (9dB Default) or GND (6dB)
                                                    SD_MODE > Open / Float (L+R Mono Mix)
                                                    [ + / - ] ──> 4Ω - 8Ω 2W-3W Speaker
```

---

## 📡 Networking & UDP Ports

| Channel | Destination / Port | Protocol | Purpose |
| :--- | :--- | :--- | :--- |
| **Control Commands** | `192.168.10.1:8889` | UDP | Sends SDK command strings (`takeoff`, `land`, `forward 50`, `flip f`) |
| **State Telemetry** | `0.0.0.0:8890` | UDP | Receives live Tello state telemetry string (battery, altitude, roll, pitch, yaw) |
| **Video Stream (Optional)** | `0.0.0.0:11111` | UDP (H.264) | Tello live FPV video stream |

---

## 🛠️ Arduino IDE Flashing Guide

1. Open `esp32_tello.ino` in Arduino IDE.
2. In **Tools > Board**, select **ESP32S3 Dev Module**.
3. Set the following board options:
   - **USB CDC On Boot**: `Enabled`
   - **CPU Frequency**: `240MHz (WiFi)`
   - **Flash Mode**: `QIO 80MHz`
   - **Flash Size**: `16MB (128Mb)`
   - **Partition Scheme**: `16M Flash (3MB APP/9.9MB FATFS)`
   - **PSRAM**: `OPI PSRAM`
4. Connect the USB-C cable to the **USB** port of the Waveshare ESP32-S3-Touch-LCD-7 board.
5. Click **Upload**.

---

## 💻 Serial Command Reference (PC & Gemini AI)

| Command Format | Example | Description |
| :--- | :--- | :--- |
| `TELLO:<cmd>` | `TELLO:takeoff` | Direct Tello SDK command routed through the ESP32 |
| `TELLO:up <cm>` | `TELLO:up 50` | Climb altitude by specified distance (cm) |
| `TELLO:forward <cm>` | `TELLO:forward 100` | Move forward by specified distance (cm) |
| `TELLO:cw <deg>` | `TELLO:cw 90` | Rotate clockwise by degrees (1-360) |
| `TELLO:flip <dir>` | `TELLO:flip f` | Perform flip (`f`, `b`, `l`, `r`) |
| `ROUTINE:<name>` | `ROUTINE:SQUARE` | Start autonomous choreography routine (`square`, `scan360`, `bounce`) |
| `status` / `ping` | `status` | Query current flight state, battery, and altitude |
| `AUDIO:TAKEOFF` / `PLAY:TAKEOFF` | `AUDIO:TAKEOFF` | Plays turbine spool-up frequency sweep sound |
| `AUDIO:LAND` / `PLAY:LAND` | `AUDIO:LAND` | Plays descending spool-down + touchdown sound |
| `AUDIO:FLIP` / `PLAY:FLIP` | `AUDIO:FLIP` | Plays high-speed aerodynamic flip whoosh |
| `AUDIO:RADAR` / `PLAY:RADAR` | `AUDIO:RADAR` | Plays HUD radar sonar ping resonance |
| `AUDIO:ALARM` / `PLAY:ALARM` | `AUDIO:ALARM` | Plays low battery warning beep sequence |
| `AUDIO:SIREN` / `PLAY:SIREN` | `AUDIO:SIREN` | Plays emergency siren sweep |
| `AUDIO:CONNECT` / `PLAY:CONNECT` | `AUDIO:CONNECT` | Plays WiFi connection handshake chime |
| `AUDIO:CLICK` / `PLAY:CLICK` | `AUDIO:CLICK` | Plays UI button click tone |
| `AUDIO:TONE:<f>:<ms>` | `AUDIO:TONE:1000:100` | Plays sine tone at frequency `f` (Hz) for `ms` |
| `AUDIO:VOL:<0-100>` | `AUDIO:VOL:80` | Sets MAX98357A audio volume percentage |
| `AUDIO:MUTE:<1\|0>` | `AUDIO:MUTE:1` | Mutes or unmutes audio output |
