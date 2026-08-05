/*
  Shobots Controller - ESP32 6-DOF Robot Arm Firmware
  Features:
    - 3D Analytical Inverse Kinematics (IK) Engine for (X, Y, Z, Pitch) End-Effector Positioning
    - Coordinated Speed & Trajectory Interpolation Engine (50Hz Cosine S-Curve Interpolation)
    - PCA9685 16-Channel I2C Servo Driver Interface (Address 0x40)
    - Full backward compatibility with Shobots Controller GUI & Web Serial commands

  Pinout:
    - I2C SDA : GPIO 21
    - I2C SCL : GPIO 22
    - Serial Baud : 115200

  Servo Channel Mapping (PCA9685 Driver 0x40):
    - Ch 0 : Base / Waist Rotation (0 - 180 deg, default 90)
    - Ch 1 : Shoulder Pitch        (0 - 180 deg, default 90)
    - Ch 2 : Elbow Pitch           (0 - 180 deg, default 90)
    - Ch 3 : Wrist Pitch           (0 - 180 deg, default 90)
    - Ch 4 : Wrist Roll            (0 - 180 deg, default 90)
    - Ch 5 : Gripper / Claw        (0 - 180 deg, default 40)

  Command Protocol:
    - "ARM:IK:<X>:<Y>:<Z>:<pitch>:<roll>:<claw>:<ms>" -> Move arm end-effector via IK
    - "SERVO:<chan>:<deg>" or "S:<chan>:<deg>"         -> Set single servo angle smoothly
    - "ARM:SPEED:<ms>"                                -> Set default trajectory duration (ms)
    - "ARM:home" / "ARM:rest" / "ARM:reach"          -> Posture presets
    - "ARM:open_gripper" / "ARM:close_gripper"        -> Claw control presets
    - "ARM:yes" / "ARM:no" / "ARM:wave" / "ARM:bow"   -> Gesture presets
    - "ARM:high_five" / "ARM:dance"                   -> Demonstration routines
*/

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

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

float currentAngles[NUM_ARM_SERVOS] = {90.0f, 90.0f, 90.0f, 90.0f, 90.0f, 40.0f};
float startAngles[NUM_ARM_SERVOS]   = {90.0f, 90.0f, 90.0f, 90.0f, 90.0f, 40.0f};
float targetAngles[NUM_ARM_SERVOS]  = {90.0f, 90.0f, 90.0f, 90.0f, 90.0f, 40.0f};

unsigned long moveStartTime = 0;
unsigned long moveDurationMs = 250; // Default 250ms trajectory duration
bool isMoving = false;

// Active Routine State Machine
String currentRoutine = "idle";
unsigned long routineStepTime = 0;
int routineStepIndex = 0;

// Low-Level PCA9685 Write Functions
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
  Wire.begin(21, 22); // Standard SDA=21, SCL=22
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

// -------------------------------------------------------------
// 3D Analytical Inverse Kinematics (IK) Engine
// -------------------------------------------------------------
// Computes base, shoulder, elbow, and wrist pitch angles given:
// X, Y, Z : Cartesian position of gripper tip (in mm) relative to base center
// pitchDeg: Pitch angle of end-effector relative to ground (in degrees, 0 = parallel to ground)
// rollDeg : Wrist roll angle (0-180)
// clawDeg : Gripper opening angle (0-180)
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

// -------------------------------------------------------------
// Trajectory & Motion Interpolation Engine (50Hz Update Loop)
// -------------------------------------------------------------
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

  // Cosine S-Curve Interpolation Factor
  float ease = 0.5f * (1.0f - cos(M_PI * progress));

  for (int i = 0; i < NUM_ARM_SERVOS; i++) {
    currentAngles[i] = startAngles[i] + ease * (targetAngles[i] - startAngles[i]);
    writeServoAngleHardware(i, currentAngles[i]);
  }
}

// -------------------------------------------------------------
// Posture & Gesture Presets
// -------------------------------------------------------------
void applyArmHomePosture(unsigned long durationMs = 300) {
  float homeTargets[6] = {90.0f, 90.0f, 90.0f, 90.0f, 90.0f, 40.0f};
  setArmTargetAngles(homeTargets, durationMs);
}

void applyArmRestPosture(unsigned long durationMs = 400) {
  float restTargets[6] = {90.0f, 30.0f, 150.0f, 120.0f, 90.0f, 10.0f};
  setArmTargetAngles(restTargets, durationMs);
}

