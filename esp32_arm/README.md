# Waveshare ESP32-S3-Touch-LCD-7C 6-DOF Robot Arm Hardware & Telemetry Guide

This document describes the hardware configuration, pinout mapping, wiring diagrams, and telemetry features for the 6-DOF Robot Arm powered by the **Waveshare ESP32-S3-Touch-LCD-7C (7.0-inch 1024x600 High-Definition Capacitive Touchscreen)** module (Version C), substituted for the standard ESP 32 Devkit.

---

## 1. Overview & 7.0-Inch (Version C) Specifications

| Feature | Legacy ESP 32 DevKit | **Waveshare ESP32-S3-Touch-LCD-7C (Version C)** |
| :--- | :--- | :--- |
| **Interactive Interface** | Headless (Terminal only) | **7.0-inch 1024×600 High-Definition RGB Display + GT911 Capacitive Multi-Touch** |
| **Backlight & Power** | Fixed / External | **Onboard CH422G I/O Expander (`EXIO2` Backlight DISP, `EXIO6` VCOM/Power)** |
| **Live Telemetry** | None | **Real-Time 6-Joint Angles, 3D Cartesian (X,Y,Z,Pitch,Roll) & Workspace Reach** |
| **Audio Output** | None | **MAX98357A I2S Mono Audio Amplifier (BCLK=19, LRC=20, DIN=21)** |
| **Kinematic Animation**| None | **Live Multi-Link Arm Simulation & Expressive Cyber Mascot Animations** |
| **Onboard Controls** | None | **1024×600 Widescreen Multi-Touch Buttons (Home, Rest, Reach, Gestures, Gripper)** |
| **Microcontroller** | ESP32-WROOM | **ESP32-S3-WROOM-1-N16R8 (Dual-Core 240MHz, 16MB Flash, 8MB Octal PSRAM)** |
| **I2C Bus for Servos** | GPIO 21 / 22 | **GPIO 8 (SDA) / GPIO 9 (SCL) via HY2.0 4-Pin I2C Header** |

---

## 2. Hardware Pinout & Wiring

### A. I2C Bus Connection (PCA9685 16-Channel Servo Driver)
Connect the PCA9685 servo driver (I2C address `0x40`) to the **Waveshare ESP32-S3-Touch-LCD-7C** I2C header:

| ESP32-S3 7C Pin | PCA9685 Driver (`0x40`) | Description |
| :--- | :--- | :--- |
| **GPIO 8 (SDA)** | SDA | I2C Data line (3.3V) |
| **GPIO 9 (SCL)** | SCL | I2C Clock line (3.3V) |
| **3.3V (VCC)** | VCC | PCA9685 Logic Power |
| **GND** | GND | Common Shared Ground |
| **External V+** | V+ (Screw Terminal) | **External 5V–6V 4A–8A Power Supply / UBEC** |

> [!IMPORTANT]
> **Power Isolation**: Never power the 6 high-torque robotic servos from the ESP32 3.3V/5V pin. Connect an external 5V–6V 5A power supply to the **V+** terminal of the PCA9685 and connect **GND** to the ESP32-S3 GND.

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

### C. Onboard Display, Touch & I/O Expander Pinout (Waveshare 7C)

| Function | ESP32-S3 / IO Expander Pin | Description |
| :--- | :--- | :--- |
| **Touch SDA (TP_SDA)** | GPIO 8 | I2C Data (Shared with PCA9685 header) |
| **Touch SCL (TP_SCL)** | GPIO 9 | I2C Clock (Shared with PCA9685 header) |
| **Touch INT (TP_IRQ)** | GPIO 4 | GT911 Capacitive Touch Interrupt |
| **Touch RST (TP_RST)** | `EXIO1` (CH422G) | Touch Controller Reset |
| **Backlight (DISP)** | `EXIO2` (CH422G) | Screen Backlight Enable / PWM Brightness |
| **LCD Power (LCD_VDD_EN)** | `EXIO6` (CH422G) | VCOM / LCD Power Rail Enable |
| **Display RGB Data** | GPIO 0-2, 10, 14, 17, 18, 21, 38-42, 45, 47, 48 | 16-bit parallel RGB interface (1024×600) |
| **Display Sync** | GPIO 7 (PCLK), GPIO 3 (VSYNC), GPIO 46 (HSYNC), GPIO 5 (DE) | RGB Timing & Clock Signals |

---

## 3. 6-DOF Servo Channel Mapping (PCA9685 `0x40`)

