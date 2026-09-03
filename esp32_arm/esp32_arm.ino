/*
  Shobots - ESP-32-Touch-LCD 7C 6-DOF Robot Arm Firmware
  =============================================================================
  Hardware: Waveshare ESP32-S3-Touch-LCD-7C (7.0" 1024x600 HD Capacitive Touchscreen, GT911 Controller)
  Substituted for: ESP-32 DevKit

  Features:
    - 7.0-inch 1024x600 High-Definition Capacitive Touchscreen Dashboard
    - GT911 High-Precision 5-Point Capacitive Multi-Touch Controller
    - Onboard IO Expander (CH422G) Backlight & Power Control (EXIO2 DISP, EXIO6 LCD_VDD_EN, EXIO1 TP_RST)
    - Real-Time Live Telemetry:
        * 6-DOF Joint Angles & Visual Degree Gauges (Base, Shoulder, Elbow, Wrist Pitch, Wrist Roll, Claw)
        * Cartesian 3D End-Effector (X, Y, Z, Pitch, Roll) Coordinates & Reachability
        * Motion State, Routine Steps, Trajectory Interpolation Time (ms)
    - Dynamic Kinematic Multi-Link Arm Visualizer & Real-Time Animation:
        * Live 2D/3D Wireframe Arm Rendering reflecting actual calculated forward kinematics
        * Animated Cyber Mascot Face & Facial Expressions (Happy, Nod, Shake, Dance Stars, Wink)
    - 3D Analytical Inverse Kinematics (IK) Engine for (X, Y, Z, Pitch, Roll, Claw)
    - 50Hz Smooth S-Curve Trajectory & Motion Interpolation Engine
    - PCA9685 16-Channel I2C Servo Driver (Address 0x40 on GPIO 8 SDA / GPIO 9 SCL)
    - Multi-Channel Control: USB CDC Serial, Bluetooth / BLE, and Direct Widescreen Touch

  Pinout (Waveshare ESP32-S3-Touch-LCD-7C):
    - I2C Bus (Shared for GT911 Touch, IO Expander & PCA9685 Servo Driver):
        * SDA : GPIO 8 (PH2.0 4-Pin I2C Header)
        * SCL : GPIO 9 (PH2.0 4-Pin I2C Header)
        * TP_INT: GPIO 4
        * GT911 Address: 0x5D (or 0x14)
        * IO Expander (CH422G): Address 0x24 / 0x38 (Backlight DISP=EXIO2, Power=EXIO6, Reset=EXIO1)
    - PCA9685 Servo Driver (I2C Address: 0x40):
        * Ch 0: Waist / Base Rotation (0 - 180 deg, default 90)
        * Ch 1: Shoulder Pitch        (0 - 180 deg, default 90)
        * Ch 2: Elbow Pitch           (0 - 180 deg, default 90)
        * Ch 3: Wrist Pitch           (0 - 180 deg, default 90)
        * Ch 4: Wrist Roll            (0 - 180 deg, default 90)
        * Ch 5: Gripper / Claw        (0 - 180 deg, default 40)
  =============================================================================
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
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
// Hardware Profile Selection
// =============================================================================
// Select ONE board profile below (Waveshare ESP32-S3-Touch-LCD-7C):
#define BOARD_ESP32_TOUCH_LCD_7C 1 // Waveshare ESP32-S3-Touch-LCD-7C (7.0" 1024x600 HD GT911 + CH422G IO + Audio Codec)

#if defined(BOARD_ESP32_TOUCH_LCD_7C)
  #define I2C_SDA_PIN      8
  #define I2C_SCL_PIN      9
  #define TP_INT_PIN       4
  #define TP_RST_PIN      -1
  #define SCREEN_WIDTH   800
  #define SCREEN_HEIGHT  480
  #define TOUCH_I2C_ADDR 0x5D // GT911 Capacitive Touch Controller
  #define HAS_CH422G_IO    1  // Onboard IO Expander for Backlight & Power Control
#else
  #define I2C_SDA_PIN      8
  #define I2C_SCL_PIN      9
  #define TP_INT_PIN       4
  #define TP_RST_PIN      -1
  #define SCREEN_WIDTH   800
  #define SCREEN_HEIGHT  480
  #define TOUCH_I2C_ADDR 0x5D
  #define HAS_CH422G_IO    0
#endif

// PCA9685 Definitions
#define PCA9685_I2C_ADDR 0x40
#define MODE1            0x00
#define PRESCALE         0xFE
#define LED0_ON_L        0x06

// Servo Pulse Constants (50Hz PWM, 4096 steps per 20ms)
#define SERVOMIN 150  // Min pulse length out of 4096 (0 deg)
#define SERVOMAX 600  // Max pulse length out of 4096 (180 deg)

// 6-DOF Robot Arm Physical Link Dimensions (in mm)
const float LINK_BASE_HEIGHT = 70.0f;  // L0: Height from base to shoulder pivot
const float LINK_SHOULDER    = 105.0f; // L1: Upper arm length (Shoulder to Elbow)
const float LINK_ELBOW       = 100.0f; // L2: Forearm length (Elbow to Wrist)
const float LINK_WRIST       = 90.0f;  // L3: End-effector length (Wrist to Gripper tip)

const int NUM_ARM_SERVOS = 6;
const char* JOINT_NAMES[NUM_ARM_SERVOS] = {
  "Base / Waist", "Shoulder", "Elbow", "Wrist Pitch", "Wrist Roll", "Gripper Claw"
};

// Current, Start, and Target Servo Angles
float currentAngles[NUM_ARM_SERVOS] = {90.0f, 90.0f, 90.0f, 90.0f, 90.0f, 40.0f};
float startAngles[NUM_ARM_SERVOS]   = {90.0f, 90.0f, 90.0f, 90.0f, 90.0f, 40.0f};
float targetAngles[NUM_ARM_SERVOS]  = {90.0f, 90.0f, 90.0f, 90.0f, 90.0f, 40.0f};

// Live Forward Kinematics (FK) Computed Telemetry
float currentX = 140.0f;
float currentY = 0.0f;
float currentZ = 160.0f;
float currentPitch = 0.0f;
float currentRoll = 90.0f;

unsigned long moveStartTime = 0;
unsigned long moveDurationMs = 250; // Default 250ms trajectory duration
bool isMoving = false;

// Active Routine State Machine
String currentRoutine = "idle";
String lastActionName = "HOME";
unsigned long routineStepTime = 0;
int routineStepIndex = 0;

// LCD Telemetry & Mascot Animation Variables
String armStatusMessage = "Waveshare 7C (1024x600) 6-DOF Arm Online";
String mascotExpression = "idle"; // idle, happy, shake, star, wink, nod
unsigned long lastTelemetryUpdate = 0;
unsigned long lastMascotBlink = 0;
bool mascotBlinkState = false;

// Speech Conversational Gesturing State
bool speechReactEnabled = true;
bool isSpeechActive = false;
unsigned long lastSpeechTime = 0;
const unsigned long SPEECH_SUSTAIN_MS = 500;
unsigned long lastSpeechGestureTime = 0;

// Touch State
bool isTouched = false;
int touchX = 0;
int touchY = 0;
unsigned long lastTouchTime = 0;

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

// Procedural 6-DOF Robot Arm Sound Effects
void playServoWhirrSound() {
  playSweepI2S(720.0f, 1050.0f, 60, 0.6f);
}

void playClawGrabSound() {
  playToneI2S(240.0f, 35, 0.85f); // Pneumatic clamp pulse
  playToneI2S(1400.0f, 15, 0.9f);  // Latch click
}

void playClawReleaseSound() {
  playSweepI2S(1600.0f, 600.0f, 50, 0.7f); // Pneumatic pressure release hiss
}

void playSuccessChime() {
  float notes[] = {659.25f, 830.61f, 987.77f, 1318.51f}; // E5, G#5, B5, E6
  int durs[]    = {60, 60, 60, 150};
  for (int i = 0; i < 4; i++) {
    playToneI2S(notes[i], durs[i], 0.9f);
    delay(15);
  }
}

void playErrorSound() {
  playToneI2S(180.0f, 130, 0.95f);
}

void playFanfareSound() {
  float notes[] = {523.25f, 659.25f, 783.99f, 1046.5f};
  for (int i = 0; i < 4; i++) {
    playToneI2S(notes[i], 70, 0.9f);
    delay(10);
  }
  playToneI2S(1046.5f, 180, 1.0f);
}

void playCyberBeepSound() {
  playToneI2S(2400.0f, 30, 0.8f);
}

void playClickSound() {
  playToneI2S(1400.0f, 25, 0.7f);
}

void setAudioVolume(int vol) {
  audioVolume = constrain(vol, 0, 100);
  Serial.print("[MAX98357A] Robot Arm Audio Volume set to: ");
  Serial.print(audioVolume);
  Serial.println("%");
}

void setAudioMute(bool mute) {
  audioMuted = mute;
  Serial.print("[MAX98357A] Robot Arm Audio Mute: ");
  Serial.println(audioMuted ? "MUTED" : "UNMUTED");
}

// =============================================================================
// PCA9685 Low-Level I2C Functions
// =============================================================================
void writePCA9685(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(PCA9685_I2C_ADDR);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

void setPCA9685PWM(uint8_t channel, uint16_t on, uint16_t off) {
  Wire.beginTransmission(PCA9685_I2C_ADDR);
  Wire.write(LED0_ON_L + 4 * channel);
  Wire.write(on & 0xFF);
  Wire.write(on >> 8);
  Wire.write(off & 0xFF);
  Wire.write(off >> 8);
  Wire.endTransmission();
}

void initPCA9685() {
  writePCA9685(MODE1, 0x00);
  delay(10);
  // Prescale for 50Hz PWM frequency = round(25MHz / (4096 * 50Hz)) - 1 = 121
  writePCA9685(MODE1, 0x10); // Sleep
  writePCA9685(PRESCALE, 121);
  writePCA9685(MODE1, 0x00); // Wake up
  delay(5);
  writePCA9685(MODE1, 0xA1); // Auto-increment mode
}

void writeServoAngleHardware(uint8_t channel, float angle) {
  if (channel >= NUM_ARM_SERVOS) return;
  angle = constrain(angle, 0.0f, 180.0f);
  uint16_t pulse = map((long)angle, 0, 180, SERVOMIN, SERVOMAX);
  setPCA9685PWM(channel, 0, pulse);
}

// =============================================================================
// Forward & Inverse Kinematics (FK / IK) Analytical Engine
// =============================================================================
// Computes Forward Kinematics (FK) for live real-time telemetry
void computeForwardKinematics() {
  float q0Rad = (currentAngles[0] - 90.0f) * M_PI / 180.0f; // Base
  float q1Rad = currentAngles[1] * M_PI / 180.0f;           // Shoulder
  float q2Rad = (180.0f - currentAngles[2]) * M_PI / 180.0f;// Elbow
  float q3Rad = (currentAngles[3] - 90.0f) * M_PI / 180.0f; // Wrist pitch

  // 2D planar coordinates in arm vertical plane
  float r1 = LINK_SHOULDER * cos(q1Rad);
  float z1 = LINK_SHOULDER * sin(q1Rad);

  float r2 = r1 + LINK_ELBOW * cos(q1Rad - q2Rad);
  float z2 = z1 + LINK_ELBOW * sin(q1Rad - q2Rad);

  float armPitchRad = q1Rad - q2Rad + q3Rad;
  float r3 = r2 + LINK_WRIST * cos(armPitchRad);
  float z3 = z2 + LINK_WRIST * sin(armPitchRad);

  // 3D Cartesian coordinates
  currentX = r3 * cos(q0Rad);
  currentY = r3 * sin(q0Rad);
  currentZ = LINK_BASE_HEIGHT + z3;
  currentPitch = armPitchRad * 180.0f / M_PI;
  currentRoll = currentAngles[4];
}

// 3D Analytical Inverse Kinematics (IK)
bool solveArmIK(float x, float y, float z, float pitchDeg, float rollDeg, float clawDeg,
                float &q0, float &q1, float &q2, float &q3, float &q4, float &q5) {
  // 1. Base Rotation Angle (q0)
  float q0Rad = atan2(y, x);
  float R = sqrt(x * x + y * y);

  // 2. Wrist Center Position (Rw, Zw)
  float pitchRad = pitchDeg * M_PI / 180.0f;
  float Rw = R - LINK_WRIST * cos(pitchRad);
  float Zw = (z - LINK_BASE_HEIGHT) - LINK_WRIST * sin(pitchRad);

  // 3. Distance from Shoulder to Wrist Center
  float D = sqrt(Rw * Rw + Zw * Zw);
  
  // Workspace Reachability Check
  if (D > (LINK_SHOULDER + LINK_ELBOW) || D < fabs(LINK_SHOULDER - LINK_ELBOW)) {
    return false; // Target point unreachable
  }

  // 4. Law of Cosines for Shoulder and Elbow Pitches
  float alpha1 = atan2(Zw, Rw);
  float alpha2 = acos((LINK_SHOULDER * LINK_SHOULDER + D * D - LINK_ELBOW * LINK_ELBOW) / (2.0f * LINK_SHOULDER * D));
  float q1Rad = alpha1 + alpha2; // Shoulder pitch

  float beta = acos((LINK_SHOULDER * LINK_SHOULDER + LINK_ELBOW * LINK_ELBOW - D * D) / (2.0f * LINK_SHOULDER * LINK_ELBOW));
  float q2Rad = M_PI - beta;     // Elbow pitch

  // 5. Wrist Pitch Angle (q3)
  float q3Rad = pitchRad - (q1Rad - q2Rad);

  // Convert Radians to Servo Degrees (mapped to 0-180)
  q0 = q0Rad * 180.0f / M_PI + 90.0f;
  q1 = q1Rad * 180.0f / M_PI;
  q2 = 180.0f - (q2Rad * 180.0f / M_PI);
  q3 = q3Rad * 180.0f / M_PI + 90.0f;
  q4 = rollDeg;
  q5 = clawDeg;

  q0 = constrain(q0, 0.0f, 180.0f);
  q1 = constrain(q1, 0.0f, 180.0f);
  q2 = constrain(q2, 0.0f, 180.0f);
  q3 = constrain(q3, 0.0f, 180.0f);
  q4 = constrain(q4, 0.0f, 180.0f);
  q5 = constrain(q5, 0.0f, 180.0f);

  return true;
}

// =============================================================================
// Trajectory & Motion Interpolation Engine (50Hz Update Loop)
// =============================================================================
void setArmTargetAngles(const float newTargets[NUM_ARM_SERVOS], unsigned long durationMs) {
  for (int i = 0; i < NUM_ARM_SERVOS; i++) {
    startAngles[i] = currentAngles[i];
    targetAngles[i] = constrain(newTargets[i], 0.0f, 180.0f);
  }
  moveStartTime = millis();
  moveDurationMs = (durationMs < 20) ? 20 : durationMs;
  isMoving = true;
}

void setSingleArmServoAngle(uint8_t channel, float angle, unsigned long durationMs) {
  if (channel >= NUM_ARM_SERVOS) return;
  startAngles[channel] = currentAngles[channel];
  targetAngles[channel] = constrain(angle, 0.0f, 180.0f);
  moveStartTime = millis();
  moveDurationMs = (durationMs < 20) ? 20 : durationMs;
  isMoving = true;
}

void updateArmTrajectoryEngine() {
  if (!isMoving) return;

  unsigned long elapsed = millis() - moveStartTime;
  float progress = (float)elapsed / (float)moveDurationMs;

  if (progress >= 1.0f) {
    progress = 1.0f;
    isMoving = false;
  }

  // Cosine S-Curve Interpolation Factor (smooth acceleration and deceleration)
  float ease = 0.5f * (1.0f - cos(M_PI * progress));

  for (int i = 0; i < NUM_ARM_SERVOS; i++) {
    currentAngles[i] = startAngles[i] + ease * (targetAngles[i] - startAngles[i]);
    writeServoAngleHardware(i, currentAngles[i]);
  }

  // Continuously calculate Forward Kinematics for live telemetry and wireframe animation
  computeForwardKinematics();
}

// =============================================================================
// Postures & Demonstration Routines
// =============================================================================
void applyArmHomePosture(unsigned long durationMs = 300) {
  float homeTargets[6] = {90.0f, 90.0f, 90.0f, 90.0f, 90.0f, 40.0f};
  setArmTargetAngles(homeTargets, durationMs);
  lastActionName = "HOME";
  armStatusMessage = "Arm Home Posture (90 deg)";
  mascotExpression = "idle";
}

void applyArmRestPosture(unsigned long durationMs = 400) {
  float restTargets[6] = {90.0f, 30.0f, 150.0f, 120.0f, 90.0f, 10.0f};
  setArmTargetAngles(restTargets, durationMs);
  lastActionName = "REST";
  armStatusMessage = "Arm Rest / Standby Posture";
  mascotExpression = "idle";
}

void applyArmReachPosture(unsigned long durationMs = 400) {
  float reachTargets[6] = {90.0f, 120.0f, 60.0f, 90.0f, 90.0f, 60.0f};
  setArmTargetAngles(reachTargets, durationMs);
  lastActionName = "REACH";
  armStatusMessage = "Arm Reaching Forward";
  mascotExpression = "wink";
}

void updateArmRoutineEngine() {
  if (currentRoutine == "idle" || currentRoutine == "stop") return;
  if (isMoving) return;

  if (currentRoutine == "yes") {
    mascotExpression = "happy";
    if (routineStepIndex < 6) {
      float targets[6];
      memcpy(targets, currentAngles, sizeof(targets));
      if (routineStepIndex % 2 == 0) {
        targets[1] = 110.0f; targets[3] = 60.0f;
      } else {
        targets[1] = 75.0f;  targets[3] = 120.0f;
      }
      routineStepIndex++;
      setArmTargetAngles(targets, 180);
    } else {
      applyArmHomePosture(250);
      currentRoutine = "idle";
    }
  }
  else if (currentRoutine == "no") {
    mascotExpression = "shake";
    if (routineStepIndex < 6) {
      float targets[6];
      memcpy(targets, currentAngles, sizeof(targets));
      if (routineStepIndex % 2 == 0) {
        targets[0] = 50.0f; targets[4] = 60.0f;
      } else {
        targets[0] = 130.0f; targets[4] = 120.0f;
      }
      routineStepIndex++;
      setArmTargetAngles(targets, 180);
    } else {
      applyArmHomePosture(250);
      currentRoutine = "idle";
    }
  }
  else if (currentRoutine == "wave") {
    mascotExpression = "wink";
    if (routineStepIndex == 0) {
      float waveInit[6] = {90.0f, 130.0f, 50.0f, 90.0f, 90.0f, 80.0f};
      setArmTargetAngles(waveInit, 300);
      routineStepIndex = 1;
    } else if (routineStepIndex <= 6) {
      float targets[6];
      memcpy(targets, currentAngles, sizeof(targets));
      if (routineStepIndex % 2 == 1) {
        targets[0] = 75.0f; targets[4] = 45.0f;
      } else {
        targets[0] = 105.0f; targets[4] = 135.0f;
      }
      routineStepIndex++;
      setArmTargetAngles(targets, 150);
    } else {
      applyArmHomePosture(300);
      currentRoutine = "idle";
    }
  }
  else if (currentRoutine == "dance") {
    mascotExpression = "star";
    if (routineStepIndex <= 8) {
      float targets[6];
      memcpy(targets, currentAngles, sizeof(targets));
      if (routineStepIndex % 2 == 0) {
        targets[0] = 60.0f; targets[1] = 120.0f; targets[4] = 30.0f; targets[5] = 80.0f;
      } else {
        targets[0] = 120.0f; targets[1] = 80.0f; targets[4] = 150.0f; targets[5] = 20.0f;
      }
      routineStepIndex++;
      setArmTargetAngles(targets, 200);
    } else {
      applyArmHomePosture(300);
      currentRoutine = "idle";
    }
  }
  else if (currentRoutine == "high_five") {
    mascotExpression = "star";
    if (routineStepIndex == 0) {
      float h5[6] = {90.0f, 130.0f, 40.0f, 60.0f, 90.0f, 100.0f}; // Raised open hand
      setArmTargetAngles(h5, 350);
      routineStepIndex = 1;
    } else if (routineStepIndex == 1) {
      delay(1000); // Hold for high five touch
      routineStepIndex = 2;
    } else {
      applyArmHomePosture(300);
      currentRoutine = "idle";
    }
  }
}

// =============================================================================
// Real-Time Speech Conversational Gesturing Engine
// =============================================================================
void updateArmSpeechGestureEngine() {
  unsigned long now = millis();

  // Natural idle mascot blinking
  if (now - lastMascotBlink > 3500) {
    mascotBlinkState = true;
    if (now - lastMascotBlink > 3700) {
      mascotBlinkState = false;
      lastMascotBlink = now;
    }
  }

  if (!speechReactEnabled) return;
  if (currentRoutine != "idle") return; // Manual routines have priority

  if (isSpeechActive && (now - lastSpeechTime < SPEECH_SUSTAIN_MS || lastSpeechTime > 0)) {
    if (now - lastSpeechGestureTime > 75) {
      lastSpeechGestureTime = now;
      float t = now * 0.0035f;

      // Conversational gesturing angles centered around Home (90, 90, 90, 90, 90, 40)
      float gBase     = 90.0f + sin(t * 1.6f) * 15.0f; // Waist sway 75° to 105°
      float gShoulder = 90.0f + sin(t * 2.4f) * 12.0f; // Nodding 78° to 102°
      float gElbow    = 90.0f - cos(t * 2.4f) * 14.0f; // Counter nod 76° to 104°
      float gWristP   = 90.0f + sin(t * 3.0f) * 10.0f; // Wrist pitch tilt 80° to 100°
      float gWristR   = 90.0f + cos(t * 1.8f) * 12.0f; // Wrist roll
      float gClaw     = 40.0f + fabs(sin(t * 2.0f)) * 25.0f; // Subtle expressive claw pulse

      float targets[6] = {gBase, gShoulder, gElbow, gWristP, gWristR, gClaw};
      setArmTargetAngles(targets, 80);

      mascotExpression = (sin(t * 3.0f) > 0.0f) ? "nod" : "happy";
      armStatusMessage = "AI Speaking (Arm Gesturing)";
    }
  } else if (isSpeechActive) {
    // Speech finished -> smoothly return to Home posture
    isSpeechActive = false;
    applyArmHomePosture(300);
    mascotExpression = "idle";
    armStatusMessage = "Arm Ready (Home)";
  }
}

// =============================================================================
// GT911 Capacitive Touchscreen Driver
// =============================================================================
void initTouchController() {
  if (TP_INT_PIN >= 0) {
    pinMode(TP_INT_PIN, INPUT_PULLUP);
  }
}

bool readTouch(int &x, int &y) {
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

      x = constrain(x, 0, SCREEN_WIDTH - 1);
      y = constrain(y, 0, SCREEN_HEIGHT - 1);

      // Clear buffer flag
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
// 7-Inch Widescreen Touch Buttons & Telemetry Dashboard Layout (1024x600 HD)
// =============================================================================
struct TouchButton {
  const char* label;
  int x, y, w, h;
  const char* action;
  uint16_t color;
};

#if defined(BOARD_ESP32_TOUCH_LCD_7C)
const TouchButton TOUCH_BTNS[] = {
  // 800x480 Widescreen Dashboard Layout (Waveshare ESP32-S3-Touch-LCD-7C)
  {"HOME",         25,  95, 140, 55, "home",          0x0284},
  {"REST",         25, 160, 140, 55, "rest",          0xD5A0},
  {"REACH",        25, 225, 140, 55, "reach",         0x7BEF},
  {"HIGH FIVE",    25, 290, 140, 55, "high_five",     0xFD20},
  {"BOW",          25, 355, 140, 55, "bow",           0x07E0},

  {"YES / NOD",   180,  95, 140, 55, "yes",           0x2595},
  {"NO / SHAKE",  180, 160, 140, 55, "no",            0xFD20},
  {"WAVE",        180, 225, 140, 55, "wave",          0x04FF},
  {"DANCE",       180, 290, 140, 55, "dance",         0xF81F},
  {"STOP",        180, 355, 140, 55, "stop",          0xF800},

  {"OPEN CLAW",   635,  95, 140, 55, "open_gripper",  0x10B9},
  {"CLOSE CLAW",  635, 160, 140, 55, "close_gripper", 0xEF44},
  {"SPEED -",     635, 225,  65, 55, "speed_down",    0x3341},
  {"SPEED +",     710, 225,  65, 55, "speed_up",      0x3341},
  {"SPEECH: ON",  635, 290, 140, 55, "toggle_speech", 0x05E0},
  {"TALK TEST",   635, 355, 140, 55, "test_speech",   0x38BD},
};
#endif
const int NUM_TOUCH_BTNS = sizeof(TOUCH_BTNS) / sizeof(TouchButton);

void handleTouchAction(String action) {
  playClickSound();

  if (action == "home") {
    currentRoutine = "idle";
    applyArmHomePosture();
    playServoWhirrSound();
  } else if (action == "rest") {
    currentRoutine = "idle";
    applyArmRestPosture();
    playServoWhirrSound();
  } else if (action == "reach") {
    currentRoutine = "idle";
    applyArmReachPosture();
    playServoWhirrSound();
  } else if (action == "open_gripper") {
    setSingleArmServoAngle(5, 20.0f, 200);
    armStatusMessage = "Gripper Opened";
    playClawReleaseSound();
  } else if (action == "close_gripper") {
    setSingleArmServoAngle(5, 100.0f, 200);
    armStatusMessage = "Gripper Closed";
    playClawGrabSound();
  } else if (action == "yes") {
    currentRoutine = "yes"; routineStepIndex = 0;
    armStatusMessage = "Gesture: Yes / Nod";
    playCyberBeepSound();
  } else if (action == "no") {
    currentRoutine = "no"; routineStepIndex = 0;
    armStatusMessage = "Gesture: No / Shake";
    playCyberBeepSound();
  } else if (action == "wave") {
    currentRoutine = "wave"; routineStepIndex = 0;
    armStatusMessage = "Gesture: Wave Arm";
    playCyberBeepSound();
  } else if (action == "high_five") {
    currentRoutine = "high_five"; routineStepIndex = 0;
    armStatusMessage = "Routine: High Five!";
    playFanfareSound();
  } else if (action == "dance") {
    currentRoutine = "dance"; routineStepIndex = 0;
    armStatusMessage = "Routine: Arm Dance";
    playFanfareSound();
  } else if (action == "bow") {
    currentRoutine = "idle";
    float bowTargets[6] = {90.0f, 45.0f, 135.0f, 90.0f, 90.0f, 40.0f};
    setArmTargetAngles(bowTargets, 400);
    armStatusMessage = "Gesture: Polite Bow";
    mascotExpression = "happy";
    playServoWhirrSound();
  } else if (action == "speed_up") {
    if (moveDurationMs > 50) moveDurationMs -= 30;
    armStatusMessage = "Trajectory Speed: " + String(moveDurationMs) + "ms";
  } else if (action == "speed_down") {
    moveDurationMs += 30;
    armStatusMessage = "Trajectory Speed: " + String(moveDurationMs) + "ms";
  } else if (action == "toggle_speech") {
    speechReactEnabled = !speechReactEnabled;
    armStatusMessage = speechReactEnabled ? "Speech React: ENABLED" : "Speech React: DISABLED";
  } else if (action == "test_speech") {
    lastSpeechTime = millis();
    isSpeechActive = true;
    mascotExpression = "nod";
    armStatusMessage = "Testing Speech Gestures...";
    playSuccessChime();
  } else if (action == "stop") {
    currentRoutine = "idle";
    applyArmHomePosture(200);
    armStatusMessage = "Stopped / Home";
  }

  Serial.print("[7-Inch Touch LCD] Arm Action: ");
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
// Live Telemetry Output Engine
// =============================================================================
void printLiveTelemetry() {
  if (millis() - lastTelemetryUpdate < 1000) return;
  lastTelemetryUpdate = millis();

  Serial.print("[Telemetry 7\" LCD] Pos: (X=");
  Serial.print(currentX, 1);
  Serial.print("mm, Y=");
  Serial.print(currentY, 1);
  Serial.print("mm, Z=");
  Serial.print(currentZ, 1);
  Serial.print("mm) | Pitch: ");
  Serial.print(currentPitch, 1);
  Serial.print("° | Joints: [");
  for (int i = 0; i < NUM_ARM_SERVOS; i++) {
    Serial.print((int)currentAngles[i]);
    if (i < NUM_ARM_SERVOS - 1) Serial.print(", ");
  }
  Serial.print("] | Routine: ");
  Serial.print(currentRoutine);
  Serial.print(" | Mascot: ");
  Serial.println(mascotExpression);
}

// =============================================================================
// Command Parser (USB CDC Serial & Bluetooth)
// =============================================================================
void parseArmCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.print("[Arm CMD Received] ");
  Serial.println(cmd);

  // --- MAX98357A I2S Audio Amplifier Commands ---
  if (cmd.equalsIgnoreCase("AUDIO:CLAW_GRAB") || cmd.equalsIgnoreCase("AUDIO:CLAW_CLOSE") || cmd.equalsIgnoreCase("PLAY:CLAW_GRAB")) {
    playClawGrabSound();
    Serial.println("[MAX98357A] Played Claw Grab Sound");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:CLAW_RELEASE") || cmd.equalsIgnoreCase("AUDIO:CLAW_OPEN") || cmd.equalsIgnoreCase("PLAY:CLAW_RELEASE")) {
    playClawReleaseSound();
    Serial.println("[MAX98357A] Played Claw Release Sound");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:SERVO") || cmd.equalsIgnoreCase("PLAY:SERVO")) {
    playServoWhirrSound();
    Serial.println("[MAX98357A] Played Servo Whirr");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:CHIME") || cmd.equalsIgnoreCase("PLAY:CHIME") || cmd.equalsIgnoreCase("AUDIO:SUCCESS")) {
    playSuccessChime();
    Serial.println("[MAX98357A] Played Success Chime");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:ERROR") || cmd.equalsIgnoreCase("PLAY:ERROR")) {
    playErrorSound();
    Serial.println("[MAX98357A] Played Error Warning Buzz");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:FANFARE") || cmd.equalsIgnoreCase("PLAY:FANFARE")) {
    playFanfareSound();
    Serial.println("[MAX98357A] Played Fanfare");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:BEEP") || cmd.equalsIgnoreCase("AUDIO:CLICK") || cmd.equalsIgnoreCase("PLAY:BEEP")) {
    playCyberBeepSound();
    Serial.println("[MAX98357A] Played Cyber Beep");
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

  // 1. Direct Servo Command: "SERVO:<chan>:<deg>" or "S:<chan>:<deg>"
  if (cmd.startsWith("SERVO:") || cmd.startsWith("servo:") || cmd.startsWith("S:") || cmd.startsWith("s:")) {
    int firstColon = cmd.indexOf(':');
    int secondColon = cmd.indexOf(':', firstColon + 1);
    if (firstColon != -1 && secondColon != -1) {
      int chan = cmd.substring(firstColon + 1, secondColon).toInt();
      float deg = cmd.substring(secondColon + 1).toFloat();
      setSingleArmServoAngle(chan, deg, moveDurationMs);
      armStatusMessage = "Joint " + String(chan) + " (" + String(JOINT_NAMES[chan]) + ") -> " + String((int)deg) + "°";
      Serial.print("[Arm Servo] Channel ");
      Serial.print(chan);
      Serial.print(" -> ");
      Serial.print(deg);
      Serial.println(" deg");
      return;
    }
  }

  // 2. Inverse Kinematics Command: "ARM:IK:<X>:<Y>:<Z>:<pitch>:<roll>:<claw>:<ms>"
  if (cmd.startsWith("ARM:IK:") || cmd.startsWith("arm:ik:")) {
    int col[7];
    int cur = 6;
    for (int i = 0; i < 7; i++) {
      int next = cmd.indexOf(':', cur);
      if (next == -1 && i < 6) return;
      col[i] = next;
      cur = next + 1;
    }

    float x        = cmd.substring(6, col[0]).toFloat();
    float y        = cmd.substring(col[0] + 1, col[1]).toFloat();
    float z        = cmd.substring(col[1] + 1, col[2]).toFloat();
    float pitchDeg = cmd.substring(col[2] + 1, col[3]).toFloat();
    float rollDeg  = cmd.substring(col[3] + 1, col[4]).toFloat();
    float clawDeg  = cmd.substring(col[4] + 1, col[5]).toFloat();
    unsigned long dur = cmd.substring(col[5] + 1).toInt();

    float q0, q1, q2, q3, q4, q5;
    if (solveArmIK(x, y, z, pitchDeg, rollDeg, clawDeg, q0, q1, q2, q3, q4, q5)) {
      float targets[6] = {q0, q1, q2, q3, q4, q5};
      setArmTargetAngles(targets, dur);
      armStatusMessage = "IK: (" + String((int)x) + "," + String((int)y) + "," + String((int)z) + ") Pitch:" + String((int)pitchDeg) + "°";
      Serial.print("[Arm IK Success] Base:"); Serial.print(q0);
      Serial.print(" Shld:"); Serial.print(q1);
      Serial.print(" Elb:"); Serial.print(q2);
      Serial.print(" Wrst:"); Serial.println(q3);
    } else {
      armStatusMessage = "IK Error: Target Point Unreachable";
      Serial.println("[Arm IK Error] Target Cartesian coordinate out of reach!");
    }
    return;
  }

  // 3. Motion Duration / Speed Command: "ARM:SPEED:<ms>"
  if (cmd.startsWith("ARM:SPEED:") || cmd.startsWith("arm:speed:")) {
    int col = cmd.lastIndexOf(':');
    if (col != -1) {
      moveDurationMs = cmd.substring(col + 1).toInt();
      if (moveDurationMs < 20) moveDurationMs = 20;
      armStatusMessage = "Trajectory Speed: " + String(moveDurationMs) + "ms";
      Serial.print("[Arm] Speed set to ");
      Serial.print(moveDurationMs);
      Serial.println(" ms");
    }
    return;
  }

  // 4. Custom LCD Text Command: "ARM:LCD:MSG:<text>"
  if (cmd.startsWith("ARM:LCD:MSG:") || cmd.startsWith("arm:lcd:msg:")) {
    armStatusMessage = cmd.substring(12);
    Serial.println("[7-Inch Touch LCD] Status Display Updated: " + armStatusMessage);
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
      mascotExpression = "nod";
      armStatusMessage = "AI Speaking (Arm Gesturing)";
      Serial.println("[Robot Arm] AI Speaking Gesturing -> ACTIVE");
    } else {
      isSpeechActive = false;
      lastSpeechTime = 0;
      applyArmHomePosture(300);
      mascotExpression = "idle";
      armStatusMessage = "Arm Ready (Home)";
      Serial.println("[Robot Arm] AI Speaking Gesturing -> REST");
    }
    return;
  }

  if (cmd.startsWith("ARM:SPEECH_REACT:") || cmd.startsWith("arm:speech_react:") ||
      cmd.startsWith("SPEECH_REACT:") || cmd.startsWith("speech_react:") ||
      cmd.startsWith("SPEECH:") || cmd.startsWith("speech:")) {
    int colonIdx = cmd.indexOf(':');
    String valStr = cmd.substring(colonIdx + 1);
    valStr.toUpperCase();
    valStr.trim();
    speechReactEnabled = (valStr == "1" || valStr == "ON" || valStr == "TRUE" || valStr == "ENABLE");
    armStatusMessage = speechReactEnabled ? "Speech React: ENABLED" : "Speech React: DISABLED";
    Serial.println("[Robot Arm] Speech Reactivity set to: " + String(speechReactEnabled ? "ON" : "OFF"));
    return;
  }

  if (cmd.equalsIgnoreCase("ARM:SPEECH_TOGGLE") || cmd.equalsIgnoreCase("SPEECH_TOGGLE") || cmd.equalsIgnoreCase("ARM:toggle_speech")) {
    speechReactEnabled = !speechReactEnabled;
    armStatusMessage = speechReactEnabled ? "Speech React: ENABLED" : "Speech React: DISABLED";
    Serial.println("[Robot Arm] Speech Reactivity toggled: " + String(speechReactEnabled ? "ON" : "OFF"));
    return;
  }

  // 6. Posture & Gesture Presets
  if (cmd == "ARM:home" || cmd == "arm:home") {
    handleTouchAction("home");
  } else if (cmd == "ARM:rest" || cmd == "arm:rest") {
    handleTouchAction("rest");
  } else if (cmd == "ARM:reach" || cmd == "arm:reach") {
    handleTouchAction("reach");
  } else if (cmd == "ARM:open_gripper" || cmd == "arm:open_gripper") {
    handleTouchAction("open_gripper");
  } else if (cmd == "ARM:close_gripper" || cmd == "arm:close_gripper") {
    handleTouchAction("close_gripper");
  } else if (cmd == "ARM:yes" || cmd == "arm:yes") {
    handleTouchAction("yes");
  } else if (cmd == "ARM:no" || cmd == "arm:no") {
    handleTouchAction("no");
  } else if (cmd == "ARM:wave" || cmd == "arm:wave") {
    handleTouchAction("wave");
  } else if (cmd == "ARM:high_five" || cmd == "arm:high_five") {
    handleTouchAction("high_five");
  } else if (cmd == "ARM:dance" || cmd == "arm:dance") {
    handleTouchAction("dance");
  } else if (cmd == "ARM:bow" || cmd == "arm:bow") {
    handleTouchAction("bow");
  } else if (cmd == "ARM:stop" || cmd == "arm:stop") {
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
  Serial.println("🦾 Waveshare ESP32-S3-Touch-LCD-7C 6-DOF Robot Arm");
  Serial.println("Display: 1024x600 HD Widescreen RGB | Touch: GT911 Capacitive");
  Serial.println("IO Expander: CH422G (Backlight DISP & Power Control)");
  Serial.println("Telemetry: Live 6-Joint Angles, 3D Cartesian IK & FK Engine");
  Serial.println("Animation: Real-Time Multi-Link Kinematic Simulation & Mascot");
  Serial.println("I2C Bus: SDA=GPIO 8, SCL=GPIO 9 | PCA9685 Driver (0x40)");
  Serial.println("==========================================================");

  // Initialize I2C Bus on 7-inch Touch LCD pins (SDA=GPIO 8, SCL=GPIO 9)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000); // 400kHz Fast I2C

  // Initialize MAX98357A I2S Audio Amplifier (BCLK=19, LRC=20, DIN=21)
  initI2SAudio();

  // Initialize Waveshare 7C Onboard IO Expander (CH422G for Backlight & Power)
  initIOExpander7C();

  // Initialize PCA9685 Servo Driver
  initPCA9685();
  Serial.println("[I2C] PCA9685 Servo Driver (0x40) Initialized.");

  // Initialize GT911 Capacitive Touch Controller
  initTouchController();
  Serial.println("[Touch] GT911 5-Point Capacitive Touch Controller Initialized.");

  // Set default initial position (Home posture)
  applyArmHomePosture(500);

  // Play startup success chime
  playSuccessChime();

  Serial.println("[Robot Arm] 6 Servos Initialized to Home Position.");
  Serial.println("==========================================================");
}

void loop() {
  // 1. Process USB Serial Commands
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    parseArmCommand(cmd);
  }

  // 2. Process 7-Inch Capacitive Touchscreen Events
  processTouchInput();

  // 3. Update 50Hz Cosine S-Curve Trajectory Interpolation Engine
  updateArmTrajectoryEngine();

  // 4. Update Gesture Routine Engine
  updateArmRoutineEngine();

  // 5. Update Speech Conversational Gesturing Engine
  updateArmSpeechGestureEngine();

  // 6. Output Real-Time Telemetry Stream
  printLiveTelemetry();

  delay(2); // Short pause for CPU efficiency
}
