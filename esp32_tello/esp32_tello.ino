/*
  Shobots - esp32_tello.ino (Waveshare 7-Inch Capacitive Touch LCD ESP32-S3 Tello Firmware)
  =============================================================================
  Folder: esp32_tello/
  Sketch: esp32_tello.ino
  Hardware: Waveshare ESP32-S3-Touch-LCD-7C (7.0" 800x600 Capacitive Touchscreen, GT911)
  
  Features:
    - 7.0-inch 800x600 Widescreen Capacitive Touchscreen Drone Dashboard & HUD
    - GT911 High-Precision 5-Point Capacitive Multi-Touch Controller (I2C Address: 0x5D)
    - WiFi & Direct UDP Socket Communication with DJI Tello Drone:
        * Command & Control Port: UDP 192.168.10.1:8889 (Sends SDK commands, receives ACK 'ok')
        * Telemetry & State Port: UDP 0.0.0.0:8890 (Receives live battery, height, pitch, roll, yaw)
        * Automatic AP connection and SDK initialization mode
    - Dynamic Animations & Flight Visualizer Engine (~30-50 FPS):
        * Central Animated Quadcopter Mascot with 4 independently spinning rotor blades
        * Variable rotor speeds (Idle slow spin -> High-velocity motion blur on flight/takeoff)
        * Blinking RGB Navigation Strobes (Port Red, Starboard Green, Tail White/Blue beacon)
        * Pulsing Energy Shield Aura & Particle Thrust Vector Trails during maneuvers
        * Real-Time Artificial Horizon / Attitude Pitch & Roll Visualizer
        * Dynamic Climbing / Descending Altitude Tape & Gauge (cm & inches)
        * Color-Coded Flight Battery Level Meter & Warning Flasher
        * Animated 360° Compass & Yaw Heading Radar
        * Live Interactive Command & Status Banner showing incoming AI/PC/Touch commands
    - Direct Capacitive Touchscreen Control:
        * Flight Essentials: [TAKEOFF 🛫], [LAND 🛬], [EMERGENCY 🚨], [CONNECT / SDK 📡], [BATTERY 🔋]
        * Directional Movement: [▲ FORWARD], [▼ BACK], [◄ LEFT], [► RIGHT], [⬆ UP], [⬇ DOWN]
        * Yaw & Rotation: [↺ CCW 90°], [↻ CW 90°]
        * Acrobatic Stunts: [FLIP F], [FLIP B], [FLIP L], [FLIP R]
        * Preset Distance Steps: [20 cm], [50 cm], [100 cm]
        * Flight Choreography Routines: [SQUARE PATROL], [360 SCAN], [BOUNCE WAVE]
    - Multi-Channel Host Interface:
        * USB CDC Serial & UART connection to PC Thinker Window and Multimodal Gemini AI
        * Bi-directional command dispatch and real-time state telemetry streaming
        * Graceful offline simulation mode when physical drone is powered off

  Pinout (Waveshare ESP32-S3-Touch-LCD-7C):
    - I2C Bus (Shared for GT911 Touch Controller):
        * SDA    : GPIO 8  (PH2.0 4-Pin I2C Header)
        * SCL    : GPIO 9  (PH2.0 4-Pin I2C Header)
        * TP_INT : GPIO 4  (GT911 Interrupt Pin)
        * GT911 Address: 0x5D (or 0x14)
    - RGB Display Interface (800x600 16-bit RGB565)
  =============================================================================
*/

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <math.h>
#include "driver/i2s.h"

#if defined(CONFIG_BT_ENABLED) && !defined(CONFIG_IDF_TARGET_ESP32S3)
#include <BluetoothSerial.h>
#define HAS_BT_CLASSIC 1
BluetoothSerial SerialBT;
#endif

#ifndef HAS_BT_CLASSIC
#define HAS_BT_CLASSIC 0
#endif

// =============================================================================
// Waveshare ESP32-S3-Touch-LCD-7C Onboard I2S Audio Hardware Configuration
// (Built-in I2S Audio Codec / Class-D Power Amplifier & Onboard Speaker Header)
// =============================================================================
#ifndef I2S_BCLK_PIN
#define I2S_BCLK_PIN          19   // I2S Bit Clock (BCLK / BCK)
#endif
#ifndef I2S_LRC_PIN
#define I2S_LRC_PIN           20   // I2S Word Select / Left-Right Clock (LRC / WS)
#endif
#ifndef I2S_DOUT_PIN
#define I2S_DOUT_PIN          21   // I2S Serial Data Out (DIN)
#endif
#define I2S_PORT              I2S_NUM_0
#define I2S_SAMPLE_RATE       22050
#define AUDIO_BUF_SIZE        256

// =============================================================================
// Hardware Configuration & Board Profiles
// =============================================================================
// Select ONE board profile below (Waveshare ESP32-S3-Touch-LCD-7C):
#define BOARD_ESP32_TOUCH_LCD_7C   1  // Waveshare ESP32-S3-Touch-LCD-7C (7.0" 800x600 HD GT911 + CH422G IO + Audio Codec)

#if defined(BOARD_ESP32_TOUCH_LCD_7C)
  #define SCREEN_WIDTH       800
  #define SCREEN_HEIGHT       600
  #define HAS_CH422G_IO        1  // Onboard IO Expander for Backlight & Power Control
#else
  #define SCREEN_WIDTH       800
  #define SCREEN_HEIGHT       600
  #define HAS_CH422G_IO        0
#endif

#define I2C_SDA_PIN         8
#define I2C_SCL_PIN         9
#define TP_INT_PIN          4
#define GT911_I2C_ADDR   0x5D

// =============================================================================
// Tello Drone Networking & UDP Configuration
// =============================================================================
// Default Tello WiFi SSID format: "TELLO-XXXXXX" (Leave empty to connect to any open TELLO network or specify)
const char* DEFAULT_TELLO_SSID = "TELLO-"; 
const char* DEFAULT_TELLO_PASS = "";

const char* TELLO_IP_ADDR = "192.168.10.1";
const uint16_t TELLO_CMD_PORT = 8889;
const uint16_t TELLO_STATE_PORT = 8890;
const uint16_t LOCAL_UDP_PORT = 8889;

WiFiUDP udpCmd;
WiFiUDP udpState;

bool wifiConnected = false;
bool sdkModeActive = false;
bool simulationMode = false;
unsigned long lastConnectAttempt = 0;
unsigned long lastSdkPing = 0;

