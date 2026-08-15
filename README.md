# Hexapod Controller - Multimodal AI Robotics & Control Suite

[![Deploy to Netlify](https://www.netlify.com/img/deploy/button.svg)](https://app.netlify.com/start/deploy?repository=https://github.com/markirgang/Hexapod)

**Hexapod Controller** is a real-time multimodal AI robotics application powered by Google's Gemini Multimodal Live API. It provides voice and video interaction with local hardware components including ESP-32-Touch-LCD microcontrollers, PCA9685 I2C servo drivers, a 6-DOF Robot Arm, a 6-leg Hexapod Robot over Bluetooth / Serial, a Tello drone over UDP, and smart home devices.

---

## Features & Supported Hardware

- **Hexapod Controller GUI**:
  - **` 🤖 Hexapod Bot (ESP-32-Touch-LCD) `**: 6-leg 3-DOF robot controller driven by an **ESP-32-Touch-LCD** module featuring an onboard color LCD display, capacitive touch panel, animated robot mascot, 3-DOF Inverse Kinematics (IK), dual PCA9685 I2C servo drivers (18 servos), and **MAX98357A I2S audio amplifier** (mechanical step thuds, robotic servos, startup/shutdown sci-fi sweeps, alert sirens, and 8-bit dance tracks).
  - **` 🦜 Waveshare 7" Touch-LCD Birds & LED Controller (ESP32-S3-Touch-LCD-7) `**: Unified controller replacing dual Left/Right ESP32s with a **7.0-inch 800x480 Capacitive Touchscreen (GT911)**, an **MCP23017 16-bit I/O Expander Board** for digital on/off outputs (solenoids, LEDs, chirps, motors), **dual PCA9685 I2C Servo Drivers (32 PWM channels)**, **MAX98357A I2S audio amplifier** (natural frequency-modulated bird chirps, trills, parrot squawks, melodic bird songs, and multi-tone symphonies), animated expressive parrot mascot (blinking eyes, singing beak, floating music notes), spotlight sweeping beams, audio spectrum waves, and built-in routines (**Sing & Chirp**, **Spotlight Sweep**, **Turntable Dance**, **Bird Symphony**, **Light Show**).
  - **` ⚙️ PCA9685 Servo Control `**: Direct pulse and angle sliders for PCA9685 PWM servos across Left and Right drivers.
  - **` 🚁 Tello Drone (ESP32-S3-Touch-LCD-7 Bridge) `**: Intelligent drone flight controller and HUD visualizer driven by an **ESP32-S3 7.0-inch 800x480 Capacitive Touchscreen (GT911)** with **MAX98357A I2S audio amplifier** (turbine spool-up takeoff sweeps, descending landing touchdown chimes, aerodynamic flip whooshes, HUD radar sonar pings, low-battery alarms, and emergency sirens). Commands from the PC Thinker Window, Gemini Live AI, and the direct 7" touchscreen are animated on the LCD display and routed to the DJI Tello drone over WiFi UDP (`192.168.10.1:8889`), with dynamic quadcopter spinning rotor animations, artificial horizon attitude gauges, altitude meters, battery warning flasher, and live flight telemetry streaming.
  - **` 🦾 6-DOF Robot Arm (ESP32-S3-Touch-LCD-7B) `**: 6-joint precision servo controller driven by a **Waveshare 7.0-inch 1024x600 HD Capacitive Touchscreen (Version B)** with real-time multi-link kinematic wireframe animation, live joint/Cartesian telemetry, cyber mascot expressions, **MAX98357A I2S audio amplifier** (pneumatic clamp grab & release hisses, industrial servo whine, 4-note pick-and-place success chime, victory fanfare, error alert buzzes), gestures (**Yes/Nod**, **No/Shake**, **High Five**, **Wave**, **Bow**, **Dance**) and demonstration routines (**Pick & Place**).

- **Unified MAX98357A I2S Audio Hardware Architecture**:
  - All 4 ESP32-S3 Touchscreen hardware projects utilize dedicated hardware I2S pins (`I2S_BCLK = GPIO 19`, `I2S_LRC = GPIO 20`, `I2S_DOUT = GPIO 21`) driving a **MAX98357A Class-D mono audio amplifier** module (3.2W into 4Ω at 5V).
  - Non-blocking procedural audio synthesis with smooth cosine anti-pop envelopes, volume control (0-100%), and software mute across all firmwares, Python Tkinter orchestrator, and Web Serial frontend.

- **Multimodal AI Voice & Vision**:
  - Real-time video stream analysis at 1 FPS.
  - Automatic tool call invocation for hardware actions, sound effects, and choreography routines.

---

## Setup & Installation

1. **Create Virtual Environment**:
   ```bash
   python -m venv .venv
   ```

2. **Activate Virtual Environment**:
   - **Windows**: `.venv\Scripts\activate`
   - **Mac/Linux**: `source .venv/bin/activate`

3. **Install Dependencies**:
   ```bash
   pip install -r requirements.txt
   ```

4. **Configure API Keys**:
   Add your Gemini API key to `.env`:
   ```env
   GEMINI_API_KEY=your_gemini_api_key_here
   ```

5. **Launch Hexapod Controller**:
   ```bash
   python main.py
   ```
   Or double-click `run.bat`.

---

## Git & GitHub Version Control

### Visualizing the Git Commit History Tree (`git tree`)
To view a clean graphical representation of your Git commit history directly in the terminal, run:
```bash
git tree
```
Or run the included batch script:
```cmd
git_tree.bat
```
*(Equivalent command: `git log --graph --oneline --all --decorate`)*

### Connecting to GitHub
To push your local **Hexapod** repository to GitHub:
```bash
# 1. Initialize Git (if not already initialized)
git init

# 2. Add files & make a commit
git add .
git commit -m "Initial commit of Hexapod Controller"

# 3. Link remote repository
git remote add origin https://github.com/<your-username>/Hexapod.git

# 4. Push to GitHub
git branch -M main
git push -u origin main
```
For detailed GitHub step-by-step instructions, see [`GITHUB_SETUP.md`](GITHUB_SETUP.md).

---

## Project Structure

```
Hexapod/
├── esp32_tello/
│   ├── esp32_tello.ino         # Waveshare ESP32-S3 7" Touch-LCD Tello Drone Bridge (HUD, Animations & WiFi UDP)
│   └── README.md               # Hardware pinout, WiFi network setup, serial protocol & flashing guide
├── esp32_arm/
│   ├── esp32_arm.ino           # Waveshare ESP32-S3-Touch-LCD-7B 6-DOF Robot Arm firmware (1024x600 HD, 3D IK)
│   └── README.md               # Hardware pinout, wiring table & telemetry guide
├── esp32_hexapod/
│   ├── esp32_hexapod.ino       # ESP-32-Touch-LCD 6-Leg Hexapod firmware (Touch UI, IK & Dual PCA9685)
│   └── README.md               # Hardware pinout, wiring table & flashing instructions
├── esp32_Birds/
│   ├── esp32_Birds.ino         # Waveshare 7" Touch-LCD Birds & LED firmware (MCP23017, Dual PCA9685 & Animations)
│   └── README.md               # Hardware wiring table, I2C address map & protocol guide
├── app.js                      # Web application frontend & Web Serial controller
├── index.html                  # Hexapod Multimodal Web Dashboard
├── main.py                     # Hexapod Controller GUI & Gemini Session Manager
├── style.css                   # Glassmorphic UI styling system
├── git_tree.bat                # Executable Git Commit Tree graph launcher
├── GITHUB_SETUP.md             # Step-by-step GitHub integration guide
└── run.bat                     # Application launcher
```
