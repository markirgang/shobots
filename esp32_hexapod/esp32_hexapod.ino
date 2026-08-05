/*
  Shobots Controller - ESP32 6-Leg Hexapod Firmware
  Features:
    - Bluetooth Classic Serial Interface (Broadcast Name: "hexapod")
    - Dual PCA9685 16-Channel I2C Servo Driver Control:
        * Driver 1 (I2C 0x40): Left Legs (FL, ML, RL)
        * Driver 2 (I2C 0x41): Right Legs (FR, MR, RR)
    - 3-DOF Inverse Kinematics (IK) Solver for (X, Y, Z) Cartesian Foot Positioning
    - Coordinated Speed & Trajectory Interpolation Engine (50Hz S-Curve Interpolation)

  Pinout:
    - I2C SDA : GPIO 21
    - I2C SCL : GPIO 22
    - Serial Baud : 115200 (USB & Bluetooth)

  Channel Mapping per Driver:
    Driver 1 (0x40 - Left):
      FL (Front Left)  : Coxa=0, Femur=1, Tibia=2
      ML (Middle Left) : Coxa=3, Femur=4, Tibia=5
      RL (Rear Left)   : Coxa=6, Femur=7, Tibia=8
    Driver 2 (0x41 - Right):
      FR (Front Right) : Coxa=0, Femur=1, Tibia=2
      MR (Middle Right): Coxa=3, Femur=4, Tibia=5
      RR (Rear Right)  : Coxa=6, Femur=7, Tibia=8

  Command Protocol (Serial / Bluetooth):
    - "HEX:SERVO:<driver>:<chan>:<deg>"  -> Sets joint angle directly (0-180)
    - "HEX:IK:<leg>:<X>:<Y>:<Z>:<ms>"    -> Sets Cartesian foot position using IK
    - "HEX:SPEED:<ms>"                   -> Sets global trajectory duration (default 200ms)
    - "HEX:stand"                        -> Stand posture
    - "HEX:sit"                          -> Sit posture
    - "HEX:flat"                         -> Flat posture
    - "HEX:walk"                         -> Tripod walk gait
    - "HEX:run"                          -> Fast tripod gait
    - "HEX:dance"                        -> Dance routine
    - "HEX:bow"                          -> Bow routine
    - "HEX:wave_left" / "HEX:wave_right" -> Wave arm routine
    - "HEX:turn_left" / "HEX:turn_right" -> Rotate in place
    - "HEX:stop"                         -> Stop current motion / return to stand
*/

#include <Arduino.h>
#include <Wire.h>
#include <BluetoothSerial.h>
#include <math.h>

// Bluetooth Serial Instance
BluetoothSerial SerialBT;

// PCA9685 Definitions
#define PCA9685_ADDR_LEFT  0x40 // Driver 1 (Left Legs FL, ML, RL)
#define PCA9685_ADDR_RIGHT 0x41 // Driver 2 (Right Legs FR, MR, RR)

#define MODE1        0x00
#define PRESCALE     0xFE
#define LED0_ON_L    0x06

// Servo Pulse Constants (50Hz PWM, 4096 steps per 20ms)
#define SERVOMIN     150  // Min pulse length out of 4096 (0 deg)
#define SERVOMAX     600  // Max pulse length out of 4096 (180 deg)

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

// Active Gait Motion state
String currentGait = "idle";
unsigned long lastGaitStepTime = 0;
int gaitStepIndex = 0;

// Leg Code to Servo Index Map
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

// Low-level PCA9685 I2C Write Functions
void writePCA9685(uint8_t addr, uint8_t reg, uint8_t data) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

void setPCA9685PWM(uint8_t addr, uint8_t channel, uint16_t on, uint16_t off) {
  Wire.beginTransmission(addr);
  Wire.write(LED0_ON_L + 4 * channel);
  Wire.write(on & 0xFF);
  Wire.write(on >> 8);
  Wire.write(off & 0xFF);
  Wire.write(off >> 8);
  Wire.endTransmission();
}

void initPCA9685(uint8_t addr) {
  writePCA9685(addr, MODE1, 0x00);
  delay(10);
  // Set frequency to 50Hz for standard servos (prescale = 121)
  writePCA9685(addr, MODE1, 0x10); // Sleep mode
  writePCA9685(addr, PRESCALE, 121);
  writePCA9685(addr, MODE1, 0x00); // Wake up
  delay(5);
  writePCA9685(addr, MODE1, 0xA1); // Auto-increment
}