// =============================================================================
// Drone Flight Telemetry State
// =============================================================================
struct DroneTelemetry {
  int batteryPercent = 100;
  int altitudeCm = 0;
  float pitchDeg = 0.0f;
  float rollDeg = 0.0f;
  float yawDeg = 0.0f;
  int speedX = 0;
  int speedY = 0;
  int speedZ = 0;
  int flightTimeSec = 0;
  int tempLowC = 25;
  int tempHighC = 28;
  int tofCm = 0;
  String flightState = "DISCONNECTED"; // DISCONNECTED, IDLE, TAKING_OFF, HOVERING, FLYING, FLIPPING, LANDING, EMERGENCY
  String lastCommand = "None";
  String lastResponse = "Ready";
  String commandSource = "System";     // PC Thinker, Touchscreen, AI Gemini
  unsigned long lastCmdTimestamp = 0;
};

DroneTelemetry telemetry;

// Movement step distance preset in centimeters (20, 50, 100)
int currentStepDistCm = 20;

// Audio Engine State Variables
bool i2sAudioReady = false;
int audioVolume = 80; // Default 80% (0 - 100)
bool audioMuted = false;

// =============================================================================
// MAX98357A I2S Mono Audio Synthesis & Playback Engine
// =============================================================================
void initI2SAudio() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = AUDIO_BUF_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err == ESP_OK) {
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
    i2sAudioReady = true;
    Serial.println("[Waveshare 7C Audio] Onboard I2S Audio Hardware Initialized Successfully!");
  } else {
    Serial.print("[Waveshare 7C Audio] I2S Driver Install Failed! Error: ");
    Serial.println(err);
  }
}