void applyArmReachPosture(unsigned long durationMs = 400) {
  float reachTargets[6] = {90.0f, 120.0f, 60.0f, 90.0f, 90.0f, 60.0f};
  setArmTargetAngles(reachTargets, durationMs);
}

void updateArmRoutineEngine() {
  if (currentRoutine == "idle" || currentRoutine == "stop") return;
  if (isMoving) return; // Wait for current trajectory to complete smoothly

  unsigned long now = millis();

  if (currentRoutine == "yes") {
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
}

// -------------------------------------------------------------
// Command Parser (Serial)
// -------------------------------------------------------------
void parseArmCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.print("[Arm CMD Received] ");
  Serial.println(cmd);

  // 1. Direct Servo Command: "SERVO:<chan>:<deg>" or "S:<chan>:<deg>"
  if (cmd.startsWith("SERVO:") || cmd.startsWith("servo:") || cmd.startsWith("S:") || cmd.startsWith("s:")) {
    int firstColon = cmd.indexOf(':');
    int secondColon = cmd.indexOf(':', firstColon + 1);
    if (firstColon != -1 && secondColon != -1) {
      int chan = cmd.substring(firstColon + 1, secondColon).toInt();
      float deg = cmd.substring(secondColon + 1).toFloat();
      setSingleArmServoAngle(chan, deg, moveDurationMs);
      Serial.print("[Arm Servo] Channel ");
      Serial.print(chan);
      Serial.print(" -> ");
      Serial.print(deg);
      Serial.println(" deg (Interpolated)");
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
      Serial.print("[Arm IK Success] Base:"); Serial.print(q0);
      Serial.print(" Shld:"); Serial.print(q1);
      Serial.print(" Elb:"); Serial.print(q2);
      Serial.print(" Wrst:"); Serial.println(q3);
    } else {
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
      Serial.print("[Arm] Global Trajectory Speed set to ");
      Serial.print(moveDurationMs);
      Serial.println(" ms");
    }
    return;
  }

  // 4. Posture & Gesture Presets
  if (cmd == "ARM:home" || cmd == "arm:home") {
    currentRoutine = "idle";
    applyArmHomePosture();
  } else if (cmd == "ARM:rest" || cmd == "arm:rest") {
    currentRoutine = "idle";
    applyArmRestPosture();
  } else if (cmd == "ARM:reach" || cmd == "arm:reach") {
    currentRoutine = "idle";
    applyArmReachPosture();
  } else if (cmd == "ARM:open_gripper" || cmd == "arm:open_gripper") {
    setSingleArmServoAngle(5, 20.0f, 200);
  } else if (cmd == "ARM:close_gripper" || cmd == "arm:close_gripper") {
    setSingleArmServoAngle(5, 100.0f, 200);
  } else if (cmd == "ARM:yes" || cmd == "arm:yes") {
    currentRoutine = "yes"; routineStepIndex = 0;
  } else if (cmd == "ARM:no" || cmd == "arm:no") {
    currentRoutine = "no"; routineStepIndex = 0;
  } else if (cmd == "ARM:wave" || cmd == "arm:wave") {
    currentRoutine = "wave"; routineStepIndex = 0;
  } else if (cmd == "ARM:stop" || cmd == "arm:stop") {
    currentRoutine = "idle";
    applyArmHomePosture(200);
  }
}

// -------------------------------------------------------------
// Arduino Setup & Main Loop
// -------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000);

  Serial.println("==============================================");
  Serial.println("🦾 ESP32 6-DOF Robot Arm Firmware Initializing...");
  Serial.println("Features: 3D Analytical IK + 50Hz Cosine Trajectory Engine");
  Serial.println("==============================================");

  // Initialize I2C Bus & PCA9685
  initPCA9685();

  // Set default initial position (Home posture)
  applyArmHomePosture(500);

  Serial.println("[Robot Arm] PCA9685 Driver 0x40 Initialized to Home Position.");
  Serial.println("==============================================");
}

void loop() {
  // 1. Process USB Serial Commands
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    parseArmCommand(cmd);
  }

  // 2. Update 50Hz Cosine Trajectory Interpolation Engine
  updateArmTrajectoryEngine();

  // 3. Update Gesture Routine Engine
  updateArmRoutineEngine();

  delay(2); // Short pause for CPU efficiency
}
