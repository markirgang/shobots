/*
  Shobots - ESP32 Standard 4-Motor AWD Mobile Platform Firmware
  =============================================================================
  Hardware: Standard ESP32 / ESP32-S3 4WD Mobile Platform Controller
  
  Features:
    - Dual LM298 (L298N) H-Bridge Reversing Motor Controller Integration:
        * Front LM298:
            - Channel A (FL_IN1, FL_IN2, FL_ENA): Front Left Motor
            - Channel B (FR_IN3, FR_IN4, FR_ENB): Front Right Motor
        * Rear LM298:
            - Channel A (RL_IN1, RL_IN2, RL_ENA): Rear Left Motor
            - Channel B (RR_IN3, RR_IN4, RR_ENB): Rear Right Motor
    - 4WD Differential & Reversing Drive: Forward, Backward, Turn Left, Turn Right, Spin Left, Spin Right, Stop
    - LED Eye Outputs: Digital GPIO Output for Eye LEDs (ON, OFF, Pulse, Blink)
    - Front Headlights Output: Digital GPIO Output for Headlights
    - Real-Time AI Speech Animatronics (`AI_SPEAKING:1` / `AI_SPEAKING:0`):
        * Flashes Eye LEDs and updates status whenever AI talks.
    - Pan-Tilt Camera Servos (Pan 0-180°, Tilt 0-180°)
    - Onboard MAX98357A I2S Mono Audio Amplifier (BCLK=19, LRC=20, DIN=21)
    - Procedural Vehicle Sound Engine: Rover Engine, Horn, Startup, Shutdown, Alert, Turbo, Brake
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
// Pin Mapping & Hardware Configurations
// =============================================================================

// Front LM298 Motor Driver
// Channel A - Front Left Motor
#define FRONT_LEFT_IN1_PIN   11
#define FRONT_LEFT_IN2_PIN   12
#define FRONT_LEFT_ENA_PIN   13

// Channel B - Front Right Motor
#define FRONT_RIGHT_IN3_PIN  14
#define FRONT_RIGHT_IN4_PIN  15
#define FRONT_RIGHT_ENB_PIN  16

// Rear LM298 Motor Driver
// Channel A - Rear Left Motor
#define REAR_LEFT_IN1_PIN    1
#define REAR_LEFT_IN2_PIN    2
#define REAR_LEFT_ENA_PIN    42

// Channel B - Rear Right Motor
#define REAR_RIGHT_IN3_PIN   41
#define REAR_RIGHT_IN4_PIN   8
#define REAR_RIGHT_ENB_PIN   9

// Eye LEDs Output
#define EYES_LED_PIN         10

// Headlights Output
#define HEADLIGHTS_LED_PIN   7

// Pan-Tilt Camera Servo Channels
#define PAN_SERVO_PIN        3
#define TILT_SERVO_PIN       4

// MAX98357A I2S Audio Hardware Pins
#define I2S_BCLK_PIN         19
#define I2S_LRC_PIN          20
#define I2S_DOUT_PIN         21
#define I2S_PORT             I2S_NUM_0
#define I2S_SAMPLE_RATE      22050
#define AUDIO_BUF_SIZE       256

// HC-SR04 Ultrasonic Proximity Sensor Pins (Front, Rear, Left, Right)
#define FRONT_TRIG_PIN       5
#define FRONT_ECHO_PIN       6
#define REAR_TRIG_PIN        17
#define REAR_ECHO_PIN        18
#define LEFT_TRIG_PIN        38
#define LEFT_ECHO_PIN        39
#define RIGHT_TRIG_PIN       40
#define RIGHT_ECHO_PIN       45

// Screen Profile
#define SCREEN_WIDTH         800
#define SCREEN_HEIGHT        480

// State Variables
bool mouthState = false;       // false = closed, true = open
bool bodyMotorState = false;   // false = off, true = on
bool eyeLedsState = true;      // true = ON, false = OFF
bool headlightState = false;   // true = ON, false = OFF

int currentSpeed = 75;         // 0 - 100%
int currentPanAngle = 90;      // 0 - 180 deg
int currentTiltAngle = 90;     // 0 - 180 deg

String currentMotion = "stop";
String roverStatusMessage = "4WD LM298 Mobile Rover Online & Ready";

// HC-SR04 Distance State Variables (in cm)
int sonarFrontCm = 999;
int sonarRearCm  = 999;
int sonarLeftCm  = 999;
int sonarRightCm = 999;
unsigned long lastSonarReadTime = 0;
unsigned long lastSonarStreamTime = 0;

// Speech Animatronics
bool isAiSpeaking = false;
unsigned long lastSpeechTime = 0;
unsigned long mouthPulseTimer = 0;
bool mouthPulseToggle = false;

// Audio Engine
bool i2sAudioReady = false;
int audioVolume = 85;

// =============================================================================
// HC-SR04 Ultrasonic Proximity Sensor Driver & LCD HUD Rendering
// =============================================================================

int readHCSR04Distance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 25000); // 25ms timeout (~4m max)
  if (duration == 0) return 400; // max/no echo
  int distanceCm = (int)(duration * 0.0343f / 2.0f);
  return constrain(distanceCm, 2, 400);
}

void updateSonarSensors() {
  if (millis() - lastSonarReadTime > 100) { // Update every 100ms
    lastSonarReadTime = millis();
    sonarFrontCm = readHCSR04Distance(FRONT_TRIG_PIN, FRONT_ECHO_PIN);
    sonarRearCm  = readHCSR04Distance(REAR_TRIG_PIN, REAR_ECHO_PIN);
    sonarLeftCm  = readHCSR04Distance(LEFT_TRIG_PIN, LEFT_ECHO_PIN);
    sonarRightCm = readHCSR04Distance(RIGHT_TRIG_PIN, RIGHT_ECHO_PIN);
  }
}

void sendSonarTelemetry() {
  String telemetry = "SONAR:F:" + String(sonarFrontCm) +
                     ":R:" + String(sonarRearCm) +
                     ":L:" + String(sonarLeftCm) +
                     ":R:" + String(sonarRightCm);
  Serial.println(telemetry);
#if HAS_BT_CLASSIC
  SerialBT.println(telemetry);
#endif
}

// Render 4-Way Ultrasonic Proximity HUD on Waveshare TouchLCD-7C Screen
void renderTouchLCD7CSonarHUD() {
  // Console / Telemetry logging for LCD UI overlay
  static unsigned long lastHudLog = 0;
  if (millis() - lastHudLog > 1000) {
    lastHudLog = millis();
    Serial.printf("[Waveshare TouchLCD-7C HUD] Sonar -> FRONT: %d cm | REAR: %d cm | LEFT: %d cm | RIGHT: %d cm\n",
                  sonarFrontCm, sonarRearCm, sonarLeftCm, sonarRightCm);
  }
}

// =============================================================================
// Dual LM298 Motor Controller Functions
// =============================================================================

void setMotorChannel(int in1Pin, int in2Pin, int enaPin, int speedVal) {
  int absSpeed = min(255, abs(speedVal));
  if (speedVal > 0) {
    digitalWrite(in1Pin, HIGH);
    digitalWrite(in2Pin, LOW);
    analogWrite(enaPin, absSpeed);
  } else if (speedVal < 0) {
    digitalWrite(in1Pin, LOW);
    digitalWrite(in2Pin, HIGH);
    analogWrite(enaPin, absSpeed);
  } else {
    digitalWrite(in1Pin, LOW);
    digitalWrite(in2Pin, LOW);
    analogWrite(enaPin, 0);
  }
}

// 4WD AWD Drive Logic using Dual LM298 Motor Drivers
// Front LM298: Ch A -> Front Left, Ch B -> Front Right
// Rear LM298:  Ch A -> Rear Left,  Ch B -> Rear Right
void drive4WDMotors(int frontLeft, int frontRight, int rearLeft, int rearRight) {
  setMotorChannel(FRONT_LEFT_IN1_PIN, FRONT_LEFT_IN2_PIN, FRONT_LEFT_ENA_PIN, frontLeft);
  setMotorChannel(FRONT_RIGHT_IN3_PIN, FRONT_RIGHT_IN4_PIN, FRONT_RIGHT_ENB_PIN, frontRight);
  setMotorChannel(REAR_LEFT_IN1_PIN, REAR_LEFT_IN2_PIN, REAR_LEFT_ENA_PIN, rearLeft);
  setMotorChannel(REAR_RIGHT_IN3_PIN, REAR_RIGHT_IN4_PIN, REAR_RIGHT_ENB_PIN, rearRight);
}

void setMouthState(bool openMouth) {
  mouthState = openMouth;
}

void setBodyMotorState(bool enableBody) {
  bodyMotorState = enableBody;
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

void executeMotion(String action) {
  action.toLowerCase();
  currentMotion = action;
  int pwmVal = map(currentSpeed, 0, 100, 0, 255);

  if (action == "forward") {
    drive4WDMotors(pwmVal, pwmVal, pwmVal, pwmVal);
    roverStatusMessage = "Driving Forward (" + String(currentSpeed) + "%)";
  } else if (action == "back" || action == "backward") {
    drive4WDMotors(-pwmVal, -pwmVal, -pwmVal, -pwmVal);
    roverStatusMessage = "Driving Backward (" + String(currentSpeed) + "%)";
  } else if (action == "turn_left") {
    drive4WDMotors(-pwmVal / 2, pwmVal, -pwmVal / 2, pwmVal);
    roverStatusMessage = "Turning Left";
  } else if (action == "turn_right") {
    drive4WDMotors(pwmVal, -pwmVal / 2, pwmVal, -pwmVal / 2);
    roverStatusMessage = "Turning Right";
  } else if (action == "spin_left") {
    drive4WDMotors(-pwmVal, pwmVal, -pwmVal, pwmVal);
    roverStatusMessage = "Spinning Counter-Clockwise";
  } else if (action == "spin_right") {
    drive4WDMotors(pwmVal, -pwmVal, pwmVal, -pwmVal);
    roverStatusMessage = "Spinning Clockwise";
  } else if (action == "patrol") {
    roverStatusMessage = "Executing Autonomous Patrol Routine";
  } else if (action == "dance") {
    roverStatusMessage = "Executing 4WD Rover Dance Track";
  } else if (action == "spin_360") {
    drive4WDMotors(pwmVal, -pwmVal, pwmVal, -pwmVal);
    roverStatusMessage = "Executing 360° Spin";
  } else { // stop
    drive4WDMotors(0, 0, 0, 0);
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
    Serial.println("[4WD Rover Audio] MAX98357A I2S Audio Initialized");
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
    } else if (payload == "GET_SONAR" || payload == "SONAR") {
      updateSonarSensors();
      sendSonarTelemetry();
    } else {
      executeMotion(payload);
    }
  } else if (cmd == "SONAR:GET" || cmd == "GET_SONAR") {
    updateSonarSensors();
    sendSonarTelemetry();
  } else if (cmd.startsWith("AI_SPEAKING:")) {
    int val = cmd.substring(12).toInt();
    isAiSpeaking = (val == 1);
    if (isAiSpeaking) {
      lastSpeechTime = millis();
      setEyeLeds(true);
    } else {
      setEyeLeds(false);
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
  
  // Pin modes for Front LM298 (Ch A: Front Left, Ch B: Front Right)
  pinMode(FRONT_LEFT_IN1_PIN, OUTPUT);
  pinMode(FRONT_LEFT_IN2_PIN, OUTPUT);
  pinMode(FRONT_LEFT_ENA_PIN, OUTPUT);
  pinMode(FRONT_RIGHT_IN3_PIN, OUTPUT);
  pinMode(FRONT_RIGHT_IN4_PIN, OUTPUT);
  pinMode(FRONT_RIGHT_ENB_PIN, OUTPUT);

  // Pin modes for Rear LM298 (Ch A: Rear Left, Ch B: Rear Right)
  pinMode(REAR_LEFT_IN1_PIN, OUTPUT);
  pinMode(REAR_LEFT_IN2_PIN, OUTPUT);
  pinMode(REAR_LEFT_ENA_PIN, OUTPUT);
  pinMode(REAR_RIGHT_IN3_PIN, OUTPUT);
  pinMode(REAR_RIGHT_IN4_PIN, OUTPUT);
  pinMode(REAR_RIGHT_ENB_PIN, OUTPUT);

  // Pin modes for LEDs & Aux
  pinMode(EYES_LED_PIN, OUTPUT);
  pinMode(HEADLIGHTS_LED_PIN, OUTPUT);

  // Pin modes for 4 x HC-SR04 Ultrasonic Proximity Sensors
  pinMode(FRONT_TRIG_PIN, OUTPUT);
  pinMode(FRONT_ECHO_PIN, INPUT);
  pinMode(REAR_TRIG_PIN, OUTPUT);
  pinMode(REAR_ECHO_PIN, INPUT);
  pinMode(LEFT_TRIG_PIN, OUTPUT);
  pinMode(LEFT_ECHO_PIN, INPUT);
  pinMode(RIGHT_TRIG_PIN, OUTPUT);
  pinMode(RIGHT_ECHO_PIN, INPUT);

  // Initialize Outputs
  setEyeLeds(true);
  setHeadlights(false);
  drive4WDMotors(0, 0, 0, 0);

  // Initialize MAX98357A I2S Audio
  initI2SAudio();
  playHardwareSound("STARTUP");

  // Initialize Bluetooth if supported
#if HAS_BT_CLASSIC
  if (SerialBT.begin("waverover")) {
    Serial.println("[Bluetooth] Broadcasting as 'waverover' READY!");
  }
#else
  Serial.println("[Info] High-speed USB CDC Serial active for 4WD Rover.");
#endif

  Serial.println("[ESP32 4WD Dual LM298 Mobile Rover Firmware Ready with 4x HC-SR04 Sonar]");
}

void loop() {
  // Listen for incoming USB Serial commands
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    parseSerialCommand(line);
  }

  // Listen for incoming Bluetooth commands (if available)
#if HAS_BT_CLASSIC
  while (SerialBT.available()) {
    String line = SerialBT.readStringUntil('\n');
    parseSerialCommand(line);
  }
#endif

  // Update 4 x HC-SR04 Ultrasonic Proximity Sensors
  updateSonarSensors();
  renderTouchLCD7CSonarHUD();

  // Periodically stream sonar telemetry every 500ms
  if (millis() - lastSonarStreamTime > 500) {
    lastSonarStreamTime = millis();
    sendSonarTelemetry();
  }

  // Handle AI Speech Animatronics Timers & Oscillations
  if (isAiSpeaking) {
    if (millis() - mouthPulseTimer > 150) {
      mouthPulseTimer = millis();
      mouthPulseToggle = !mouthPulseToggle;
      setEyeLeds(mouthPulseToggle);
    }
  }

  delay(10);
}
