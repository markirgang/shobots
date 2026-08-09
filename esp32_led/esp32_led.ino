/*
  Hexapod Controller - ESP32 Controller Firmware (Dual Board & Robot Arm Compatible)
  
  This sketch runs on any ESP32 board (Left, Right, or Arm) for the Hexapod Controller project.
  It configures output GPIO pins and controls PCA9685 16-channel I2C Servo Drivers.

    GPIO 0  : Parrot Mouth
    GPIO 1  : Parrot Eyes
    GPIO 2  : Parrot Body
    GPIO 3  : Parrot Light
    GPIO 4  : Parrot Mouth Select
    GPIO 5  : Rear Bird Rear Move
    GPIO 12 : Rear Bird Rear Light
    GPIO 13 : Front Bird Move
    GPIO 14 : Front Bird Light
    GPIO 15 : Bird Front Chirp
    GPIO 16 : Center Bird Move

  PCA9685 I2C Connection:
    SDA: GPIO 21
    SCL: GPIO 22
    I2C Address: 0x40

  Serial Baud Rate: 115200

  Command Protocol (sent over USB Serial from Thinker Window):
    - "<gpio>"              (e.g., "12")         -> Toggles the current state of GPIO 12.
    - "<gpio>:1"            (e.g., "12:1")       -> Turns GPIO 12 HIGH (ON).
    - "<gpio>:0"            (e.g., "12:0")       -> Turns GPIO 12 LOW (OFF).
    - "<gpio>:PULSE"        (e.g., "12:PULSE")   -> Pulses GPIO 12 HIGH for 300ms, then LOW.
    - "SERVO:<chan>:<deg>"  (e.g., "SERVO:0:90") -> Sets PCA9685 servo channel <chan> to <deg> degrees (0-180).
    - "S:<chan>:<deg>"      (e.g., "S:0:90")     -> Short form of SERVO command.
    - "1"                   (Legacy)             -> Turns GPIO 2 (Body / LED) HIGH.
    - "0"                   (Legacy)             -> Turns GPIO 2 (Body / LED) LOW.
*/

#include <Arduino.h>
#include <Wire.h>

// PCA9685 Definitions
#define PCA9685_I2C_ADDR 0x40
#define MODE1 0x00
#define PRESCALE 0xFE
#define LED0_ON_L 0x06

// Servo Pulse Constants (50Hz PWM, 4096 steps per 20ms)
#define SERVOMIN 150  // Min pulse length out of 4096 (0 deg)
#define SERVOMAX 600  // Max pulse length out of 4096 (180 deg)

// All 11 active GPIO pins for the Birds functions
const int NUM_PINS = 11;
const int PROJECT_PINS[NUM_PINS] = {0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16};

// Track current state for each pin (LOW = 0, HIGH = 1)
int pinStates[128]; 
int servoAngles[16];

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
  Wire.begin(21, 22); // Standard ESP32 SDA=21, SCL=22
  writePCA9685(MODE1, 0x00);
  delay(10);
  
  // Set frequency to 50Hz for standard servos
  // prescale = round(25MHz / (4096 * 50Hz)) - 1 = 121
  writePCA9685(MODE1, 0x10); // Sleep mode to set prescale
  writePCA9685(PRESCALE, 121);
  writePCA9685(MODE1, 0x00); // Wake up
  delay(5);
  writePCA9685(MODE1, 0xa1); // Auto-increment mode enabled
}

void setServoDegree(uint8_t channel, int angle) {
  if (channel > 15) return;
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  
  servoAngles[channel] = angle;
  uint16_t pulse = map(angle, 0, 180, SERVOMIN, SERVOMAX);
  setPCA9685PWM(channel, 0, pulse);
  Serial.print("[PCA9685] Servo Channel ");
  Serial.print(channel);
  Serial.print(" -> ");
  Serial.print(angle);
  Serial.println(" deg");
}

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
    // Wait for serial monitor / connection
  }

  // Initialize pin states array & servo angles
  for (int i = 0; i < 128; i++) {
    pinStates[i] = LOW;
  }
  for (int i = 0; i < 16; i++) {
    servoAngles[i] = 90; // Default 90 degrees
  }

  // Configure project GPIO pins as OUTPUTs (skipping UART0 USB Serial RX/TX pins 1 and 3)
  for (int i = 0; i < NUM_PINS; i++) {
    int pin = PROJECT_PINS[i];
    if (pin == 1 || pin == 3) {
      continue;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    pinStates[pin] = LOW;
  }

  // Initialize PCA9685 Servo Driver and set all channels to default 90 degrees
  initPCA9685();
  for (uint8_t i = 0; i < 16; i++) {
    setServoDegree(i, 90);
  }

  Serial.println("==========================================");
  Serial.println("🦜 Birds Project ESP32 Firmware Ready!");
  Serial.println("Configured GPIOs: 0, 2, 4, 5, 12, 13, 14, 15, 16");
  Serial.println("PCA9685 I2C Driver: Active (Channels 0-15 defaulted to 90 deg)");
  Serial.println("Commands:");
  Serial.println("  '<gpio>'           -> Toggle GPIO");
  Serial.println("  '<gpio>:1' / ':0'  -> Set GPIO HIGH/LOW");
  Serial.println("  'SERVO:<chan>:<deg>' -> Set PCA9685 Servo Angle (0-180)");
  Serial.println("==========================================");
}

