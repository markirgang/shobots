/*
  Shobots - esp32_Birds.ino (Waveshare 7-Inch Capacitive Touch LCD ESP32 Firmware)
  =============================================================================
  Folder: esp32_Birds/
  Sketch: esp32_Birds.ino
  Hardware: Waveshare ESP32-S3-Touch-LCD-7C (7.0" 800x600 Capacitive Touchscreen, GT911)
  Substituted for: Dual ESP32 DevKits (Left & Right Boards)

  Features:
    - 7.0-inch 800x600 Widescreen Capacitive Touchscreen Dashboard
    - GT911 High-Precision 5-Point Capacitive Multi-Touch Controller
    - MCP23017 16-Bit I2C I/O Expander Board for On/Off Bird Outputs (Lights, Solenoids, Chirps, Motors)
        * Primary MCP23017 (I2C Address: 0x20): Left & Right Bird Outputs (Pins 0-15)
        * Secondary MCP23017 (I2C Address: 0x21, Optional): Extended 16 Outputs
    - Dual PCA9685 16-Channel I2C Servo Drivers (32 PWM Channels Total):
        * Driver 1 (I2C Address 0x40): Left Side Servos (Parrot Up/Dn, Turn, Rotate, Spotlights, Center Bird)
        * Driver 2 (I2C Address 0x41): Right Side Servos (Parrot Up/Dn, Turn, Rotate, Spotlights, Center Turntable)
    - Dynamic Animations & Mascot Visualizer:
        * Animated Expressive Parrot/Bird Mascot with Blinking Eyes, Winking, and Pupil Tracking
        * Dynamic Beak/Mouth Opening & Closing synchronized with Chirps and Speech Pulses
        * Floating Music Notes (♪ ♫ ♩), Head Bobbing & Wing Flapping during routines
        * Real-Time Sweeping Spotlight Beams matching actual PCA9685 Servo Angles
        * Glowing Neon Auras, Ripple Pulses, and Animated VU Audio Spectrum Waves
    - Built-in Demonstration & Choreography Routines:
        * PARROT SING: Beak animations, chirping pulses, and head movements
        * SPOTLIGHT SWEEP: Synchronized sweeping spotlight beam choreography
        * TURNTABLE DANCE: Center turntable rotation with pulsing party lights
        * BIRD SYMPHONY: Full synchronized multi-bird choreography
        * LIGHT SHOW: Sequential chase and breathing pulse patterns
        * ALL HOME: Returns all 32 servos to default 90° and resets all outputs
    - Multi-Channel Host Control: USB CDC Serial, Bluetooth, and Direct Capacitive Touchscreen

  I2C Bus Wiring (Waveshare ESP32-S3-Touch-LCD-7C):
    - SDA    : GPIO 8  (PH2.0 4-Pin I2C Header)
    - SCL    : GPIO 9  (PH2.0 4-Pin I2C Header)
    - TP_INT : GPIO 4  (GT911 Interrupt)
    - GT911 Address      : 0x5D
    - MCP23017 Address   : 0x20 (A0=0, A1=0, A2=0) [Secondary: 0x21]
    - PCA9685 #1 Address : 0x40 (A0=0, A1=0, A2=0, A3=0, A4=0, A5=0) -> Left Servos
    - PCA9685 #2 Address : 0x41 (A0=1, A1=0, A2=0, A3=0, A4=0, A5=0) -> Right Servos
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
// Touch Dashboard Button Definition
// =============================================================================
struct TouchButton {
  const char* label;
  int x, y, w, h;
  const char* type;   // "output", "routine", "servo"
  const char* board;
  int id;
};

// =============================================================================
// Hardware Configuration & Board Profiles
// =============================================================================
// Select ONE board profile below (Waveshare ESP32-S3-Touch-LCD-7C):
#define BOARD_ESP32_TOUCH_LCD_7C   1  // Waveshare ESP32-S3-Touch-LCD-7C (7.0" 800x600 HD GT911 + CH422G IO + Audio Codec)

#if defined(BOARD_ESP32_TOUCH_LCD_7C)
  #define SCREEN_WIDTH          800
  #define SCREEN_HEIGHT         600
  #define HAS_CH422G_IO           1   // Onboard IO Expander for Backlight & Power Control
#else
  #define SCREEN_WIDTH          800
  #define SCREEN_HEIGHT         600
  #define HAS_CH422G_IO           0
#endif

#define I2C_SDA_PIN            8
#define I2C_SCL_PIN            9
#define TP_INT_PIN             4
#define MIC_SENSOR_GPIO_PIN    7   // Direct ESP32 GPIO Input for Sound Detector Module

#define GT911_I2C_ADDR      0x5D

// =============================================================================
// Waveshare 7.0" Parallel 16-Bit RGB Display Driver Initialization (Arduino_GFX)
// =============================================================================
#if __has_include(<Arduino_GFX_Library.h>)
#include <Arduino_GFX_Library.h>
#define HAS_ARDUINO_GFX 1
#else
#define HAS_ARDUINO_GFX 0
#endif

#if HAS_ARDUINO_GFX
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    5 /* DE */, 3 /* VSYNC */, 46 /* HSYNC */, 7 /* PCLK */,
    1, 2, 42, 41, 40,      // R3-R7
    39, 0, 45, 48, 47, 21, // G2-G7
    14, 38, 18, 17, 10,    // B3-B7
    1, 48, 162, 152,
    1, 3, 45, 13,
    1, 16000000
);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(SCREEN_WIDTH, SCREEN_HEIGHT, rgbpanel, 0, true);
bool gfxAvailable = true;
#else
bool gfxAvailable = false;
#endif
#define MCP23017_ADDR_PRIMARY 0x20 // Primary I/O Expander for Left & Right Birds
#define MCP23017_ADDR_SEC   0x21 // Optional Secondary I/O Expander
#define PCA9685_ADDR_LEFT   0x40 // Driver 1: Left Servos (Channels 0-15)
#define PCA9685_ADDR_RIGHT  0x41 // Driver 2: Right Servos (Channels 0-15)

// PCA9685 Registers & Constants
#define PCA9685_MODE1       0x00
#define PCA9685_PRESCALE    0xFE
#define PCA9685_LED0_ON_L   0x06
#define SERVOMIN            150  // Min pulse for 0 deg (50Hz, 4096 counts)
#define SERVOMAX            600  // Max pulse for 180 deg (50Hz, 4096 counts)

// MCP23017 Registers
#define MCP_IODIRA          0x00
#define MCP_IODIRB          0x01
#define MCP_GPPUA           0x0C // Pull-up resistor configuration Port A
#define MCP_GPPUB           0x0D // Pull-up resistor configuration Port B
#define MCP_GPIOA           0x12
#define MCP_GPIOB           0x13
#define MCP_OLATA           0x14
#define MCP_OLATB           0x15

// Total Servos and IO Pins Supported
const int TOTAL_PCA_DRIVERS = 2;
const int SERVOS_PER_DRIVER = 16;
const int TOTAL_SERVOS = TOTAL_PCA_DRIVERS * SERVOS_PER_DRIVER; // 32 Servos

const int TOTAL_MCP_PINS = 32; // Up to 32 outputs across primary/secondary MCP23017

// =============================================================================
// State Tracking Variables & Parrot Selection
// =============================================================================
enum ParrotSelection {
  PARROT_LEFT = 0,
  PARROT_RIGHT = 1,
  PARROT_BOTH = 2
};

ParrotSelection selectedParrot = PARROT_LEFT;
bool micReactivityEnabled = true;
bool isSpeechActive = false;
unsigned long lastSoundDetectTime = 0;
const unsigned long SOUND_SUSTAIN_MS = 450; // Sustain speech motion between word pauses
unsigned long lastSpeechMouthToggle = 0;
bool speechMouthOpen = false;
unsigned long lastSpeechWingToggle = 0;
bool speechWingFlap = false;
unsigned long lastSpeechEyeToggle = 0;
bool speechEyeBlinkState = false;

// MCP23017 Pin States (0 = LOW, 1 = HIGH)
uint16_t mcpPrimaryLatch  = 0x0000; // Port A (bits 0-7) + Port B (bits 8-15)
uint16_t mcpSecLatch      = 0x0000;
bool mcpPrimaryPresent    = false;
bool mcpSecPresent        = false;
bool pcaLeftPresent       = false;
bool pcaRightPresent      = false;

// Direct GPIO Fallback States for onboard ESP32 pins
int directGpioStates[40];

// Servo Angles (0 - 180 degrees)
float servoCurrentAngles[TOTAL_SERVOS];
float servoStartAngles[TOTAL_SERVOS];
float servoTargetAngles[TOTAL_SERVOS];