| Channel | Joint Name | Range | Default | Function |
| :--- | :--- | :--- | :--- | :--- |
| **Ch 0** | Base / Waist | 0° – 180° | 90° | Horizontal rotation |
| **Ch 1** | Shoulder | 0° – 180° | 90° | Vertical shoulder pitch |
| **Ch 2** | Elbow | 0° – 180° | 90° | Forearm pitch |
| **Ch 3** | Wrist Pitch | 0° – 180° | 90° | End-effector pitch angle |
| **Ch 4** | Wrist Roll | 0° – 180° | 90° | Gripper roll rotation |
| **Ch 5** | Gripper / Claw | 0° – 180° | 40° | Claw aperture (0°=Closed, 100°=Open) |

---

## 4. Live Telemetry & Animation Features

1. **Forward & Inverse Kinematics Telemetry**:
   - Calculates real-time 3D Cartesian coordinates $(X, Y, Z)$ of the gripper tip in mm.
   - Computes live end-effector pitch angle and roll orientation.
2. **Dynamic Kinematic Multi-Link Wireframe Animation**:
   - Renders a real-time 2D/3D kinematic simulation of the arm links on the 1024×600 display following joint angles at 50Hz.
3. **Cyber Mascot Facial Expressions**:
   - **Idle**: Gentle breathing / blinking eyes.
   - **Yes / Nod**: Happy curved smile eyes.
   - **No / Shake**: Alert wide eyes.
   - **Dance / High Five**: Pulsing star eyes.
   - **Wave**: Playful wink expression.

---

## 5. Serial & Audio Command Protocol

| Command Format | Example | Description |
| :--- | :--- | :--- |
| `ARM:<action>` | `ARM:home` / `ARM:reach` | Executes postures and routines (`home`, `rest`, `reach`, `yes`, `no`, `wave`, `high_five`, `dance`, `bow`, `stop`) |
| `SERVO:<chan>:<deg>` | `SERVO:5:90` | Direct servo positioning for joint channel (0..5) |
| `ARM:IK:<X>:<Y>:<Z>:<pitch>:<roll>:<claw>:<ms>` | `ARM:IK:120:0:150:0:90:40:250` | 3D Inverse Kinematics target with end-effector pitch, roll, claw aperture and duration (ms) |
| `ARM:SPEED:<ms>` | `ARM:SPEED:200` | Sets motion interpolation duration in ms |
| `AUDIO:CLAW_GRAB` / `PLAY:CLAW_GRAB` | `AUDIO:CLAW_GRAB` | Plays pneumatic clamp latch sound |
| `AUDIO:CLAW_RELEASE` / `PLAY:CLAW_RELEASE` | `AUDIO:CLAW_RELEASE` | Plays pneumatic air release hiss |
| `AUDIO:SERVO` / `PLAY:SERVO` | `AUDIO:SERVO` | Plays industrial robotic joint actuation whine |
| `AUDIO:CHIME` / `PLAY:CHIME` | `AUDIO:CHIME` | Plays 4-note pick-and-place success chime |
| `AUDIO:ERROR` / `PLAY:ERROR` | `AUDIO:ERROR` | Plays workspace limit warning buzz |
| `AUDIO:FANFARE` / `PLAY:FANFARE` | `AUDIO:FANFARE` | Plays victory gesture fanfare |
| `AUDIO:BEEP` / `AUDIO:CLICK` | `AUDIO:BEEP` | Plays high-tech cyber blip |
| `AUDIO:TONE:<f>:<ms>` | `AUDIO:TONE:1200:100` | Plays sine tone at frequency `f` (Hz) for `ms` |
| `AUDIO:VOL:<0-100>` | `AUDIO:VOL:80` | Sets MAX98357A audio volume percentage |
| `AUDIO:MUTE:<1\|0>` | `AUDIO:MUTE:1` | Mutes or unmutes audio output |

---

## 6. Arduino IDE Flashing Settings

1. Open `esp32_arm/esp32_arm.ino` in **Arduino IDE**.
2. Select Board: **ESP32S3 Dev Module** (or **Waveshare ESP32-S3-Touch-LCD-7C**).
3. Recommended Board Configuration:
   - **USB CDC On Boot**: *Enabled*
   - **Flash Size**: *16MB (128Mb)*
   - **Partition Scheme**: *16MB Flash (3MB APP / 9.9MB FATFS)*
   - **PSRAM**: *OPI PSRAM* (8MB)
   - **Upload Speed**: *921600*
4. Click **Upload**.