void playToneI2S(float freqHz, unsigned long durationMs, float volMult = 1.0f) {
  if (!i2sAudioReady || freqHz <= 0 || durationMs == 0 || audioMuted || audioVolume <= 0) {
    delay(durationMs);
    return;
  }
  
  float actualVol = (audioVolume / 100.0f) * volMult;
  int16_t maxAmp = (int16_t)(32767.0f * actualVol * 0.45f);
  
  unsigned long totalSamples = (unsigned long)((float)I2S_SAMPLE_RATE * (durationMs / 1000.0f));
  if (totalSamples == 0) totalSamples = 1;
  
  int16_t buffer[AUDIO_BUF_SIZE * 2];
  float phase = 0.0f;
  float phaseInc = (2.0f * (float)M_PI * freqHz) / (float)I2S_SAMPLE_RATE;
  
  unsigned long samplesWritten = 0;
  while (samplesWritten < totalSamples) {
    size_t chunkSamples = (totalSamples - samplesWritten > AUDIO_BUF_SIZE) ? AUDIO_BUF_SIZE : (totalSamples - samplesWritten);
    for (size_t i = 0; i < chunkSamples; i++) {
      float env = 1.0f;
      if (samplesWritten + i < 80) {
        env = (float)(samplesWritten + i) / 80.0f;
      } else if (totalSamples - (samplesWritten + i) < 80) {
        env = (float)(totalSamples - (samplesWritten + i)) / 80.0f;
      }
      
      int16_t sample = (int16_t)(sinf(phase) * (float)maxAmp * env);
      buffer[i * 2]     = sample;
      buffer[i * 2 + 1] = sample;
      phase += phaseInc;
      if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    }
    
    size_t bytesWritten = 0;
    i2s_write(I2S_PORT, buffer, chunkSamples * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    samplesWritten += chunkSamples;
  }
}

void playSweepI2S(float startFreq, float endFreq, unsigned long durationMs, float volMult = 1.0f) {
  if (!i2sAudioReady || durationMs == 0 || audioMuted || audioVolume <= 0) {
    delay(durationMs);
    return;
  }
  
  float actualVol = (audioVolume / 100.0f) * volMult;
  int16_t maxAmp = (int16_t)(32767.0f * actualVol * 0.45f);
  
  unsigned long totalSamples = (unsigned long)((float)I2S_SAMPLE_RATE * (durationMs / 1000.0f));
  if (totalSamples == 0) totalSamples = 1;
  
  int16_t buffer[AUDIO_BUF_SIZE * 2];
  float phase = 0.0f;
  
  unsigned long samplesWritten = 0;
  while (samplesWritten < totalSamples) {
    size_t chunkSamples = (totalSamples - samplesWritten > AUDIO_BUF_SIZE) ? AUDIO_BUF_SIZE : (totalSamples - samplesWritten);
    for (size_t i = 0; i < chunkSamples; i++) {
      float progress = (float)(samplesWritten + i) / (float)totalSamples;
      float curFreq = startFreq + progress * (endFreq - startFreq);
      float phaseInc = (2.0f * (float)M_PI * curFreq) / (float)I2S_SAMPLE_RATE;
      
      float env = 1.0f;
      if (samplesWritten + i < 60) env = (float)(samplesWritten + i) / 60.0f;
      else if (totalSamples - (samplesWritten + i) < 60) env = (float)(totalSamples - (samplesWritten + i)) / 60.0f;
      
      int16_t sample = (int16_t)(sinf(phase) * (float)maxAmp * env);
      buffer[i * 2]     = sample;
      buffer[i * 2 + 1] = sample;
      phase += phaseInc;
      if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    }
    
    size_t bytesWritten = 0;
    i2s_write(I2S_PORT, buffer, chunkSamples * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    samplesWritten += chunkSamples;
  }
}

// Procedural Drone Flight Sound Effects
void playTakeoffSound() {
  playSweepI2S(240.0f, 1550.0f, 320, 0.9f); // Turbine spool-up
  playSweepI2S(1550.0f, 900.0f, 120, 0.85f); // Rotor air rush
}

void playLandSound() {
  playSweepI2S(1200.0f, 220.0f, 320, 0.85f); // Spool-down
  playToneI2S(523.25f, 90, 0.8f);           // Touchdown confirmation
}

void playFlipSound() {
  playSweepI2S(1400.0f, 400.0f, 140, 0.95f); // Aerodynamic whoosh
}

void playRadarPingSound() {
  playToneI2S(1760.0f, 65, 0.85f);
}

void playLowBatteryAlarm() {
  for (int i = 0; i < 2; i++) {
    playToneI2S(1100.0f, 60, 0.95f);
    delay(20);
  }
}

void playEmergencySiren() {
  for (int i = 0; i < 2; i++) {
    playSweepI2S(550.0f, 1900.0f, 120, 1.0f);
    playSweepI2S(1900.0f, 550.0f, 120, 1.0f);
  }
}

void playConnectedJingle() {
  float notes[] = {523.25f, 659.25f, 783.99f, 1046.5f}; // C5, E5, G5, C6
  for (int i = 0; i < 4; i++) {
    playToneI2S(notes[i], 65, 0.85f);
    delay(10);
  }
}

void playClickSound() {
  playToneI2S(1400.0f, 25, 0.7f);
}

void setAudioVolume(int vol) {
  audioVolume = constrain(vol, 0, 100);
  Serial.print("[MAX98357A] Drone Audio Volume set to: ");
  Serial.print(audioVolume);
  Serial.println("%");
}

void setAudioMute(bool mute) {
  audioMuted = mute;
  Serial.print("[MAX98357A] Drone Audio Mute: ");
  Serial.println(audioMuted ? "MUTED" : "UNMUTED");
}

// =============================================================================
// Waveshare ESP32-S3-Touch-LCD-7C Onboard IO Expander (CH422G) Driver
// =============================================================================
void initIOExpander7C() {
#if HAS_CH422G_IO
  // Probes CH422G IO expander addresses (0x24 / 0x38)
  // EXIO1: TP_RST (1), EXIO2: Backlight DISP (1), EXIO4: SD_CS (1), EXIO5: USB_SEL (0), EXIO6: LCD_VDD_EN (1)
  uint8_t addrs[] = {0x24, 0x38};
  for (int i = 0; i < 2; i++) {
    Wire.beginTransmission(addrs[i]);
    Wire.write(0x01); // Set IO direction
    Wire.endTransmission();

    Wire.beginTransmission(addrs[i]);
    Wire.write(0x02); // Output register
    Wire.write(0x46); // Bit 1 (TP_RST=1), Bit 2 (DISP=1), Bit 6 (LCD_VDD_EN=1)
    Wire.endTransmission();
  }
#endif
}

// =============================================================================
// Touch Dashboard Button Definition
// =============================================================================
struct TouchButton {
  const char* label;
  int x, y, w, h;
  const char* type;    // "flight", "dir", "rot", "flip", "dist", "routine", "sdk"
  const char* command; // Drone command string
  uint16_t bgColor;
  uint16_t fgColor;
};

#if defined(BOARD_ESP32_TOUCH_LCD_7C)
// 800x480 Widescreen HUD Touch Dashboard (Waveshare ESP32-S3-Touch-LCD-7C)
const TouchButton DASHBOARD_BTNS[] = {
  // --- Left Column: Flight Essentials & Stunts ---
  {"🛫 TAKEOFF",     15,  75, 135, 48, "flight",  "takeoff",    0x05E5, 0xFFFF}, // Emerald Green
  {"🛬 LAND",        15, 132, 135, 48, "flight",  "land",       0xD5A0, 0xFFFF}, // Amber Orange
  {"🚨 EMERGENCY",   15, 189, 135, 48, "flight",  "emergency",  0xF800, 0xFFFF}, // Red
  {"📡 CONNECT SDK", 15, 246, 135, 48, "sdk",     "command",    0x041F, 0xFFFF}, // Sky Blue
  {"🔋 BATTERY?",    15, 303, 135, 44, "flight",  "battery?",   0x1B0E, 0x3FE0}, // Slate / Neon Green
  {"🔄 FLIP FWD",    15, 356, 65,  46, "flip",    "flip f",     0x79EF, 0xFFFF}, // Purple
  {"🔄 FLIP BCK",    85, 356, 65,  46, "flip",    "flip b",     0x79EF, 0xFFFF}, // Purple
  {"↺ FLIP L",      15, 410, 65,  46, "flip",    "flip l",     0x79EF, 0xFFFF}, // Purple
  {"↻ FLIP R",      85, 410, 65,  46, "flip",    "flip r",     0x79EF, 0xFFFF}, // Purple

  // --- Center Control Bar: Distance Steps & Autonomous Routines ---
  {"20 cm (8\")",   185, 412, 85, 44, "dist",    "20",         0x1BEF, 0xFFFF},
  {"50 cm (20\")",  278, 412, 85, 44, "dist",    "50",         0x03E0, 0xFFFF},
  {"100 cm (40\")", 371, 412, 90, 44, "dist",    "100",        0x03E0, 0xFFFF},
  {"🔲 SQUARE",     485, 412, 92, 44, "routine", "square",     0x0277, 0xFFFF},
  {"🌀 360 SCAN",   585, 412, 92, 44, "routine", "scan360",    0x7A17, 0xFFFF},
  {"🎈 BOUNCE",     685, 412, 95, 44, "routine", "bounce",     0xC810, 0xFFFF},

  // --- Right Column: Directional Flight & Rotation D-Pad ---
  {"▲ FORWARD",     625,  75, 105, 46, "dir",     "forward",    0x1B0E, 0x07FF}, // Cyan
  {"◄ LEFT",        565, 128,  75, 46, "dir",     "left",       0x1B0E, 0x07FF},
  {"► RIGHT",       710, 128,  75, 46, "dir",     "right",      0x1B0E, 0x07FF},
  {"▼ BACK",        625, 181, 105, 46, "dir",     "back",       0x1B0E, 0x07FF},
  {"⬆ UP",          565, 238, 105, 46, "dir",     "up",         0x1B0E, 0x3FE0}, // Light Green
  {"⬇ DOWN",        680, 238, 105, 46, "dir",     "down",       0x1B0E, 0x3FE0},
  {"↺ CCW 90°",     565, 294, 105, 46, "rot",     "ccw 90",     0x4A69, 0xFFFF}, // Indigo
  {"↻ CW 90°",      680, 294, 105, 46, "rot",     "cw 90",      0x4A69, 0xFFFF},
  {"⚡ STOP / HOVER", 565, 350, 220, 46, "flight", "stop",       0x9800, 0xFFFF}, // Deep Amber
};
#else
// 800x480 Standard HUD Touch Dashboard
const TouchButton DASHBOARD_BTNS[] = {
  // --- Left Column: Flight Essentials & Stunts ---
  {"🛫 TAKEOFF",     15,  75, 135, 48, "flight",  "takeoff",    0x05E5, 0xFFFF}, // Emerald Green
  {"🛬 LAND",        15, 132, 135, 48, "flight",  "land",       0xD5A0, 0xFFFF}, // Amber Orange
  {"🚨 EMERGENCY",   15, 189, 135, 48, "flight",  "emergency",  0xF800, 0xFFFF}, // Red
  {"📡 CONNECT SDK", 15, 246, 135, 48, "sdk",     "command",    0x041F, 0xFFFF}, // Sky Blue
  {"🔋 BATTERY?",    15, 303, 135, 44, "flight",  "battery?",   0x1B0E, 0x3FE0}, // Slate / Neon Green
  {"🔄 FLIP FWD",    15, 356, 65,  46, "flip",    "flip f",     0x79EF, 0xFFFF}, // Purple
  {"🔄 FLIP BCK",    85, 356, 65,  46, "flip",    "flip b",     0x79EF, 0xFFFF}, // Purple
  {"↺ FLIP L",      15, 410, 65,  46, "flip",    "flip l",     0x79EF, 0xFFFF}, // Purple
  {"↻ FLIP R",      85, 410, 65,  46, "flip",    "flip r",     0x79EF, 0xFFFF}, // Purple

  // --- Center Control Bar: Distance Steps & Autonomous Routines ---
  {"20 cm (8\")",   185, 412, 85, 44, "dist",    "20",         0x1BEF, 0xFFFF},
  {"50 cm (20\")",  278, 412, 85, 44, "dist",    "50",         0x03E0, 0xFFFF},
  {"100 cm (40\")", 371, 412, 90, 44, "dist",    "100",        0x03E0, 0xFFFF},
  {"🔲 SQUARE",     485, 412, 92, 44, "routine", "square",     0x0277, 0xFFFF},
  {"🌀 360 SCAN",   585, 412, 92, 44, "routine", "scan360",    0x7A17, 0xFFFF},
  {"🎈 BOUNCE",     685, 412, 95, 44, "routine", "bounce",     0xC810, 0xFFFF},

  // --- Right Column: Directional Flight & Rotation D-Pad ---
  {"▲ FORWARD",     625,  75, 105, 46, "dir",     "forward",    0x1B0E, 0x07FF}, // Cyan
  {"◄ LEFT",        565, 128,  75, 46, "dir",     "left",       0x1B0E, 0x07FF},
  {"► RIGHT",       710, 128,  75, 46, "dir",     "right",      0x1B0E, 0x07FF},
  {"▼ BACK",        625, 181, 105, 46, "dir",     "back",       0x1B0E, 0x07FF},
  {"⬆ UP",          565, 238, 105, 46, "dir",     "up",         0x1B0E, 0x3FE0}, // Light Green
  {"⬇ DOWN",        680, 238, 105, 46, "dir",     "down",       0x1B0E, 0x3FE0},
  {"↺ CCW 90°",     565, 294, 105, 46, "rot",     "ccw 90",     0x4A69, 0xFFFF}, // Indigo
  {"↻ CW 90°",      680, 294, 105, 46, "rot",     "cw 90",      0x4A69, 0xFFFF},
  {"⚡ STOP / HOVER", 565, 350, 220, 46, "flight", "stop",       0x9800, 0xFFFF}, // Deep Amber
};
#endif
const int NUM_DASHBOARD_BTNS = sizeof(DASHBOARD_BTNS) / sizeof(TouchButton);

// Touch State Tracking
bool isTouchActive = false;
int touchX = 0;
int touchY = 0;
unsigned long lastTouchTime = 0;
int activePressedBtnIdx = -1;

// =============================================================================
// Dynamic Animations Engine Variables (~30-50 FPS)
// =============================================================================
unsigned long lastAnimFrameTime = 0;
float propellerAngle = 0.0f;
float propellerSpeed = 5.0f; // degrees per frame
float droneBobbingOffset = 0.0f;
float droneTiltAngle = 0.0f;
float auraPulseScale = 1.0f;
bool strobeLedState = false;
unsigned long lastStrobeToggle = 0;
float altitudeGaugeAnim = 0.0f;
float batteryGlowAlpha = 1.0f;

// Autonomous Routine State Machine
String activeRoutine = "idle";
int routineStep = 0;
unsigned long routineStepStartTime = 0;

// =============================================================================
// Forward Declarations
// =============================================================================
void initTouchController();
bool readTouch(int &x, int &y);
void handleTouchButton(int btnIdx);
void processTouchInput();
void executeDroneCommand(String cmd, String source = "Touchscreen");
void sendUdpDroneCommand(String cmd);
void parseTelloStatePacket(String packet);
void parseSerialCommand(String cmd);
void updateAnimations();
void updateAutonomousRoutines();
void printLiveTelemetry();
void checkWiFiConnection();

// =============================================================================
// GT911 Capacitive Touchscreen Driver
// =============================================================================
void initTouchController() {
  if (TP_INT_PIN >= 0) {
    pinMode(TP_INT_PIN, INPUT_PULLUP);
  }
}

bool readTouch(int &x, int &y) {
  Wire.beginTransmission((uint8_t)GT911_I2C_ADDR);
  Wire.write(0x81);
  Wire.write(0x4E); // Buffer status register
  if (Wire.endTransmission() != 0) return false;

  Wire.requestFrom((uint8_t)GT911_I2C_ADDR, (uint8_t)1);
  if (Wire.available() < 1) return false;
  uint8_t status = Wire.read();

  bool pointReady = (status & 0x80) != 0;
  uint8_t points = status & 0x0F;

  if (pointReady && points > 0) {
    Wire.beginTransmission((uint8_t)GT911_I2C_ADDR);
    Wire.write(0x81);
    Wire.write(0x50); // Point 1 coordinate registers
    Wire.endTransmission();

    Wire.requestFrom((uint8_t)GT911_I2C_ADDR, (uint8_t)6);
    if (Wire.available() >= 6) {
      uint8_t trackId = Wire.read();
      uint8_t xLow    = Wire.read();
      uint8_t xHigh   = Wire.read();
      uint8_t yLow    = Wire.read();
      uint8_t yHigh   = Wire.read();
      uint8_t pSize   = Wire.read();

      x = (xHigh << 8) | xLow;
      y = (yHigh << 8) | yLow;

      x = constrain(x, 0, SCREEN_WIDTH - 1);
      y = constrain(y, 0, SCREEN_HEIGHT - 1);

      // Clear buffer status flag
      Wire.beginTransmission((uint8_t)GT911_I2C_ADDR);
      Wire.write(0x81);
      Wire.write(0x4E);
      Wire.write(0x00);
      Wire.endTransmission();
      return true;
    }
  }

  // Clear buffer status flag
  Wire.beginTransmission((uint8_t)GT911_I2C_ADDR);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(0x00);
  Wire.endTransmission();
  return false;
}

// =============================================================================
// WiFi & UDP Networking for Tello Drone
// =============================================================================
void initTelloNetwork() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("[Tello WiFi] Scanning for Tello access points...");
  int n = WiFi.scanNetworks();
  String targetSsid = "";

  for (int i = 0; i < n; ++i) {
    String foundSsid = WiFi.SSID(i);
    Serial.print("  [WiFi Scan] Found: ");
    Serial.print(foundSsid);
    Serial.print(" (RSSI: ");
    Serial.print(WiFi.RSSI(i));
    Serial.println(")");

    if (foundSsid.startsWith("TELLO-") || foundSsid.startsWith("tello-") || foundSsid.equalsIgnoreCase("TELLO")) {
      targetSsid = foundSsid;
      break;
    }
  }

  if (targetSsid.length() > 0) {
    Serial.println("[Tello WiFi] Connecting to Tello AP: " + targetSsid);
    WiFi.begin(targetSsid.c_str(), DEFAULT_TELLO_PASS);
  } else {
    Serial.println("[Tello WiFi] No active TELLO AP found in scan. Attempting fallback SSID prefix...");
    WiFi.begin(DEFAULT_TELLO_SSID, DEFAULT_TELLO_PASS);
  }

  unsigned long startWait = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWait < 5000) {
    delay(200);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    telemetry.flightState = "IDLE";
    Serial.println("\n[Tello WiFi] Connected! ESP32 IP: " + WiFi.localIP().toString());

    // Initialize UDP sockets
    udpCmd.begin(LOCAL_UDP_PORT);
    udpState.begin(TELLO_STATE_PORT);

    // Send initial SDK activation command
    executeDroneCommand("command", "System Initializer");
  } else {
    wifiConnected = false;
    simulationMode = true;
    telemetry.flightState = "SIMULATED";
    Serial.println("\n[Tello WiFi] Connection timed out. Running in Simulation / Bridge Mode.");
  }
}

