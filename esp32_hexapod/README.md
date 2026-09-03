# Waveshare ESP32-S3-Touch-LCD-7C 6-Leg Hexapod Hardware & Telemetry Guide

This document describes the hardware configuration, pinout mapping, wiring diagrams, and flashing instructions for the 6-leg Hexapod robot running on the **Waveshare ESP32-S3-Touch-LCD-7C (7.0-inch 1024×600 High-Definition Capacitive Touchscreen)** module (Version C), substituted for the standard ESP 32 Devkit.

---

## 1. Overview & 7C Specifications

| Feature | Legacy ESP 32 DevKit | **Waveshare ESP32-S3-Touch-LCD-7C (Version C)** |
| :--- | :--- | :--- |
| **Display Panel** | None | **7.0-inch 1024×600 High-Definition IPS RGB Display** |
| **Touch Controller** | None | **Goodix GT911 5-Point Capacitive Multi-Touch (I2C: `0x5D`)** |
| **IO Expander** | None | **WCH CH422G (Addresses `0x24`/`0x38`) for Backlight, LCD Power & Touch Reset** |
| **Onboard Controls** | None | **1024×600 HD Widescreen Touch Buttons (Stand, Walk, Dance, Gestures, Speed)** |
| **Telemetry & Visuals** | Serial logs | **Live Telemetry, Joint Angles, Speed & Animated Robot Face** |
| **Audio Output** | None | **MAX98357A I2S Mono Audio Amplifier (BCLK=19, LRC=20, DIN=21)** |
| **Microcontroller** | ESP32-WROOM | **ESP32-S3-WROOM-1 (16MB Flash, 8MB Octal PSRAM @ 240MHz)** |
| **I2C Bus for Servos** | GPIO 21 / 22 | **GPIO 8 (SDA) / GPIO 9 (SCL) via PH2.0 4-Pin I2C Header** |

---

## 2. Hardware Pinout & Wiring

### A. I2C Bus Connection (Dual PCA9685 Servo Drivers)
Connect the external Dual PCA9685 servo drivers to the **Waveshare ESP32-S3-Touch-LCD-7C** I2C bus:

| ESP32-S3 7C Touch Pin | Driver 1 (Left Legs - `0x40`) | Driver 2 (Right Legs - `0x41`) | Power Source |
| :--- | :--- | :--- | :--- |
| **GPIO 8 (SDA)** | SDA | SDA | Logic High (3.3V) |
| **GPIO 9 (SCL)** | SCL | SCL | Logic High (3.3V) |
| **3.3V (VCC)** | VCC | VCC | ESP32 3.3V Logic |
| **GND** | GND | GND | Common Shared Ground |
| **External V+** | V+ (Screw Terminal) | V+ (Screw Terminal) | **External 5V–6V 5A–10A Battery / UBEC** |

> [!IMPORTANT]
> **Power Isolation**: Do **NOT** power the 18 servo motors from the ESP32 3.3V or 5V regulator. Connect an external 5V–6V high-current power supply (or 2S LiPo battery with 5V/6V step-down UBEC) to the **V+** terminal of the PCA9685 drivers and ensure **GND** is common.

### B. Onboard Waveshare ESP32-S3-Touch-LCD-7C Audio Hardware

The **Waveshare ESP32-S3-Touch-LCD-7C** includes **built-in onboard audio hardware** (I2S audio codec / Class-D power amplifier circuit and onboard speaker connector). **No external audio amplifier module is required.**

```
[ Waveshare ESP32-S3-Touch-LCD-7C Onboard PCB ]
  ├── Onboard I2S BCLK  ─> GPIO 19
  ├── Onboard I2S LRC   ─> GPIO 20
  ├── Onboard I2S DOUT  ─> GPIO 21
  └── Onboard Speaker Header ──> Connect directly to 8Ω 1W / 4Ω 2W Mini Speaker
```

| Signal | ESP32-S3 Pin | Description |
| :--- | :--- | :--- |
| **I2S BCLK** | **GPIO 19** | Bit Clock (BCK) |
| **I2S LRC** | **GPIO 20** | Word Select (WS / LRCLK) |
| **I2S DOUT** | **GPIO 21** | Serial Data (DIN) |

### C. Onboard Display, Touch & IO Expander Pinout (Waveshare 7C)

| Function | Pin / Address | Description |
| :--- | :--- | :--- |
| **GT911 SDA** | `GPIO 8` | I2C Data (Shared with PCA9685 header) |
| **GT911 SCL** | `GPIO 9` | I2C Clock (Shared with PCA9685 header) |
| **GT911 INT** | `GPIO 4` | Capacitive Touch Interrupt |
| **GT911 RST** | `EXIO1` (CH422G) | Touch Reset line (Active High) |
| **Backlight (DISP)** | `EXIO2` (CH422G) | LCD Backlight Enable (Active High) |
| **LCD Power (LCD_VDD_EN)** | `EXIO6` (CH422G) | LCD Power Rail Enable (Active High) |
| **Display RGB Data** | `GPIO 0-2, 10, 14, 17, 18, 21, 38-42, 45, 47, 48` | 16-bit parallel RGB interface (1024×600) |
| **Display Sync** | `GPIO 7` (PCLK), `GPIO 3` (VSYNC), `GPIO 46` (HSYNC), `GPIO 5` (DE) | RGB Timing & Clock Signals |

