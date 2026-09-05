# ESP32 4-Motor AWD Mobile Platform Hardware Guide (Dual LM298 Motor Drivers)

This document describes the hardware configuration, pinout mapping, Dual LM298 (L298N) motor controller wiring diagrams, LED eyes wiring, and flashing instructions for the **Standard 4-Motor AWD Mobile Platform** running on an **ESP32 / ESP32-S3** microcontroller.

---

## 1. Overview & Hardware Specifications

| Feature | **ESP32 4-Motor AWD Mobile Platform** |
| :--- | :--- |
| **Drive Architecture** | **4-Motor All-Wheel Drive (AWD) Differential Drive System** |
| **Motor Controllers** | **2 x LM298 (L298N) Reversing Dual H-Bridge Motor Drivers** |
| **Front LM298 Driver** | **Channel A: Front Left Motor | Channel B: Front Right Motor** |
| **Rear LM298 Driver** | **Channel A: Rear Left Motor | Channel B: Rear Right Motor** |
| **Ultrasonic Sensors** | **4 x HC-SR04 Proximity Sensors (Front, Rear, Left, Right)** |
| **LCD Readout** | **Waveshare ST7789VW 240x320 IPS 2" Live Telemetry & Proximity HUD** |
| **Eye LEDs** | **Digital GPIO Output for Animated Eye LEDs** |
| **Headlights** | **Digital GPIO Output for Front Headlights** |
| **Pan-Tilt Servos** | **Pan-Tilt Servos (Pan 0-180°, Tilt 0-180°)** |
| **Audio Output** | **MAX98357A I2S Mono Audio Amplifier (BCLK=19, LRC=20, DIN=21)** |
| **Microcontroller** | **ESP32 / ESP32-S3 Dev Module** |

---

## 2. Hardware Pinout & Wiring

### A. Dual LM298 (L298N) Motor Driver Wiring

The mobile platform uses two LM298 dual motor drivers to power all 4 wheels independently:

1. **Front LM298 Motor Driver (Front Axle)**:
   - **Channel A (OUT1 & OUT2)**: Front Left Wheel / Motor
   - **Channel B (OUT3 & OUT4)**: Front Right Wheel / Motor
2. **Rear LM298 Motor Driver (Rear Axle)**:
   - **Channel A (OUT1 & OUT2)**: Rear Left Wheel / Motor
   - **Channel B (OUT3 & OUT4)**: Rear Right Wheel / Motor

#### 1. Front LM298 Driver Wiring Table
| ESP32 Pin | Front LM298 Pin | Target Motor | Description / Function |
| :--- | :--- | :--- | :--- |
| **GPIO 11** | **IN1** | Front Left Motor | Direction Line 1 |
| **GPIO 12** | **IN2** | Front Left Motor | Direction Line 2 |
| **GPIO 13** | **ENA** | Front Left Motor | ENA PWM Speed Control |
| **GPIO 14** | **IN3** | Front Right Motor | Direction Line 1 |
| **GPIO 15** | **IN4** | Front Right Motor | Direction Line 2 |
| **GPIO 16** | **ENB** | Front Right Motor | ENB PWM Speed Control |

#### 2. Rear LM298 Driver Wiring Table
| ESP32 Pin | Rear LM298 Pin | Target Motor | Description / Function |
| :--- | :--- | :--- | :--- |
| **GPIO 1** | **IN1** | Rear Left Motor | Direction Line 1 |
| **GPIO 2** | **IN2** | Rear Left Motor | Direction Line 2 |
| **GPIO 42** | **ENA** | Rear Left Motor | ENA PWM Speed Control |
| **GPIO 41** | **IN3** | Rear Right Motor | Direction Line 1 |
| **GPIO 8** | **IN4** | Rear Right Motor | Direction Line 2 |
| **GPIO 9** | **ENB** | Rear Right Motor | ENB PWM Speed Control |

#### 3. 3rd Auxiliary LM298 Driver Wiring Table (Mouth & Body Actuators)
| ESP32 Pin | Aux LM298 Pin | Target Actuator | Description / Function |
| :--- | :--- | :--- | :--- |
| **GPIO 35** | **IN1** | Mouth Motor | Direction Line 1 (Channel A) |
| **GPIO 36** | **IN2** | Mouth Motor | Direction Line 2 (Channel A) |
| **GPIO 37** | **ENA** | Mouth Motor | ENA PWM Speed Control |
| **GPIO 26** | **IN3** | Body Motor | Direction Line 1 (Channel B) |
| **GPIO 27** | **IN4** | Body Motor | Direction Line 2 (Channel B) |
| **GPIO 33** | **ENB** | Body Motor | ENB PWM Speed Control |

> [!IMPORTANT]
> **Power Isolation**: Do **NOT** power the LM298 motor controllers or DC motors from the ESP32 3.3V pin. Connect an external 6V–12V battery power source to the **VCC (+12V)** terminals of all three LM298 drivers and ensure **GND** is common between the ESP32 and all LM298 modules.