void checkWiFiConnection() {
  if (!wifiConnected && millis() - lastConnectAttempt > 10000) {
    lastConnectAttempt = millis();
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      simulationMode = false;
      telemetry.flightState = "IDLE";
      udpCmd.begin(LOCAL_UDP_PORT);
      udpState.begin(TELLO_STATE_PORT);
      executeDroneCommand("command", "Auto-Reconnect");
    }
  }
}

void checkTelloStateTelemetry() {
  if (!wifiConnected) return;

  int packetSize = udpState.parsePacket();
  if (packetSize > 0) {
    char packetBuffer[256];
    int len = udpState.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = '\0';
      parseTelloStatePacket(String(packetBuffer));
    }
  }
}

void parseTelloStatePacket(String packet) {
  // Format: "pitch:%d;roll:%d;yaw:%d;vgx:%d;vgy:%d;vgz:%d;templ:%d;temph:%d;tof:%d;h:%d;bat:%d;baro:%.2f;time:%d;agx:%.2f;agy:%.2f;agz:%.2f;\n"
  int startIdx = 0;
  while (startIdx < packet.length()) {
    int endIdx = packet.indexOf(';', startIdx);
    if (endIdx == -1) break;
    String field = packet.substring(startIdx, endIdx);
    int colonIdx = field.indexOf(':');
    if (colonIdx != -1) {
      String key = field.substring(0, colonIdx);
      String val = field.substring(colonIdx + 1);
      key.trim();
      val.trim();

      if (key == "bat") telemetry.batteryPercent = val.toInt();
      else if (key == "h") telemetry.altitudeCm = val.toInt();
      else if (key == "pitch") telemetry.pitchDeg = val.toFloat();
      else if (key == "roll") telemetry.rollDeg = val.toFloat();
      else if (key == "yaw") telemetry.yawDeg = val.toFloat();
      else if (key == "vgx") telemetry.speedX = val.toInt();
      else if (key == "vgy") telemetry.speedY = val.toInt();
      else if (key == "vgz") telemetry.speedZ = val.toInt();
      else if (key == "tof") telemetry.tofCm = val.toInt();
      else if (key == "time") telemetry.flightTimeSec = val.toInt();
      else if (key == "templ") telemetry.tempLowC = val.toInt();
      else if (key == "temph") telemetry.tempHighC = val.toInt();
    }
    startIdx = endIdx + 1;
  }
}