unsigned long servoMoveStartTime = 0;
unsigned long servoMoveDurationMs = 250; // Smooth trajectory duration
bool isServoMoving = false;

// Active Routine State Machine
String currentRoutine = "idle";
unsigned long routineStepTime = 0;
int routineStepIndex = 0;
String statusMessage = "Waveshare 7.0\" Touch-LCD 7C Online";

// Animation State Variables
unsigned long lastAnimUpdate = 0;
unsigned long lastBlinkTime = 0;
bool eyeBlinkState = false;
int mouthOpenPercent = 0;      // 0 = closed, 100 = wide open
float headBobOffset = 0.0f;
float wingFlapAngle = 0.0f;
float spotlightBeamLeft = 90.0f;
float spotlightBeamRight = 90.0f;
float audioSpectrumBars[8];
int musicNoteX[3] = {200, 220, 185};
int musicNoteY[3] = {150, 180, 210};
bool musicNoteActive[3] = {false, false, false};

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

// Procedural Bird & Mascot Sound FX
void playBirdChirpSound() {
  playSweepI2S(2200.0f, 3900.0f, 40, 0.9f);
  playSweepI2S(3900.0f, 2100.0f, 65, 1.0f);
  delay(15);
  playSweepI2S(2600.0f, 4400.0f, 35, 0.9f);
  playSweepI2S(4400.0f, 2300.0f, 55, 0.85f);
}

void playParrotSquawkSound() {
  playSweepI2S(750.0f, 1600.0f, 60, 1.0f);
  playToneI2S(1350.0f, 40, 0.9f);
  playSweepI2S(1600.0f, 850.0f, 80, 0.85f);
}

void playBirdTrillSound() {
  for (int i = 0; i < 4; i++) {
    playSweepI2S(2800.0f, 3600.0f, 25, 0.85f);
    playSweepI2S(3600.0f, 2800.0f, 25, 0.85f);
  }
}

void playBirdSongMelody() {
  float notes[] = {1046.5f, 1318.5f, 1567.98f, 2093.0f, 1567.98f, 1318.5f, 2093.0f};
  int durs[]    = {70, 70, 70, 120, 60, 60, 160};
  for (int i = 0; i < 7; i++) {
    playToneI2S(notes[i], durs[i], 0.9f);
    delay(15);
  }
}

void playBirdSymphonySound() {
  float notes[] = {523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f, 1567.98f, 2093.0f};
  for (int i = 0; i < 7; i++) {
    playSweepI2S(notes[i] * 0.85f, notes[i], 55, 0.9f);
    delay(10);
  }
  playToneI2S(2093.0f, 220, 1.0f);
}

void playBeepSound(int freq = 1200, int dur = 45) {
  playToneI2S(freq, dur, 0.8f);
}

void setAudioVolume(int vol) {
  audioVolume = constrain(vol, 0, 100);
  Serial.print("[MAX98357A] Audio Volume set to: ");
  Serial.print(audioVolume);
  Serial.println("%");
}

void setAudioMute(bool mute) {
  audioMuted = mute;
  Serial.print("[MAX98357A] Audio Mute: ");
  Serial.println(audioMuted ? "MUTED" : "UNMUTED");
}

// Non-blocking Pulse Timers
struct PulseTimer {
  bool active;
  String board;
  int pin;
  unsigned long endTime;
};
PulseTimer activePulses[8];

// Touch State
bool isTouchActive = false;
int touchX = 0;
int touchY = 0;
unsigned long lastTouchTime = 0;

// =============================================================================
// Output Mapping Definition for Left & Right Bird Functions
// =============================================================================
struct BirdFunction {
  const char* name;
  const char* board; // "left" or "right"
  int pin;           // Logical pin index (0-15 on MCP23017 or GPIO)
  int mcpChip;       // 0 = Primary (0x20), 1 = Secondary (0x21)
  int mcpBit;        // 0-15
};

const BirdFunction BIRD_FUNCTIONS[] = {
  // Left Bird Outputs (Mapped to MCP23017 Primary Port A & Port B lower)
  {"L Parrot Mouth",        "left",  0,  0,  0},
  {"L Parrot Eyes",         "left",  1,  0,  1},
  {"L Parrot Body",         "left",  2,  0,  2},
  {"L Parrot Light",        "left",  3,  0,  3},
  {"L Parrot Mouth Select", "left",  4,  0,  4},
  {"L Rear Bird Rear Move", "left",  5,  0,  5},
  {"L Rear Bird Rear Light","left", 12,  0,  6},
  {"L Front Bird Move",     "left", 13,  0,  7},
  {"L Front Bird Light",    "left", 14,  0,  8},
  {"L Bird Front Chirp",    "left", 15,  0,  9},
  {"Center Bird Move",      "left", 16,  0, 10},

  // Right Bird Outputs (Mapped to MCP23017 Primary Port B upper & Sec/Direct)
  {"R Parrot Mouth",        "right",  0,  0, 11},
  {"R Parrot Eyes",         "right",  1,  0, 12},
  {"R Parrot Body",         "right",  2,  0, 13},
  {"R Parrot Light",        "right",  3,  0, 14},
  {"R Parrot Mouth Select", "right",  4,  0, 15},
  {"R Rear Bird Rear Move", "right",  5,  1,  0},
  {"R Rear Bird Rear Light","right", 12,  1,  1},
  {"R Front Bird Move",     "right", 13,  1,  2},
  {"R Front Bird Light",    "right", 14,  1,  3},
  {"R Bird Front Chirp",    "right", 15,  1,  4},
  {"Center Bird Move",      "right", 16,  1,  5},
};
const int NUM_BIRD_FUNCTIONS = sizeof(BIRD_FUNCTIONS) / sizeof(BirdFunction);

