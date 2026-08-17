# Waveshare ESP32-S3-Touch-LCD-7B Birds & LED Controller Hardware Guide

Firmware for the **Waveshare ESP32-S3-Touch-LCD-7B** (7.0-inch 1024×600 High-Definition Capacitive Touchscreen, GT911 controller, CH422G IO Expander) replacing the dual ESP32 DevKits (Left and Right boards) with a single high-performance multimodal controller featuring an **MCP23017 16-Bit I/O Expander Board**, **Dual PCA9685 16-Channel I2C Servo Drivers** (32 PWM channels total), and a **Microphone Sound Detection Module** for AI speech animatronics.

---

## 🌟 Features

- **7.0-inch 1024×600 HD Capacitive Touchscreen Dashboard**:
  - Live interactive tiles for Left and Right bird functions with glowing cyan & neon purple status rings.
  - On-screen **Parrot Speaker Selector Switch** (`👈 L PARROT`, `🦜 BOTH`, `👉 R PARROT`) and `🎤 MIC REACT: ON/OFF`.
  - Direct touch sliders and position readouts for PCA9685 PWM servos.
  - One-touch demonstration routines: `🦜 SING`, `💡 SWEEP`, `🔄 DANCE`, `🌟 LIGHT SHOW`, `🎶 BIRD SYMPHONY`, `🏠 ALL HOME`.
- **🎤 Microphone Sound Detection & AI Voice Animatronics Engine**:
  - Automatically triggers when the LLM/AI speaks or when the sound detection module senses audio from the computer speaker.
  - **Mouth / Beak**: Selected parrot(s) mouth moves up and down rapidly to mouth the spoken words.
  - **Wings**: Selected parrot(s) wings flap actively while speaking.
  - **3 Base Servos (Selected Parrot)**: Up/Down tilt (`80°-100°`), Right/Left side-to-side sway (`75°-105°`), and Turn/Rotate (`70°-110°`) move gently at slow-to-medium speed.
  - **Spotlight**: Selected spotlight turns up/down and right/left slowly to medium speed and illuminates the speaking parrot.
  - **Center Turntable**: Center turntable slowly rotates right and left (`60°-120°`).
  - **Smooth Home Rest**: Returns all servos gently to neutral 90° and turns off mouth/wings when speech finishes.
- **Dynamic On-Screen Mascot & Light Animations**:
  - **Animated Mascot**: Expressive blinking eyes, winking, pupil tracking, and mouth/beak movements synchronized with chirps and voice prompts.
  - **Floating Musical Notes**: Floating animated notes (♪ ♫ ♩) during singing routines.
  - **Sweeping Spotlight Beams**: Real-time geometric beam visualizers reflecting actual PCA9685 servo positions.
  - **Audio / VU Spectrum Wave**: Animated reactive spectrum bar graph.
- **Onboard CH422G IO Expander**:
  - Controls LCD Backlight (`EXIO2`), LCD Power (`EXIO6`), and GT911 Touch Reset (`EXIO1`) via I2C (`0x24`/`0x38`).
- **MCP23017 16-Bit I2C I/O Expander**:
  - Drives high-current digital outputs for bird solenoids, LEDs, chirps, and motors with hardware I2C offloading.
  - Digital input on `GPA4` (Bit 4) with 100k pull-up for sound detector modules.
- **🔊 MAX98357A I2S Mono Audio Amplifier & Procedural Synthesizer**:
  - High-fidelity mono Class-D digital audio output driving 4Ω–8Ω (up to 3.2W) speakers directly.
  - Realistic bird sound effects: `SOUND_CHIRP`, `SOUND_SQUAWK`, `SOUND_TRILL`, `SOUND_SONG` (cheerful arpeggio), and `SOUND_SYMPHONY` (multi-note fanfare).
  - Synchronized audio playback during routines (`SING`, `DANCE`, `LIGHT SHOW`) and chirp pulse triggers.
  - Software volume control (0 - 100%) and mute toggle.

---

## 🔌 Hardware Wiring & I2C Address Map

All peripherals share the high-speed I2C bus and dedicated I2S audio pins on the Waveshare ESP32-S3-Touch-LCD-7B:

| Peripheral / Interface | Signal / Address | ESP32-S3 Pin | Notes |
|---|---|---|---|
| **I2C SDA** | - | **GPIO 8** | PH2.0 4-Pin I2C Header |
| **I2C SCL** | - | **GPIO 9** | PH2.0 4-Pin I2C Header |
| **GT911 Touch INT** | `0x5D` | **GPIO 4** | Onboard Capacitive Touch Controller |
| **CH422G IO Expander** | `0x24` / `0x38` | GPIO 8 / 9 | Backlight (`EXIO2`), Power (`EXIO6`), Touch RST (`EXIO1`) |
| **Mic Sound Sensor (Direct)** | - | **GPIO 7** | Direct ESP32 GPIO input (Pull-up) |
| **MCP23017 Primary** | `0x20` | GPIO 8 / 9 | Address jumpers: A0=GND, A1=GND, A2=GND |
| **MCP23017 Secondary** | `0x21` (Optional) | GPIO 8 / 9 | Address jumpers: A0=VCC, A1=GND, A2=GND |
| **PCA9685 #1 (Left Servos)** | `0x40` | GPIO 8 / 9 | Default address (all solder pads open) |
| **PCA9685 #2 (Right Servos)** | `0x41` | GPIO 8 / 9 | Bridge `A0` solder jumper pad to VCC |
| **MAX98357A I2S BCLK** | Bit Clock | **GPIO 19** | I2S Bit Clock (BCK) |
| **MAX98357A I2S LRC** | Word Select (WS) | **GPIO 20** | I2S Left-Right Clock / WS |
| **MAX98357A I2S DIN** | Data In | **GPIO 21** | I2S Serial Data Out from ESP32 |