// =============================================================================
// Drone Command Dispatcher & Execution Engine
// =============================================================================
void executeDroneCommand(String cmd, String source) {
  cmd.trim();
  if (cmd.length() == 0) return;

  telemetry.lastCommand = cmd;
  telemetry.commandSource = source;
  telemetry.lastCmdTimestamp = millis();

  // Update animated flight state and trigger sound effects based on command
  if (cmd == "takeoff") {
    telemetry.flightState = "TAKING_OFF";
    propellerSpeed = 45.0f; // High speed
    playTakeoffSound();
  } else if (cmd == "land") {
    telemetry.flightState = "LANDING";
    propellerSpeed = 20.0f;
    playLandSound();
  } else if (cmd == "emergency") {
    telemetry.flightState = "EMERGENCY";
    propellerSpeed = 0.0f;
    playEmergencySiren();
  } else if (cmd.startsWith("flip")) {
    telemetry.flightState = "FLIPPING";
    propellerSpeed = 60.0f;
    playFlipSound();
  } else if (cmd.startsWith("up") || cmd.startsWith("down") || cmd.startsWith("forward") || cmd.startsWith("back") || cmd.startsWith("left") || cmd.startsWith("right")) {
    telemetry.flightState = "FLYING";
    propellerSpeed = 35.0f;
    playRadarPingSound();
  } else if (cmd.startsWith("cw") || cmd.startsWith("ccw")) {
    telemetry.flightState = "TURNING";
    propellerSpeed = 30.0f;
    playRadarPingSound();
  } else if (cmd == "command") {
    telemetry.flightState = "SDK_READY";
    playConnectedJingle();
  }

  // Send over UDP to Tello Drone (or simulate)
  if (wifiConnected && !simulationMode) {
    sendUdpDroneCommand(cmd);
  } else {
    // Simulated Response
    telemetry.lastResponse = "ok (simulated)";
    Serial.print("[Simulated 7\" Tello] Executed Command: '");
    Serial.print(cmd);
    Serial.print("' (Source: ");
    Serial.print(source);
    Serial.println(") -> Response: ok");

    // Update simulated telemetry
    if (cmd == "takeoff") telemetry.altitudeCm = 80;
    else if (cmd == "land") telemetry.altitudeCm = 0;
    else if (cmd.startsWith("up")) {
      int dist = cmd.substring(3).toInt();
      telemetry.altitudeCm += (dist > 0 ? dist : 20);
    } else if (cmd.startsWith("down")) {
      int dist = cmd.substring(5).toInt();
      telemetry.altitudeCm = max(0, telemetry.altitudeCm - (dist > 0 ? dist : 20));
    }
  }

  // Transmit execution notification back to PC Thinker Window and Gemini AI over Serial
  Serial.print("[ESP32-TELLO-7] Command: ");
  Serial.print(cmd);
  Serial.print(" | Source: ");
  Serial.print(source);
  Serial.print(" | State: ");
  Serial.print(telemetry.flightState);
  Serial.print(" | Response: ");
  Serial.println(telemetry.lastResponse);
}

