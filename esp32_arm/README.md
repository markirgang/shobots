# ESP32-S3-Touch-LCD-7 6-DOF Robot Arm Hardware & Telemetry Guide

This document describes the hardware configuration, pinout mapping, wiring diagrams, and telemetry features for the 6-DOF Robot Arm powered by the **ESP32-S3-Touch-LCD-7 (7.0-inch 800x480 Capacitive Touchscreen)** module, substituted for the standard ESP 32 Devkit.

---

## 1. Overview & 7.0-Inch Specifications

| Feature | Legacy ESP 32 DevKit | **ESP32-S3-Touch-LCD-7 (7.0" Edition)** |
| :--- | :--- | :--- |
| **Interactive Interface** | Headless (Terminal only) | **7.0-inch 800×480 High-Resolution RGB Display + GT911 Capacitive Touch** |
| **Live Telemetry** | None | **Real-Time 6-Joint Angles, 3D Cartesian (X,Y,Z,Pitch,Roll) & Workspace Reach** |
| **Kinematic Animation**| None | **Live Multi-Link Arm Simulation & Expressive Cyber Mascot Animations** |
| **Onboard Controls** | None | **Widescreen Multi-Touch Buttons (Home, Rest, Reach, Gestures, Gripper)** |
| **Microcontroller** | ESP32-WROOM | **ESP32-S3-WROOM-1 (Dual-Core, 16MB Flash, 8MB Octal PSRAM)** |
| **I2C Bus for Servos** | GPIO 21 / 22 | **GPIO 8 (SDA) / GPIO 9 (SCL) via PH2.0 4-Pin I2C Header** |

---

## 2. Hardware Pinout & Wiring

### A. I2C Bus Connection (PCA9685 16-Channel Servo Driver)
Connect the PCA9685 servo driver (I2C address `0x40`) to the **ESP32-S3-Touch-LCD-7** I2C header:

| ESP32-S3 7" Touch Pin | PCA9685 Driver (`0x40`) | Description |
| :--- | :--- | :--- |
| **GPIO 8 (SDA)** | SDA | I2C Data line (3.3V) |
| **GPIO 9 (SCL)** | SCL | I2C Clock line (3.3V) |
| **3.3V (VCC)** | VCC | PCA9685 Logic Power |
| **GND** | GND | Common Shared Ground |
| **External V+** | V+ (Screw Terminal) | **External 5V–6V 4A–8A Power Supply / UBEC** |

> [!IMPORTANT]
> **Power Isolation**: Never power the 6 high-torque robotic servos from the ESP32 3.3V/5V pin. Connect an external 5V–6V 5A power supply to the **V+** terminal of the PCA9685 and connect **GND** to the ESP32-S3 GND.

### B. Onboard Display & GT911 Touch Controller Pinout

| Function | ESP32-S3 Pin | Description |
| :--- | :--- | :--- |
| **GT911 SDA** | GPIO 8 | I2C Data (Shared with PCA9685 header) |
| **GT911 SCL** | GPIO 9 | I2C Clock (Shared with PCA9685 header) |
| **GT911 INT** | GPIO 4 | Capacitive Touch Interrupt |
| **GT911 RST** | IO Expander / CH422G | Touch Controller Reset |
| **Display RGB**| GPIO 1-3, 10-18, 38-45 | 16-bit parallel RGB ST7262 interface (800x480) |

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
   - Renders a real-time 2D/3D kinematic simulation of the arm links on the 800x480 display following joint angles at 50Hz.
3. **Cyber Mascot Facial Expressions**:
   - **Idle**: Gentle breathing / blinking eyes.
   - **Yes / Nod**: Happy curved smile eyes.
   - **No / Shake**: Alert wide eyes.
   - **Dance / High Five**: Pulsing star eyes.
   - **Wave**: Playful wink expression.

---

## 5. Arduino IDE Flashing Settings

1. Open `esp32_arm/esp32_arm.ino` in **Arduino IDE**.
2. Board: **ESP32S3 Dev Module** (or **Waveshare ESP32-S3-Touch-LCD-7**).
3. Settings:
   - **USB CDC On Boot**: *Enabled*
   - **Flash Size**: *16MB (128Mb)*
   - **Partition Scheme**: *16MB Flash (3MB APP / 9.9MB FATFS)*
   - **PSRAM**: *OPI PSRAM* (8MB)
4. Click **Upload**.