---

## 3. 18-Servo Channel Mapping

### Driver 1 (`0x40` - Left Side Legs)
- **FL (Front Left)**:
  - Channel 0: Coxa (Hip Swivel)
  - Channel 1: Femur (Upper Leg)
  - Channel 2: Tibia (Lower Leg)
- **ML (Middle Left)**:
  - Channel 3: Coxa
  - Channel 4: Femur
  - Channel 5: Tibia
- **RL (Rear Left)**:
  - Channel 6: Coxa
  - Channel 7: Femur
  - Channel 8: Tibia

### Driver 2 (`0x41` - Right Side Legs)
- **FR (Front Right)**:
  - Channel 0: Coxa
  - Channel 1: Femur
  - Channel 2: Tibia
- **MR (Middle Right)**:
  - Channel 3: Coxa
  - Channel 4: Femur
  - Channel 5: Tibia
- **RR (Rear Right)**:
  - Channel 6: Coxa
  - Channel 7: Femur
  - Channel 8: Tibia

---

## 4. Flashing Firmware (Arduino IDE / PlatformIO)

1. Open `esp32_hexapod/esp32_hexapod.ino` in **Arduino IDE**.
2. Select Board: **ESP32S3 Dev Module** (or **Waveshare ESP32-S3-Touch-LCD-7C**).
3. Board Settings:
   - **USB CDC On Boot**: *Enabled*
   - **Flash Size**: *16MB (128Mb)*
   - **Partition Scheme**: *16MB Flash (3MB APP / 9.9MB FATFS / SPIFFS)*
   - **PSRAM**: *OPI PSRAM* (8MB)
4. Select Port and click **Upload**.

---

## 5. Controlling the 7C Hexapod

- **Direct HD Touchscreen**: Tap any button on the 1024×600 touchscreen (`STAND`, `SIT`, `FLAT`, `BOW`, `WALK`, `RUN`, `DANCE`, `WAVE LEFT`, `WAVE RIGHT`, `TURN LEFT`, `TURN RIGHT`, `STOP`, `SPEED +/-`, `SPEECH: ON`).
- **Python GUI / Gemini AI**: Connect via Bluetooth (`hexapod-touch-7c` or USB COM port) and control verbally or via GUI sliders in `main.py` and `app.js`.

---

## 6. Serial & Audio Command Protocol

| Command Format | Example | Description |
| :--- | :--- | :--- |
| `HEX:<action>` | `HEX:walk` / `HEX:stand` | Postures and gaits (`stand`, `sit`, `flat`, `bow`, `walk`, `run`, `dance`, `wave_left`, `wave_right`, `turn_left`, `turn_right`, `stop`) |
| `HEX:SERVO:<d>:<ch>:<deg>` | `HEX:SERVO:1:0:90` | Direct servo positioning on Driver `d` (1 or 2), Channel `ch` (0..8) |
| `HEX:IK:<leg>:<X>:<Y>:<Z>:<ms>` | `HEX:IK:FL:0:80:-60:200` | 3D Inverse Kinematics coordinate target for leg (`FL`, `ML`, `RL`, `FR`, `MR`, `RR`) |
| `HEX:SPEED:<ms>` | `HEX:SPEED:150` | Sets interpolation transition duration in ms |
| `AUDIO:STEP` / `PLAY:STEP` | `AUDIO:STEP` | Plays mechanical footstep thud and joint actuation click |
| `AUDIO:STARTUP` | `AUDIO:STARTUP` | Plays sci-fi robotic power-up sweep |
| `AUDIO:SHUTDOWN` | `AUDIO:SHUTDOWN` | Plays descending power-down sweep |
| `AUDIO:ALERT` | `AUDIO:ALERT` | Plays dual-tone emergency alert siren |
| `AUDIO:DANCE` | `AUDIO:DANCE` | Plays upbeat 8-bit techno rhythm |
| `AUDIO:R2D2` | `AUDIO:R2D2` | Plays expressive droid robotic chatter vocalization |
| `AUDIO:SERVO` | `AUDIO:SERVO` | Plays hydraulic servo whine sound |
| `AUDIO:CLICK` | `AUDIO:CLICK` | Plays UI button click feedback tone |
| `AUDIO:TONE:<f>:<ms>` | `AUDIO:TONE:1000:150` | Plays sine tone at frequency `f` (Hz) for `ms` |
| `AUDIO:VOL:<0-100>` | `AUDIO:VOL:85` | Sets MAX98357A audio amplifier volume percentage |
| `AUDIO:MUTE:<1|0>` | `AUDIO:MUTE:1` | Mutes or unmutes audio output |