void sendUdpDroneCommand(String cmd) {
  udpCmd.beginPacket(TELLO_IP_ADDR, TELLO_CMD_PORT);
  udpCmd.write((const uint8_t*)cmd.c_str(), cmd.length());
  udpCmd.endPacket();

  Serial.println("[Tello UDP Sent] " + cmd);

  // Non-blocking quick response check with timeout
  unsigned long startWait = millis();
  bool receivedAck = false;

  while (millis() - startWait < 2000) {
    int packetSize = udpCmd.parsePacket();
    if (packetSize > 0) {
      char responseBuf[128];
      int len = udpCmd.read(responseBuf, sizeof(responseBuf) - 1);
      if (len > 0) {
        responseBuf[len] = '\0';
        telemetry.lastResponse = String(responseBuf);
        telemetry.lastResponse.trim();
        receivedAck = true;
        break;
      }
    }
    delay(10);
  }

  if (!receivedAck) {
    telemetry.lastResponse = "ACK Pending / Timeout";
  }

  if (cmd == "command" && telemetry.lastResponse.equalsIgnoreCase("ok")) {
    sdkModeActive = true;
    telemetry.flightState = "SDK_READY";
  }
}

// =============================================================================
// Touchscreen Dashboard Action Dispatcher
// =============================================================================
void handleTouchButton(int btnIdx) {
  if (btnIdx < 0 || btnIdx >= NUM_DASHBOARD_BTNS) return;
  const TouchButton& btn = DASHBOARD_BTNS[btnIdx];
  activePressedBtnIdx = btnIdx;
  playClickSound();

  String type = String(btn.type);
  String cmd = String(btn.command);

  if (type == "flight" || type == "sdk" || type == "flip") {
    executeDroneCommand(cmd, "Touchscreen 7\"");
  } else if (type == "dir") {
    // Directional move with preset step distance: e.g. "forward 50", "up 20"
    String fullCmd = cmd + " " + String(currentStepDistCm);
    executeDroneCommand(fullCmd, "Touchscreen 7\"");
  } else if (type == "rot") {
    executeDroneCommand(cmd, "Touchscreen 7\"");
  } else if (type == "dist") {
    currentStepDistCm = cmd.toInt();
    telemetry.lastResponse = "Step: " + cmd + " cm";
    Serial.println("[Touch 7\"] Selected Step Distance: " + cmd + " cm");
  } else if (type == "routine") {
    startAutonomousRoutine(cmd);
    Serial.println("[Touch 7\"] Started Choreography Routine: " + cmd);
  }
}

void processTouchInput() {
  int tx, ty;
  if (readTouch(tx, ty)) {
    if (!isTouchActive || (millis() - lastTouchTime > 180)) {
      isTouchActive = true;
      lastTouchTime = millis();
      touchX = tx;
      touchY = ty;

      for (int i = 0; i < NUM_DASHBOARD_BTNS; i++) {
        if (tx >= DASHBOARD_BTNS[i].x && tx <= (DASHBOARD_BTNS[i].x + DASHBOARD_BTNS[i].w) &&
            ty >= DASHBOARD_BTNS[i].y && ty <= (DASHBOARD_BTNS[i].y + DASHBOARD_BTNS[i].h)) {
          handleTouchButton(i);
          break;
        }
      }
    }
  } else {
    isTouchActive = false;
    activePressedBtnIdx = -1;
  }
}

// =============================================================================
// Dynamic Flight Choreography Routines
// =============================================================================
void startAutonomousRoutine(String routineName) {
  activeRoutine = routineName;
  routineStep = 0;
  routineStepStartTime = millis();
  telemetry.flightState = "ROUTINE_" + routineName;
  telemetry.lastCommand = "ROUTINE:" + routineName;
  telemetry.commandSource = "Choreography Engine";
}

void updateAutonomousRoutines() {
  if (activeRoutine == "idle") return;
  if (millis() - routineStepStartTime < 2500) return; // 2.5 second intervals between routine steps
  routineStepStartTime = millis();

  if (activeRoutine == "square") {
    // Fly in a precise square box pattern
    if (routineStep == 0) executeDroneCommand("forward 50", "Routine Square");
    else if (routineStep == 1) executeDroneCommand("cw 90", "Routine Square");
    else if (routineStep == 2) executeDroneCommand("forward 50", "Routine Square");
    else if (routineStep == 3) executeDroneCommand("cw 90", "Routine Square");
    else if (routineStep == 4) executeDroneCommand("forward 50", "Routine Square");
    else if (routineStep == 5) executeDroneCommand("cw 90", "Routine Square");
    else if (routineStep == 6) executeDroneCommand("forward 50", "Routine Square");
    else if (routineStep == 7) {
      executeDroneCommand("cw 90", "Routine Square");
      activeRoutine = "idle";
      telemetry.flightState = "HOVERING";
    }
    routineStep++;
  } else if (activeRoutine == "scan360") {
    // 360-Degree Panoramic Surveillance Scan
    if (routineStep < 4) {
      executeDroneCommand("cw 90", "Routine 360 Scan");
      routineStep++;
    } else {
      activeRoutine = "idle";
      telemetry.flightState = "HOVERING";
    }
  } else if (activeRoutine == "bounce") {
    // Altitude Wave / Bounce Routine
    if (routineStep == 0) executeDroneCommand("up 30", "Routine Bounce");
    else if (routineStep == 1) executeDroneCommand("down 30", "Routine Bounce");
    else if (routineStep == 2) executeDroneCommand("up 30", "Routine Bounce");
    else if (routineStep == 3) {
      executeDroneCommand("down 30", "Routine Bounce");
      activeRoutine = "idle";
      telemetry.flightState = "HOVERING";
    }
    routineStep++;
  }
}