---

## 🔊 MAX98357A I2S Mono Audio Amplifier Wiring

Connect the **MAX98357A I2S Mono Class-D Audio Amplifier** module:

```
[ Waveshare ESP32-S3-Touch-LCD-7 ]             [ MAX98357A I2S Amp ]
  ├── 5V (or 3.3V) ───────────────────────────────> VIN (5V recommended for 3.2W)
  ├── GND ────────────────────────────────────────> GND
  ├── GPIO 19 (I2S BCLK) ─────────────────────────> BCLK
  ├── GPIO 20 (I2S LRC / WS) ─────────────────────> LRC
  └── GPIO 21 (I2S DOUT) ─────────────────────────> DIN
                                                    GAIN ───> Open (9dB Default) or GND (6dB)
                                                    SD_MODE > Open / Float (L+R Mono Mix)
                                                    [ + / - ] ──> 4Ω - 8Ω 2W-3W Speaker
```

### MAX98357A Gain Settings
| GAIN Pin Connection | Amplifier Gain | Best For |
|---|---|---|
| 100kΩ to GND | 3 dB | Very low distortion / Sensitive transducers |
| Connected to GND | 6 dB | Small 8Ω speakers |
| **Unconnected / Floating (Default)** | **9 dB** | **Standard 4Ω–8Ω 2W–3W mini speakers (Recommended)** |
| Connected to VDD (3.3V/5V) | 12 dB | Higher output volume |
| 100kΩ to VDD | 15 dB | Maximum amplification |

---

## 🎙️ Microphone Sound Detection Module Wiring

Place the sound detection sensor (e.g. LM393 / KY-037 / KY-038 / MAX9814) near the computer speaker:

```
[ Microphone Sound Module ]
 ├── VCC ───> +3.3V or +5V (ESP32-S3 VCC / Header Pin)
 ├── GND ───> GND (ESP32-S3 GND)
 └── DO  ───> MCP23017 Pin 25 (GPA4 / Bit 4) OR ESP32 GPIO 7
```

---

## 📋 MCP23017 Pin Mapping

| Logical Pin | Function Name | Board | MCP23017 Pin / Bit | Direction | Description |
|---|---|---|---|---|---|
| `0` | **L Parrot Mouth** | Left | GPA0 (Bit 0) | OUTPUT | Parrot Beak / Mouth solenoid |
| `1` | **L Parrot Eyes** | Left | GPA1 (Bit 1) | OUTPUT | Left Parrot LED Eyes |
| `2` | **L Parrot Body / Wings** | Left | GPA2 (Bit 2) | OUTPUT | Left Parrot Body LED & Wing Flap |
| `3` | **L Parrot Light** | Left | GPA3 (Bit 3) | OUTPUT | Left Parrot Spotlight LED |
| `4` | **Mic Sound Sensor / In** | Left | GPA4 (Bit 4) | INPUT (Pull-up) | Sound Detection Module Digital Input |
| `5` | **L Rear Bird Move** | Left | GPA5 (Bit 5) | OUTPUT | Rear bird animation motor |
| `12` | **L Rear Bird Light** | Left | GPA6 (Bit 6) | OUTPUT | Rear bird illumination |
| `13` | **L Front Bird Move** | Left | GPA7 (Bit 7) | OUTPUT | Front bird animation motor |
| `14` | **L Front Bird Light** | Left | GPB0 (Bit 8) | OUTPUT | Front bird illumination |
| `15` | **L Bird Chirp** | Left | GPB1 (Bit 9) | OUTPUT | Front bird chirp sound trigger |
| `16` | **Center Bird Move** | Left | GPB2 (Bit 10) | OUTPUT | Center bird movement |
| `0` | **R Parrot Mouth** | Right | GPB3 (Bit 11) | OUTPUT | Right Parrot Beak solenoid |
| `1` | **R Parrot Eyes** | Right | GPB4 (Bit 12) | OUTPUT | Right Parrot LED Eyes |
| `2` | **R Parrot Body / Wings** | Right | GPB5 (Bit 13) | OUTPUT | Right Parrot Body LED & Wing Flap |
| `3` | **R Parrot Light** | Right | GPB6 (Bit 14) | OUTPUT | Right Parrot Spotlight LED |
| `4` | **R Parrot Mouth Sel** | Right | GPB7 (Bit 15) | OUTPUT | Secondary trigger / Extended |
| `5..16` | **R Bird Functions** | Right | Sec MCP / Direct | OUTPUT | Extended right bird movement & lights |