### B. Waveshare ST7789VW 240x320 IPS 2" SPI Display Wiring Table
| ESP32 Pin | ST7789 Module Pin | Description / Function |
| :--- | :--- | :--- |
| **GPIO 47** | **DIN / MOSI** | SPI Data Input |
| **GPIO 48** | **CLK / SCLK** | SPI Clock Input |
| **GPIO 46** | **CS** | Chip Select (Active Low) |
| **GPIO 0** | **DC / RS** | Data / Command Selection |
| **GPIO 44** | **RST** | Reset (Active Low) |
| **GPIO 43** | **BL** | Display Backlight Enable |
| **3.3V** | **VCC** | 3.3V Power Supply |
### C. N-Channel MOSFET Headlight Driver Wiring Table
| ESP32 Pin | MOSFET Module Terminal | Target Load | Description / Function |
| :--- | :--- | :--- | :--- |
| **GPIO 7** | **SIG** | Gate Control Signal | PWM Dimming / ON-OFF Control Line |
| **GND** | **GND** | Signal Ground | Common ESP32 Ground |
| **External V+ (5V/12V)** | **V+ (Power In)** | Power Supply | Main Battery (+5V to +12V) |
| **External GND** | **V- (Power In)** | Battery Ground | Main Battery Ground (-) |
| **LED Headlight (+)** | **OUT+** | High-Power Headlight | LED Positive Terminal |
| **LED Headlight (-)** | **OUT-** | High-Power Headlight | MOSFET Drain Output Terminal |

---

## 3. 4WD Motion Matrix

| Action | Front Left (Front LM298 Ch A) | Front Right (Front LM298 Ch B) | Rear Left (Rear LM298 Ch A) | Rear Right (Rear LM298 Ch B) |
| :--- | :--- | :--- | :--- | :--- |
| **`forward`** | FORWARD | FORWARD | FORWARD | FORWARD |
| **`back`** | REVERSE | REVERSE | REVERSE | REVERSE |
| **`turn_left`** | REVERSE / SLOW | FORWARD | REVERSE / SLOW | FORWARD |
| **`turn_right`** | FORWARD | REVERSE / SLOW | FORWARD | REVERSE / SLOW |
| **`spin_left`** | REVERSE | FORWARD | REVERSE | FORWARD |
| **`spin_right`** | FORWARD | REVERSE | FORWARD | REVERSE |
| **`stop`** | OFF (PWM=0) | OFF (PWM=0) | OFF (PWM=0) | OFF (PWM=0) |

---

## 4. Serial & Audio Command Protocol

| Command Format | Example | Description |
| :--- | :--- | :--- |
| `ROVER:<action>` | `ROVER:forward` / `ROVER:stop` | Rover motion (`forward`, `back`, `turn_left`, `turn_right`, `spin_left`, `spin_right`, `stop`, `patrol`, `spin_360`, `dance`, `obstacle_avoidance`) |
| `ROVER:OBSTACLE_AVOID:<1\|0>` | `ROVER:OBSTACLE_AVOID:1` | Enables (`1`) or disables (`0`) automatic 4-way ultrasonic obstacle detection and evasion |
| `ROVER:OBSTACLE_THRESH:<cm>` | `ROVER:OBSTACLE_THRESH:20` | Sets obstacle evasion proximity threshold distance in centimeters (default: 20 cm) |
| `ROVER:EYES:<1\|0>` | `ROVER:EYES:1` | Eye LEDs Output: `1` = Power ON, `0` = Power OFF |
| `ROVER:SPEED:<0-100>` | `ROVER:SPEED:80` | Sets 4WD drive motor speed percentage |
| `ROVER:PANTILT:<pan>:<tilt>` | `ROVER:PANTILT:90:45` | Sets Pan-Tilt camera servo angles in degrees |
| `ROVER:LED:<1\|0>` | `ROVER:LED:1` | Turns front headlights ON (`1`) or OFF (`0`) |
| `ROVER:LCD:MSG:<msg>` | `ROVER:LCD:MSG:Patrolling` | Custom status message |
| `AI_SPEAKING:<1\|0>` | `AI_SPEAKING:1` | Real-time AI talking signal: `1` = Eyes illuminate & pulse; `0` = Eyes idle |
| `AUDIO:ROVER_ENGINE` | `AUDIO:ROVER_ENGINE` | Plays engine hum sound effect over MAX98357A I2S |
| `AUDIO:HORN` | `AUDIO:HORN` | Plays dual-tone horn alert |
| `AUDIO:STARTUP` | `AUDIO:STARTUP` | Plays vehicle power-up sweep |
| `AUDIO:SHUTDOWN` | `AUDIO:SHUTDOWN` | Plays descending vehicle power-down sweep |
| `AUDIO:ALERT` | `AUDIO:ALERT` | Plays emergency siren warning tone |
| `AUDIO:TURBO` | `AUDIO:TURBO` | Plays turbo boost acceleration sound effect |
| `AUDIO:BRAKE` | `AUDIO:BRAKE` | Plays braking sound effect |

---

## 5. 📶 Connectivity & Python App Setup

The 4WD Rover firmware supports USB CDC Serial and Bluetooth communication:
- **Bluetooth Broadcast Name**: `waverover` (also supports `rover` or `wave-rover`)
- **Baud Rate**: `115200`
- **Setup**:
  1. Power on the ESP32 4WD Rover module.
  2. Pair over Bluetooth or connect USB cable to your PC.
  3. Launch `main.py` (GUI or CLI mode) and select the `waverover` COM / Bluetooth port.

---

## 6. Flashing Firmware (Arduino IDE / PlatformIO)

1. Open `esp32_waverover/esp32_waverover.ino` in **Arduino IDE**.
2. Select Board: **ESP32 Dev Module** or **ESP32S3 Dev Module**.
3. Select COM Port and click **Upload**.