void writeHardwareAngle(int servoIndex, float angle) {
  angle = constrain(angle, 0.0f, 180.0f);
  uint8_t addr = (servoIndex < 9) ? PCA9685_ADDR_LEFT : PCA9685_ADDR_RIGHT;
  uint8_t channel = servoIndex % 9;
  
  uint16_t pulse = map((long)angle, 0, 180, SERVOMIN, SERVOMAX);
  setPCA9685PWM(addr, channel, 0, pulse);
}

// -------------------------------------------------------------
// 3-DOF Inverse Kinematics (IK) Solver
// -------------------------------------------------------------
// Computes coxa, femur, and tibia angles given foot Cartesian coordinates (x, y, z)
// x: Forward/Backward displacement relative to hip
// y: Outward displacement relative to hip
// z: Downward vertical displacement relative to hip
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

// -------------------------------------------------------------
// Trajectory & Motion Interpolation Engine (50Hz Update Loop)
// -------------------------------------------------------------
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

// -------------------------------------------------------------
// Preset Postures & Motion Routines
// -------------------------------------------------------------
void applyStandPosture(unsigned long durationMs = 300) {
  float newTargets[TOTAL_SERVOS];
  for (int leg = 0; leg < 6; leg++) {
    newTargets[getServoIndex(leg, 0)] = 90.0f; // Coxa centered
    newTargets[getServoIndex(leg, 1)] = 90.0f; // Femur parallel
    newTargets[getServoIndex(leg, 2)] = 90.0f; // Tibia perpendicular
  }
  setTargetAngles(newTargets, durationMs);
}

void applySitPosture(unsigned long durationMs = 400) {
  float newTargets[TOTAL_SERVOS];
  for (int leg = 0; leg < 6; leg++) {
    newTargets[getServoIndex(leg, 0)] = 90.0f;
    newTargets[getServoIndex(leg, 1)] = 30.0f;
    newTargets[getServoIndex(leg, 2)] = 150.0f;
  }
  setTargetAngles(newTargets, durationMs);
}

void applyFlatPosture(unsigned long durationMs = 400) {
  float newTargets[TOTAL_SERVOS];
  for (int leg = 0; leg < 6; leg++) {
    newTargets[getServoIndex(leg, 0)] = 90.0f;
    newTargets[getServoIndex(leg, 1)] = 0.0f;
    newTargets[getServoIndex(leg, 2)] = 0.0f;
  }
  setTargetAngles(newTargets, durationMs);
}

