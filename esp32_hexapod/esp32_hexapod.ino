/*
  Hexapod Controller - ESP-32-Touch-LCD Firmware (7-Inch Capacitive Touchscreen Edition)
  =============================================================================
  Hardware: ESP32-S3-Touch-LCD-7 (7.0" 800x480 Capacitive Touchscreen, GT911 Controller)
            Also supports 1.28", 2.8", 3.5", 4.3" and Generic ESP32 Touch LCDs.
  Substituted for: ESP-32 DevKit

  Features:
    - 7.0-inch 800x480 Widescreen Capacitive Touchscreen Dashboard
    - GT911 High-Precision 5-Point Capacitive Multi-Touch Interface
    - Interactive Touch Buttons:
        [STAND], [SIT], [FLAT], [WALK], [RUN], [DANCE], [BOW], [WAVE L], [WAVE R], [TURN L], [TURN R], [STOP], [SPEED +/-]
    - Widescreen Robot Telemetry & Live Animated Robot Mascot / Expressions
    - 3-DOF Inverse Kinematics (IK) Solver for (X, Y, Z) Cartesian Foot Positioning
    - Dual PCA9685 16-Channel I2C Servo Driver Control (18 Servos Total):
        * Driver 1 (I2C 0x40): Left Legs  (FL, ML, RL) - 9 Servos
        * Driver 2 (I2C 0x41): Right Legs (FR, MR, RR) - 9 Servos
    - 50Hz Smooth S-Curve Trajectory & Motion Interpolation Engine
    - Multi-Channel Control: USB CDC Serial, Bluetooth / BLE, and Direct Touch Screen

  Pinout & Hardware Configuration (Waveshare ESP32-S3-Touch-LCD-7):
    - I2C Bus (Shared for GT911 Touch Controller & PCA9685 Servo Drivers):
        * SDA : GPIO 8
        * SCL : GPIO 9
        * TP_INT: GPIO 4
        * TP_RST: Controlled via onboard IO Expander (or direct GPIO)
        * GT911 I2C Address: 0x5D (or 0x14)
    - RGB Display Interface (800x480 ST7262 / 16-bit RGB565)
  =============================================================================
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>

#if defined(CONFIG_BT_ENABLED) && !defined(CONFIG_IDF_TARGET_ESP32S3)
#include <BluetoothSerial.h>
#define HAS_BT_CLASSIC 1
BluetoothSerial SerialBT;
#else
#define HAS_BT_CLASSIC 0
#endif

// =============================================================================
// Hardware Profile Selection
// =============================================================================
// Select ONE board profile below:
#define BOARD_ESP32_TOUCH_LCD_7    1  // Waveshare ESP32-S3-Touch-LCD-7 (7.0" 800x480 GT911 Capacitive Touch)
//#define BOARD_ESP32_TOUCH_LCD_128 1  // Waveshare ESP32-S3-Touch-LCD-1.28 (1.28" Round 240x240 GC9A01 + CST816S)
//#define BOARD_ESP32_TOUCH_LCD_28  1  // Waveshare / Sunton ESP32-Touch-LCD-2.8 / 3.5 (240x320 ST7789)
//#define BOARD_ESP32_TOUCH_LCD_GENERIC 1 // Generic ESP32 with external Touch LCD

// Pin & Resolution Definitions
#if defined(BOARD_ESP32_TOUCH_LCD_7)
  #define I2C_SDA_PIN      8
  #define I2C_SCL_PIN      9
  #define TP_INT_PIN       4
  #define TP_RST_PIN      -1
  #define SCREEN_WIDTH   800
  #define SCREEN_HEIGHT  480
  #define TOUCH_I2C_ADDR 0x5D // GT911 Capacitive Touch Controller
  #define IS_GT911_TOUCH   1
#elif defined(BOARD_ESP32_TOUCH_LCD_128)
  #define I2C_SDA_PIN      6
  #define I2C_SCL_PIN      7
  #define LCD_DC_PIN       8
  #define LCD_CS_PIN       9
  #define LCD_CLK_PIN     10
  #define LCD_MOSI_PIN    11
  #define LCD_RST_PIN     12
  #define LCD_BL_PIN      40
  #define TP_INT_PIN       5
  #define TP_RST_PIN      13
  #define SCREEN_WIDTH   240
  #define SCREEN_HEIGHT  240
  #define TOUCH_I2C_ADDR 0x15 // CST816S Capacitive Touch
  #define IS_GT911_TOUCH   0
#elif defined(BOARD_ESP32_TOUCH_LCD_28)
  #define I2C_SDA_PIN      4
  #define I2C_SCL_PIN      5
  #define LCD_DC_PIN       2
  #define LCD_CS_PIN      15
  #define LCD_CLK_PIN     14
  #define LCD_MOSI_PIN    13
  #define LCD_RST_PIN     -1
  #define LCD_BL_PIN      21
  #define TP_INT_PIN      -1
  #define TP_RST_PIN      -1
  #define SCREEN_WIDTH   240
  #define SCREEN_HEIGHT  320
  #define TOUCH_I2C_ADDR 0x5D // GT911 / CST328 Touch
  #define IS_GT911_TOUCH   1
#else
  #define I2C_SDA_PIN     21
  #define I2C_SCL_PIN     22
  #define LCD_DC_PIN      16
  #define LCD_CS_PIN       5
  #define LCD_CLK_PIN     18
  #define LCD_MOSI_PIN    23
  #define LCD_RST_PIN     17
  #define LCD_BL_PIN       4
  #define TP_INT_PIN      -1
  #define TP_RST_PIN      -1
  #define SCREEN_WIDTH   800
  #define SCREEN_HEIGHT  480
  #define TOUCH_I2C_ADDR 0x5D
  #define IS_GT911_TOUCH   1
#endif

// =============================================================================
// PCA9685 Servo Driver Constants
// =============================================================================
#define PCA9685_ADDR_LEFT   0x40 // Driver 1: Left Legs  (FL, ML, RL)
#define PCA9685_ADDR_RIGHT  0x41 // Driver 2: Right Legs (FR, MR, RR)

#define PCA9685_MODE1       0x00
#define PCA9685_PRESCALE    0xFE
#define PCA9685_LED0_ON_L   0x06

// Servo Pulse Constants (50Hz PWM, 4096 steps per 20ms)
#define SERVOMIN            150  // Min pulse length out of 4096 (0 deg)
#define SERVOMAX            600  // Max pulse length out of 4096 (180 deg)

// Hexapod Physical Link Dimensions (in mm)
const float COXA_LEN  = 30.0f; // Hip swivel length
const float FEMUR_LEN = 60.0f; // Upper leg length
const float TIBIA_LEN = 90.0f; // Lower leg length

// 18 Servos Total (9 on Driver 1, 9 on Driver 2)
// Index 0..8   = Driver 1 (FL: 0-2, ML: 3-5, RL: 6-8)
// Index 9..17  = Driver 2 (FR: 9-11, MR: 12-14, RR: 15-17)
const int TOTAL_SERVOS = 18;

float currentAngles[TOTAL_SERVOS];
float startAngles[TOTAL_SERVOS];
float targetAngles[TOTAL_SERVOS];

unsigned long moveStartTime = 0;
unsigned long moveDurationMs = 200; // Default 200ms smooth interpolation
bool isMoving = false;

// Active Motion / Gait State
String currentGait = "idle";
String lastActionName = "STAND";
unsigned long lastGaitStepTime = 0;
int gaitStepIndex = 0;

// LCD & UI Status Variables
unsigned long lastDisplayRefresh = 0;
unsigned long lastEyeBlink = 0;
bool eyeBlinkState = false;
String lcdStatusMessage = "Hexapod 7-Inch System Online";

// Speech Reactivity & AI Audio Stream State
bool speechReactEnabled = true;
bool isSpeechActive = false;
unsigned long lastSpeechTime = 0;
const unsigned long SPEECH_SUSTAIN_MS = 500;
unsigned long lastSpeechEyeToggle = 0;
bool speechEyeState = false;
unsigned long lastSpeechSwayTime = 0;

// Touch State
bool isTouched = false;
int touchX = 0;
int touchY = 0;
unsigned long lastTouchTime = 0;

// =============================================================================
// Helper Functions: Leg Index Mapping
// =============================================================================
// legIndex: 0=FL, 1=ML, 2=RL, 3=FR, 4=MR, 5=RR
int getServoIndex(int legIndex, int joint) { // joint: 0=coxa, 1=femur, 2=tibia
  return (legIndex * 3) + joint;
}

int parseLegCode(String code) {
  code.toUpperCase();
  if (code == "FL") return 0;
  if (code == "ML") return 1;
  if (code == "RL") return 2;
  if (code == "FR") return 3;
  if (code == "MR") return 4;
  if (code == "RR") return 5;
  return -1;
}

// =============================================================================
// PCA9685 Low-Level I2C Drivers
// =============================================================================
void writePCA9685(uint8_t addr, uint8_t reg, uint8_t data) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

void setPCA9685PWM(uint8_t addr, uint8_t channel, uint16_t on, uint16_t off) {
  Wire.beginTransmission(addr);
  Wire.write(PCA9685_LED0_ON_L + 4 * channel);
  Wire.write(on & 0xFF);
  Wire.write(on >> 8);
  Wire.write(off & 0xFF);
  Wire.write(off >> 8);
  Wire.endTransmission();
}

void initPCA9685(uint8_t addr) {
  writePCA9685(addr, PCA9685_MODE1, 0x00);
  delay(10);
  // Set frequency to 50Hz for standard servos (prescale = 121)
  writePCA9685(addr, PCA9685_MODE1, 0x10); // Sleep mode
  writePCA9685(addr, PCA9685_PRESCALE, 121);
  writePCA9685(addr, PCA9685_MODE1, 0x00); // Wake up
  delay(5);
  writePCA9685(addr, PCA9685_MODE1, 0xA1); // Auto-increment
}

void writeHardwareAngle(int servoIndex, float angle) {
  angle = constrain(angle, 0.0f, 180.0f);
  uint8_t addr = (servoIndex < 9) ? PCA9685_ADDR_LEFT : PCA9685_ADDR_RIGHT;
  uint8_t channel = servoIndex % 9;
  
  uint16_t pulse = map((long)angle, 0, 180, SERVOMIN, SERVOMAX);
  setPCA9685PWM(addr, channel, 0, pulse);
}

// =============================================================================
// 3-DOF Inverse Kinematics (IK) Solver
// =============================================================================
bool solveIK(float x, float y, float z, bool isRightLeg, float &coxaDeg, float &femurDeg, float &tibiaDeg) {
  float coxaRad = atan2(x, y); // Swivel angle
  float r = sqrt(x * x + y * y) - COXA_LEN;
  float d = sqrt(r * r + z * z);
  
  // Check workspace reachability
  if (d > (FEMUR_LEN + TIBIA_LEN) || d < fabs(FEMUR_LEN - TIBIA_LEN)) {
    return false; // Point unreachable
  }
  
  float alpha1 = atan2(z, r);
  float alpha2 = acos((FEMUR_LEN * FEMUR_LEN + d * d - TIBIA_LEN * TIBIA_LEN) / (2.0f * FEMUR_LEN * d));
  float femurRad = alpha1 + alpha2;
  
  float beta = acos((FEMUR_LEN * FEMUR_LEN + TIBIA_LEN * TIBIA_LEN - d * d) / (2.0f * FEMUR_LEN * TIBIA_LEN));
  float tibiaRad = beta;
  
  coxaDeg  = coxaRad * 180.0f / M_PI;
  femurDeg = femurRad * 180.0f / M_PI;
  tibiaDeg = tibiaRad * 180.0f / M_PI;

  // Map to 0-180 degree servo range centered at 90
  coxaDeg  = 90.0f + coxaDeg;
  femurDeg = 90.0f - femurDeg; 
  tibiaDeg = 180.0f - tibiaDeg;

  // Right side leg mirroring
  if (isRightLeg) {
    coxaDeg = 180.0f - coxaDeg;
  }

  coxaDeg  = constrain(coxaDeg, 0.0f, 180.0f);
  femurDeg = constrain(femurDeg, 0.0f, 180.0f);
  tibiaDeg = constrain(tibiaDeg, 0.0f, 180.0f);

  return true;
}

// =============================================================================
// Trajectory & Motion Interpolation Engine (50Hz Update Loop)
// =============================================================================
void setTargetAngles(const float newTargets[TOTAL_SERVOS], unsigned long durationMs) {
  for (int i = 0; i < TOTAL_SERVOS; i++) {
    startAngles[i] = currentAngles[i];
    targetAngles[i] = constrain(newTargets[i], 0.0f, 180.0f);
  }
  moveStartTime = millis();
  moveDurationMs = (durationMs < 20) ? 20 : durationMs;
  isMoving = true;
}

void setSingleTargetAngle(int servoIndex, float angle, unsigned long durationMs) {
  if (servoIndex < 0 || servoIndex >= TOTAL_SERVOS) return;
  startAngles[servoIndex] = currentAngles[servoIndex];
  targetAngles[servoIndex] = constrain(angle, 0.0f, 180.0f);
  moveStartTime = millis();
  moveDurationMs = (durationMs < 20) ? 20 : durationMs;
  isMoving = true;
}

void updateTrajectoryEngine() {
  if (!isMoving) return;
  
  unsigned long elapsed = millis() - moveStartTime;
  float progress = (float)elapsed / (float)moveDurationMs;
  
  if (progress >= 1.0f) {
    progress = 1.0f;
    isMoving = false;
  }
  
  // Cosine S-Curve Interpolation Factor (smooth acceleration and deceleration)
  float ease = 0.5f * (1.0f - cos(M_PI * progress));

  for (int i = 0; i < TOTAL_SERVOS; i++) {
    currentAngles[i] = startAngles[i] + ease * (targetAngles[i] - startAngles[i]);
    writeHardwareAngle(i, currentAngles[i]);
  }
}

// =============================================================================
// Postures & Motion Routines
// =============================================================================
void applyStandPosture(unsigned long durationMs = 300) {
  float newTargets[TOTAL_SERVOS];
  for (int leg = 0; leg < 6; leg++) {
    newTargets[getServoIndex(leg, 0)] = 90.0f; // Coxa centered
    newTargets[getServoIndex(leg, 1)] = 90.0f; // Femur parallel
    newTargets[getServoIndex(leg, 2)] = 90.0f; // Tibia perpendicular
  }
  setTargetAngles(newTargets, durationMs);
  lastActionName = "STAND";
  lcdStatusMessage = "Hexapod Standing Upright";
}

void applySitPosture(unsigned long durationMs = 400) {
  float newTargets[TOTAL_SERVOS];
  for (int leg = 0; leg < 6; leg++) {
    newTargets[getServoIndex(leg, 0)] = 90.0f;
    newTargets[getServoIndex(leg, 1)] = 30.0f;
    newTargets[getServoIndex(leg, 2)] = 150.0f;
  }
  setTargetAngles(newTargets, durationMs);
  lastActionName = "SIT";
  lcdStatusMessage = "Hexapod Sitting Down";
}

void applyFlatPosture(unsigned long durationMs = 400) {
  float newTargets[TOTAL_SERVOS];
  for (int leg = 0; leg < 6; leg++) {
    newTargets[getServoIndex(leg, 0)] = 90.0f;
    newTargets[getServoIndex(leg, 1)] = 0.0f;
    newTargets[getServoIndex(leg, 2)] = 0.0f;
  }
  setTargetAngles(newTargets, durationMs);
  lastActionName = "FLAT";
  lcdStatusMessage = "Hexapod Flat to Floor";
}

void applyBowPosture(unsigned long durationMs = 400) {
  float newTargets[TOTAL_SERVOS];
  for (int leg = 0; leg < 6; leg++) {
    newTargets[getServoIndex(leg, 0)] = 90.0f;
    if (leg == 0 || leg == 3) {
      newTargets[getServoIndex(leg, 1)] = 20.0f;
      newTargets[getServoIndex(leg, 2)] = 160.0f;
    } else {
      newTargets[getServoIndex(leg, 1)] = 110.0f;
      newTargets[getServoIndex(leg, 2)] = 80.0f;
    }
  }
  setTargetAngles(newTargets, durationMs);
  lastActionName = "BOW";
  lcdStatusMessage = "Hexapod Bowing";
}

void applyWavePosture(bool isRight, unsigned long durationMs = 300) {
  float targets[TOTAL_SERVOS];
  memcpy(targets, currentAngles, sizeof(targets));
  int legIdx = isRight ? 3 : 0;
  targets[getServoIndex(legIdx, 1)] = 150.0f;
  targets[getServoIndex(legIdx, 2)] = 40.0f;
  targets[getServoIndex(legIdx, 0)] = isRight ? 130.0f : 50.0f;
  setTargetAngles(targets, durationMs);
  lastActionName = isRight ? "WAVE R" : "WAVE L";
  lcdStatusMessage = isRight ? "Waving Right Arm" : "Waving Left Arm";
}

// Tripod Gait Step Engine
void updateGaitEngine() {
  if (currentGait == "idle" || currentGait == "stop") return;
  if (isMoving) return;

  if (currentGait == "walk" || currentGait == "run") {
    int stepDuration = (currentGait == "run") ? 120 : 220;
    
    // Group A: FL (0), MR (4), RL (2)
    // Group B: FR (3), ML (1), RR (5)
    float targets[TOTAL_SERVOS];
    memcpy(targets, targetAngles, sizeof(targets));
    
    if (gaitStepIndex == 0) {
      targets[getServoIndex(0, 1)] = 120.0f;
      targets[getServoIndex(4, 1)] = 120.0f;
      targets[getServoIndex(2, 1)] = 120.0f;
      targets[getServoIndex(0, 0)] = 120.0f;
      targets[getServoIndex(4, 0)] = 120.0f;
      targets[getServoIndex(2, 0)] = 120.0f;
      
      targets[getServoIndex(3, 0)] = 60.0f;
      targets[getServoIndex(1, 0)] = 60.0f;
      targets[getServoIndex(5, 0)] = 60.0f;
      gaitStepIndex = 1;
    } else if (gaitStepIndex == 1) {
      targets[getServoIndex(0, 1)] = 90.0f;
      targets[getServoIndex(4, 1)] = 90.0f;
      targets[getServoIndex(2, 1)] = 90.0f;
      gaitStepIndex = 2;
    } else if (gaitStepIndex == 2) {
      targets[getServoIndex(3, 1)] = 120.0f;
      targets[getServoIndex(1, 1)] = 120.0f;
      targets[getServoIndex(5, 1)] = 120.0f;
      targets[getServoIndex(3, 0)] = 120.0f;
      targets[getServoIndex(1, 0)] = 120.0f;
      targets[getServoIndex(5, 0)] = 120.0f;
      
      targets[getServoIndex(0, 0)] = 60.0f;
      targets[getServoIndex(4, 0)] = 60.0f;
      targets[getServoIndex(2, 0)] = 60.0f;
      gaitStepIndex = 3;
    } else {
      targets[getServoIndex(3, 1)] = 90.0f;
      targets[getServoIndex(1, 1)] = 90.0f;
      targets[getServoIndex(5, 1)] = 90.0f;
      gaitStepIndex = 0;
    }
    setTargetAngles(targets, stepDuration);
  }
  else if (currentGait == "dance") {
    float targets[TOTAL_SERVOS];
    memcpy(targets, targetAngles, sizeof(targets));
    if (gaitStepIndex == 0) {
      for (int l = 0; l < 3; l++) targets[getServoIndex(l, 1)] = 60.0f;
      for (int l = 3; l < 6; l++) targets[getServoIndex(l, 1)] = 120.0f;
      gaitStepIndex = 1;
    } else {
      for (int l = 0; l < 3; l++) targets[getServoIndex(l, 1)] = 120.0f;
      for (int l = 3; l < 6; l++) targets[getServoIndex(l, 1)] = 60.0f;
      gaitStepIndex = 0;
    }
    setTargetAngles(targets, 300);
  }
}

// =============================================================================
// Real-Time Speech Motion & Eye Animation Engine
// =============================================================================
void updateSpeechEngine() {
  unsigned long now = millis();

  // Natural idle eye blinking
  if (now - lastEyeBlink > 3500) {
    eyeBlinkState = true;
    if (now - lastEyeBlink > 3700) {
      eyeBlinkState = false;
      lastEyeBlink = now;
    }
  }

  if (!speechReactEnabled) return;

  // Active speaking window
  if (isSpeechActive && (now - lastSpeechTime < SPEECH_SUSTAIN_MS || lastSpeechTime > 0)) {
    // Dynamic rapid eye blinking during speech
    if (now - lastSpeechEyeToggle > 160) {
      speechEyeState = !speechEyeState;
      eyeBlinkState = speechEyeState;
      lastSpeechEyeToggle = now;
    }

    // Expressive body breathing sway if hexapod is in idle/stand posture
    if (currentGait == "idle" && (now - lastSpeechSwayTime > 80)) {
      lastSpeechSwayTime = now;
      float t = now * 0.004f;
      float swayAngle = sin(t * 2.2f) * 10.0f; // ±10° pitch/height sway
      float coxaSway  = cos(t * 1.5f) * 8.0f;  // ±8° yaw sway

      float targets[TOTAL_SERVOS];
      memcpy(targets, targetAngles, sizeof(targets));

      for (int l = 0; l < 6; l++) {
        targets[getServoIndex(l, 0)] = 90.0f + ((l < 3) ? coxaSway : -coxaSway);
        targets[getServoIndex(l, 1)] = 90.0f + swayAngle;
        targets[getServoIndex(l, 2)] = 90.0f - swayAngle;
      }
      setTargetAngles(targets, 90);
    }
  } else if (isSpeechActive) {
    // Speech ended -> restore normal stand posture
    isSpeechActive = false;
    if (currentGait == "idle") {
      applyStandPosture(250);
      lcdStatusMessage = "Hexapod Standing / Ready";
    }
  }
}

// =============================================================================
// Capacitive Touch Driver (GT911 & CST816S Multi-Touch Controller)
// =============================================================================
void initTouchController() {
  if (TP_RST_PIN >= 0) {
    pinMode(TP_RST_PIN, OUTPUT);
    digitalWrite(TP_RST_PIN, LOW);
    delay(10);
    digitalWrite(TP_RST_PIN, HIGH);
    delay(50);
  }
  if (TP_INT_PIN >= 0) {
    pinMode(TP_INT_PIN, INPUT_PULLUP);
  }
}

bool readTouch(int &x, int &y) {
#if IS_GT911_TOUCH
  // GT911 5-point capacitive touch register polling
  Wire.beginTransmission((uint8_t)TOUCH_I2C_ADDR);
  Wire.write(0x81);
  Wire.write(0x4E); // Register 0x814E: Buffer status & point count
  if (Wire.endTransmission() != 0) return false;

  Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)1);
  if (Wire.available() < 1) return false;
  uint8_t status = Wire.read();

  bool pointReady = (status & 0x80) != 0;
  uint8_t points = status & 0x0F;

  if (pointReady && points > 0) {
    Wire.beginTransmission((uint8_t)TOUCH_I2C_ADDR);
    Wire.write(0x81);
    Wire.write(0x50); // Point 1 coordinate registers
    Wire.endTransmission();

    Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)6);
    if (Wire.available() >= 6) {
      uint8_t trackId = Wire.read();
      uint8_t xLow    = Wire.read();
      uint8_t xHigh   = Wire.read();
      uint8_t yLow    = Wire.read();
      uint8_t yHigh   = Wire.read();
      uint8_t pSize   = Wire.read();

      x = (xHigh << 8) | xLow;
      y = (yHigh << 8) | yLow;

      // Clear buffer status flag by writing 0 to 0x814E
      Wire.beginTransmission((uint8_t)TOUCH_I2C_ADDR);
      Wire.write(0x81);
      Wire.write(0x4E);
      Wire.write(0x00);
      Wire.endTransmission();
      return true;
    }
  }

  // Clear buffer flag
  Wire.beginTransmission((uint8_t)TOUCH_I2C_ADDR);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(0x00);
  Wire.endTransmission();
  return false;
#else
  // CST816S capacitive touch polling
  Wire.beginTransmission((uint8_t)TOUCH_I2C_ADDR);
  Wire.write(0x01);
  if (Wire.endTransmission() != 0) return false;

  Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)6);
  if (Wire.available() >= 6) {
    uint8_t gesture = Wire.read();
    uint8_t points  = Wire.read();
    uint8_t xHigh   = Wire.read();
    uint8_t xLow    = Wire.read();
    uint8_t yHigh   = Wire.read();
    uint8_t yLow    = Wire.read();

    if (points > 0) {
      x = ((xHigh & 0x0F) << 8) | xLow;
      y = ((yHigh & 0x0F) << 8) | yLow;
      return true;
    }
  }
  return false;
#endif
}

// =============================================================================
// 7-Inch Capacitive Touchscreen UI Dashboard Layout
// =============================================================================
struct TouchButton {
  const char* label;
  int x, y, w, h;
  const char* action;
  uint16_t color;
};

// 7-inch Widescreen (800x480) Touch Button Layout
#if defined(BOARD_ESP32_TOUCH_LCD_7)
const TouchButton TOUCH_BTNS[] = {
  // Column 1: Core Postures
  {"STAND",       30, 100, 150, 60, "stand",       0x2595},
  {"SIT",         30, 175, 150, 60, "sit",         0xD5A0},
  {"FLAT",        30, 250, 150, 60, "flat",        0x7BEF},
  {"BOW",         30, 325, 150, 60, "bow",         0x07E0},

  // Column 2: Gaits & Movement
  {"WALK",       200, 100, 150, 60, "walk",        0x04FF},
  {"RUN",        200, 175, 150, 60, "run",         0xF800},
  {"DANCE",      200, 250, 150, 60, "dance",       0xF81F},
  {"STOP",       200, 325, 150, 60, "stop",        0xF800},

  // Column 3: Gestures & Rotation
  {"WAVE LEFT",  370, 100, 150, 60, "wave_left",   0xFD20},
  {"WAVE RIGHT", 370, 175, 150, 60, "wave_right",  0xFD20},
  {"TURN LEFT",  370, 250, 150, 60, "turn_left",   0x0284},
  {"TURN RIGHT", 370, 325, 150, 60, "turn_right",  0x0284},

  // Column 4: Speech Reactivity & Speed Controls
  {"SPEECH: ON", 550, 100, 215, 60, "toggle_speech",0x05E0},
  {"TALK TEST",  550, 175, 215, 60, "test_speech",  0x38BD},
  {"SPEED -",    550, 250, 100, 60, "speed_down",   0x3341},
  {"SPEED +",    665, 250, 100, 60, "speed_up",     0x3341},
  {"ALL HOME",   550, 325, 215, 60, "stand",        0x2595},
};
#else
// Compact 240x240 / 240x320 Layout
const TouchButton TOUCH_BTNS[] = {
  {"STAND",   15, 120, 65, 30, "stand", 0x2595},
  {"SIT",     87, 120, 65, 30, "sit",   0xD5A0},
  {"FLAT",   160, 120, 65, 30, "flat",  0x7BEF},
  {"WALK",    15, 155, 65, 30, "walk",  0x04FF},
  {"RUN",     87, 155, 65, 30, "run",   0xF800},
  {"DANCE",  160, 155, 65, 30, "dance", 0xF81F},
  {"BOW",     15, 190, 65, 30, "bow",   0x07E0},
  {"STOP",    87, 190, 65, 30, "stop",  0xF800},
  {"SPEECH", 160, 190, 65, 30, "toggle_speech", 0x05E0},
};
#endif
const int NUM_TOUCH_BTNS = sizeof(TOUCH_BTNS) / sizeof(TouchButton);

void handleTouchAction(String action) {
  if (action == "stand") {
    currentGait = "idle";
    applyStandPosture();
  } else if (action == "sit") {
    currentGait = "idle";
    applySitPosture();
  } else if (action == "flat") {
    currentGait = "idle";
    applyFlatPosture();
  } else if (action == "walk") {
    currentGait = "walk";
    lastActionName = "WALK";
    lcdStatusMessage = "Gait: Tripod Walk";
    gaitStepIndex = 0;
  } else if (action == "run") {
    currentGait = "run";
    lastActionName = "RUN";
    lcdStatusMessage = "Gait: Fast Run";
    gaitStepIndex = 0;
  } else if (action == "dance") {
    currentGait = "dance";
    lastActionName = "DANCE";
    lcdStatusMessage = "Routine: Dance";
    gaitStepIndex = 0;
  } else if (action == "bow") {
    currentGait = "idle";
    applyBowPosture();
  } else if (action == "wave" || action == "wave_right") {
    currentGait = "idle";
    applyWavePosture(true);
  } else if (action == "wave_left") {
    currentGait = "idle";
    applyWavePosture(false);
  } else if (action == "turn_left") {
    lastActionName = "TURN L";
    lcdStatusMessage = "Turning Left";
  } else if (action == "turn_right") {
    lastActionName = "TURN R";
    lcdStatusMessage = "Turning Right";
  } else if (action == "speed_up") {
    if (moveDurationMs > 50) moveDurationMs -= 30;
    lcdStatusMessage = "Speed: " + String(moveDurationMs) + "ms";
  } else if (action == "speed_down") {
    moveDurationMs += 30;
    lcdStatusMessage = "Speed: " + String(moveDurationMs) + "ms";
  } else if (action == "toggle_speech") {
    speechReactEnabled = !speechReactEnabled;
    lcdStatusMessage = speechReactEnabled ? "Speech React: ENABLED" : "Speech React: DISABLED";
  } else if (action == "test_speech") {
    lastSpeechTime = millis();
    isSpeechActive = true;
    lcdStatusMessage = "Testing Speech Reactivity...";
  } else if (action == "stop") {
    currentGait = "idle";
    applyStandPosture(200);
    lastActionName = "STOP";
    lcdStatusMessage = "Stopped / Stand";
  }

  Serial.print("[7-Inch Touch LCD] Action Triggered: ");
  Serial.println(action);
}

void processTouchInput() {
  int tx, ty;
  if (readTouch(tx, ty)) {
    if (!isTouched || (millis() - lastTouchTime > 200)) {
      isTouched = true;
      lastTouchTime = millis();
      touchX = tx;
      touchY = ty;

      for (int i = 0; i < NUM_TOUCH_BTNS; i++) {
        if (tx >= TOUCH_BTNS[i].x && tx <= (TOUCH_BTNS[i].x + TOUCH_BTNS[i].w) &&
            ty >= TOUCH_BTNS[i].y && ty <= (TOUCH_BTNS[i].y + TOUCH_BTNS[i].h)) {
          handleTouchAction(TOUCH_BTNS[i].action);
          break;
        }
      }
    }
  } else {
    isTouched = false;
  }
}

// =============================================================================
// Command Parser (Serial, Bluetooth & Web)
// =============================================================================
void parseCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.print("[Hexapod CMD] ");
  Serial.println(cmd);

  // 1. Direct Servo Command: "HEX:SERVO:<driver>:<chan>:<deg>"
  if (cmd.startsWith("HEX:SERVO:") || cmd.startsWith("hex:servo:")) {
    int col1 = cmd.indexOf(':', 4);
    int col2 = cmd.indexOf(':', col1 + 1);
    int col3 = cmd.indexOf(':', col2 + 1);
    if (col1 != -1 && col2 != -1 && col3 != -1) {
      int driver = cmd.substring(col1 + 1, col2).toInt(); // 1 (Left) or 2 (Right)
      int chan   = cmd.substring(col2 + 1, col3).toInt(); // 0..8
      float deg  = cmd.substring(col3 + 1).toFloat();
      
      int servoIdx = (driver == 1 ? 0 : 9) + chan;
      if (servoIdx >= 0 && servoIdx < TOTAL_SERVOS) {
        setSingleTargetAngle(servoIdx, deg, moveDurationMs);
        lcdStatusMessage = "Servo D" + String(driver) + " Ch" + String(chan) + " -> " + String((int)deg) + "d";
        Serial.print("[Hexapod] Driver ");
        Serial.print(driver);
        Serial.print(" Chan ");
        Serial.print(chan);
        Serial.print(" -> ");
        Serial.print(deg);
        Serial.println(" deg");
      }
    }
    return;
  }

  // 2. Inverse Kinematics Command: "HEX:IK:<leg>:<X>:<Y>:<Z>:<ms>"
  if (cmd.startsWith("HEX:IK:") || cmd.startsWith("hex:ik:")) {
    int p[5];
    int cur = 6;
    for (int i = 0; i < 5; i++) {
      int next = cmd.indexOf(':', cur);
      if (next == -1 && i < 4) return;
      p[i] = next;
      cur = next + 1;
    }
    
    String legCode = cmd.substring(6, p[0]);
    float x = cmd.substring(p[0] + 1, p[1]).toFloat();
    float y = cmd.substring(p[1] + 1, p[2]).toFloat();
    float z = cmd.substring(p[2] + 1, p[3]).toFloat();
    unsigned long dur = cmd.substring(p[3] + 1).toInt();

    int legIdx = parseLegCode(legCode);
    if (legIdx != -1) {
      float coxaDeg, femurDeg, tibiaDeg;
      bool isRight = (legIdx >= 3);
      if (solveIK(x, y, z, isRight, coxaDeg, femurDeg, tibiaDeg)) {
        float targets[TOTAL_SERVOS];
        memcpy(targets, currentAngles, sizeof(targets));
        targets[getServoIndex(legIdx, 0)] = coxaDeg;
        targets[getServoIndex(legIdx, 1)] = femurDeg;
        targets[getServoIndex(legIdx, 2)] = tibiaDeg;
        setTargetAngles(targets, dur);
        lcdStatusMessage = "IK: " + legCode + " (" + String((int)x) + "," + String((int)y) + "," + String((int)z) + ")";
        Serial.print("[IK Success] ");
        Serial.print(legCode);
        Serial.print(" -> Coxa: "); Serial.print(coxaDeg);
        Serial.print(" Femur: "); Serial.print(femurDeg);
        Serial.print(" Tibia: "); Serial.println(tibiaDeg);
      } else {
        lcdStatusMessage = "IK Error: Out of Reach";
        Serial.print("[IK Error] Position out of reach for leg ");
        Serial.println(legCode);
      }
    }
    return;
  }

  // 3. Motion Duration / Speed Command: "HEX:SPEED:<ms>"
  if (cmd.startsWith("HEX:SPEED:") || cmd.startsWith("HEX:SET_SPEED:")) {
    int col = cmd.lastIndexOf(':');
    if (col != -1) {
      moveDurationMs = cmd.substring(col + 1).toInt();
      if (moveDurationMs < 20) moveDurationMs = 20;
      lcdStatusMessage = "Speed: " + String(moveDurationMs) + "ms";
      Serial.print("[Hexapod] Speed set to ");
      Serial.print(moveDurationMs);
      Serial.println(" ms");
    }
    return;
  }

  // 4. Custom LCD Text Command: "HEX:LCD:MSG:<text>"
  if (cmd.startsWith("HEX:LCD:MSG:") || cmd.startsWith("hex:lcd:msg:")) {
    lcdStatusMessage = cmd.substring(12);
    Serial.println("[7-Inch Touch LCD] Message Displayed: " + lcdStatusMessage);
    return;
  }

  // 5. Speech Reactivity & AI Audio Stream Commands
  if (cmd.startsWith("AI_SPEAKING:") || cmd.startsWith("ai_speaking:") ||
      cmd.startsWith("TALK:") || cmd.startsWith("talk:") ||
      cmd.startsWith("TALKING:") || cmd.startsWith("talking:")) {
    int colonIdx = cmd.indexOf(':');
    String stateStr = cmd.substring(colonIdx + 1);
    stateStr.toUpperCase();
    stateStr.trim();
    if (stateStr == "1" || stateStr == "ON" || stateStr == "TRUE" || stateStr == "START") {
      lastSpeechTime = millis();
      isSpeechActive = true;
      lcdStatusMessage = "AI Speaking (Hexapod Reacts)";
      Serial.println("[Hexapod] AI Speaking Animatronics -> ACTIVE");
    } else {
      isSpeechActive = false;
      lastSpeechTime = 0;
      if (currentGait == "idle") applyStandPosture(250);
      lcdStatusMessage = "Hexapod Standing / Ready";
      Serial.println("[Hexapod] AI Speaking Animatronics -> REST");
    }
    return;
  }

  if (cmd.startsWith("HEX:SPEECH_REACT:") || cmd.startsWith("hex:speech_react:") ||
      cmd.startsWith("SPEECH_REACT:") || cmd.startsWith("speech_react:") ||
      cmd.startsWith("SPEECH:") || cmd.startsWith("speech:")) {
    int colonIdx = cmd.indexOf(':');
    String valStr = cmd.substring(colonIdx + 1);
    valStr.toUpperCase();
    valStr.trim();
    speechReactEnabled = (valStr == "1" || valStr == "ON" || valStr == "TRUE" || valStr == "ENABLE");
    lcdStatusMessage = speechReactEnabled ? "Speech React: ENABLED" : "Speech React: DISABLED";
    Serial.println("[Hexapod] Speech Reactivity set to: " + String(speechReactEnabled ? "ON" : "OFF"));
    return;
  }

  if (cmd.equalsIgnoreCase("HEX:SPEECH_TOGGLE") || cmd.equalsIgnoreCase("SPEECH_TOGGLE") || cmd.equalsIgnoreCase("HEX:toggle_speech")) {
    speechReactEnabled = !speechReactEnabled;
    lcdStatusMessage = speechReactEnabled ? "Speech React: ENABLED" : "Speech React: DISABLED";
    Serial.println("[Hexapod] Speech Reactivity toggled: " + String(speechReactEnabled ? "ON" : "OFF"));
    return;
  }

  // 6. Action & Gait Commands
  if (cmd == "HEX:stand" || cmd == "hex:stand") {
    handleTouchAction("stand");
  } else if (cmd == "HEX:sit" || cmd == "hex:sit") {
    handleTouchAction("sit");
  } else if (cmd == "HEX:flat" || cmd == "hex:flat") {
    handleTouchAction("flat");
  } else if (cmd == "HEX:walk" || cmd == "hex:walk") {
    handleTouchAction("walk");
  } else if (cmd == "HEX:run" || cmd == "hex:run") {
    handleTouchAction("run");
  } else if (cmd == "HEX:dance" || cmd == "hex:dance") {
    handleTouchAction("dance");
  } else if (cmd == "HEX:bow" || cmd == "hex:bow") {
    handleTouchAction("bow");
  } else if (cmd == "HEX:wave_left" || cmd == "hex:wave_left") {
    handleTouchAction("wave_left");
  } else if (cmd == "HEX:wave_right" || cmd == "hex:wave_right") {
    handleTouchAction("wave_right");
  } else if (cmd == "HEX:turn_left" || cmd == "hex:turn_left") {
    handleTouchAction("turn_left");
  } else if (cmd == "HEX:turn_right" || cmd == "hex:turn_right") {
    handleTouchAction("turn_right");
  } else if (cmd == "HEX:stop" || cmd == "hex:stop") {
    handleTouchAction("stop");
  }
}

// =============================================================================
// Arduino Setup & Main Loop
// =============================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000);

  Serial.println("==========================================================");
  Serial.println("🤖 ESP32-S3 7.0-Inch Capacitive Touchscreen Hexapod Robot");
  Serial.println("Display: 800x480 Widescreen RGB | Touch: GT911 Capacitive");
  Serial.println("I2C Bus: SDA=GPIO 8, SCL=GPIO 9 | Dual PCA9685 18 Servos");
  Serial.println("Board Substituted: ESP-32 DevKit -> ESP32-S3-Touch-LCD-7");
  Serial.println("==========================================================");

  // Initialize I2C Bus on 7-inch Touch LCD pins (SDA=GPIO 8, SCL=GPIO 9)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000); // 400kHz Fast I2C

  // Initialize Dual PCA9685 Servo Drivers
  initPCA9685(PCA9685_ADDR_LEFT);
  initPCA9685(PCA9685_ADDR_RIGHT);
  Serial.println("[I2C] Dual PCA9685 Servo Drivers (0x40 & 0x41) Initialized.");

  // Initialize GT911 Capacitive Touch Controller
  initTouchController();
  Serial.println("[Touch] GT911 5-Point Capacitive Touch Controller Initialized.");

  // Initialize Bluetooth if supported
#if HAS_BT_CLASSIC
  if (SerialBT.begin("hexapod-touch-7")) {
    Serial.println("[Bluetooth] Broadcasting as 'hexapod-touch-7' READY!");
  }
#else
  Serial.println("[Info] High-speed USB CDC Serial active for ESP32-S3 7-Inch Touch LCD.");
#endif

  // Initialize default standing posture (90 deg per servo)
  for (int i = 0; i < TOTAL_SERVOS; i++) {
    currentAngles[i] = 90.0f;
    startAngles[i]   = 90.0f;
    targetAngles[i]  = 90.0f;
    writeHardwareAngle(i, 90.0f);
  }

  applyStandPosture(300);
  Serial.println("[Hexapod] 18 Servos Initialized to 90 Deg Stand Posture.");
  Serial.println("==========================================================");
}

void loop() {
  // 1. Process USB Serial Commands
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    parseCommand(cmd);
  }

  // 2. Process Bluetooth Commands (if available)
#if HAS_BT_CLASSIC
  if (SerialBT.available() > 0) {
    String cmd = SerialBT.readStringUntil('\n');
    parseCommand(cmd);
  }
#endif

  // 3. Process 7-Inch Capacitive Touchscreen Events
  processTouchInput();

  // 4. Update 50Hz S-Curve Trajectory Interpolation Engine
  updateTrajectoryEngine();

  // 5. Update Gait Motion Step Loop
  updateGaitEngine();

  // 6. Update Real-Time Speech Animatronics & Eye Blinking Engine
  updateSpeechEngine();

  delay(2); // Short loop pause for CPU efficiency
}