---

## ⚙️ PCA9685 Servo Channels Mapping

### Driver 1 (`0x40` - Left Servos)
- **Ch 0**: **Left Parrot Up/Dn** (0-180°, Default 90°)
- **Ch 1**: **Left Parrot Right/Left** (0-180°, Default 90°)
- **Ch 2**: **Left Parrot Rotate** (0-180°, Default 90°)
- **Ch 3**: **Left Spotlight Up/Dn** (0-180°, Default 90°)
- **Ch 4**: **Left Spotlight Rotate** (0-180°, Default 90°)
- **Ch 5**: Center Bird Up/Dn (0-180°, Default 90°)
- **Ch 6**: Center Bird Right/Left (0-180°, Default 90°)
- **Ch 7**: Center Bird Rotate (0-180°, Default 90°)
- **Ch 8-15**: Auxiliary Servo Channels

### Driver 2 (`0x41` - Right Servos)
- **Ch 0**: **Right Parrot Up/Dn** (0-180°, Default 90°)
- **Ch 1**: **Right Parrot Right/Left** (0-180°, Default 90°)
- **Ch 2**: **Right Parrot Rotate** (0-180°, Default 90°)
- **Ch 3**: **Right Spotlight Up/Dn** (0-180°, Default 90°)
- **Ch 4**: **Right Spotlight Rotate** (0-180°, Default 90°)
- **Ch 5**: **Center Turntable Rotate** (0-180°, Default 90°)
- **Ch 6-15**: Auxiliary Servo Channels

---

## 📡 Serial & Bluetooth Command Protocol

The firmware accepts standard 115200 baud ASCII lines via USB CDC Serial or Bluetooth:

| Command | Example | Description |
|---|---|---|
| `PARROT_SEL:<choice>` | `PARROT_SEL:LEFT` / `RIGHT` / `BOTH` | Sets which parrot speaks (`LEFT`, `RIGHT`, `BOTH`) |
| `MIC_REACT:<1\|0>` | `MIC_REACT:1` / `MIC_REACT:0` | Enables or disables microphone sound reactivity |
| `AI_SPEAKING:<1\|0>` | `AI_SPEAKING:1` / `AI_SPEAKING:0` | Directly triggers or stops speech animatronics from AI audio |
| `MIC_TRIGGER` | `MIC_TRIGGER` | Simulates a sound detection trigger event |
| `L:<pin>:<state>` | `L:12:1` / `L:12:0` | Sets Left Bird pin 12 ON (1) or OFF (0) |
| `R:<pin>:<state>` | `R:14:1` / `R:14:0` | Sets Right Bird pin 14 ON (1) or OFF (0) |
| `L:<pin>:PULSE` | `L:15:PULSE` | Pulses pin HIGH for 300ms, then LOW |
| `L:<pin>` / `R:<pin>` | `L:0` / `R:3` | Toggles the output state |
| `SERVO:L:<chan>:<deg>` | `SERVO:L:0:110` | Sets Left PCA9685 (`0x40`) channel 0 to 110° |
| `SERVO:R:<chan>:<deg>` | `SERVO:R:5:45` | Sets Right PCA9685 (`0x41`) channel 5 to 45° |
| `SERVO:<driver>:<chan>:<deg>` | `SERVO:1:0:90` | Sets Driver index (0=Left, 1=Right) channel to angle |
| `ROUTINE:<name>` | `ROUTINE:SING` | Starts a routine (`SING`, `SWEEP`, `DANCE`, `LIGHTSHOW`, `HOME`) |
| `AUDIO:CHIRP` / `CHIRP` | `AUDIO:CHIRP` | Plays realistic dual-sweep bird chirp sound |
| `AUDIO:SQUAWK` / `SQUAWK` | `AUDIO:SQUAWK` | Plays parrot squawk harmonic vocalization |
| `AUDIO:TRILL` / `TRILL` | `AUDIO:TRILL` | Plays rapid songbird warble / trill |
| `AUDIO:SONG` / `SONG` | `AUDIO:SONG` | Plays 7-note cheerful songbird melody |
| `AUDIO:SYMPHONY` | `AUDIO:SYMPHONY` | Plays full multi-note symphony fanfare |
| `AUDIO:BEEP` | `AUDIO:BEEP` | Plays UI feedback confirmation blip (1200Hz) |
| `AUDIO:TONE:<freq>:<dur>` | `AUDIO:TONE:1500:200` | Plays pure sine tone at frequency (Hz) for duration (ms) |
| `AUDIO:SWEEP:<f1>:<f2>:<dur>` | `AUDIO:SWEEP:500:2500:300` | Frequency sweep from f1 to f2 (Hz) over duration (ms) |
| `AUDIO:VOL:<0-100>` | `AUDIO:VOL:90` | Sets audio output volume percentage (0..100) |
| `AUDIO:MUTE:<1\|0>` | `AUDIO:MUTE:1` | Mutes or unmutes the audio amplifier |

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
