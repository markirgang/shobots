# Shobots Controller - Multimodal AI Robotics & Control Suite

[![Deploy to Netlify](https://www.netlify.com/img/deploy/button.svg)](https://app.netlify.com/start/deploy?repository=https://github.com/markirgang/Shobots)

**Shobots Controller** is a real-time multimodal AI robotics application powered by Google's Gemini Multimodal Live API. It provides voice and video interaction with local hardware components including ESP32 microcontrollers, PCA9685 I2C servo drivers, a 6-DOF Robot Arm, a 6-leg Hexapod over Bluetooth, a Tello drone over UDP, and smart home devices.

---

## Features & Supported Hardware

- **Shobots Controller GUI**:
  - **` 🔘 GPIO Pin Controls `**: Control up to 22 digital outputs across Dual ESP32 boards.
  - **` ⚙️ PCA9685 Servo Control `**: Direct pulse and angle sliders for 14 PCA9685 PWM servos.
  - **` 🕷️ Hexapod `**: 6-leg 3-DOF robot controller over Bluetooth (`broadcast: hexapod`).
  - **` 🚁 Tello Drone `**: Direct flight navigation and directional movement controller over UDP.
  - **` 🦾 6-DOF Robot Arm `**: 6-joint precision servo controller with gestures (**Yes/Nod**, **No/Shake**, **High Five**, **Wave**, **Bow**, **Dance**) and demonstration routines (**Pick & Place**).

- **Multimodal AI Voice & Vision**:
  - Real-time video stream analysis at 1 FPS.
  - Automatic tool call invocation for hardware actions.

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

5. **Launch Shobots Controller**:
   ```bash
   python main.py
   ```
   Or double-click [`run.bat`](file:///c:/Users/marki/OneDrive/Desktop/Hexapod/run.bat).

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
To push your local **Shobots** repository to GitHub:
```bash
# 1. Initialize Git (if not already initialized)
git init

# 2. Add files & make a commit
git add .
git commit -m "Initial commit of Shobots Controller"

# 3. Link remote repository
git remote add origin https://github.com/<your-username>/Shobots.git

# 4. Push to GitHub
git branch -M main
git push -u origin main
```
For detailed GitHub step-by-step instructions, see [`GITHUB_SETUP.md`](file:///c:/Users/marki/OneDrive/Desktop/Hexapod/GITHUB_SETUP.md).

---

## Project Structure

```
Shobots/
├── esp32_hexapod/
│   └── esp32_hexapod.ino       # ESP32 6-Leg Hexapod firmware (IK & Trajectory Interpolation)
├── esp32_led/
│   └── esp32_led.ino           # ESP32 dual-board & Robot Arm firmware
├── app.js                      # Web application frontend & Web Serial controller
├── index.html                  # Shobots Multimodal Web Dashboard
├── main.py                     # Shobots Controller GUI & Gemini Session Manager
├── style.css                   # Glassmorphic UI styling system
├── git_tree.bat                # Executable Git Commit Tree graph launcher
├── GITHUB_SETUP.md             # Step-by-step GitHub integration guide
└── run.bat                     # Application launcher
```