// =============================================================================
// Low-Level I2C Helper Functions
// =============================================================================
bool checkI2CDevice(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

// --- MCP23017 Driver ---
void initMCP23017() {
  mcpPrimaryPresent = checkI2CDevice(MCP23017_ADDR_PRIMARY);
  mcpSecPresent     = checkI2CDevice(MCP23017_ADDR_SEC);

  if (mcpPrimaryPresent) {
    // Configure Port A: Bit 4 (GPA4) as INPUT (0x10) for microphone module, rest as OUTPUTs (0x00)
    // Configure Port B: all pins as OUTPUTs (0x00)
    Wire.beginTransmission(MCP23017_ADDR_PRIMARY);
    Wire.write(MCP_IODIRA);
    Wire.write(0x10); // Port A: Bit 4 is input, rest outputs
    Wire.write(0x00); // Port B: all outputs
    Wire.endTransmission();

    // Enable internal 100k pull-up resistor on GPA4
    Wire.beginTransmission(MCP23017_ADDR_PRIMARY);
    Wire.write(MCP_GPPUA);
    Wire.write(0x10); // Port A Bit 4 pull-up enabled
    Wire.write(0x00); // Port B
    Wire.endTransmission();

    // Set all initial outputs to LOW
    Wire.beginTransmission(MCP23017_ADDR_PRIMARY);
    Wire.write(MCP_OLATA);
    Wire.write(0x00);
    Wire.write(0x00);
    Wire.endTransmission();
    mcpPrimaryLatch = 0x0000;
  }

  if (mcpSecPresent) {
    Wire.beginTransmission(MCP23017_ADDR_SEC);
    Wire.write(MCP_IODIRA);
    Wire.write(0x00);
    Wire.write(0x00);
    Wire.endTransmission();

    Wire.beginTransmission(MCP23017_ADDR_SEC);
    Wire.write(MCP_OLATA);
    Wire.write(0x00);
    Wire.write(0x00);
    Wire.endTransmission();
    mcpSecLatch = 0x0000;
  }
}

void writeMCPPin(uint8_t chip, uint8_t pinBit, bool state) {
  if (pinBit > 15) return;
  uint8_t addr = (chip == 0) ? MCP23017_ADDR_PRIMARY : MCP23017_ADDR_SEC;
  uint16_t* latchPtr = (chip == 0) ? &mcpPrimaryLatch : &mcpSecLatch;
  bool present = (chip == 0) ? mcpPrimaryPresent : mcpSecPresent;

  if (state) {
    *latchPtr |= (1 << pinBit);
  } else {
    *latchPtr &= ~(1 << pinBit);
  }

  if (present) {
    Wire.beginTransmission(addr);
    if (pinBit < 8) {
      Wire.write(MCP_OLATA);
      Wire.write((uint8_t)(*latchPtr & 0xFF));
    } else {
      Wire.write(MCP_OLATB);
      Wire.write((uint8_t)((*latchPtr >> 8) & 0xFF));
    }
    Wire.endTransmission();
  }
}

bool getMCPPinState(uint8_t chip, uint8_t pinBit) {
  if (pinBit > 15) return false;
  uint16_t latch = (chip == 0) ? mcpPrimaryLatch : mcpSecLatch;
  return ((latch & (1 << pinBit)) != 0);
}

bool readMCPPin(uint8_t chip, uint8_t pinBit) {
  if (pinBit > 15) return false;
  uint8_t addr = (chip == 0) ? MCP23017_ADDR_PRIMARY : MCP23017_ADDR_SEC;
  bool present = (chip == 0) ? mcpPrimaryPresent : mcpSecPresent;
  if (!present) return false;

  uint8_t reg = (pinBit < 8) ? MCP_GPIOA : MCP_GPIOB;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;

  Wire.requestFrom(addr, (uint8_t)1);
  if (Wire.available() >= 1) {
    uint8_t val = Wire.read();
    uint8_t bitIdx = pinBit % 8;
    return ((val & (1 << bitIdx)) != 0);
  }
  return false;
}

// --- PCA9685 Servo Driver ---
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

void initPCA9685Drivers() {
  pcaLeftPresent  = checkI2CDevice(PCA9685_ADDR_LEFT);
  pcaRightPresent = checkI2CDevice(PCA9685_ADDR_RIGHT);

  uint8_t addrs[2] = {PCA9685_ADDR_LEFT, PCA9685_ADDR_RIGHT};
  bool presents[2] = {pcaLeftPresent, pcaRightPresent};

  for (int i = 0; i < 2; i++) {
    if (presents[i]) {
      writePCA9685(addrs[i], PCA9685_MODE1, 0x00);
      delay(10);
      writePCA9685(addrs[i], PCA9685_MODE1, 0x10); // Sleep
      writePCA9685(addrs[i], PCA9685_PRESCALE, 121); // 50Hz PWM
      writePCA9685(addrs[i], PCA9685_MODE1, 0x00); // Wake
      delay(5);
      writePCA9685(addrs[i], PCA9685_MODE1, 0xA1); // Auto-increment
    }
  }
}

void writeHardwareServo(uint8_t driverIdx, uint8_t channel, float angle) {
  if (driverIdx >= TOTAL_PCA_DRIVERS || channel >= SERVOS_PER_DRIVER) return;
  angle = constrain(angle, 0.0f, 180.0f);
  uint8_t addr = (driverIdx == 0) ? PCA9685_ADDR_LEFT : PCA9685_ADDR_RIGHT;
  bool present = (driverIdx == 0) ? pcaLeftPresent : pcaRightPresent;

  uint16_t pulse = map((long)angle, 0, 180, SERVOMIN, SERVOMAX);
  if (present) {
    setPCA9685PWM(addr, channel, 0, pulse);
  }

  // Update animated spotlight angles for onscreen visualizer
  if (driverIdx == 0 && (channel == 3 || channel == 4)) {
    spotlightBeamLeft = angle;
  } else if (driverIdx == 1 && (channel == 3 || channel == 4)) {
    spotlightBeamRight = angle;
  }
}

// =============================================================================
// Output & Servo Control Logic
// =============================================================================
int findBirdFunctionIndex(String board, int pin) {
  board.toLowerCase();
  for (int i = 0; i < NUM_BIRD_FUNCTIONS; i++) {
    if (String(BIRD_FUNCTIONS[i].board) == board && BIRD_FUNCTIONS[i].pin == pin) {
      return i;
    }
  }
  return -1;
}

void setBirdOutputState(String board, int pin, int state) {
  int idx = findBirdFunctionIndex(board, pin);
  if (idx >= 0) {
    bool onState = (state == 1 || state == HIGH);
    writeMCPPin(BIRD_FUNCTIONS[idx].mcpChip, BIRD_FUNCTIONS[idx].mcpBit, onState);

    // Trigger visual animation reaction
    if (onState) {
      if (pin == 0 || pin == 4) mouthOpenPercent = 85; // Parrot mouth
      if (pin == 15) {
        mouthOpenPercent = 100; // Chirp
        playBirdChirpSound();
      }
    }
  } else {
    // Direct GPIO fallback
    if (pin >= 0 && pin < 40 && pin != 1 && pin != 3) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, (state == 1 || state == HIGH) ? HIGH : LOW);
      directGpioStates[pin] = state;
    }
  }
}

bool getBirdOutputState(String board, int pin) {
  int idx = findBirdFunctionIndex(board, pin);
  if (idx >= 0) {
    return getMCPPinState(BIRD_FUNCTIONS[idx].mcpChip, BIRD_FUNCTIONS[idx].mcpBit);
  }
  if (pin >= 0 && pin < 40) return (directGpioStates[pin] == HIGH);
  return false;
}

void pulseBirdOutput(String board, int pin, unsigned long durationMs = 300) {
  setBirdOutputState(board, pin, 1);
  for (int i = 0; i < 8; i++) {
    if (!activePulses[i].active) {
      activePulses[i].active = true;
      activePulses[i].board = board;
      activePulses[i].pin = pin;
      activePulses[i].endTime = millis() + durationMs;
      break;
    }
  }
}

void updatePulseTimers() {
  unsigned long now = millis();
  for (int i = 0; i < 8; i++) {
    if (activePulses[i].active && now >= activePulses[i].endTime) {
      setBirdOutputState(activePulses[i].board, activePulses[i].pin, 0);
      activePulses[i].active = false;
    }
  }
}

// Servo Setters & Trajectory Interpolator
void setSingleServoAngle(uint8_t globalChannel, float targetAngle, unsigned long durationMs = 250) {
  if (globalChannel >= TOTAL_SERVOS) return;
  servoStartAngles[globalChannel] = servoCurrentAngles[globalChannel];
  servoTargetAngles[globalChannel] = constrain(targetAngle, 0.0f, 180.0f);
  servoMoveStartTime = millis();
  servoMoveDurationMs = (durationMs < 20) ? 20 : durationMs;
  isServoMoving = true;
}

void setDriverServoAngle(uint8_t driverIdx, uint8_t chan, float angle, unsigned long durationMs = 250) {
  if (driverIdx >= TOTAL_PCA_DRIVERS || chan >= SERVOS_PER_DRIVER) return;
  uint8_t globalChan = driverIdx * SERVOS_PER_DRIVER + chan;
  setSingleServoAngle(globalChan, angle, durationMs);
}

void updateServoTrajectoryEngine() {
  if (!isServoMoving) return;

  unsigned long elapsed = millis() - servoMoveStartTime;
  float progress = (float)elapsed / (float)servoMoveDurationMs;

  if (progress >= 1.0f) {
    progress = 1.0f;
    isServoMoving = false;
  }

  // Smooth Cosine S-Curve ease
  float ease = 0.5f * (1.0f - cos(M_PI * progress));

  for (int i = 0; i < TOTAL_SERVOS; i++) {
    servoCurrentAngles[i] = servoStartAngles[i] + ease * (servoTargetAngles[i] - servoStartAngles[i]);
    uint8_t driver = i / SERVOS_PER_DRIVER;
    uint8_t ch = i % SERVOS_PER_DRIVER;
    writeHardwareServo(driver, ch, servoCurrentAngles[i]);
  }
}

// =============================================================================
// Routine State Machine & Choreography Engine
// =============================================================================
void startRoutine(String routineName) {
  currentRoutine = routineName;
  routineStepIndex = 0;
  routineStepTime = millis();
  statusMessage = "Routine: " + routineName;

  if (routineName == "sing") {
    mouthOpenPercent = 90;
    musicNoteActive[0] = true;
    musicNoteActive[1] = true;
    musicNoteActive[2] = true;
  } else if (routineName == "home") {
    for (int i = 0; i < TOTAL_SERVOS; i++) {
      setSingleServoAngle(i, 90.0f, 300);
    }
    for (int i = 0; i < NUM_BIRD_FUNCTIONS; i++) {
      writeMCPPin(BIRD_FUNCTIONS[i].mcpChip, BIRD_FUNCTIONS[i].mcpBit, false);
    }
    mouthOpenPercent = 0;
    statusMessage = "All Servos 90° | All Lights OFF";
    currentRoutine = "idle";
  }
}

