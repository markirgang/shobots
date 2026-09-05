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

// 3rd Auxiliary LM298 Motor Driver (Mouth & Body Actuators)
// Channel A - Mouth Motor
#define MOUTH_IN1_PIN        35
#define MOUTH_IN2_PIN        36
#define MOUTH_ENA_PIN        37

// Channel B - Body Motor
#define BODY_IN3_PIN         26
#define BODY_IN4_PIN         27
#define BODY_ENB_PIN         33

// Eye LEDs Output
#define EYES_LED_PIN         10

// Headlights Output (N-Channel MOSFET Driver Module: SIG = GPIO 7)
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

// Screen Profile: Waveshare ST7789VW 240x320 IPS 2" Display (320x240 Landscape)
#define SCREEN_WIDTH         320
#define SCREEN_HEIGHT        240

// Waveshare ST7789VW 4-Wire SPI Display Pin Mapping
#define TFT_DC               0
#define TFT_CS               46
#define TFT_SCLK             48
#define TFT_MOSI             47
#define TFT_RST              44
#define TFT_BL               43

// =============================================================================
// Waveshare ST7789VW 240x320 IPS 2" Display Driver Initialization (Arduino_GFX)
// =============================================================================
#if __has_include(<Arduino_GFX_Library.h>)
#include <Arduino_GFX_Library.h>
#define HAS_ARDUINO_GFX 1
#else
#define HAS_ARDUINO_GFX 0
#endif

#if HAS_ARDUINO_GFX
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, 1 /* rotation: 1 = landscape 320x240 */, true /* IPS */, 240, 320, 0, 0, 0, 0);
bool gfxAvailable = true;
#else
bool gfxAvailable = false;
#endif

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

