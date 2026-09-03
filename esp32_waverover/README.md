# Waveshare ESP32-S3-Touch-LCD-7C Wave Rover Mobile Platform Guide

This document describes the hardware configuration, pinout mapping, L298N (LM298) motor controller wiring diagrams, LED eyes wiring, and flashing instructions for the **Waveshare Wave Rover 4WD Mobile Platform** running on the **Waveshare ESP32-S3-Touch-LCD-7C (7.0-inch 1024×600 High-Definition Capacitive Touchscreen)** module.

---

## 1. Overview & Hardware Specifications

| Feature | **Waveshare ESP32-S3-Touch-LCD-7C Wave Rover** |
| :--- | :--- |
| **Display Panel** | **7.0-inch 1024×600 High-Definition IPS RGB Display** |
| **Touch Controller** | **Goodix GT911 5-Point Capacitive Multi-Touch (I2C: `0x5D`)** |
| **IO Expander** | **WCH CH422G (Addresses `0x24`/`0x38`) for Backlight, LCD Power & Touch Reset** |
| **Onboard Controls** | **1024×600 HD Widescreen Touch Buttons (Forward, Back, Left, Right, Spin, Stop, Mouth, Body, Eyes, Speed, Routines)** |
| **Telemetry & Visuals** | **Live Telemetry, Motor Speeds, Pan-Tilt Angles & Animated Cyber Rover Mascot** |
| **Audio Output** | **MAX98357A I2S Mono Audio Amplifier (BCLK=19, LRC=20, DIN=21)** |
| **L298N Motor Driver** | **Dual H-Bridge Motor Driver for DC Mouth Motor & DC Body Up/Down Motion Motor** |
| **Eye LEDs** | **Digital GPIO Output for Animated Eye LEDs** |
| **Microcontroller** | **ESP32-S3-WROOM-1 (16MB Flash, 8MB Octal PSRAM @ 240MHz)** |

---

## 2. Hardware Pinout & Wiring

### A. L298N (LM298) Dual H-Bridge Motor Driver Wiring

The L298N motor controller drives two independent DC motors:
1. **DC Mouth Motor** (Channel A / OUT1 & OUT2): Power ON = Mouth Open; Power OFF = Mouth Closed.
2. **DC Body Motion Motor** (Channel B / OUT3 & OUT4): Power ON = Body attached to mouth moves up and down via eccentric cam / mechanical link; Power OFF = Stopped.

| ESP32-S3 7C Pin | L298N Pin | Target Component | Description / Function |
| :--- | :--- | :--- | :--- |
| **GPIO 11** | **IN1** | DC Mouth Motor | Mouth Motor Direction Line 1 |
| **GPIO 12** | **IN2** | DC Mouth Motor | Mouth Motor Direction Line 2 |
| **GPIO 13** | **ENA** | DC Mouth Motor | Mouth Enable / PWM Speed Control |
| **GPIO 14** | **IN3** | DC Body Motion Motor | Body Up/Down Motor Direction Line 1 |
| **GPIO 15** | **IN4** | DC Body Motion Motor | Body Up/Down Motor Direction Line 2 |
| **GPIO 16** | **ENB** | DC Body Motion Motor | Body Up/Down Enable / Speed Control |
| **GND** | **GND** | L298N Ground | Common Shared Ground |
| **External 5V-12V** | **VCC (+12V)** | Power Terminal | Motor Drive Battery Power Source |

> [!IMPORTANT]
> **Power Isolation**: Do **NOT** power the L298N motor controller or DC motors from the ESP32 3.3V pin. Connect an external 6V–12V battery power source to the **VCC** terminal of the L298N driver and ensure **GND** is common between the ESP32 and L298N.

---

### B. Eye LEDs Output Wiring

| ESP32-S3 Pin | Component | Connection | Description |
| :--- | :--- | :--- | :--- |
| **GPIO 10** | **Eye LEDs (Anode +)** | 220Ω Resistor to LED(+) | Digital Output for LED Eyes (ON/OFF/Blink/Pulse) |
| **GND** | **Eye LEDs (Cathode -)** | Ground (-) | Shared Common Ground |

---

### C. 4WD Drive Motors & Pan-Tilt Servos (PCA9685 / GPIO)