void updateRoutines() {
  if (currentRoutine == "idle" || currentRoutine == "stop") return;
  if (millis() - routineStepTime < 180) return;
  routineStepTime = millis();

  if (currentRoutine == "sing") {
    // Animated Parrot Sing & Chirp Routine
    if (routineStepIndex < 12) {
      bool openBeak = (routineStepIndex % 2 == 0);
      mouthOpenPercent = openBeak ? 100 : 20;
      setBirdOutputState("left", 0, openBeak ? 1 : 0);  // L Parrot Mouth
      setBirdOutputState("right", 0, openBeak ? 1 : 0); // R Parrot Mouth
      pulseBirdOutput("left", 15, 120);                 // Chirp

      // Oscillate Parrot Head Up/Dn & Rotate Servos
      float upAngle = openBeak ? 115.0f : 75.0f;
      setDriverServoAngle(0, 0, upAngle, 150); // Left Parrot Up/Dn
      setDriverServoAngle(1, 0, upAngle, 150); // Right Parrot Up/Dn
      setDriverServoAngle(0, 1, 90.0f + (openBeak ? 25.0f : -25.0f), 150);

      routineStepIndex++;
    } else {
      setBirdOutputState("left", 0, 0);
      setBirdOutputState("right", 0, 0);
      mouthOpenPercent = 0;
      startRoutine("home");
    }
  }
  else if (currentRoutine == "sweep") {
    // Spotlight Sweep Routine
    if (routineStepIndex < 10) {
      float sweepAngleL = (routineStepIndex % 2 == 0) ? 45.0f : 135.0f;
      float sweepAngleR = (routineStepIndex % 2 == 0) ? 135.0f : 45.0f;
      setDriverServoAngle(0, 3, sweepAngleL, 300); // L Spotlight Up/Dn
      setDriverServoAngle(0, 4, sweepAngleL, 300); // L Spotlight Rotate
      setDriverServoAngle(1, 3, sweepAngleR, 300); // R Spotlight Up/Dn
      setDriverServoAngle(1, 4, sweepAngleR, 300); // R Spotlight Rotate

      setBirdOutputState("left", 3, 1);  // L Parrot Light
      setBirdOutputState("right", 3, 1); // R Parrot Light
      routineStepIndex++;
    } else {
      startRoutine("home");
    }
  }
  else if (currentRoutine == "dance") {
    // Center Turntable & Birds Dance
    if (routineStepIndex < 12) {
      float ttAngle = (routineStepIndex % 2 == 0) ? 40.0f : 140.0f;
      setDriverServoAngle(1, 5, ttAngle, 200); // Center Turntable Rotate
      setDriverServoAngle(0, 7, ttAngle, 200); // Center Bird Rotate

      setBirdOutputState("left", 2, (routineStepIndex % 2 == 0) ? 1 : 0);
      setBirdOutputState("right", 2, (routineStepIndex % 2 == 1) ? 1 : 0);
      routineStepIndex++;
    } else {
      startRoutine("home");
    }
  }
  else if (currentRoutine == "lightshow") {
    // Pulse light show across all outputs
    if (routineStepIndex < NUM_BIRD_FUNCTIONS) {
      for (int i = 0; i < NUM_BIRD_FUNCTIONS; i++) {
        writeMCPPin(BIRD_FUNCTIONS[i].mcpChip, BIRD_FUNCTIONS[i].mcpBit, (i == routineStepIndex));
      }
      routineStepIndex++;
    } else {
      startRoutine("home");
    }
  }
}

// =============================================================================
// Real-Time Speech Motion & Animatronics Engine (Microphone & AI Voice Sync)
// =============================================================================
void updateSpeechMotionEngine() {
  if (!micReactivityEnabled) return;
  if (currentRoutine != "idle") return; // Manual routines have priority

  unsigned long now = millis();

  // 1. Read Hardware Microphone Sound Detector
  // KY-037/KY-038/LM393 sound detection modules pull digital OUT (DO) LOW on sound detection
  bool gpioTrigger = (digitalRead(MIC_SENSOR_GPIO_PIN) == LOW);
  bool mcpTrigger  = (mcpPrimaryPresent && !readMCPPin(0, 4)); // GPA4 (Bit 4) triggered LOW

  if (gpioTrigger || mcpTrigger) {
    lastSoundDetectTime = now;
  }

  // 2. Active Speaking / Sound Detection Window
  if (now - lastSoundDetectTime < SOUND_SUSTAIN_MS) {
    isSpeechActive = true;
    float t = now * 0.003f;

    // --- A. Dynamic Mouth Up/Down Movement ---
    if (now - lastSpeechMouthToggle > 120) {
      speechMouthOpen = !speechMouthOpen;
      lastSpeechMouthToggle = now;

      // Selected parrot(s) mouth moves up and down
      if (selectedParrot == PARROT_LEFT || selectedParrot == PARROT_BOTH) {
        writeMCPPin(0, 0, speechMouthOpen);  // L Parrot Mouth (Bit 0)
      } else {
        writeMCPPin(0, 0, false);
      }

      if (selectedParrot == PARROT_RIGHT || selectedParrot == PARROT_BOTH) {
        writeMCPPin(0, 11, speechMouthOpen); // R Parrot Mouth (Bit 11)
      } else {
        writeMCPPin(0, 11, false);
      }

      mouthOpenPercent = speechMouthOpen ? 90 : 15;
    }

    // --- B. Dynamic LED Eye Blinking / Pulsing on Speech ---
    if (now - lastSpeechEyeToggle > 160) {
      speechEyeBlinkState = !speechEyeBlinkState;
      lastSpeechEyeToggle = now;

      if (selectedParrot == PARROT_LEFT || selectedParrot == PARROT_BOTH) {
        writeMCPPin(0, 1, speechEyeBlinkState);  // L Parrot Eyes (Bit 1)
      } else {
        writeMCPPin(0, 1, false);
      }

      if (selectedParrot == PARROT_RIGHT || selectedParrot == PARROT_BOTH) {
        writeMCPPin(0, 12, speechEyeBlinkState); // R Parrot Eyes (Bit 12)
      } else {
        writeMCPPin(0, 12, false);
      }
    }

    // --- C. Wing Flapping Movement ---
    if (now - lastSpeechWingToggle > 180) {
      speechWingFlap = !speechWingFlap;
      lastSpeechWingToggle = now;

      if (selectedParrot == PARROT_LEFT || selectedParrot == PARROT_BOTH) {
        writeMCPPin(0, 2, speechWingFlap);  // L Parrot Body/Wings (Bit 2)
      } else {
        writeMCPPin(0, 2, false);
      }

      if (selectedParrot == PARROT_RIGHT || selectedParrot == PARROT_BOTH) {
        writeMCPPin(0, 13, speechWingFlap); // R Parrot Body/Wings (Bit 13)
      } else {
        writeMCPPin(0, 13, false);
      }
    }

    // --- D. 3 Base Servos of Selected Parrot (Gentle Slow-to-Medium Speed) ---
    // Channel 0: Up/Down Tilt (78° to 102°)
    // Channel 1: Right/Left Side-to-Side Sway (74° to 106°)
    // Channel 2: Turn / Rotate (70° to 110°)
    float lUpAngle    = 90.0f + sin(t * 3.5f) * 12.0f;
    float lSideAngle  = 90.0f + cos(t * 2.8f) * 16.0f;
    float lTurnAngle  = 90.0f + sin(t * 2.0f) * 20.0f;

    float rUpAngle    = 90.0f + sin(t * 3.5f + 1.4f) * 12.0f;
    float rSideAngle  = 90.0f + cos(t * 2.8f + 1.4f) * 16.0f;
    float rTurnAngle  = 90.0f + sin(t * 2.0f + 1.4f) * 20.0f;

    if (selectedParrot == PARROT_LEFT || selectedParrot == PARROT_BOTH) {
      setDriverServoAngle(0, 0, lUpAngle, 100);   // Left Parrot Up/Dn
      setDriverServoAngle(0, 1, lSideAngle, 100); // Left Parrot Right/Left
      setDriverServoAngle(0, 2, lTurnAngle, 100); // Left Parrot Rotate
    }

    if (selectedParrot == PARROT_RIGHT || selectedParrot == PARROT_BOTH) {
      setDriverServoAngle(1, 0, rUpAngle, 100);   // Right Parrot Up/Dn
      setDriverServoAngle(1, 1, rSideAngle, 100); // Right Parrot Right/Left
      setDriverServoAngle(1, 2, rTurnAngle, 100); // Right Parrot Rotate
    }

    // --- E. Spotlight Movement & Lighting ---
    if (selectedParrot == PARROT_LEFT || selectedParrot == PARROT_BOTH) {
      float lSpotUp  = 90.0f + sin(t * 1.8f) * 16.0f;
      float lSpotRot = 90.0f + cos(t * 1.4f) * 22.0f;
      setDriverServoAngle(0, 3, lSpotUp, 120);  // Left Spotlight Up/Dn
      setDriverServoAngle(0, 4, lSpotRot, 120); // Left Spotlight Rotate
      writeMCPPin(0, 3, true);                  // Left Spotlight LED ON
    } else {
      writeMCPPin(0, 3, false);
    }

    if (selectedParrot == PARROT_RIGHT || selectedParrot == PARROT_BOTH) {
      float rSpotUp  = 90.0f + cos(t * 1.8f) * 16.0f;
      float rSpotRot = 90.0f + sin(t * 1.4f) * 22.0f;
      setDriverServoAngle(1, 3, rSpotUp, 120);  // Right Spotlight Up/Dn
      setDriverServoAngle(1, 4, rSpotRot, 120); // Right Spotlight Rotate
      writeMCPPin(0, 14, true);                 // Right Spotlight LED ON
    } else {
      writeMCPPin(0, 14, false);
    }

    // --- F. Center Turntable (Slow Right and Left Rotation) ---
    float ttAngle = 90.0f + sin(t * 1.2f) * 35.0f; // 55° to 125°
    setDriverServoAngle(1, 5, ttAngle, 150);        // Center Turntable Rotate

  } else if (isSpeechActive) {
    // Silence detected -> Return to neutral rest positions
    isSpeechActive = false;

    // Reset base servos to neutral 90
    if (selectedParrot == PARROT_LEFT || selectedParrot == PARROT_BOTH) {
      setDriverServoAngle(0, 0, 90.0f, 250);
      setDriverServoAngle(0, 1, 90.0f, 250);
      setDriverServoAngle(0, 2, 90.0f, 250);
      writeMCPPin(0, 0, false); // Mouth OFF
      writeMCPPin(0, 1, false); // Eyes OFF
      writeMCPPin(0, 2, false); // Wings OFF
      writeMCPPin(0, 3, false); // Light OFF
    }

    if (selectedParrot == PARROT_RIGHT || selectedParrot == PARROT_BOTH) {
      setDriverServoAngle(1, 0, 90.0f, 250);
      setDriverServoAngle(1, 1, 90.0f, 250);
      setDriverServoAngle(1, 2, 90.0f, 250);
      writeMCPPin(0, 11, false); // Mouth OFF
      writeMCPPin(0, 12, false); // Eyes OFF
      writeMCPPin(0, 13, false); // Wings OFF
      writeMCPPin(0, 14, false); // Light OFF
    }

    // Reset spotlights and turntable to neutral 90
    setDriverServoAngle(0, 3, 90.0f, 250);
    setDriverServoAngle(0, 4, 90.0f, 250);
    setDriverServoAngle(1, 3, 90.0f, 250);
    setDriverServoAngle(1, 4, 90.0f, 250);
    setDriverServoAngle(1, 5, 90.0f, 250);

    mouthOpenPercent = 0;
  }
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

      // Clear buffer flag
      Wire.beginTransmission((uint8_t)GT911_I2C_ADDR);
      Wire.write(0x81);
      Wire.write(0x4E);
      Wire.write(0x00);
      Wire.endTransmission();
      return true;
    }
  }

  // Clear buffer flag
  Wire.beginTransmission((uint8_t)GT911_I2C_ADDR);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(0x00);
  Wire.endTransmission();
  return false;
}

