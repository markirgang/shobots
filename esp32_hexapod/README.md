# ESP-32-Touch-LCD 7.0" Capacitive Touchscreen Hexapod Guide

This document describes the hardware configuration, pinout mapping, wiring diagrams, and flashing instructions for the 6-leg Hexapod robot running on the **7.0-inch Capacitive Touchscreen ESP32-S3 Module** (**Waveshare ESP32-S3-Touch-LCD-7 / 7B** or **Sunton ESP32-8048S070**), substituted for the standard ESP 32 Devkit.

---

## 1. Overview & 7.0-Inch Specifications

| Feature | Legacy ESP 32 DevKit | **ESP32-S3-Touch-LCD-7 (7.0" Edition)** |
| :--- | :--- | :--- |
| **Display Panel** | None | **7.0-inch 800×480 High-Resolution RGB Display** |
| **Touch Controller** | None | **GT911 5-Point Capacitive Multi-Touch (I2C: `0x5D`)** |
| **Onboard Controls** | None | **800x480 Widescreen Touch Buttons (Stand, Walk, Dance, etc.)** |
| **Telemetry & Visuals** | Serial logs | **Live Telemetry, Joint Angles, Speed & Animated Robot Face** |
| **Microcontroller** | ESP32-WROOM | **ESP32-S3-WROOM-1 (16MB Flash, 8MB Octal PSRAM)** |
| **I2C Bus for Servos** | GPIO 21 / 22 | **GPIO 8 (SDA) / GPIO 9 (SCL) via PH2.0 4-Pin I2C Header** |

---

## 2. Hardware Pinout & Wiring

### A. I2C Bus Connection (Dual PCA9685 Servo Drivers)
Connect the external Dual PCA9685 servo drivers to the **ESP32-S3-Touch-LCD-7** I2C bus:

| ESP32-S3 7" Touch Pin | Driver 1 (Left Legs - `0x40`) | Driver 2 (Right Legs - `0x41`) | Power Source |
| :--- | :--- | :--- | :--- |
| **GPIO 8 (SDA)** | SDA | SDA | Logic High (3.3V) |
| **GPIO 9 (SCL)** | SCL | SCL | Logic High (3.3V) |
| **3.3V (VCC)** | VCC | VCC | ESP32 3.3V Logic |
| **GND** | GND | GND | Common Shared Ground |
| **External V+** | V+ (Screw Terminal) | V+ (Screw Terminal) | **External 5V–6V 5A–10A Battery / UBEC** |

> [!IMPORTANT]
> **Power Isolation**: Do **NOT** power the 18 servo motors from the ESP32 3.3V or 5V regulator. Connect an external 5V–6V high-current power supply (or 2S LiPo battery with 5V/6V step-down UBEC) to the **V+** terminal of the PCA9685 drivers and ensure **GND** is common.

### B. Onboard 7-Inch Display & Touch Controller Pinout (Waveshare 7" / 7B)

| Function | ESP32-S3 Pin | Description |
| :--- | :--- | :--- |
| **GT911 SDA** | GPIO 8 | I2C Data (Shared with PCA9685 header) |
| **GT911 SCL** | GPIO 9 | I2C Clock (Shared with PCA9685 header) |
| **GT911 INT** | GPIO 4 | Capacitive Touch Interrupt |
| **GT911 RST** | IO Expander / CH422G | Touch Reset line |
| **RGB LCD Data** | GPIO 1-3, 10-18, 38-45 | 16-bit parallel RGB ST7262 interface |
| **Backlight (BL)** | GPIO 6 (or PWM) | Adjustable LCD Backlight |

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
2. Select Board: **ESP32S3 Dev Module** (or **Waveshare ESP32-S3-Touch-LCD-7**).
3. Board Settings:
   - **USB CDC On Boot**: *Enabled*
   - **Flash Size**: *16MB (128Mb)*
   - **Partition Scheme**: *16MB Flash (3MB APP / 9.9MB FATFS / SPIFFS)*
   - **PSRAM**: *OPI PSRAM* (8MB)
4. Select Port and click **Upload**.

---

## 5. Controlling the 7-Inch Hexapod

- **Direct Widescreen Touch**: Tap any button on the 800x480 touchscreen (`STAND`, `SIT`, `FLAT`, `BOW`, `WALK`, `RUN`, `DANCE`, `WAVE LEFT`, `WAVE RIGHT`, `TURN LEFT`, `TURN RIGHT`, `STOP`, `SPEED +/-`).
- **Python GUI / Gemini AI**: Connect via Bluetooth (`hexapod-touch-7` or USB COM port) and control verbally or via GUI sliders in `main.py`.