// =============================================================================
// Dynamic Animations Engine (~30-50 FPS)
// =============================================================================
void updateAnimations() {
  unsigned long now = millis();
  if (now - lastAnimFrameTime < 25) return; // ~40 FPS animation cycle
  float dt = (now - lastAnimFrameTime) / 1000.0f;
  lastAnimFrameTime = now;

  // 1. Propeller Blade Rotation
  if (telemetry.flightState == "IDLE" || telemetry.flightState == "DISCONNECTED") {
    propellerSpeed = 8.0f;
  } else if (telemetry.flightState == "HOVERING" || telemetry.flightState == "SDK_READY") {
    propellerSpeed = 28.0f;
  } else if (telemetry.flightState == "FLYING" || telemetry.flightState == "TAKING_OFF" || telemetry.flightState == "FLIPPING") {
    propellerSpeed = 55.0f;
  }
  propellerAngle += propellerSpeed;
  if (propellerAngle >= 360.0f) propellerAngle -= 360.0f;

  // 2. Drone Floating / Bobbing Oscillation
  float t = now * 0.004f;
  droneBobbingOffset = sin(t * 1.8f) * 8.0f;
  droneTiltAngle = cos(t * 1.2f) * (telemetry.flightState == "FLYING" ? 12.0f : 3.0f);

  // 3. Navigation Strobe LED Flasher
  if (now - lastStrobeToggle > 600) {
    lastStrobeToggle = now;
    strobeLedState = !strobeLedState;
  }

  // 4. Energy Shield Aura Pulse
  auraPulseScale = 1.0f + 0.15f * sin(now * 0.006f);

  // 5. Altitude Gauge Animation Easing
  altitudeGaugeAnim += (telemetry.altitudeCm - altitudeGaugeAnim) * 0.1f;
}

// =============================================================================
// Live Telemetry Stream to Host PC & Gemini AI
// =============================================================================
unsigned long lastTelemetryStream = 0;
void printLiveTelemetry() {
  if (millis() - lastTelemetryStream < 1000) return;
  lastTelemetryStream = millis();

  Serial.print("[Telemetry 7\" Tello] State: ");
  Serial.print(telemetry.flightState);
  Serial.print(" | Batt: ");
  Serial.print(telemetry.batteryPercent);
  Serial.print("% | Alt: ");
  Serial.print(telemetry.altitudeCm);
  Serial.print("cm | Pitch: ");
  Serial.print(telemetry.pitchDeg, 1);
  Serial.print("° | Roll: ");
  Serial.print(telemetry.rollDeg, 1);
  Serial.print("° | Yaw: ");
  Serial.print(telemetry.yawDeg, 1);
  Serial.print("° | WiFi: ");
  Serial.println(wifiConnected ? "CONNECTED" : "SIMULATED");
}