// =============================================================================
// On-Screen Touch Dashboard Layout & Action Dispatcher
// =============================================================================
#if defined(BOARD_ESP32_TOUCH_LCD_7C)
// 1024x600 High-Definition Touch Dashboard
const TouchButton DASHBOARD_BTNS[] = {
  // Left Bird Output Toggles (Columns 1 & 2, Left Side: X=25, 135)
  {"L Mouth",       25,  80, 100, 48, "output", "left", 0},
  {"L Eyes",       135,  80, 100, 48, "output", "left", 1},
  {"L Body",        25, 136, 100, 48, "output", "left", 2},
  {"L Light",      135, 136, 100, 48, "output", "left", 3},
  {"L Mouth Sel",   25, 192, 100, 48, "output", "left", 4},
  {"L Rear Move",  135, 192, 100, 48, "output", "left", 5},
  {"L Rear Light",  25, 248, 100, 48, "output", "left", 12},
  {"L Front Move", 135, 248, 100, 48, "output", "left", 13},
  {"L Front Light", 25, 304, 100, 48, "output", "left", 14},
  {"L Chirp",      135, 304, 100, 48, "output", "left", 15},
  {"Ctr Bird Move", 25, 360, 210, 48, "output", "left", 16},

  // Parrot Selection & Mic Sound Reactivity (Center Top Bar: X=270..710)
  {"👈 L PARROT",  270,  80, 145, 48, "parrot_sel", "left", 0},
  {"🦜 BOTH",      425,  80, 130, 48, "parrot_sel", "both", 0},
  {"👉 R PARROT",  565,  80, 145, 48, "parrot_sel", "right", 0},
  {"🎤 MIC REACT", 335, 138, 310, 48, "mic_toggle", "toggle", 0},

  // Right Bird Output Toggles (Columns 5 & 6, Right Side: X=770, 880)
  {"R Mouth",      770,  80, 100, 48, "output", "right", 0},
  {"R Eyes",       880,  80, 100, 48, "output", "right", 1},
  {"R Body",       770, 136, 100, 48, "output", "right", 2},
  {"R Light",      880, 136, 100, 48, "output", "right", 3},
  {"R Mouth Sel",  770, 192, 100, 48, "output", "right", 4},
  {"R Rear Move",  880, 192, 100, 48, "output", "right", 5},
  {"R Rear Light", 770, 248, 100, 48, "output", "right", 12},
  {"R Front Move", 880, 248, 100, 48, "output", "right", 13},
  {"R Front Light",770, 304, 100, 48, "output", "right", 14},
  {"R Chirp",      880, 304, 100, 48, "output", "right", 15},
  {"Center Move",  770, 360, 210, 48, "output", "right", 16},

  // Routine Quick-Action Buttons (Center Bottom Control Bar: Y=495, H=68)
  {"🦜 SING",       25, 495, 145, 68, "routine", "sing", 0},
  {"💡 SWEEP",     180, 495, 145, 68, "routine", "sweep", 0},
  {"🔄 DANCE",     335, 495, 145, 68, "routine", "dance", 0},
  {"🌟 LIGHTS",    490, 495, 145, 68, "routine", "lightshow", 0},
  {"🎶 SYMPHONY",  645, 495, 165, 68, "routine", "sing", 0},
  {"🏠 ALL HOME",  820, 495, 175, 68, "routine", "home", 0},
};
#else
// 800x480 Standard Dashboard
const TouchButton DASHBOARD_BTNS[] = {
  // Left Bird Output Toggles (Column 1 & 2, Left Side of 7" Screen)
  {"L Mouth",       20,  70, 90, 42, "output", "left", 0},
  {"L Eyes",       115,  70, 90, 42, "output", "left", 1},
  {"L Body",        20, 118, 90, 42, "output", "left", 2},
  {"L Light",      115, 118, 90, 42, "output", "left", 3},
  {"L Mouth Sel",   20, 166, 90, 42, "output", "left", 4},
  {"L Rear Move",  115, 166, 90, 42, "output", "left", 5},
  {"L Rear Light",  20, 214, 90, 42, "output", "left", 12},
  {"L Front Move", 115, 214, 90, 42, "output", "left", 13},
  {"L Front Light", 20, 262, 90, 42, "output", "left", 14},
  {"L Chirp",      115, 262, 90, 42, "output", "left", 15},
  {"Ctr Bird Move", 20, 310, 185, 42, "output", "left", 16},

  // Parrot Selection & Mic Sound Reactivity (Center Top Bar)
  {"👈 L PARROT",  215,  70, 115, 38, "parrot_sel", "left", 0},
  {"🦜 BOTH",      335,  70,  95, 38, "parrot_sel", "both", 0},
  {"👉 R PARROT",  435,  70, 115, 38, "parrot_sel", "right", 0},
  {"🎤 MIC REACT", 265, 115, 235, 38, "mic_toggle", "toggle", 0},

  // Right Bird Output Toggles (Column 5 & 6, Right Side of 7" Screen)
  {"R Mouth",      595,  70, 90, 42, "output", "right", 0},
  {"R Eyes",       690,  70, 90, 42, "output", "right", 1},
  {"R Body",       595, 118, 90, 42, "output", "right", 2},
  {"R Light",      690, 118, 90, 42, "output", "right", 3},
  {"R Mouth Sel",  595, 166, 90, 42, "output", "right", 4},
  {"R Rear Move",  690, 166, 90, 42, "output", "right", 5},
  {"R Rear Light", 595, 214, 90, 42, "output", "right", 12},
  {"R Front Move", 690, 214, 90, 42, "output", "right", 13},
  {"R Front Light",595, 262, 90, 42, "output", "right", 14},
  {"R Chirp",      690, 262, 90, 42, "output", "right", 15},
  {"Center Move",  595, 310, 185, 42, "output", "right", 16},

  // Routine Quick-Action Buttons (Center Bottom Control Bar)
  {"🦜 SING",       20, 395, 115, 55, "routine", "sing", 0},
  {"💡 SWEEP",     145, 395, 115, 55, "routine", "sweep", 0},
  {"🔄 DANCE",     270, 395, 115, 55, "routine", "dance", 0},
  {"🌟 LIGHTS",    395, 395, 115, 55, "routine", "lightshow", 0},
  {"🎶 SYMPHONY",  520, 395, 125, 55, "routine", "sing", 0},
  {"🏠 ALL HOME",  655, 395, 125, 55, "routine", "home", 0},
};
#endif
const int NUM_DASHBOARD_BTNS = sizeof(DASHBOARD_BTNS) / sizeof(TouchButton);

