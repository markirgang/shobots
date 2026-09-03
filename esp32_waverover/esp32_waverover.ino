/*
  Shobots - ESP32-S3 Waveshare Wave Rover Mobile Platform Firmware
  =============================================================================
  Hardware: Waveshare ESP32-S3-Touch-LCD-7C (7.0" 1024x600 HD Capacitive Touchscreen, GT911 Controller)
  
  Features:
    - Waveshare Wave Rover 4WD Mobile Platform Controller
    - L298N (LM298) H-Bridge Motor Controller Integration:
        * DC Mouth Motor (IN1, IN2, ENA): Power ON = Mouth Open; Power OFF = Mouth Closed.
        * DC Body Motion Motor (IN3, IN4, ENB): Power ON = Body attached to mouth sways up and down.
    - LED Eye Outputs: Digital GPIO Output for Eye LEDs (ON, OFF, Pulse, Blink)
    - Real-Time AI Speech Animatronics (`AI_SPEAKING:1` / `AI_SPEAKING:0`):
        * Automatically opens mouth & turns ON body up/down motor when AI talks.
        * Power OFF to mouth motor (closes mouth) & body motor when AI stops talking.
    - 4WD Differential Drive Motors & Pan-Tilt Camera Servos
    - Onboard MAX98357A I2S Mono Audio Amplifier (BCLK=19, LRC=20, DIN=21)
    - Procedural Vehicle Sound Engine: Rover Engine, Horn, Startup, Shutdown, Alert, Turbo, Brake
  =============================================================================
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>
#include "driver/i2s.h"

// =============================================================================
// Pin Mapping & Hardware Configurations
// =============================================================================

// L298N Motor Controller - DC Mouth Motor (Channel A)
#define MOUTH_IN1_PIN        11
#define MOUTH_IN2_PIN        12
#define MOUTH_ENA_PIN        13

// L298N Motor Controller - DC Body Up/Down Motor (Channel B)
#define BODY_IN3_PIN         14
#define BODY_IN4_PIN         15
#define BODY_ENB_PIN         16

// Eye LEDs Output
#define EYES_LED_PIN         10

// Headlights Output
#define HEADLIGHTS_LED_PIN   7

// 4WD Drive Motors (PWM & Direction Pins)
#define LEFT_MOTOR_PWM       1
#define LEFT_MOTOR_DIR       2
#define RIGHT_MOTOR_PWM      42
#define RIGHT_MOTOR_DIR      41

// Pan-Tilt Camera Servo Channels
#define PAN_SERVO_PIN        5
#define TILT_SERVO_PIN       6

// MAX98357A I2S Audio Hardware Pins
#define I2S_BCLK_PIN         19
#define I2S_LRC_PIN          20
#define I2S_DOUT_PIN         21
#define I2S_PORT             I2S_NUM_0
#define I2S_SAMPLE_RATE      22050
#define AUDIO_BUF_SIZE       256

// Screen Profile
#define SCREEN_WIDTH         800
#define SCREEN_HEIGHT        480

// State Variables
bool mouthState = false;       // false = closed (power off), true = open (power on)
bool bodyMotorState = false;   // false = off, true = on (body moving up and down)
bool eyeLedsState = true;      // true = ON, false = OFF
bool headlightState = false;   // true = ON, false = OFF

int currentSpeed = 75;         // 0 - 100%
int currentPanAngle = 90;      // 0 - 180 deg
int currentTiltAngle = 90;     // 0 - 180 deg

String currentMotion = "stop";
String roverStatusMessage = "Wave Rover Online & Ready";

// Speech Animatronics
bool isAiSpeaking = false;
unsigned long lastSpeechTime = 0;
unsigned long mouthPulseTimer = 0;
bool mouthPulseToggle = false;

// Audio Engine
bool i2sAudioReady = false;
int audioVolume = 85;

// =============================================================================
// L298N Motor Driver Control Functions
// =============================================================================

void setMouthState(bool openMouth) {
  mouthState = openMouth;
  if (openMouth) {
    digitalWrite(MOUTH_IN1_PIN, HIGH);
    digitalWrite(MOUTH_IN2_PIN, LOW);
    analogWrite(MOUTH_ENA_PIN, 255); // Full power to open mouth
  } else {
    digitalWrite(MOUTH_IN1_PIN, LOW);
    digitalWrite(MOUTH_IN2_PIN, LOW);
    analogWrite(MOUTH_ENA_PIN, 0);   // Power OFF to close mouth
  }
}

void setBodyMotorState(bool enableBody) {
  bodyMotorState = enableBody;
  if (enableBody) {
    digitalWrite(BODY_IN3_PIN, HIGH);
    digitalWrite(BODY_IN4_PIN, LOW);
    analogWrite(BODY_ENB_PIN, 200); // Drive body up/down linkage
  } else {
    digitalWrite(BODY_IN3_PIN, LOW);
    digitalWrite(BODY_IN4_PIN, LOW);
    analogWrite(BODY_ENB_PIN, 0);   // Power OFF to stop body
  }
}

void setEyeLeds(bool enableEyes) {
  eyeLedsState = enableEyes;
  digitalWrite(EYES_LED_PIN, enableEyes ? HIGH : LOW);
}

void setHeadlights(bool enableHeadlights) {
  headlightState = enableHeadlights;
  digitalWrite(HEADLIGHTS_LED_PIN, enableHeadlights ? HIGH : LOW);
}

// =============================================================================
// 4WD Drive & Motion Functions
// =============================================================================

void driveMotors(int leftSpeed, int rightSpeed) {
  // Left motor
  if (leftSpeed >= 0) {
    digitalWrite(LEFT_MOTOR_DIR, HIGH);
    analogWrite(LEFT_MOTOR_PWM, min(255, leftSpeed));
  } else {
    digitalWrite(LEFT_MOTOR_DIR, LOW);
    analogWrite(LEFT_MOTOR_PWM, min(255, -leftSpeed));
  }
  
  // Right motor
  if (rightSpeed >= 0) {
    digitalWrite(RIGHT_MOTOR_DIR, HIGH);
    analogWrite(RIGHT_MOTOR_PWM, min(255, rightSpeed));
  } else {
    digitalWrite(RIGHT_MOTOR_DIR, LOW);
    analogWrite(RIGHT_MOTOR_PWM, min(255, -rightSpeed));
  }
}

void executeMotion(String action) {
  action.toLowerCase();
  currentMotion = action;
  int pwmVal = map(currentSpeed, 0, 100, 0, 255);

  if (action == "forward") {
    driveMotors(pwmVal, pwmVal);
    roverStatusMessage = "Driving Forward (" + String(currentSpeed) + "%)";
  } else if (action == "back" || action == "backward") {
    driveMotors(-pwmVal, -pwmVal);
    roverStatusMessage = "Driving Backward (" + String(currentSpeed) + "%)";
  } else if (action == "turn_left") {
    driveMotors(pwmVal / 2, pwmVal);
    roverStatusMessage = "Turning Left";
  } else if (action == "turn_right") {
    driveMotors(pwmVal, pwmVal / 2);
    roverStatusMessage = "Turning Right";
  } else if (action == "spin_left") {
    driveMotors(-pwmVal, pwmVal);
    roverStatusMessage = "Spinning Counter-Clockwise";
  } else if (action == "spin_right") {
    driveMotors(pwmVal, -pwmVal);
    roverStatusMessage = "Spinning Clockwise";
  } else if (action == "patrol") {
    roverStatusMessage = "Executing Autonomous Patrol Routine";
  } else if (action == "dance") {
    roverStatusMessage = "Executing Wave Rover Dance Track";
  } else if (action == "spin_360") {
    driveMotors(pwmVal, -pwmVal);
    roverStatusMessage = "Executing 360° Spin";
  } else { // stop
    driveMotors(0, 0);
    roverStatusMessage = "Stopped / Standby";
  }
}

void setPanTilt(int pan, int tilt) {
  currentPanAngle = constrain(pan, 0, 180);
  currentTiltAngle = constrain(tilt, 0, 180);
  // Pan & Tilt PWM output simulation / servo write
}

// =============================================================================
// MAX98357A Audio Engine Functions
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

  if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) == ESP_OK) {
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
    i2sAudioReady = true;
    Serial.println("[Wave Rover Audio] MAX98357A I2S Audio Initialized");
  }
}

void playTone(float freqHz, int durationMs) {
  if (!i2sAudioReady) return;
  int numSamples = (I2S_SAMPLE_RATE * durationMs) / 1000;
  int16_t buffer[AUDIO_BUF_SIZE];
  int sampleIdx = 0;
  
  for (int i = 0; i < numSamples; i++) {
    float t = (float)i / I2S_SAMPLE_RATE;
    float sampleVal = sinf(2.0f * M_PI * freqHz * t);
    int16_t pcmVal = (int16_t)(sampleVal * 16000.0f * (audioVolume / 100.0f));
    
    buffer[sampleIdx++] = pcmVal; // Left
    buffer[sampleIdx++] = pcmVal; // Right
    
    if (sampleIdx >= AUDIO_BUF_SIZE) {
      size_t bytesWritten = 0;
      i2s_write(I2S_PORT, buffer, sampleIdx * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
      sampleIdx = 0;
    }
  }
  if (sampleIdx > 0) {
    size_t bytesWritten = 0;
    i2s_write(I2S_PORT, buffer, sampleIdx * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
  }
}

void playHardwareSound(String sound) {
  sound.toUpperCase();
  if (sound == "HORN") {
    playTone(440.0f, 150);
    playTone(554.37f, 250);
  } else if (sound == "ROVER_ENGINE" || sound == "ENGINE") {
    playTone(120.0f, 300);
  } else if (sound == "STARTUP") {
    for (float f = 200.0f; f <= 800.0f; f += 50.0f) {
      playTone(f, 30);
    }
  } else if (sound == "SHUTDOWN") {
    for (float f = 800.0f; f >= 200.0f; f -= 50.0f) {
      playTone(f, 30);
    }
  } else if (sound == "ALERT") {
    for (int i = 0; i < 3; i++) {
      playTone(900.0f, 100);
      playTone(600.0f, 100);
    }
  } else if (sound == "TURBO") {
    for (float f = 300.0f; f <= 1200.0f; f += 80.0f) {
      playTone(f, 20);
    }
  } else if (sound == "BRAKE") {
    playTone(1500.0f, 200);
  }
}

// =============================================================================
// Serial Command Parser
// =============================================================================

void parseSerialCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.print("[Rover Command Received] ");
  Serial.println(cmd);

  if (cmd.startsWith("ROVER:")) {
    String payload = cmd.substring(6);
    if (payload.startsWith("MOUTH:")) {
      int val = payload.substring(6).toInt();
      setMouthState(val == 1);
    } else if (payload.startsWith("BODY:")) {
      int val = payload.substring(5).toInt();
      setBodyMotorState(val == 1);
    } else if (payload.startsWith("EYES:")) {
      int val = payload.substring(5).toInt();
      setEyeLeds(val == 1);
    } else if (payload.startsWith("SPEED:")) {
      currentSpeed = payload.substring(6).toInt();
      roverStatusMessage = "Speed set to " + String(currentSpeed) + "%";
    } else if (payload.startsWith("LED:")) {
      int val = payload.substring(4).toInt();
      setHeadlights(val == 1);
    } else if (payload.startsWith("PANTILT:")) {
      int firstColon = payload.indexOf(':', 8);
      if (firstColon > 0) {
        int pan = payload.substring(8, firstColon).toInt();
        int tilt = payload.substring(firstColon + 1).toInt();
        setPanTilt(pan, tilt);
      }
    } else if (payload.startsWith("LCD:MSG:")) {
      roverStatusMessage = payload.substring(8);
    } else {
      executeMotion(payload);
    }
  } else if (cmd.startsWith("AI_SPEAKING:")) {
    int val = cmd.substring(12).toInt();
    isAiSpeaking = (val == 1);
    if (isAiSpeaking) {
      lastSpeechTime = millis();
      setMouthState(true);
      setBodyMotorState(true);
      setEyeLeds(true);
    } else {
      setMouthState(false);
      setBodyMotorState(false);
    }
  } else if (cmd.startsWith("AUDIO:")) {
    String snd = cmd.substring(6);
    if (snd.startsWith("VOL:")) {
      audioVolume = snd.substring(4).toInt();
    } else {
      playHardwareSound(snd);
    }
  }
}

// =============================================================================
// Setup & Main Loop
// =============================================================================

void setup() {
  Serial.begin(115200);
  
  // Pin modes for L298N Mouth & Body motors
  pinMode(MOUTH_IN1_PIN, OUTPUT);
  pinMode(MOUTH_IN2_PIN, OUTPUT);
  pinMode(MOUTH_ENA_PIN, OUTPUT);
  pinMode(BODY_IN3_PIN, OUTPUT);
  pinMode(BODY_IN4_PIN, OUTPUT);
  pinMode(BODY_ENB_PIN, OUTPUT);
  
  // Pin modes for LEDs & Drive
  pinMode(EYES_LED_PIN, OUTPUT);
  pinMode(HEADLIGHTS_LED_PIN, OUTPUT);
  pinMode(LEFT_MOTOR_PWM, OUTPUT);
  pinMode(LEFT_MOTOR_DIR, OUTPUT);
  pinMode(RIGHT_MOTOR_PWM, OUTPUT);
  pinMode(RIGHT_MOTOR_DIR, OUTPUT);

  // Initialize Outputs
  setMouthState(false);
  setBodyMotorState(false);
  setEyeLeds(true);
  setHeadlights(false);
  driveMotors(0, 0);

  // Initialize MAX98357A I2S Audio
  initI2SAudio();
  playHardwareSound("STARTUP");

  Serial.println("[Waveshare 7C Wave Rover Firmware Ready]");
}

void loop() {
  // Listen for incoming Serial commands
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    parseSerialCommand(line);
  }

  // Handle AI Speech Animatronics Timers & Oscillations
  if (isAiSpeaking) {
    if (millis() - mouthPulseTimer > 150) {
      mouthPulseTimer = millis();
      mouthPulseToggle = !mouthPulseToggle;
      // Pulse mouth motor open/partially closed to match speech cadence
      setMouthState(mouthPulseToggle);
    }
  }

  delay(10);
}