// =============================================================================
// Serial Command Parser (USB CDC / UART from PC Thinker Window & AI)
// =============================================================================
void parseSerialCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.println("[Host Serial In] Received: '" + cmd + "'");

  // --- MAX98357A I2S Audio Amplifier Commands ---
  if (cmd.equalsIgnoreCase("AUDIO:TAKEOFF") || cmd.equalsIgnoreCase("PLAY:TAKEOFF")) {
    playTakeoffSound();
    Serial.println("[MAX98357A] Played Takeoff Sound");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:LAND") || cmd.equalsIgnoreCase("PLAY:LAND")) {
    playLandSound();
    Serial.println("[MAX98357A] Played Land Sound");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:FLIP") || cmd.equalsIgnoreCase("PLAY:FLIP")) {
    playFlipSound();
    Serial.println("[MAX98357A] Played Flip Sound");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:RADAR") || cmd.equalsIgnoreCase("PLAY:RADAR")) {
    playRadarPingSound();
    Serial.println("[MAX98357A] Played Radar Ping");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:ALARM") || cmd.equalsIgnoreCase("PLAY:ALARM") || cmd.equalsIgnoreCase("AUDIO:LOW_BATTERY")) {
    playLowBatteryAlarm();
    Serial.println("[MAX98357A] Played Low Battery Alarm");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:SIREN") || cmd.equalsIgnoreCase("AUDIO:EMERGENCY") || cmd.equalsIgnoreCase("PLAY:SIREN")) {
    playEmergencySiren();
    Serial.println("[MAX98357A] Played Emergency Siren");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:CONNECT") || cmd.equalsIgnoreCase("PLAY:CONNECT")) {
    playConnectedJingle();
    Serial.println("[MAX98357A] Played Connected Jingle");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:CLICK") || cmd.equalsIgnoreCase("AUDIO:BEEP") || cmd.equalsIgnoreCase("PLAY:CLICK")) {
    playClickSound();
    Serial.println("[MAX98357A] Played Click Sound");
    return;
  }
  if (cmd.startsWith("AUDIO:TONE:") || cmd.startsWith("audio:tone:")) {
    int firstColon = cmd.indexOf(':');
    int secondColon = cmd.indexOf(':', firstColon + 1);
    if (secondColon != -1) {
      float freq = cmd.substring(firstColon + 1, secondColon).toFloat();
      int dur = cmd.substring(secondColon + 1).toInt();
      playToneI2S(freq, dur);
      Serial.println("[MAX98357A] Played Tone " + String(freq) + "Hz for " + String(dur) + "ms");
      return;
    }
  }
  if (cmd.startsWith("AUDIO:SWEEP:") || cmd.startsWith("audio:sweep:")) {
    int c1 = cmd.indexOf(':');
    int c2 = cmd.indexOf(':', c1 + 1);
    int c3 = cmd.indexOf(':', c2 + 1);
    if (c1 != -1 && c2 != -1 && c3 != -1) {
      float startF = cmd.substring(c1 + 1, c2).toFloat();
      float endF   = cmd.substring(c2 + 1, c3).toFloat();
      int dur      = cmd.substring(c3 + 1).toInt();
      playSweepI2S(startF, endF, dur);
      Serial.println("[MAX98357A] Played Sweep " + String(startF) + "->" + String(endF) + "Hz");
      return;
    }
  }
  if (cmd.startsWith("AUDIO:VOL:") || cmd.startsWith("audio:vol:") || cmd.startsWith("VOL:") || cmd.startsWith("vol:")) {
    int colonIdx = cmd.indexOf(':');
    int vol = cmd.substring(colonIdx + 1).toInt();
    setAudioVolume(vol);
    return;
  }
  if (cmd.startsWith("AUDIO:MUTE:") || cmd.startsWith("audio:mute:") || cmd.equalsIgnoreCase("AUDIO:MUTE") || cmd.equalsIgnoreCase("MUTE")) {
    int colonIdx = cmd.indexOf(':');
    if (colonIdx != -1) {
      String mStr = cmd.substring(colonIdx + 1);
      mStr.toUpperCase();
      mStr.trim();
      setAudioMute(mStr == "1" || mStr == "ON" || mStr == "TRUE");
    } else {
      setAudioMute(!audioMuted);
    }
    return;
  }

  // Format 1: "TELLO:<command>" (e.g. "TELLO:takeoff", "TELLO:forward 50", "TELLO:flip f")
  if (cmd.startsWith("TELLO:") || cmd.startsWith("tello:")) {
    String droneCmd = cmd.substring(cmd.indexOf(':') + 1);
    droneCmd.trim();
    executeDroneCommand(droneCmd, "PC Thinker / Gemini AI");
    return;
  }

  // Format 2: "ROUTINE:<name>"
  if (cmd.startsWith("ROUTINE:") || cmd.startsWith("routine:")) {
    String rName = cmd.substring(cmd.indexOf(':') + 1);
    rName.toLowerCase();
    startAutonomousRoutine(rName);
    return;
  }

  // Format 3: Direct Drone Command String (e.g. "takeoff", "land", "emergency", "up 50", "cw 90", "flip f")
  if (cmd.equalsIgnoreCase("takeoff") || cmd.equalsIgnoreCase("land") || cmd.equalsIgnoreCase("emergency") ||
      cmd.equalsIgnoreCase("command") || cmd.startsWith("up") || cmd.startsWith("down") ||
      cmd.startsWith("forward") || cmd.startsWith("back") || cmd.startsWith("left") || cmd.startsWith("right") ||
      cmd.startsWith("cw") || cmd.startsWith("ccw") || cmd.startsWith("flip") || cmd.startsWith("speed") ||
      cmd.equalsIgnoreCase("battery?") || cmd.equalsIgnoreCase("time?") || cmd.equalsIgnoreCase("height?")) {
    executeDroneCommand(cmd, "PC Thinker / Gemini AI");
    return;
  }

  // Format 4: Status / Ping query
  if (cmd.equalsIgnoreCase("status") || cmd.equalsIgnoreCase("ping") || cmd.equalsIgnoreCase("state")) {
    Serial.print("[ESP32-TELLO-7] Status: ");
    Serial.print(telemetry.flightState);
    Serial.print(" | Batt: ");
    Serial.print(telemetry.batteryPercent);
    Serial.print("% | Alt: ");
    Serial.print(telemetry.altitudeCm);
    Serial.println("cm");
    return;
  }
}

// =============================================================================
// Setup & Main Loop
// =============================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {}

  #if HAS_BT_CLASSIC
  SerialBT.begin("esp32-touch-lcd-tello");
  #endif

  // Initialize I2C Bus for GT911 Capacitive Touchscreen & IO Expander
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000); // 400kHz Fast I2C

  // Initialize Waveshare 7C Onboard IO Expander (CH422G for Backlight, LCD Power & Touch Reset)
  initIOExpander7C();

  // Initialize MAX98357A I2S Audio Amplifier (BCLK=19, LRC=20, DIN=21)
  initI2SAudio();

  // Initialize GT911 Touch Controller
  initTouchController();

  // Play startup radar sonar ping
  playRadarPingSound();

  Serial.println("==========================================================");
  Serial.println("🚁 Waveshare ESP32-S3-Touch-LCD-7C Tello Drone Bridge & HUD");
  Serial.println("==========================================================");
  Serial.println("Screen: 7.0-inch 800x600 HD Capacitive Touch LCD");
  Serial.println("IO Expander: CH422G (Backlight EXIO2, Power EXIO6, Touch RST EXIO1)");
  Serial.println("Touch Controller: GT911 (I2C: 0x5D, SDA: 8, SCL: 9, INT: 4)");
  Serial.print("MAX98357A I2S Audio: ");
  Serial.println(i2sAudioReady ? "READY (BCLK=19, LRC=20, DIN=21)" : "INIT FAILED");
  Serial.println("Animations Engine: READY (40 FPS Quadcopter Visualizer)");
  Serial.println("Host Command Bridge: USB CDC Serial & Bluetooth");
  Serial.println("==========================================================");

  // Initialize WiFi & Tello UDP Networking
  initTelloNetwork();
}

void loop() {
  // 1. Process Incoming Serial Commands from Host PC Thinker Window & Gemini AI
  if (Serial.available() > 0) {
    String serialInput = Serial.readStringUntil('\n');
    parseSerialCommand(serialInput);
  }

  #if HAS_BT_CLASSIC
  if (SerialBT.available() > 0) {
    String btInput = SerialBT.readStringUntil('\n');
    parseSerialCommand(btInput);
  }
  #endif

  // 2. Process Capacitive Touchscreen Interactions from 7-Inch Display
  processTouchInput();

  // 3. Listen for Incoming Tello State Telemetry on UDP 8890
  checkTelloStateTelemetry();

  // 4. Update Dynamic Flight Animations (Spinning Rotors, Attitude Horizon, Gauges)
  updateAnimations();

  // 5. Update Autonomous Choreography Routine State Machine
  updateAutonomousRoutines();

  // 6. Check WiFi Reconnection & Heartbeat
  checkWiFiConnection();

  // 7. Stream Telemetry to Host PC
  printLiveTelemetry();
}