void handleTouchAction(int btnIdx) {
  if (btnIdx < 0 || btnIdx >= NUM_DASHBOARD_BTNS) return;
  const TouchButton& btn = DASHBOARD_BTNS[btnIdx];
  if (String(btn.type) == "output") {
    bool current = getBirdOutputState(btn.board, btn.id);
    setBirdOutputState(btn.board, btn.id, current ? 0 : 1);
    Serial.print("[Touch 7\"] Toggled ");
    Serial.print(btn.board);
    Serial.print(" pin ");
    Serial.print(btn.id);
    Serial.print(" -> ");
    Serial.println(current ? "OFF" : "ON");
  } else if (String(btn.type) == "routine") {
    startRoutine(btn.board);
    Serial.print("[Touch 7\"] Started Routine: ");
    Serial.println(btn.board);
  } else if (String(btn.type) == "parrot_sel") {
    if (String(btn.board) == "left") {
      selectedParrot = PARROT_LEFT;
      statusMessage = "Speaker Parrot: LEFT";
    } else if (String(btn.board) == "right") {
      selectedParrot = PARROT_RIGHT;
      statusMessage = "Speaker Parrot: RIGHT";
    } else if (String(btn.board) == "both") {
      selectedParrot = PARROT_BOTH;
      statusMessage = "Speaker Parrot: BOTH";
    }
    Serial.print("[Touch 7\"] Parrot Selected: ");
    Serial.println(btn.board);
  } else if (String(btn.type) == "mic_toggle") {
    micReactivityEnabled = !micReactivityEnabled;
    statusMessage = micReactivityEnabled ? "Mic React: ENABLED" : "Mic React: DISABLED";
    Serial.print("[Touch 7\"] Mic Sound Reactivity: ");
    Serial.println(micReactivityEnabled ? "ENABLED" : "DISABLED");
  }
}

void processTouchInput() {
  int tx, ty;
  if (readTouch(tx, ty)) {
    if (!isTouchActive || (millis() - lastTouchTime > 220)) {
      isTouchActive = true;
      lastTouchTime = millis();
      touchX = tx;
      touchY = ty;

      for (int i = 0; i < NUM_DASHBOARD_BTNS; i++) {
        if (tx >= DASHBOARD_BTNS[i].x && tx <= (DASHBOARD_BTNS[i].x + DASHBOARD_BTNS[i].w) &&
            ty >= DASHBOARD_BTNS[i].y && ty <= (DASHBOARD_BTNS[i].y + DASHBOARD_BTNS[i].h)) {
          handleTouchAction(i);
          break;
        }
      }
    }
  } else {
    isTouchActive = false;
  }
}

// =============================================================================
// Dynamic Animations Engine (Parrot Mascot, Beams, Audio Waves)
// =============================================================================
void updateAnimations() {
  unsigned long now = millis();
  if (now - lastAnimUpdate < 33) return; // ~30 FPS animation loop
  float dt = (now - lastAnimUpdate) / 1000.0f;
  lastAnimUpdate = now;

  // 1. Mascot Eye Blinking Logic & Idle Natural Eye Blinks
  if (now - lastBlinkTime > 3500) {
    eyeBlinkState = true;
    if (!isSpeechActive && currentRoutine == "idle") {
      writeMCPPin(0, 1, true);  // L Parrot Eyes ON briefly
      writeMCPPin(0, 12, true); // R Parrot Eyes ON briefly
    }
    if (now - lastBlinkTime > 3700) {
      eyeBlinkState = false;
      if (!isSpeechActive && currentRoutine == "idle") {
        writeMCPPin(0, 1, false);
        writeMCPPin(0, 12, false);
      }
      lastBlinkTime = now;
    }
  }

  // 2. Beak / Mouth decay towards closed when idle
  if (currentRoutine == "idle" && mouthOpenPercent > 0) {
    mouthOpenPercent -= 5;
    if (mouthOpenPercent < 0) mouthOpenPercent = 0;
  }

  // 3. Head Bob & Wing Flap Oscillations
  float t = now * 0.005f;
  headBobOffset = sin(t * 1.5f) * 6.0f;
  wingFlapAngle = cos(t * 3.0f) * 15.0f;

  // 4. Audio Spectrum Bars Animation
  for (int i = 0; i < 8; i++) {
    if (currentRoutine == "sing" || mouthOpenPercent > 30) {
      audioSpectrumBars[i] = fabs(sin(t * 2.0f + i * 0.8f)) * 40.0f + 10.0f;
    } else {
      audioSpectrumBars[i] = audioSpectrumBars[i] * 0.85f;
    }
  }

  // 5. Floating Music Notes Update
  for (int i = 0; i < 3; i++) {
    if (musicNoteActive[i]) {
      musicNoteY[i] -= 2;
      musicNoteX[i] += (int)(sin(now * 0.008f + i) * 2);
      if (musicNoteY[i] < 60) {
        musicNoteY[i] = 230;
        if (currentRoutine != "sing") musicNoteActive[i] = false;
      }
    }
  }
}

// =============================================================================
// Live Telemetry Broadcast
// =============================================================================
unsigned long lastTelemetryBroadcast = 0;
void printLiveTelemetry() {
  if (millis() - lastTelemetryBroadcast < 1000) return;
  lastTelemetryBroadcast = millis();

  Serial.print("[7\" Touch-LCD Telemetry] Parrot: ");
  Serial.print(selectedParrot == PARROT_LEFT ? "LEFT" : (selectedParrot == PARROT_RIGHT ? "RIGHT" : "BOTH"));
  Serial.print(" | Mic React: ");
  Serial.print(micReactivityEnabled ? "ON" : "OFF");
  Serial.print(" | Speech: ");
  Serial.print(isSpeechActive ? "TALKING" : "IDLE");
  Serial.print(" | MCP: 0x");
  Serial.print(mcpPrimaryLatch, HEX);
  Serial.print(" | Servos L[0..7]: (");
  for (int i = 0; i < 8; i++) {
    Serial.print((int)servoCurrentAngles[i]);
    if (i < 7) Serial.print(",");
  }
  Serial.print(") R[0..5]: (");
  for (int i = 0; i < 6; i++) {
    Serial.print((int)servoCurrentAngles[16 + i]);
    if (i < 5) Serial.print(",");
  }
  Serial.print(") | Routine: ");
  Serial.print(currentRoutine);
  Serial.print(" | Status: ");
  Serial.println(statusMessage);
}

