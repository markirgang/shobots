# Waveshare 7-Inch Capacitive Touch LCD ESP32 (Birds, LEDs & Dual PCA9685 Servo Controller)

Firmware for the **Waveshare ESP32-S3-Touch-LCD-7** (7.0" 800x480 Capacitive Touchscreen, GT911 controller) replacing the dual ESP32 DevKits (Left and Right boards) with a single high-performance multimodal controller featuring an **MCP23017 16-Bit I/O Expander Board** and **Dual PCA9685 16-Channel I2C Servo Drivers** (32 PWM channels total).

---

## 🌟 Features

- **7.0-inch 800x480 Capacitive Touchscreen Dashboard**:
  - Live interactive tiles for Left and Right bird functions with glowing cyan & neon purple status rings.
  - Direct touch sliders and position readouts for PCA9685 PWM servos.
  - One-touch demonstration routines: `🦜 SING`, `💡 SWEEP`, `🔄 DANCE`, `🌟 LIGHT SHOW`, `🎶 BIRD SYMPHONY`, `🏠 ALL HOME`.
- **Dynamic On-Screen Mascot & Light Animations**:
  - **Animated Mascot**: Expressive blinking eyes, winking, pupil tracking, and mouth/beak movements synchronized with chirps and voice prompts.
  - **Floating Musical Notes**: Floating animated notes (♪ ♫ ♩) during singing routines.
  - **Sweeping Spotlight Beams**: Real-time geometric beam visualizers reflecting actual PCA9685 servo positions.
  - **Audio / VU Spectrum Wave**: Animated reactive spectrum bar graph.
- **MCP23017 16-Bit I2C I/O Expander**:
  - Drives high-current digital outputs for bird solenoids, LEDs, chirps, and motors with hardware I2C offloading.
- **Dual PCA9685 16-Channel Servo Drivers (32 Channels)**:
  - Driver 1 (`0x40`): Left side servos (Left Parrot, Left Spotlights, Center Bird).
  - Driver 2 (`0x41`): Right side servos (Right Parrot, Right Spotlights, Center Turntable).
  - 50Hz PWM output with smooth cosine S-curve trajectory interpolation.

---

## 🔌 Hardware Wiring & I2C Address Map

All peripherals share the high-speed I2C bus on the Waveshare ESP32-S3-Touch-LCD-7:

| Peripheral | I2C Address | ESP32-S3 Pin | Notes |
|---|---|---|---|
| **I2C SDA** | - | **GPIO 8** | PH2.0 4-Pin I2C Header |
| **I2C SCL** | - | **GPIO 9** | PH2.0 4-Pin I2C Header |
| **GT911 Touch INT** | `0x5D` | **GPIO 4** | Onboard Capacitive Touch Controller |
| **MCP23017 Primary** | `0x20` | GPIO 8 / 9 | Address jumpers: A0=GND, A1=GND, A2=GND |
| **MCP23017 Secondary** | `0x21` (Optional) | GPIO 8 / 9 | Address jumpers: A0=VCC, A1=GND, A2=GND |
| **PCA9685 #1 (Left Servos)** | `0x40` | GPIO 8 / 9 | Default address (all solder pads open) |
| **PCA9685 #2 (Right Servos)** | `0x41` | GPIO 8 / 9 | Bridge `A0` solder jumper pad to VCC |

---

## 📋 MCP23017 Output Pin Mapping

| Logical Pin | Function Name | Board | MCP23017 Pin / Bit | Description |
|---|---|---|---|---|
| `0` | **L Parrot Mouth** | Left | GPA0 (Bit 0) | Parrot Beak / Mouth solenoid |
| `1` | **L Parrot Eyes** | Left | GPA1 (Bit 1) | Left Parrot LED Eyes |
| `2` | **L Parrot Body** | Left | GPA2 (Bit 2) | Left Parrot Body LED / Movement |
| `3` | **L Parrot Light** | Left | GPA3 (Bit 3) | Left Parrot Spotlight LED |
| `4` | **L Parrot Mouth Sel** | Left | GPA4 (Bit 4) | Mouth selection trigger |
| `5` | **L Rear Bird Move** | Left | GPA5 (Bit 5) | Rear bird animation motor |
| `12` | **L Rear Bird Light** | Left | GPA6 (Bit 6) | Rear bird illumination |
| `13` | **L Front Bird Move** | Left | GPA7 (Bit 7) | Front bird animation motor |
| `14` | **L Front Bird Light** | Left | GPB0 (Bit 8) | Front bird illumination |
| `15` | **L Bird Chirp** | Left | GPB1 (Bit 9) | Front bird chirp sound trigger |
| `16` | **Center Bird Move** | Left | GPB2 (Bit 10) | Center bird movement |
| `0` | **R Parrot Mouth** | Right | GPB3 (Bit 11) | Right Parrot Beak solenoid |
| `1` | **R Parrot Eyes** | Right | GPB4 (Bit 12) | Right Parrot LED Eyes |
| `2` | **R Parrot Body** | Right | GPB5 (Bit 13) | Right Parrot Body LED |
| `3` | **R Parrot Light** | Right | GPB6 (Bit 14) | Right Parrot Spotlight LED |
| `4` | **R Parrot Mouth Sel** | Right | GPB7 (Bit 15) | Mouth selection trigger |
| `5..16` | **R Bird Functions** | Right | Sec MCP / Direct | Extended right bird movement & lights |

---

## ⚙️ PCA9685 Servo Channels Mapping

### Driver 1 (`0x40` - Left Servos)
- **Ch 0**: Left Parrot Up/Dn (0-180°, Default 90°)
- **Ch 1**: Left Parrot Right/Left (0-180°, Default 90°)
- **Ch 2**: Left Parrot Rotate (0-180°, Default 90°)
- **Ch 3**: Left Spotlight Up/Dn (0-180°, Default 90°)
- **Ch 4**: Left Spotlight Rotate (0-180°, Default 90°)
- **Ch 5**: Center Bird Up/Dn (0-180°, Default 90°)
- **Ch 6**: Center Bird Right/Left (0-180°, Default 90°)
- **Ch 7**: Center Bird Rotate (0-180°, Default 90°)
- **Ch 8-15**: Auxiliary Servo Channels

### Driver 2 (`0x41` - Right Servos)
- **Ch 0**: Right Parrot Up/Dn (0-180°, Default 90°)
- **Ch 1**: Right Parrot Right/Left (0-180°, Default 90°)
- **Ch 2**: Right Parrot Rotate (0-180°, Default 90°)
- **Ch 3**: Right Spotlight Up/Dn (0-180°, Default 90°)
- **Ch 4**: Right Spotlight Rotate (0-180°, Default 90°)
- **Ch 5**: Center Turntable Rotate (0-180°, Default 90°)
- **Ch 6-15**: Auxiliary Servo Channels

---

## 📡 Serial & Bluetooth Command Protocol

The firmware accepts standard 115200 baud ASCII lines via USB CDC Serial or Bluetooth:

| Command | Example | Description |
|---|---|---|
| `L:<pin>:<state>` | `L:12:1` / `L:12:0` | Sets Left Bird pin 12 ON (1) or OFF (0) |
| `R:<pin>:<state>` | `R:14:1` / `R:14:0` | Sets Right Bird pin 14 ON (1) or OFF (0) |
| `L:<pin>:PULSE` | `L:15:PULSE` | Pulses pin HIGH for 300ms, then LOW |
| `L:<pin>` / `R:<pin>` | `L:0` / `R:3` | Toggles the output state |
| `SERVO:L:<chan>:<deg>` | `SERVO:L:0:110` | Sets Left PCA9685 (`0x40`) channel 0 to 110° |
| `SERVO:R:<chan>:<deg>` | `SERVO:R:5:45` | Sets Right PCA9685 (`0x41`) channel 5 to 45° |
| `SERVO:<driver>:<chan>:<deg>` | `SERVO:1:0:90` | Sets Driver index (0=Left, 1=Right) channel to angle |
| `ROUTINE:<name>` | `ROUTINE:SING` | Starts a routine (`SING`, `SWEEP`, `DANCE`, `LIGHTSHOW`, `HOME`) |

---

## 🚀 Flashing Instructions

1. Connect the Waveshare ESP32-S3-Touch-LCD-7 via USB-C.
2. Open `esp32_Birds.ino` in **Arduino IDE** (or VS Code with ESP-IDF/PlatformIO).
3. Select Board: **ESP32S3 Dev Module** (or **Waveshare ESP32-S3-Touch-LCD-7**).
4. Configure Settings:
   - USB CDC On Boot: **Enabled**
   - Flash Size: **16MB (128Mb)**
   - PSRAM: **OPI PSRAM**
   - Partition Scheme: **16M Flash (3MB APP / 9.9MB FATFS)**
5. Click **Upload**.