// Tripod Gait Step Engine
void updateGaitEngine() {
  if (currentGait == "idle" || currentGait == "stop") return;
  if (isMoving) return; // Wait for current trajectory to complete smoothly

  unsigned long now = millis();
  
  if (currentGait == "walk" || currentGait == "run") {
    int stepDuration = (currentGait == "run") ? 120 : 220;
    
    // Group A: FL (0), MR (4), RL (2)
    // Group B: FR (3), ML (1), RR (5)
    float targets[TOTAL_SERVOS];
    memcpy(targets, targetAngles, sizeof(targets));
    
    if (gaitStepIndex == 0) {
      // Step 1: Lift Group A, Push Group B backward
      targets[getServoIndex(0, 1)] = 120.0f; // FL Femur lift
      targets[getServoIndex(4, 1)] = 120.0f; // MR Femur lift
      targets[getServoIndex(2, 1)] = 120.0f; // RL Femur lift
      targets[getServoIndex(0, 0)] = 120.0f; // FL Coxa forward
      targets[getServoIndex(4, 0)] = 120.0f; // MR Coxa forward
      targets[getServoIndex(2, 0)] = 120.0f; // RL Coxa forward
      
      targets[getServoIndex(3, 0)] = 60.0f;  // FR Coxa back
      targets[getServoIndex(1, 0)] = 60.0f;  // ML Coxa back
      targets[getServoIndex(5, 0)] = 60.0f;  // RR Coxa back
      gaitStepIndex = 1;
    } else if (gaitStepIndex == 1) {
      // Step 2: Lower Group A to ground
      targets[getServoIndex(0, 1)] = 90.0f;
      targets[getServoIndex(4, 1)] = 90.0f;
      targets[getServoIndex(2, 1)] = 90.0f;
      gaitStepIndex = 2;
    } else if (gaitStepIndex == 2) {
      // Step 3: Lift Group B, Push Group A backward
      targets[getServoIndex(3, 1)] = 120.0f; // FR Femur lift
      targets[getServoIndex(1, 1)] = 120.0f; // ML Femur lift
      targets[getServoIndex(5, 1)] = 120.0f; // RR Femur lift
      targets[getServoIndex(3, 0)] = 120.0f; // FR Coxa forward
      targets[getServoIndex(1, 0)] = 120.0f; // ML Coxa forward
      targets[getServoIndex(5, 0)] = 120.0f; // RR Coxa forward
      
      targets[getServoIndex(0, 0)] = 60.0f;  // FL Coxa back
      targets[getServoIndex(4, 0)] = 60.0f;  // MR Coxa back
      targets[getServoIndex(2, 0)] = 60.0f;  // RL Coxa back
      gaitStepIndex = 3;
    } else {
      // Step 4: Lower Group B to ground
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

// -------------------------------------------------------------
// Command Parser (Serial & Bluetooth)
// -------------------------------------------------------------
void parseCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.print("[Hexapod CMD Received] ");
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
        Serial.print("[Hexapod] Driver ");
        Serial.print(driver);
        Serial.print(" Chan ");
        Serial.print(chan);
        Serial.print(" -> ");
        Serial.print(deg);
        Serial.println(" deg (Interpolated)");
      }
    }
    return;
  }

  // 2. Inverse Kinematics Command: "HEX:IK:<leg>:<X>:<Y>:<Z>:<ms>"
  if (cmd.startsWith("HEX:IK:") || cmd.startsWith("hex:ik:")) {
    // Parsing HEX:IK:FL:0:50:80:300
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
        Serial.print("[IK Success] ");
        Serial.print(legCode);
        Serial.print(" -> Coxa: "); Serial.print(coxaDeg);
        Serial.print(" Femur: "); Serial.print(femurDeg);
        Serial.print(" Tibia: "); Serial.println(tibiaDeg);
      } else {
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
      Serial.print("[Hexapod] Global Trajectory Speed set to ");
      Serial.print(moveDurationMs);
      Serial.println(" ms");
    }
    return;
  }

  // 4. Action & Gait Commands: "HEX:stand", "HEX:walk", etc.
  if (cmd == "HEX:stand" || cmd == "hex:stand") {
    currentGait = "idle";
    applyStandPosture();
  } else if (cmd == "HEX:sit" || cmd == "hex:sit") {
    currentGait = "idle";
    applySitPosture();
  } else if (cmd == "HEX:flat" || cmd == "hex:flat") {
    currentGait = "idle";
    applyFlatPosture();
  } else if (cmd == "HEX:walk" || cmd == "hex:walk") {
    currentGait = "walk";
    gaitStepIndex = 0;
  } else if (cmd == "HEX:run" || cmd == "hex:run") {
    currentGait = "run";
    gaitStepIndex = 0;
  } else if (cmd == "HEX:dance" || cmd == "hex:dance") {
    currentGait = "dance";
    gaitStepIndex = 0;
  } else if (cmd == "HEX:stop" || cmd == "hex:stop") {
    currentGait = "idle";
    applyStandPosture(200);
  }
}

// -------------------------------------------------------------
// Arduino Setup & Main Loop
// -------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000);

  Serial.println("==============================================");
  Serial.println("🕷️ ESP32 6-Leg Hexapod Firmware Initializing...");
  Serial.println("Features: Bluetooth Classic + IK + 50Hz Interpolation");
  Serial.println("==============================================");

  // Initialize Bluetooth Classic Serial
  if (SerialBT.begin("hexapod")) {
    Serial.println("[Bluetooth] ESP32 Broadcasting as 'hexapod' READY!");
  } else {
    Serial.println("[Bluetooth] Failed to start Bluetooth Serial!");
  }

  // Initialize I2C Bus and PCA9685 Drivers
  Wire.begin(21, 22); // SDA=21, SCL=22
  initPCA9685(PCA9685_ADDR_LEFT);
  initPCA9685(PCA9685_ADDR_RIGHT);

  // Initialize default angles to standing posture (90 deg per servo)
  for (int i = 0; i < TOTAL_SERVOS; i++) {
    currentAngles[i] = 90.0f;
    startAngles[i]   = 90.0f;
    targetAngles[i]  = 90.0f;
    writeHardwareAngle(i, 90.0f);
  }

  Serial.println("[Hexapod] Drivers 0x40 & 0x41 Initialized to 90 Deg Stand Posture.");
  Serial.println("==============================================");
}

void loop() {
  // 1. Process USB Serial Commands
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    parseCommand(cmd);
  }

  // 2. Process Bluetooth Commands
  if (SerialBT.available() > 0) {
    String cmd = SerialBT.readStringUntil('\n');
    parseCommand(cmd);
  }

  // 3. Update 50Hz S-Curve Trajectory Interpolation Engine
  updateTrajectoryEngine();

  // 4. Update Gait Motion Step Loop
  updateGaitEngine();

  delay(2); // Short loop pause for CPU efficiency
}