| Function | Pin / Channel | Description |
| :--- | :--- | :--- |
| **Left Wheels Drive PWM** | `GPIO 1` | Left side 4WD motor speed PWM |
| **Left Wheels Direction** | `GPIO 2` | Left side 4WD motor direction HIGH/LOW |
| **Right Wheels Drive PWM** | `GPIO 42` | Right side 4WD motor speed PWM |
| **Right Wheels Direction** | `GPIO 41` | Right side 4WD motor direction HIGH/LOW |
| **Pan Servo (Heading)** | PCA9685 `Ch 0` / `GPIO 5` | Pan-Tilt Horizontal Rotation (0° to 180°, default 90°) |
| **Tilt Servo (Pitch)** | PCA9685 `Ch 1` / `GPIO 6` | Pan-Tilt Vertical Pitch (0° to 180°, default 90°) |
| **Headlights LED Pin** | `GPIO 7` | Front Headlights LED Output (ON/OFF) |

---

### D. Onboard Waveshare ESP32-S3-Touch-LCD-7C Audio Hardware

| Signal | ESP32-S3 Pin | Description |
| :--- | :--- | :--- |
| **I2S BCLK** | **GPIO 19** | Bit Clock (BCK) |
| **I2S LRC** | **GPIO 20** | Word Select (WS / LRCLK) |
| **I2S DOUT** | **GPIO 21** | Serial Data (DIN) |

---

## 3. Serial & Audio Command Protocol

| Command Format | Example | Description |
| :--- | :--- | :--- |
| `ROVER:<action>` | `ROVER:forward` / `ROVER:stop` | Rover motion (`forward`, `back`, `turn_left`, `turn_right`, `spin_left`, `spin_right`, `stop`, `patrol`, `spin_360`, `dance`, `obstacle_avoidance`) |
| `ROVER:MOUTH:<1\|0>` | `ROVER:MOUTH:1` | L298N DC Mouth Motor: `1` = Power ON (mouth open), `0` = Power OFF (mouth closed) |
| `ROVER:BODY:<1\|0>` | `ROVER:BODY:1` | L298N DC Body Motion Motor: `1` = Power ON (moves body up/down), `0` = Power OFF |
| `ROVER:EYES:<1\|0>` | `ROVER:EYES:1` | Eye LEDs Output: `1` = Power ON, `0` = Power OFF |
| `ROVER:SPEED:<0-100>` | `ROVER:SPEED:80` | Sets 4WD drive motor speed percentage |
| `ROVER:PANTILT:<pan>:<tilt>` | `ROVER:PANTILT:90:45` | Sets Pan-Tilt camera servo angles in degrees |
| `ROVER:LED:<1\|0>` | `ROVER:LED:1` | Turns front headlights ON (`1`) or OFF (`0`) |
| `ROVER:LCD:MSG:<msg>` | `ROVER:LCD:MSG:Patrolling` | Displays custom status message on the 1024×600 HD touchscreen |
| `AI_SPEAKING:<1\|0>` | `AI_SPEAKING:1` | Real-time AI talking signal: `1` = Mouth opens & pulses, body sways up/down, eyes illuminate; `0` = Mouth closes, body stops, eyes idle |
| `AUDIO:ROVER_ENGINE` | `AUDIO:ROVER_ENGINE` | Plays diesel engine hum sound effect over MAX98357A I2S |
| `AUDIO:HORN` | `AUDIO:HORN` | Plays dual-tone car/rover horn alert |
| `AUDIO:STARTUP` | `AUDIO:STARTUP` | Plays sci-fi vehicle power-up sweep |
| `AUDIO:SHUTDOWN` | `AUDIO:SHUTDOWN` | Plays descending vehicle power-down sweep |
| `AUDIO:ALERT` | `AUDIO:ALERT` | Plays emergency siren warning tone |
| `AUDIO:TURBO` | `AUDIO:TURBO` | Plays turbo boost acceleration sound effect |
| `AUDIO:BRAKE` | `AUDIO:BRAKE` | Plays tire squeal / braking sound effect |

---

## 4. Flashing Firmware (Arduino IDE / PlatformIO)

1. Open `esp32_waverover/esp32_waverover.ino` in **Arduino IDE**.
2. Select Board: **ESP32S3 Dev Module** (or **Waveshare ESP32-S3-Touch-LCD-7C**).
3. Board Settings:
   - **USB CDC On Boot**: *Enabled*
   - **Flash Size**: *16MB (128Mb)*
   - **Partition Scheme**: *16MB Flash (3MB APP / 9.9MB FATFS / SPIFFS)*
   - **PSRAM**: *OPI PSRAM* (8MB)
4. Select COM Port and click **Upload**.