// =============================================================================
// Command Parser (USB CDC Serial & Bluetooth)
// =============================================================================
void parseCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  // --- MAX98357A I2S Audio Amplifier Commands ---
  // Formats: "AUDIO:CHIRP", "AUDIO:SQUAWK", "AUDIO:SONG", "AUDIO:SYMPHONY", "AUDIO:TRILL", "AUDIO:BEEP"
  // "AUDIO:TONE:<freq>:<dur>", "AUDIO:SWEEP:<start>:<end>:<dur>", "AUDIO:VOL:<0-100>", "AUDIO:MUTE:<1|0>"
  if (cmd.equalsIgnoreCase("AUDIO:CHIRP") || cmd.equalsIgnoreCase("CHIRP") || cmd.equalsIgnoreCase("PLAY:CHIRP")) {
    playBirdChirpSound();
    Serial.println("[MAX98357A] Played Bird Chirp Sound");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:SQUAWK") || cmd.equalsIgnoreCase("SQUAWK") || cmd.equalsIgnoreCase("PLAY:SQUAWK")) {
    playParrotSquawkSound();
    Serial.println("[MAX98357A] Played Parrot Squawk Sound");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:TRILL") || cmd.equalsIgnoreCase("TRILL") || cmd.equalsIgnoreCase("PLAY:TRILL")) {
    playBirdTrillSound();
    Serial.println("[MAX98357A] Played Bird Trill Sound");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:SONG") || cmd.equalsIgnoreCase("SONG") || cmd.equalsIgnoreCase("PLAY:SONG")) {
    playBirdSongMelody();
    Serial.println("[MAX98357A] Played Songbird Melody");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:SYMPHONY") || cmd.equalsIgnoreCase("SYMPHONY") || cmd.equalsIgnoreCase("PLAY:SYMPHONY")) {
    playBirdSymphonySound();
    Serial.println("[MAX98357A] Played Bird Symphony Fanfare");
    return;
  }
  if (cmd.equalsIgnoreCase("AUDIO:BEEP") || cmd.equalsIgnoreCase("BEEP") || cmd.equalsIgnoreCase("PLAY:BEEP")) {
    playBeepSound();
    Serial.println("[MAX98357A] Played Beep Tone");
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

  // --- 0. PARROT SELECTION & MIC REACTIVITY Commands ---
  // Formats: "PARROT_SEL:<LEFT|RIGHT|BOTH>", "PARROT:<LEFT|RIGHT|BOTH>", "PARROT:L", "PARROT:R", "PARROT:BOTH"
  if (cmd.startsWith("PARROT_SEL:") || cmd.startsWith("parrot_sel:") || cmd.startsWith("PARROT:") || cmd.startsWith("parrot:")) {
    int colonIdx = cmd.indexOf(':');
    String pChoice = cmd.substring(colonIdx + 1);
    pChoice.toUpperCase();
    pChoice.trim();
    if (pChoice == "LEFT" || pChoice == "L") {
      selectedParrot = PARROT_LEFT;
      statusMessage = "Speaker Parrot: LEFT";
      Serial.println("[ESP32-Touch-7] Speaker Parrot set to LEFT");
    } else if (pChoice == "RIGHT" || pChoice == "R") {
      selectedParrot = PARROT_RIGHT;
      statusMessage = "Speaker Parrot: RIGHT";
      Serial.println("[ESP32-Touch-7] Speaker Parrot set to RIGHT");
    } else if (pChoice == "BOTH" || pChoice == "B" || pChoice == "ALL") {
      selectedParrot = PARROT_BOTH;
      statusMessage = "Speaker Parrot: BOTH";
      Serial.println("[ESP32-Touch-7] Speaker Parrot set to BOTH");
    }
    return;
  }

  // "SPEECH_REACT:<1|0|ON|OFF>", "SPEECH:<1|0|ON|OFF>", "MIC_REACT:<1|0|ON|OFF>", "MIC:<1|0|ON|OFF>"
  if (cmd.startsWith("SPEECH_REACT:") || cmd.startsWith("speech_react:") ||
      cmd.startsWith("SPEECH:") || cmd.startsWith("speech:") ||
      cmd.startsWith("MIC_REACT:") || cmd.startsWith("mic_react:") ||
      cmd.startsWith("MIC:") || cmd.startsWith("mic:")) {
    int colonIdx = cmd.indexOf(':');
    String valStr = cmd.substring(colonIdx + 1);
    valStr.toUpperCase();
    valStr.trim();
    micReactivityEnabled = (valStr == "1" || valStr == "ON" || valStr == "TRUE" || valStr == "ENABLE");
    statusMessage = micReactivityEnabled ? "Speech React: ENABLED" : "Speech React: DISABLED";
    Serial.println("[ESP32-Touch-7] Speech Reactivity set to: " + String(micReactivityEnabled ? "ON" : "OFF"));
    return;
  }

  if (cmd.equalsIgnoreCase("SPEECH_TOGGLE") || cmd.equalsIgnoreCase("MIC_TOGGLE")) {
    micReactivityEnabled = !micReactivityEnabled;
    statusMessage = micReactivityEnabled ? "Speech React: ENABLED" : "Speech React: DISABLED";
    Serial.println("[ESP32-Touch-7] Speech Reactivity toggled to: " + String(micReactivityEnabled ? "ON" : "OFF"));
    return;
  }

  // "AI_SPEAKING:<1|0>", "TALK:<1|0>", "TALKING:<1|0>"
  if (cmd.startsWith("AI_SPEAKING:") || cmd.startsWith("ai_speaking:") ||
      cmd.startsWith("TALK:") || cmd.startsWith("talk:") ||
      cmd.startsWith("TALKING:") || cmd.startsWith("talking:")) {
    int colonIdx = cmd.indexOf(':');
    String stateStr = cmd.substring(colonIdx + 1);
    stateStr.toUpperCase();
    stateStr.trim();
    if (stateStr == "1" || stateStr == "ON" || stateStr == "TRUE" || stateStr == "START") {
      lastSoundDetectTime = millis();
      isSpeechActive = true;
      Serial.println("[ESP32-Touch-7] AI Speaking Animatronics -> ACTIVE");
    } else {
      lastSoundDetectTime = 0; // Trigger silence transition in updateSpeechMotionEngine
      Serial.println("[ESP32-Touch-7] AI Speaking Animatronics -> REST");
    }
    return;
  }

  // "MIC_TRIGGER" / "SOUND_DETECT" (Direct pulse trigger for testing)
  if (cmd.equalsIgnoreCase("MIC_TRIGGER") || cmd.equalsIgnoreCase("SOUND_DETECT") || cmd.equalsIgnoreCase("TALK")) {
    lastSoundDetectTime = millis();
    isSpeechActive = true;
    Serial.println("[ESP32-Touch-7] Triggered sound pulse event");
    return;
  }

  // --- 1. SERVO Commands ---
  // Formats:
  // "SERVO:<driver>:<chan>:<deg>" or "SERVO:L:<chan>:<deg>" or "SERVO:R:<chan>:<deg>" or "SERVO:<chan>:<deg>"
  // "S:<driver>:<chan>:<deg>" or "S:<chan>:<deg>"
  if (cmd.startsWith("SERVO:") || cmd.startsWith("servo:") || cmd.startsWith("S:") || cmd.startsWith("s:")) {
    int firstColon = cmd.indexOf(':');
    int secondColon = cmd.indexOf(':', firstColon + 1);
    int thirdColon = (secondColon != -1) ? cmd.indexOf(':', secondColon + 1) : -1;

    if (thirdColon != -1) {
      // 3 colons: SERVO:<board/driver>:<chan>:<deg>
      String boardStr = cmd.substring(firstColon + 1, secondColon);
      int chan = cmd.substring(secondColon + 1, thirdColon).toInt();
      int deg = cmd.substring(thirdColon + 1).toInt();
      uint8_t driverIdx = (boardStr.equalsIgnoreCase("R") || boardStr.equalsIgnoreCase("RIGHT") || boardStr == "1") ? 1 : 0;
      setDriverServoAngle(driverIdx, chan, deg);
      Serial.print("[PCA9685] Driver ");
      Serial.print(driverIdx == 0 ? "Left (0x40)" : "Right (0x41)");
      Serial.print(" Ch ");
      Serial.print(chan);
      Serial.print(" -> ");
      Serial.print(deg);
      Serial.println(" deg");
      return;
    } else if (secondColon != -1) {
      // 2 colons: SERVO:<chan>:<deg>
      int chan = cmd.substring(firstColon + 1, secondColon).toInt();
      int deg = cmd.substring(secondColon + 1).toInt();
      if (chan >= 16) {
        setDriverServoAngle(1, chan - 16, deg);
      } else {
        setDriverServoAngle(0, chan, deg);
      }
      Serial.print("[PCA9685] Servo Global Channel ");
      Serial.print(chan);
      Serial.print(" -> ");
      Serial.print(deg);
      Serial.println(" deg");
      return;
    }
  }

  // --- 2. ROUTINE Commands ---
  // Formats: "ROUTINE:<name>" or "R:<name>" (e.g. "ROUTINE:SING", "ROUTINE:SWEEP", "ROUTINE:HOME")
  if (cmd.startsWith("ROUTINE:") || cmd.startsWith("routine:")) {
    String rName = cmd.substring(cmd.indexOf(':') + 1);
    rName.toLowerCase();
    startRoutine(rName);
    return;
  }

  // --- 3. Targeted Board GPIO Commands ---
  // Formats: "L:<pin>:<state>", "R:<pin>:<state>", "L:<pin>", "R:<pin>", "LEFT:...", "RIGHT:..."
  if (cmd.startsWith("L:") || cmd.startsWith("l:") || cmd.startsWith("LEFT:") || cmd.startsWith("left:") ||
      cmd.startsWith("R:") || cmd.startsWith("r:") || cmd.startsWith("RIGHT:") || cmd.startsWith("right:")) {
    String board = (cmd.startsWith("R") || cmd.startsWith("r")) ? "right" : "left";
    int firstColon = cmd.indexOf(':');
    int secondColon = cmd.indexOf(':', firstColon + 1);

    if (secondColon != -1) {
      int pin = cmd.substring(firstColon + 1, secondColon).toInt();
      String subCmd = cmd.substring(secondColon + 1);
      subCmd.toUpperCase();

      if (subCmd == "1" || subCmd == "ON" || subCmd == "HIGH") {
        setBirdOutputState(board, pin, 1);
        Serial.println("[ESP32-Touch-7] " + board + " pin " + String(pin) + " -> ON");
      } else if (subCmd == "0" || subCmd == "OFF" || subCmd == "LOW") {
        setBirdOutputState(board, pin, 0);
        Serial.println("[ESP32-Touch-7] " + board + " pin " + String(pin) + " -> OFF");
      } else if (subCmd == "PULSE") {
        pulseBirdOutput(board, pin, 300);
        Serial.println("[ESP32-Touch-7] " + board + " pin " + String(pin) + " -> PULSED");
      }
      return;
    } else {
      // Toggle
      int pin = cmd.substring(firstColon + 1).toInt();
      bool cur = getBirdOutputState(board, pin);
      setBirdOutputState(board, pin, cur ? 0 : 1);
      Serial.println("[ESP32-Touch-7] " + board + " pin " + String(pin) + " -> " + (cur ? "OFF" : "ON"));
      return;
    }
  }

  // --- 4. MCP Direct Commands ---
  // Formats: "MCP:<pin>:<state>" or "IO:<pin>:<state>" (0-15)
  if (cmd.startsWith("MCP:") || cmd.startsWith("mcp:") || cmd.startsWith("IO:") || cmd.startsWith("io:")) {
    int firstColon = cmd.indexOf(':');
    int secondColon = cmd.indexOf(':', firstColon + 1);
    if (secondColon != -1) {
      int pinBit = cmd.substring(firstColon + 1, secondColon).toInt();
      int st = cmd.substring(secondColon + 1).toInt();
      writeMCPPin(0, pinBit, st == 1);
      Serial.println("[MCP23017] Pin " + String(pinBit) + " -> " + String(st));
      return;
    }
  }

  // --- 5. Legacy Generic Pin Commands ---
  // Formats: "<pin>:<state>" or "<pin>"
  int colonIdx = cmd.indexOf(':');
  if (colonIdx != -1) {
    int targetPin = cmd.substring(0, colonIdx).toInt();
    String subCmd = cmd.substring(colonIdx + 1);
    subCmd.toUpperCase();

    if (subCmd == "1" || subCmd == "ON") {
      setBirdOutputState("left", targetPin, 1);
      Serial.println("[ESP32-Touch-7] Pin " + String(targetPin) + " -> ON");
    } else if (subCmd == "0" || subCmd == "OFF") {
      setBirdOutputState("left", targetPin, 0);
      Serial.println("[ESP32-Touch-7] Pin " + String(targetPin) + " -> OFF");
    } else if (subCmd == "PULSE") {
      pulseBirdOutput("left", targetPin, 300);
      Serial.println("[ESP32-Touch-7] Pin " + String(targetPin) + " -> PULSED");
    }
  } else {
    // Single number command (legacy toggle or '1'/'0')
    if (cmd == "1") {
      setBirdOutputState("left", 2, 1); // Builtin Body/LED ON
      Serial.println("[ESP32-Touch-7] Body LED -> ON");
    } else if (cmd == "0") {
      setBirdOutputState("left", 2, 0); // Builtin Body/LED OFF
      Serial.println("[ESP32-Touch-7] Body LED -> OFF");
    } else {
      int targetPin = cmd.toInt();
      bool cur = getBirdOutputState("left", targetPin);
      setBirdOutputState("left", targetPin, cur ? 0 : 1);
      Serial.println("[ESP32-Touch-7] Toggled Pin " + String(targetPin) + " -> " + (cur ? "OFF" : "ON"));
    }
  }
}