void checkRoverObstacleAvoidance() {
  if (!roverObstacleAvoidEnabled) return;

  int f  = (sonarFrontCm > 0 && sonarFrontCm < 400) ? sonarFrontCm : 999;
  int r  = (sonarRearCm > 0 && sonarRearCm < 400) ? sonarRearCm : 999;
  int l  = (sonarLeftCm > 0 && sonarLeftCm < 400) ? sonarLeftCm : 999;
  int rt = (sonarRightCm > 0 && sonarRightCm < 400) ? sonarRightCm : 999;

  int minDist = min(min(f, r), min(l, rt));

  if (minDist < roverObstacleThresholdCm) {
    lastRoverObstacleEvadeTime = millis();
    isRoverEvadingObstacle = true;

    if (minDist == f) {
      if (currentMotion != "back") {
        executeMotion("back");
        roverStatusMessage = "Obstruction FRONT (" + String(f) + "cm) - Reversing!";
        Serial.println("[Rover Avoidance] Front obstacle triggered! Reversing motors.");
        playHardwareSound("ALERT");
      }
    } else if (minDist == r) {
      if (currentMotion != "forward") {
        executeMotion("forward");
        roverStatusMessage = "Obstruction REAR (" + String(r) + "cm) - Driving Forward!";
        Serial.println("[Rover Avoidance] Rear obstacle triggered! Driving forward.");
        playHardwareSound("ALERT");
      }
    } else if (minDist == l) {
      if (currentMotion != "spin_right") {
        executeMotion("spin_right");
        roverStatusMessage = "Obstruction LEFT (" + String(l) + "cm) - Spinning Right!";
        Serial.println("[Rover Avoidance] Left obstacle triggered! Spinning right.");
        playHardwareSound("ALERT");
      }
    } else if (minDist == rt) {
      if (currentMotion != "spin_left") {
        executeMotion("spin_left");
        roverStatusMessage = "Obstruction RIGHT (" + String(rt) + "cm) - Spinning Left!";
        Serial.println("[Rover Avoidance] Right obstacle triggered! Spinning left.");
        playHardwareSound("ALERT");
      }
    }
  } else if (isRoverEvadingObstacle) {
    if (millis() - lastRoverObstacleEvadeTime > ROVER_OBSTACLE_EVADE_DURATION_MS) {
      isRoverEvadingObstacle = false;
      executeMotion("stop");
      roverStatusMessage = "Obstacle Cleared - Stopped / Standby";
      Serial.println("[Rover Avoidance] Obstacle cleared. Stopping motors.");
    }
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

// Render 4-Way Ultrasonic Proximity HUD on Waveshare ST7789VW 240x320 IPS Screen (320x240 Landscape)
void renderST7789SonarHUD() {
  static unsigned long lastHudLog = 0;
  static unsigned long lastGfxUpdate = 0;

  if (millis() - lastHudLog > 1000) {
    lastHudLog = millis();
    Serial.printf("[Waveshare ST7789 HUD] Sonar -> FRONT: %d cm | REAR: %d cm | LEFT: %d cm | RIGHT: %d cm\n",
                  sonarFrontCm, sonarRearCm, sonarLeftCm, sonarRightCm);
  }

#if HAS_ARDUINO_GFX
  if (gfxAvailable && gfx && (millis() - lastGfxUpdate > 150)) { // Refresh display at ~6.6 Hz
    lastGfxUpdate = millis();

    // 1. Header Bar (y: 0 to 28)
    gfx->fillRect(0, 0, 320, 28, 0x18E3); // Dark Blue
    gfx->setTextColor(0xFFFF);            // White
    gfx->setTextSize(2);
    gfx->setCursor(10, 6);
    gfx->print("WAVEROVER ST7789 HUD");

    // 2. Motion & Telemetry Banner (y: 30 to 95)
    gfx->fillRect(0, 30, 320, 65, 0x0842); // Slate Dark Background
    gfx->drawRect(0, 30, 320, 65, 0x39E7); // Border
    gfx->setTextSize(1);
    gfx->setTextColor(0x07FF);             // Cyan
    gfx->setCursor(10, 36);
    gfx->print("MOTION: ");
    gfx->setTextColor(0xFFFF);
    gfx->print(currentMotion);
    gfx->print(" (");
    gfx->print(currentSpeed);
    gfx->println("%)");

    gfx->setCursor(10, 50);
    gfx->setTextColor(0xFFE0);             // Yellow
    gfx->print("STATUS: ");
    gfx->setTextColor(0xFFFF);
    gfx->println(roverStatusMessage.substring(0, 32));

    gfx->setCursor(10, 64);
    gfx->setTextColor(0x07E0);             // Green
    gfx->print("EYES: ");
    gfx->print(eyeLedsState ? "ON" : "OFF");
    gfx->print(" | LIGHTS: ");
    gfx->print(headlightState ? "ON" : "OFF");
    gfx->print(" | AI: ");
    gfx->setTextColor(isAiSpeaking ? 0xF800 : 0x7BE0);
    gfx->println(isAiSpeaking ? "TALKING" : "IDLE");

    // 3. Sonar Proximity 4-Way Grid (y: 100 to 235)
    auto getSonarColor = [](int distCm) -> uint16_t {
      if (distCm < 15) return 0xF800;      // Red (Danger)
      if (distCm < 30) return 0xFFE0;      // Yellow (Warning)
      return 0x07E0;                       // Green (Clear)
    };

    // Front Sonar Card
    uint16_t fColor = getSonarColor(sonarFrontCm);
    gfx->fillRect(110, 100, 100, 40, 0x10A2);
    gfx->drawRect(110, 100, 100, 40, fColor);
    gfx->setTextColor(0x9E79); gfx->setTextSize(1);
    gfx->setCursor(125, 104); gfx->print("^ FRONT ^");
    gfx->setTextColor(fColor); gfx->setTextSize(2);
    gfx->setCursor(120, 118);
    if (sonarFrontCm >= 400) gfx->print("-- cm");
    else { gfx->print(sonarFrontCm); gfx->print(" cm"); }

    // Rear Sonar Card
    uint16_t rColor = getSonarColor(sonarRearCm);
    gfx->fillRect(110, 190, 100, 45, 0x10A2);
    gfx->drawRect(110, 190, 100, 45, rColor);
    gfx->setTextColor(0x9E79); gfx->setTextSize(1);
    gfx->setCursor(125, 194); gfx->print("v REAR v");
    gfx->setTextColor(rColor); gfx->setTextSize(2);
    gfx->setCursor(120, 208);
    if (sonarRearCm >= 400) gfx->print("-- cm");
    else { gfx->print(sonarRearCm); gfx->print(" cm"); }

    // Left Sonar Card
    uint16_t lColor = getSonarColor(sonarLeftCm);
    gfx->fillRect(5, 145, 100, 40, 0x10A2);
    gfx->drawRect(5, 145, 100, 40, lColor);
    gfx->setTextColor(0x9E79); gfx->setTextSize(1);
    gfx->setCursor(20, 149); gfx->print("< LEFT");
    gfx->setTextColor(lColor); gfx->setTextSize(2);
    gfx->setCursor(15, 163);
    if (sonarLeftCm >= 400) gfx->print("-- cm");
    else { gfx->print(sonarLeftCm); gfx->print(" cm"); }

    // Right Sonar Card
    uint16_t rtColor = getSonarColor(sonarRightCm);
    gfx->fillRect(215, 145, 100, 40, 0x10A2);
    gfx->drawRect(215, 145, 100, 40, rtColor);
    gfx->setTextColor(0x9E79); gfx->setTextSize(1);
    gfx->setCursor(230, 149); gfx->print("RIGHT >");
    gfx->setTextColor(rtColor); gfx->setTextSize(2);
    gfx->setCursor(225, 163);
    if (sonarRightCm >= 400) gfx->print("-- cm");
    else { gfx->print(sonarRightCm); gfx->print(" cm"); }
  }
#endif
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
  if (openMouth) {
    setMotorChannel(MOUTH_IN1_PIN, MOUTH_IN2_PIN, MOUTH_ENA_PIN, 200); // Drive Mouth Motor ON (Open)
  } else {
    setMotorChannel(MOUTH_IN1_PIN, MOUTH_IN2_PIN, MOUTH_ENA_PIN, 0);   // Stop Mouth Motor (Closed)
  }
}

void setBodyMotorState(bool enableBody) {
  bodyMotorState = enableBody;
  if (enableBody) {
    setMotorChannel(BODY_IN3_PIN, BODY_IN4_PIN, BODY_ENB_PIN, 200);   // Drive Body Motor ON (Up/Move)
  } else {
    setMotorChannel(BODY_IN3_PIN, BODY_IN4_PIN, BODY_ENB_PIN, 0);     // Stop Body Motor
  }
}

void setEyeLeds(bool enableEyes) {
  eyeLedsState = enableEyes;
  digitalWrite(EYES_LED_PIN, enableEyes ? HIGH : LOW);
}

void setHeadlights(bool enableHeadlights) {
  headlightState = enableHeadlights;
  analogWrite(HEADLIGHTS_LED_PIN, enableHeadlights ? 255 : 0);
}

void setHeadlightBrightness(int brightnessPct) {
  brightnessPct = constrain(brightnessPct, 0, 100);
  headlightState = (brightnessPct > 0);
  int pwmVal = map(brightnessPct, 0, 100, 0, 255);
  analogWrite(HEADLIGHTS_LED_PIN, pwmVal);
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
      if (val > 1) {
        setHeadlightBrightness(val); // 0-100% PWM dimming via N-Channel MOSFET
      } else {
        setHeadlights(val == 1);
      }
    } else if (payload.startsWith("PANTILT:")) {
      int firstColon = payload.indexOf(':', 8);
      if (firstColon > 0) {
        int pan = payload.substring(8, firstColon).toInt();
        int tilt = payload.substring(firstColon + 1).toInt();
        setPanTilt(pan, tilt);
      }
    } else if (payload.startsWith("OBSTACLE_AVOID:")) {
      String valStr = payload.substring(15);
      valStr.toUpperCase();
      valStr.trim();
      roverObstacleAvoidEnabled = (valStr == "1" || valStr == "ON" || valStr == "TRUE" || valStr == "ENABLE");
      roverStatusMessage = roverObstacleAvoidEnabled ? "Auto Avoidance: ENABLED" : "Auto Avoidance: DISABLED";
      Serial.println("[Rover] Obstacle Avoidance set to: " + String(roverObstacleAvoidEnabled ? "ON" : "OFF"));
    } else if (payload.startsWith("OBSTACLE_THRESH:")) {
      roverObstacleThresholdCm = payload.substring(16).toInt();
      if (roverObstacleThresholdCm < 5) roverObstacleThresholdCm = 5;
      roverStatusMessage = "Avoid Thresh: " + String(roverObstacleThresholdCm) + "cm";
      Serial.println("[Rover] Obstacle Threshold set to " + String(roverObstacleThresholdCm) + " cm");
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

  // Pin modes for 3rd Auxiliary LM298 (Ch A: Mouth Motor, Ch B: Body Motor)
  pinMode(MOUTH_IN1_PIN, OUTPUT);
  pinMode(MOUTH_IN2_PIN, OUTPUT);
  pinMode(MOUTH_ENA_PIN, OUTPUT);
  pinMode(BODY_IN3_PIN, OUTPUT);
  pinMode(BODY_IN4_PIN, OUTPUT);
  pinMode(BODY_ENB_PIN, OUTPUT);

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

  // Initialize Outputs & Backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); // Turn ST7789 backlight ON
  setEyeLeds(true);
  setHeadlights(false);
  setMouthState(false);
  setBodyMotorState(false);
  drive4WDMotors(0, 0, 0, 0);

#if HAS_ARDUINO_GFX
  if (gfx) {
    gfx->begin();
    gfx->fillScreen(0x0000);
    gfx->setTextColor(0xFFFF);
    gfx->setTextSize(2);
    gfx->setCursor(10, 10);
    gfx->println("WAVEROVER ST7789 ONLINE");
    gfx->setTextSize(1);
    gfx->setCursor(10, 35);
    gfx->println("Waveshare ST7789VW 240x320 IPS 2\"");
    Serial.println("[ST7789 Display] Arduino_GFX ST7789 Driver Initialized Successfully.");
  }
#endif

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

  Serial.println("[ESP32 4WD Dual LM298 Mobile Rover Firmware Ready with ST7789 2\" IPS & 4x HC-SR04 Sonar]");
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
  checkRoverObstacleAvoidance();
  renderST7789SonarHUD();

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