void loop() {
  if (Serial.available() > 0) {
    String inputStr = Serial.readStringUntil('\n');
    inputStr.trim();

    if (inputStr.length() == 0) return;

    // Handle SERVO commands: "SERVO:<chan>:<deg>" or "S:<chan>:<deg>"
    if (inputStr.startsWith("SERVO:") || inputStr.startsWith("servo:") || inputStr.startsWith("S:") || inputStr.startsWith("s:")) {
      int firstColon = inputStr.indexOf(':');
      int secondColon = inputStr.indexOf(':', firstColon + 1);
      if (firstColon != -1 && secondColon != -1) {
        int chan = inputStr.substring(firstColon + 1, secondColon).toInt();
        int deg = inputStr.substring(secondColon + 1).toInt();
        setServoDegree(chan, deg);
        return;
      }
    }

    // Handle legacy single-character commands ('1' / '0') for GPIO 2
    if (inputStr == "1") {
      digitalWrite(2, HIGH);
      pinStates[2] = HIGH;
      Serial.println("[ESP32] GPIO 2 -> HIGH (ON)");
      return;
    } else if (inputStr == "0") {
      digitalWrite(2, LOW);
      pinStates[2] = LOW;
      Serial.println("[ESP32] GPIO 2 -> LOW (OFF)");
      return;
    }

    // Parse "<gpio>:<cmd>" or "<gpio>"
    int colonIdx = inputStr.indexOf(':');
    int targetPin = -1;
    String subCmd = "";

    if (colonIdx != -1) {
      targetPin = inputStr.substring(0, colonIdx).toInt();
      subCmd = inputStr.substring(colonIdx + 1);
      subCmd.toUpperCase();
    } else {
      targetPin = inputStr.toInt();
    }

    // Prevent overriding UART0 Serial RX/TX pins
    if (targetPin == 1 || targetPin == 3) {
      Serial.print("[ESP32] Notice: GPIO ");
      Serial.print(targetPin);
      Serial.println(" is reserved for USB Serial (RX/TX) and cannot be toggled as a digital output.");
      return;
    }

    // Validate GPIO pin number
    bool isValidPin = false;
    for (int i = 0; i < NUM_PINS; i++) {
      if (PROJECT_PINS[i] == targetPin) {
        isValidPin = true;
        break;
      }
    }

    if (!isValidPin) {
      Serial.print("[ESP32] Warning: GPIO ");
      Serial.print(targetPin);
      Serial.println(" is not in configured project pins list.");
      if (targetPin < 0 || targetPin > 39) {
        return;
      }
      pinMode(targetPin, OUTPUT);
    }

    // Execute requested command on targetPin
    if (subCmd == "1" || subCmd == "ON" || subCmd == "HIGH") {
      digitalWrite(targetPin, HIGH);
      pinStates[targetPin] = HIGH;
      Serial.print("[ESP32] GPIO ");
      Serial.print(targetPin);
      Serial.println(" -> HIGH (ON)");
    } 
    else if (subCmd == "0" || subCmd == "OFF" || subCmd == "LOW") {
      digitalWrite(targetPin, LOW);
      pinStates[targetPin] = LOW;
      Serial.print("[ESP32] GPIO ");
      Serial.print(targetPin);
      Serial.println(" -> LOW (OFF)");
    } 
    else if (subCmd == "PULSE") {
      Serial.print("[ESP32] Pulsing GPIO ");
      Serial.println(targetPin);
      digitalWrite(targetPin, HIGH);
      delay(300);
      digitalWrite(targetPin, LOW);
      pinStates[targetPin] = LOW;
      Serial.print("[ESP32] GPIO ");
      Serial.print(targetPin);
      Serial.println(" -> PULSED (OFF)");
    } 
    else {
      // Default behavior for plain pin number: Toggle pin state
      int newState = (pinStates[targetPin] == LOW) ? HIGH : LOW;
      digitalWrite(targetPin, newState);
      pinStates[targetPin] = newState;

      Serial.print("[ESP32] Toggled GPIO ");
      Serial.print(targetPin);
      Serial.print(" -> ");
      Serial.println(newState == HIGH ? "HIGH (ON)" : "LOW (OFF)");
    }
  }
}