// =============================================================================
// Setup & Main Loop
// =============================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {}

  #if HAS_BT_CLASSIC
  SerialBT.begin("esp32-touch-lcd-birds");
  #endif

  // Initialize I2C Bus for GT911, MCP23017, and Dual PCA9685
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000); // 400kHz Fast Mode

  // Initialize Direct GPIO Mic Sound Sensor Input Pin
  pinMode(MIC_SENSOR_GPIO_PIN, INPUT_PULLUP);

  // Initialize Servo default angles to 90 degrees
  for (int i = 0; i < TOTAL_SERVOS; i++) {
    servoCurrentAngles[i] = 90.0f;
    servoStartAngles[i]   = 90.0f;
    servoTargetAngles[i]  = 90.0f;
  }

  // Initialize Peripherals
  initIOExpander7C(); // Enable CH422G Backlight, Power & Touch Reset

#if HAS_ARDUINO_GFX
  if (gfx) {
    gfx->begin();
    gfx->fillScreen(0x0000);
    gfx->setTextColor(0xFFFF);
    gfx->setTextSize(2);
    gfx->setCursor(20, 20);
    gfx->println("Waveshare Birds Controller LCD Online");
    gfx->setTextSize(1);
    gfx->setCursor(20, 50);
    gfx->println("16-Bit RGB Panel Driver Active (800x600)");
    Serial.println("[RGB Display] Arduino_GFX RGB Panel Driver Initialized Successfully.");
  }
#endif
  initI2SAudio();
  initMCP23017();
  initPCA9685Drivers();
  initTouchController();

  // Set all PCA9685 servos to 90 degrees default
  for (uint8_t d = 0; d < TOTAL_PCA_DRIVERS; d++) {
    for (uint8_t ch = 0; ch < SERVOS_PER_DRIVER; ch++) {
      writeHardwareServo(d, ch, 90.0f);
    }
  }

  // Play startup greeting chirp
  playBirdChirpSound();

  Serial.println("==========================================================");
  Serial.println("🦜 Waveshare ESP32-S3-Touch-LCD-7C Birds & LED Controller");
  Serial.println("Display: 800x600 HD Widescreen RGB | Touch: GT911 Capacitive");
  Serial.println("IO Expander: CH422G (Backlight EXIO2, Power EXIO6, Touch RST EXIO1)");
  Serial.println("==========================================================");
  Serial.print("MAX98357A I2S Audio: ");
  Serial.println(i2sAudioReady ? "READY (BCLK=19, LRC=20, DIN=21)" : "INIT FAILED");
  Serial.print("MCP23017 Primary (0x20): ");
  Serial.println(mcpPrimaryPresent ? "CONNECTED" : "NOT DETECTED");
  Serial.print("MCP23017 Secondary (0x21): ");
  Serial.println(mcpSecPresent ? "CONNECTED" : "NOT DETECTED");
  Serial.print("PCA9685 Left (0x40): ");
  Serial.println(pcaLeftPresent ? "CONNECTED" : "NOT DETECTED");
  Serial.print("PCA9685 Right (0x41): ");
  Serial.println(pcaRightPresent ? "CONNECTED" : "NOT DETECTED");
  Serial.println("GT911 Capacitive Touch (0x5D): READY");
  Serial.println("Mic Sound Detection Sensor: GPIO 7 & MCP GPA4 READY");
  Serial.print("Active Speaker Parrot: ");
  Serial.println(selectedParrot == PARROT_LEFT ? "LEFT" : (selectedParrot == PARROT_RIGHT ? "RIGHT" : "BOTH"));
  Serial.println("Speech Animatronics Engine: READY (Mouth/Wings/3 Servos/Spotlight/Turntable)");
  Serial.println("Animations Engine: READY (30 FPS)");
  Serial.println("==========================================================");
}

void loop() {
  // 1. Process USB Serial Commands
  if (Serial.available() > 0) {
    String inputStr = Serial.readStringUntil('\n');
    parseCommand(inputStr);
  }

  #if HAS_BT_CLASSIC
  // Process Bluetooth Commands
  if (SerialBT.available() > 0) {
    String btStr = SerialBT.readStringUntil('\n');
    parseCommand(btStr);
  }
  #endif

  // 2. Process Capacitive Touchscreen Interactions
  processTouchInput();

  // 3. Update Real-Time Speech Motion & Animatronics Engine (Mouth, Wings, 3 Servos, Spotlights, Turntable)
  updateSpeechMotionEngine();

  // 4. Update Trajectory & Motion Interpolation Engine (50Hz)
  updateServoTrajectoryEngine();

  // 5. Update Non-blocking Output Pulse Timers
  updatePulseTimers();

  // 6. Update Choreography & Routine State Machine
  updateRoutines();

  // 7. Update Dynamic Animations (Mascot Eyes, Beak, Spectrum, Beams)
  updateAnimations();

  // 8. Output Live Telemetry Stream
  printLiveTelemetry();
}
