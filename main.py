"""
## Documentation
Quickstart: https://github.com/google-gemini/cookbook/blob/main/quickstarts/Get_started_LiveAPI.py

## Setup

To install the dependencies for this script, run:

```
pip install google-genai opencv-python pyaudio pillow mss python-dotenv
```
"""

import os
import asyncio
import base64
import io
import traceback
import socket

import cv2
import pyaudio
import PIL.Image

import argparse
from dotenv import load_dotenv

from google import genai
from google.genai import types

load_dotenv()

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None


class TelloController:
    def __init__(self, ip="192.168.10.1", port=8889, audio_loop=None):
        self.drone_address = (ip, port)
        self.audio_loop = audio_loop
        self.sock = None
        self.sdk_enabled = False
        self.simulated = False  # Start by trying real connection, fallback on failure
        self.last_telemetry = {}

    def init_socket(self):
        if self.sock is None and not self.simulated:
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                self.sock.bind(('', 0))
                self.sock.settimeout(3.0)  # 3-second timeout for quick detection
            except Exception as e:
                print(f"[Tello] Failed to initialize socket: {e}. Switching to simulation.")
                self.simulated = True

    def send_cmd(self, command: str) -> dict:
        command = command.strip()
        
        # 1. Check if ESP32-S3 7-inch Touch LCD Screen serial connection is active
        if self.audio_loop and hasattr(self.audio_loop, 'serial_tello') and self.audio_loop.serial_tello and self.audio_loop.serial_tello.is_open:
            try:
                conn = self.audio_loop.serial_tello
                cmd_to_send = f"TELLO:{command}\r\n"
                print(f"\n[ESP32-S3 7\" LCD Tello Screen] Forwarding command: '{command}'")
                conn.write(cmd_to_send.encode('utf-8'))
                conn.flush()
                
                # Non-blocking read with timeout to capture response from ESP32 screen
                import time
                start_t = time.time()
                resp_line = "ok (sent to ESP32-S3 screen)"
                while time.time() - start_t < 1.5:
                    if conn.in_waiting:
                        raw_line = conn.readline().decode('utf-8', errors='replace').strip()
                        if raw_line:
                            print(f"[ESP32-S3 7\" LCD Tello Response] {raw_line}")
                            if "Response:" in raw_line or "Command:" in raw_line or "ok" in raw_line.lower():
                                resp_line = raw_line
                                break
                    time.sleep(0.05)
                
                return {
                    "status": "success",
                    "response": resp_line,
                    "bridge": "ESP32-S3 7\" Touch LCD",
                    "command": command
                }
            except Exception as e:
                print(f"[ESP32-S3 7\" LCD Tello Error] Serial bridge error: {e}. Falling back to UDP/Simulation.")

        # 2. Fallback to direct PC UDP socket if configured & not in simulation mode
        if not self.simulated:
            self.init_socket()
            try:
                # Automatically enable SDK mode if not already done
                if not self.sdk_enabled and command != "command":
                    print("[Tello UDP] Enabling SDK mode...")
                    self.sock.settimeout(3.0)
                    self.sock.sendto(b"command", self.drone_address)
                    response, _ = self.sock.recvfrom(1024)
                    print(f"[Tello UDP] SDK response: {response.decode('utf-8').strip()}")
                    self.sdk_enabled = True

                print(f"[Tello UDP] Sending command: {command}")
                self.sock.settimeout(15.0)
                self.sock.sendto(command.encode('utf-8'), self.drone_address)
                response, _ = self.sock.recvfrom(1024)
                res_str = response.decode('utf-8').strip()
                print(f"[Tello UDP] Response: {res_str}")
                return {"status": "success", "response": res_str, "bridge": "Direct UDP", "command": command}
            except (socket.timeout, socket.error) as e:
                if not self.sdk_enabled:
                    print(f"[Tello UDP] Initial connection/SDK activation failed ({e}). Falling back to simulation mode.")
                    self.simulated = True
                else:
                    print(f"[Tello UDP] Command '{command}' failed or timed out ({e}). Retaining connection.")
                    return {"status": "error", "message": f"Command execution failed: {e}", "command": command}

        # 3. Simulated Fallback Mode
        print(f"\n[Simulated Tello / ESP32 Bridge] Executing command: {command}")
        return {
            "status": "success",
            "response": "ok (simulated - ESP32-S3 Screen / Tello offline)",
            "simulated": True,
            "command": command
        }


class LevitonController:
    def __init__(self, email=None, password=None):
        self.email = email or os.environ.get("LEVITON_EMAIL")
        self.password = password or os.environ.get("LEVITON_PASSWORD")
        self.session = None
        # Start in simulated mode if no email or password is provided or if they are placeholders
        self.simulated = not (self.email and self.password) or "placeholder" in self.email or "placeholder" in self.password
        if self.simulated:
            print("[Leviton] Running in simulated mode (no valid credentials in .env).")

    def login(self):
        if self.simulated:
            return True
        if self.session is not None:
            return True
        try:
            from decora_wifi import DecoraWiFiSession
            self.session = DecoraWiFiSession()
            self.session.login(self.email, self.password)
            print("[Leviton] Successfully authenticated with Leviton Cloud Services.")
            return True
        except Exception as e:
            print(f"[Leviton] Failed to authenticate: {e}. Switching to simulation.")
            self.simulated = True
            return True

    def set_light_state(self, switch_name: str, state: bool, brightness: int = None) -> dict:
        if self.simulated:
            state_str = "ON" if state else "OFF"
            bright_str = f" at {brightness}%" if brightness is not None else ""
            print(f"\n[Simulated Leviton] Set switch '{switch_name}' to {state_str}{bright_str}")
            return {"status": "success", "switch_name": switch_name, "state": state_str, "brightness": brightness, "simulated": True}

        self.login()
        if self.simulated:
            return self.set_light_state(switch_name, state, brightness)

        try:
            from decora_wifi.models.residential_account import ResidentialAccount
            
            perms = self.session.user.get_residential_permissions()
            found_switch = None
            
            for permission in perms:
                acct = ResidentialAccount(self.session, permission.residentialAccountId)
                residences = acct.get_residences()
                for residence in residences:
                    switches = residence.get_iot_switches()
                    for switch in switches:
                        if switch_name.lower() in switch.name.lower():
                            found_switch = switch
                            break
                    if found_switch:
                        break
                if found_switch:
                    break

            if not found_switch:
                print(f"[Leviton] Switch '{switch_name}' not found.")
                return {"status": "error", "message": f"Switch '{switch_name}' not found."}

            attribs = {'power': 'ON' if state else 'OFF'}
            if brightness is not None:
                attribs['brightness'] = brightness

            found_switch.update_attributes(attribs)
            found_switch.refresh()
            print(f"[Leviton] Updated switch '{found_switch.name}' to power={found_switch.power}, brightness={found_switch.brightness}")
            return {
                "status": "success",
                "switch_name": found_switch.name,
                "state": found_switch.power,
                "brightness": found_switch.brightness
            }
        except Exception as e:
            print(f"[Leviton] Error updating switch '{switch_name}': {e}")
            return {"status": "error", "message": str(e)}


class EwelinkController:
    def __init__(self, username=None, password=None, region="us"):
        self.username = username or os.environ.get("EWELINK_USERNAME")
        self.password = password or os.environ.get("EWELINK_PASSWORD")
        self.region = region or os.environ.get("EWELINK_REGION", "us")
        self.client = None
        # Start in simulated mode if no credentials or placeholders are present
        self.simulated = not (self.username and self.password) or "placeholder" in self.username or "placeholder" in self.password
        if self.simulated:
            print("[eWeLink] Running in simulated mode (no valid credentials in .env).")

    def login(self):
        if self.simulated:
            return True
        if self.client is not None:
            return True
        try:
            import sonoff
            import random
            import time
            import requests

            # Patch update_devices to work with modern eWeLink API and query parameters
            def patched_update_devices(self_sonoff):
                if not self_sonoff._wshost:
                    return []
                
                # Check skipped login / grace period
                if self_sonoff._skipped_login and self_sonoff.is_grace_period():
                    return self_sonoff._devices

                nonce = ''.join([str(random.randint(0, 9)) for _ in range(15)])
                params = {
                    'appid': 'oeVkj2lYFGnJu5XUtWisfW4utiN4u9Mq',
                    'ts': int(time.time()),
                    'nonce': nonce,
                    'version': '6'
                }

                try:
                    url = f'https://{self_sonoff._api_region}-api.coolkit.cc:8080/api/user/device'
                    r = requests.get(url, headers=self_sonoff._headers, params=params)
                    resp = r.json()
                    
                    if 'error' in resp and resp['error'] != 0:
                        print(f"[eWeLink] API error response: {resp}")
                        if resp['error'] in [400, 401]:
                            return self_sonoff._devices

                    if isinstance(resp, dict) and 'devicelist' in resp:
                        self_sonoff._devices = resp['devicelist']
                    else:
                        self_sonoff._devices = resp
                except Exception as e:
                    print(f"[eWeLink] Error updating devices: {e}")
                
                return self_sonoff._devices

            sonoff.Sonoff.update_devices = patched_update_devices

            self.client = sonoff.Sonoff(self.username, self.password, self.region)
            print("[eWeLink] Successfully authenticated with eWeLink Cloud Services.")
            return True
        except Exception as e:
            print(f"[eWeLink] Failed to authenticate: {e}. Switching to simulation.")
            self.simulated = True
            return True

    def set_device_state(self, device_name: str, state: bool) -> dict:
        if self.simulated:
            state_str = "ON" if state else "OFF"
            print(f"\n[Simulated eWeLink] Set device '{device_name}' to {state_str}")
            return {"status": "success", "device_name": device_name, "state": state_str, "simulated": True}

        self.login()
        if self.simulated:
            return self.set_device_state(device_name, state)

        try:
            devices = self.client.get_devices()
            found_device = None
            if devices:
                for device in devices:
                    name = device.get('name', '')
                    if device_name.lower() in name.lower():
                        found_device = device
                        break

            if not found_device:
                print(f"[eWeLink] Device '{device_name}' not found.")
                return {"status": "error", "message": f"Device '{device_name}' not found."}

            device_id = found_device['deviceid']
            state_str = 'on' if state else 'off'
            self.client.switch(state_str, device_id, None)
            print(f"[eWeLink] Updated device '{found_device.get('name')}' state to {state_str}")
            return {
                "status": "success",
                "device_name": found_device.get('name'),
                "state": "ON" if state else "OFF"
            }
        except Exception as e:
            print(f"[eWeLink] Error updating device '{device_name}': {e}")
            return {"status": "error", "message": str(e)}


FORMAT = pyaudio.paInt16
CHANNELS = 1
SEND_SAMPLE_RATE = 16000
RECEIVE_SAMPLE_RATE = 24000
CHUNK_SIZE = 1024

MODEL = "models/gemini-3.1-flash-live-preview"

DEFAULT_MODE = "camera"

client = genai.Client(
    http_options={"api_version": "v1beta"},
    api_key=os.environ.get("GEMINI_API_KEY"),
)


def get_config(voice_name="Zephyr", enable_esp32=True):
    tools = []
    
    # Common tools list
    function_declarations = []
    
    if enable_esp32:
        function_declarations.append(
            types.FunctionDeclaration(
                name="set_led_state",
                description="Controls the onboard LED of the ESP32 dev module. State should be True to turn the LED on, or False to turn it off.",
                parameters={
                    "type": "object",
                    "properties": {
                        "state": {
                            "type": "boolean",
                            "description": "True to turn on the LED (red), False to turn it off."
                        }
                    },
                    "required": ["state"]
                }
            )
        )
        function_declarations.append(
            types.FunctionDeclaration(
                name="pulse_led",
                description="Pulses (blinks) a specific GPIO pin on the ESP32 module on and off a specified number of times. When finger gestures are shown (1 finger -> GPIO 1, 2 fingers -> GPIO 2, 3 fingers -> GPIO 3, 4 fingers -> GPIO 4), pulse target GPIO pin N on and off 1 time.",
                parameters={
                    "type": "object",
                    "properties": {
                        "count": {
                            "type": "integer",
                            "description": "The number of times to pulse/blink the pin (defaults to 1)."
                        },
                        "gpio": {
                            "type": "integer",
                            "description": "The target ESP32 GPIO pin number to pulse (e.g. 1 for 1 finger, 2 for 2 fingers, 3 for 3 fingers, 4 for 4 fingers)."
                        },
                        "duration_ms": {
                            "type": "integer",
                            "description": "Optional duration in milliseconds for the ON and OFF state of each pulse. Defaults to 500ms."
                        }
                    },
                    "required": ["gpio"]
                }
            )
        )
        function_declarations.append(
            types.FunctionDeclaration(
                name="set_servo_angle",
                description=(
                    "Controls a PCA9685 servo driver channel on the unified Waveshare 7-inch ESP32-S3 Touchscreen Birds controller (Driver 1 for Left Side, Driver 2 for Right Side) or the Robot Arm. "
                    "Left Side Servos (Driver 1): 'Left Parrot Up/Dn' (0), 'Left Parrot Right/Left' (1), 'Left Parrot Rotate' (2), "
                    "'Left Spotlight Up/Dn' (3), 'Left Spotlight Rotate' (4), 'Center Bird Up/Dn' (5), 'Center Bird Right/Left' (6), 'Center Bird Rotate' (7). "
                    "Right Side Servos (Driver 2): 'Right Parrot Up/Dn' (0), 'Right Parrot Right/Left' (1), 'Right Parrot Rotate' (2), "
                    "'Right Spotlight Up/Dn' (3), 'Right Spotlight Rotate' (4), 'Center Turntable Rotate' (5). "
                    "Degree range is 0 to 180 (default 90 degrees)."
                ),
                parameters={
                    "type": "object",
                    "properties": {
                        "board": {
                            "type": "string",
                            "description": "Target servo side / board: 'left' (Driver 1), 'right' (Driver 2), or 'arm'."
                        },
                        "servo_name": {
                            "type": "string",
                            "description": "Name or description of the target servo."
                        },
                        "channel": {
                            "type": "integer",
                            "description": "PCA9685 channel index (0-15)."
                        },
                        "angle": {
                            "type": "integer",
                            "description": "Target angle position in degrees (0 to 180)."
                        }
                    },
                    "required": ["board", "angle"]
                }
            )
        )

        function_declarations.append(
            types.FunctionDeclaration(
                name="trigger_bird_routine",
                description=(
                    "Triggers an automated choreography, light show, or animated performance routine on the unified Waveshare 7-inch Touch LCD ESP32 controller (with MCP23017 and dual PCA9685 servo drivers). "
                    "Supported routines: 'sing' (Parrot singing, beak animation, chirping and head movement), "
                    "'sweep' (Spotlight sweeping beam choreography), "
                    "'dance' (Center turntable rotation and bird dancing), "
                    "'lightshow' (Chasing pulse light show across all bird LEDs and outputs), "
                    "'symphony' (Synchronized multi-bird performance), "
                    "'home' (Resets all servos to 90 degrees and turns all lights off)."
                ),
                parameters={
                    "type": "object",
                    "properties": {
                        "routine": {
                            "type": "string",
                            "description": "Routine name: 'sing', 'sweep', 'dance', 'lightshow', 'symphony', 'home'."
                        }
                    },
                    "required": ["routine"]
                }
            )
        )

        function_declarations.append(
            types.FunctionDeclaration(
                name="control_hexapod",
                description=(
                    "Controls the 6-leg Hexapod robot driven by the ESP-32-Touch-LCD controller (over Bluetooth 'hexapod-touch-lcd' or USB serial). "
                    "Supported motion presets: 'walk', 'run', 'wave_left_arm', 'wave_right_arm', 'dance', 'sit', 'stand', 'flat_to_floor', 'stop', 'turn_left', 'turn_right', 'bow', 'set_lcd_message'. "
                    "Can also adjust individual leg joints (leg_name: 'FL', 'ML', 'RL', 'FR', 'MR', 'RR', joint_name: 'coxa', 'femur', 'tibia'), "
                    "set 3D Inverse Kinematics (IK) foot position (x, y, z in mm), or display custom status text on the robot's onboard Touch LCD screen."
                ),
                parameters={
                    "type": "object",
                    "properties": {
                        "action": {
                            "type": "string",
                            "description": "Motion preset or action to perform: 'walk', 'run', 'wave_left_arm', 'wave_right_arm', 'dance', 'sit', 'stand', 'flat_to_floor', 'stop', 'turn_left', 'turn_right', 'bow', 'set_joint', 'set_ik', 'set_lcd_message'."
                        },
                        "leg_name": {
                            "type": "string",
                            "description": "Leg identifier: 'FL', 'ML', 'RL', 'FR', 'MR', 'RR'."
                        },
                        "joint_name": {
                            "type": "string",
                            "description": "Joint name: 'coxa' (hip), 'femur' (thigh), or 'tibia' (knee)."
                        },
                        "angle": {
                            "type": "integer",
                            "description": "Target angle in degrees (0-180)."
                        },
                        "x": {
                            "type": "number",
                            "description": "Cartesian X offset in mm for Inverse Kinematics foot position."
                        },
                        "y": {
                            "type": "number",
                            "description": "Cartesian Y offset in mm for Inverse Kinematics foot position."
                        },
                        "z": {
                            "type": "number",
                            "description": "Cartesian Z offset in mm for Inverse Kinematics foot position."
                        },
                        "duration": {
                            "type": "integer",
                            "description": "Trajectory move duration in milliseconds (default 200)."
                        },
                        "lcd_message": {
                            "type": "string",
                            "description": "Custom message or expression text to display on the onboard ESP-32-Touch-LCD screen."
                        }
                    },
                    "required": ["action"]
                }
            )
        )

    # Add Tello drone control tool
    function_declarations.append(
        types.FunctionDeclaration(
            name="send_tello_command",
            description=(
                "Sends a control command to the Tello drone over UDP. "
                "Supported commands include:\n"
                "- 'takeoff': Take off from the ground\n"
                "- 'land': Land on the ground\n"
                "- 'up x': Fly up (x = 20-200 cm)\n"
                "- 'down x': Fly down (x = 20-200 cm)\n"
                "- 'left x': Fly left (x = 20-200 cm)\n"
                "- 'right x': Fly right (x = 20-200 cm)\n"
                "- 'forward x': Fly forward (x = 20-200 cm)\n"
                "- 'back x': Fly back (x = 20-200 cm)\n"
                "- 'cw x': Rotate x degrees clockwise (x = 1-360)\n"
                "- 'ccw x': Rotate x degrees counter-clockwise (x = 1-360)\n"
                "- 'flip x': Flip in direction x ('l', 'r', 'f', 'b')\n"
                "- 'emergency': Stop motors immediately"
            ),
            parameters={
                "type": "object",
                "properties": {
                    "command": {
                        "type": "string",
                        "description": "The SDK command to send to the drone."
                    }
                },
                "required": ["command"]
            }
        )
    )

    # Add Leviton switch control tool
    function_declarations.append(
        types.FunctionDeclaration(
            name="set_leviton_light_state",
            description=(
                "Controls Leviton Decora Smart Wi-Fi switches and dimmers in the user's home. "
                "Allows turning lights on/off and optionally setting brightness levels (for dimmers)."
            ),
            parameters={
                "type": "object",
                "properties": {
                    "switch_name": {
                        "type": "string",
                        "description": "The name of the light switch to control (e.g. 'Kitchen', 'Living Room')."
                    },
                    "state": {
                        "type": "boolean",
                        "description": "True to turn the light on, False to turn it off."
                    },
                    "brightness": {
                        "type": "integer",
                        "description": "Optional brightness level as a percentage (0 to 100). Only applicable to dimmable switches."
                    }
                },
                "required": ["switch_name", "state"]
            }
        )
    )

    # Add eWeLink switch control tool
    function_declarations.append(
        types.FunctionDeclaration(
            name="set_ewelink_device_state",
            description=(
                "Controls eWeLink (Sonoff) smart plugs, switches, and other devices in the user's home. "
                "Allows turning devices on or off."
            ),
            parameters={
                "type": "object",
                "properties": {
                    "device_name": {
                        "type": "string",
                        "description": "The name of the device to control (e.g. 'Fan', 'Desk Light')."
                    },
                    "state": {
                        "type": "boolean",
                        "description": "True to turn the device on, False to turn it off."
                    }
                },
                "required": ["device_name", "state"]
            }
        )
    )
    
    # Add 6-DOF Robot Arm control tool
    function_declarations.append(
        types.FunctionDeclaration(
            name="control_robot_arm",
            description=(
                "Controls a 6 degrees of freedom (6-DOF) Robot Arm driven by a Waveshare ESP32-S3-Touch-LCD-7C (7.0-inch 800x600 HD Capacitive Touchscreen with live telemetry and animation) via a PCA9685 servo driver. "
                "Allows triggering gestures ('yes', 'no', 'high_five', 'wave', 'bow', 'dance'), "
                "executing preset demonstration motions ('home', 'rest', 'reach', 'pick_and_place', 'open_gripper', 'close_gripper', 'stop'), "
                "setting individual joint angles (channel 0: Base, 1: Shoulder, 2: Elbow, 3: Wrist Pitch, 4: Wrist Roll, 5: Gripper), "
                "or positioning the arm end-effector via 3D Inverse Kinematics (IK) coordinates (x, y, z in mm, pitch, roll, claw)."
            ),
            parameters={
                "type": "object",
                "properties": {
                    "action": {
                        "type": "string",
                        "description": "Gesture or preset motion: 'yes', 'no', 'high_five', 'wave', 'bow', 'dance', 'home', 'rest', 'reach', 'pick_and_place', 'open_gripper', 'close_gripper', 'stop', 'move_ik'."
                    },
                    "channel": {
                        "type": "integer",
                        "description": "PCA9685 channel index (0: Base, 1: Shoulder, 2: Elbow, 3: Wrist Pitch, 4: Wrist Roll, 5: Gripper)."
                    },
                    "angle": {
                        "type": "integer",
                        "description": "Target angle in degrees (0 to 180)."
                    },
                    "x": {
                        "type": "number",
                        "description": "Cartesian X coordinate in mm for end-effector IK position."
                    },
                    "y": {
                        "type": "number",
                        "description": "Cartesian Y coordinate in mm for end-effector IK position."
                    },
                    "z": {
                        "type": "number",
                        "description": "Cartesian Z coordinate in mm for end-effector IK position."
                    },
                    "pitch": {
                        "type": "number",
                        "description": "Wrist pitch angle in degrees (default 0)."
                    },
                    "roll": {
                        "type": "number",
                        "description": "Wrist roll angle in degrees (0-180, default 90)."
                    },
                    "claw": {
                        "type": "number",
                        "description": "Gripper claw opening angle in degrees (0-180, default 40)."
                    },
                    "duration": {
                        "type": "integer",
                        "description": "Trajectory move duration in milliseconds (default 250)."
                    }
                }
            }
        )
    )

    function_declarations.append(
        types.FunctionDeclaration(
            name="control_wave_rover",
            description=(
                "Controls the standard 4WD mobile robot platform powered by ESP32 using 2 x LM298 dual reversing motor drivers (Front LM298: Ch A Front Left, Ch B Front Right; Rear LM298: Ch A Rear Left, Ch B Rear Right). "
                "Supports 4WD movement ('forward', 'back', 'turn_left', 'turn_right', 'spin_left', 'spin_right', 'stop'), "
                "Eye LEDs ('eyes_on', 'eyes_off'), drive speed (0-100%), pan-tilt camera angles (pan, tilt 0-180 deg), "
                "headlights ('headlight_on', 'headlight_off'), preset routines ('patrol', 'spin_360', 'dance', 'obstacle_avoidance'), "
                "and custom status messages on the onboard screen."
            ),
            parameters={
                "type": "object",
                "properties": {
                    "action": {
                        "type": "string",
                        "description": "Action: 'forward', 'back', 'turn_left', 'turn_right', 'spin_left', 'spin_right', 'stop', 'mouth_open', 'mouth_close', 'body_on', 'body_off', 'eyes_on', 'eyes_off', 'headlight_on', 'headlight_off', 'set_speed', 'set_pan_tilt', 'patrol', 'spin_360', 'dance', 'obstacle_avoidance', 'set_lcd_message'."
                    },
                    "speed": {
                        "type": "integer",
                        "description": "Drive speed percentage (0 to 100)."
                    },
                    "pan": {
                        "type": "integer",
                        "description": "Pan-tilt camera pan angle in degrees (0 to 180)."
                    },
                    "tilt": {
                        "type": "integer",
                        "description": "Pan-tilt camera tilt angle in degrees (0 to 180)."
                    },
                    "mouth": {
                        "type": "boolean",
                        "description": "True to power ON (open) DC mouth motor, False to power OFF (close) DC mouth motor."
                    },
                    "body_motion": {
                        "type": "boolean",
                        "description": "True to power ON DC body motor (sways body up and down), False to power OFF."
                    },
                    "eye_leds": {
                        "type": "boolean",
                        "description": "True to turn Eye LEDs ON, False to turn OFF."
                    },
                    "headlight": {
                        "type": "boolean",
                        "description": "True to turn headlights ON, False to turn OFF."
                    },
                    "duration": {
                        "type": "integer",
                        "description": "Movement duration in milliseconds (default 300)."
                    },
                    "lcd_message": {
                        "type": "string",
                        "description": "Custom status message to display on the Wave Rover's LCD screen."
                    }
                },
                "required": ["action"]
            }
        )
    )

    # Add Parrot Selection & Sound Reactivity tool
    function_declarations.append(
        types.FunctionDeclaration(
            name="select_active_parrot",
            description=(
                "Selects which parrot ('left', 'right', or 'both') will mouth the words, flap its wings, "
                "sway its 3 base servos (up/down, right/left side-to-side, turn), and activate its spotlight when the AI speaks."
            ),
            parameters={
                "type": "object",
                "properties": {
                    "parrot": {
                        "type": "string",
                        "description": "Target parrot: 'left', 'right', or 'both'."
                    }
                },
                "required": ["parrot"]
            }
        )
    )

    function_declarations.append(
        types.FunctionDeclaration(
            name="set_parrot_sound_reactivity",
            description=(
                "Enables or disables automatic microphone sound detection and AI voice animatronics for the parrots."
            ),
            parameters={
                "type": "object",
                "properties": {
                    "enabled": {
                        "type": "boolean",
                        "description": "True to enable auto-reactivity, False to disable."
                    }
                },
                "required": ["enabled"]
            }
        )
    )

    function_declarations.append(
        types.FunctionDeclaration(
            name="set_speech_reactivity",
            description=(
                "Controls AI speech reactivity across robots. Choose which robots react when the AI speaks: "
                "'birds' (mouth moves & LED eyes blink), 'hexapod' (LED eyes blink & body sways), "
                "'arm' (conversational gestures), 'rover' (DC mouth opens, body sways up/down, eye LEDs flash), or 'all' (all robots react)."
            ),
            parameters={
                "type": "object",
                "properties": {
                    "robot": {
                        "type": "string",
                        "description": "Target robot: 'birds', 'hexapod', 'arm', 'rover', or 'all'."
                    },
                    "enabled": {
                        "type": "boolean",
                        "description": "True to enable speech reactivity, False to disable."
                    }
                },
                "required": ["robot", "enabled"]
            }
        )
    )

    function_declarations.append(
        types.FunctionDeclaration(
            name="play_hardware_sound",
            description=(
                "Plays procedural audio sound effects on the MAX98357A I2S Audio Amplifier attached to any of the 5 ESP32-S3 Touchscreen hardware boards: "
                "'birds' (chirp, squawk, trill, melody, symphony, beep), "
                "'hexapod' (step, startup, shutdown, alert, dance, r2d2, servo, click), "
                "'arm' (claw_grab, claw_release, servo, chime, error, fanfare, beep), "
                "'rover'/'waverover' (rover_engine, horn, startup, shutdown, alert, turbo, brake), "
                "or 'tello'/'drone' (takeoff, land, flip, radar, alarm, siren, connect). "
                "Can also set volume (0-100)."
            ),
            parameters={
                "type": "object",
                "properties": {
                    "device": {
                        "type": "string",
                        "description": "Target hardware device: 'birds', 'hexapod', 'arm', 'rover' (or 'waverover'), 'drone' (or 'tello')."
                    },
                    "sound": {
                        "type": "string",
                        "description": "Sound name to play (e.g. 'chirp', 'squawk', 'step', 'startup', 'rover_engine', 'horn', 'claw_grab', 'chime', 'takeoff', 'land', 'flip', 'radar', 'fanfare', etc.)."
                    },
                    "volume": {
                        "type": "integer",
                        "description": "Optional volume percentage (0 to 100)."
                    }
                },
                "required": ["device", "sound"]
            }
        )
    )

    function_declarations.append(
        types.FunctionDeclaration(
            name="get_proximity_sensors",
            description=(
                "Returns current distance measurements (in centimeters) from the 4 HC-SR04 ultrasonic proximity sensors "
                "attached to the front, rear, left, and right of the WaveRover or Hexapod platform. "
                "Use this to check for obstacles, measure clear space, or verify navigation paths."
            ),
            parameters={
                "type": "object",
                "properties": {
                    "robot": {
                        "type": "string",
                        "description": "Target robot platform: 'rover' (or 'waverover') or 'hexapod'."
                    }
                }
            }
        )
    )

    if function_declarations:
        tools.append(types.Tool(function_declarations=function_declarations))

    system_instruction = (
        "You are a helpful real-time multimodal voice assistant running on the user's local computer. "
        "You have direct access to local hardware and smart devices: an onboard LED of an ESP32 microcontroller, "
        "a 6-leg Hexapod robot powered by the ESP-32-Touch-LCD controller (Bluetooth: 'hexapod-touch-lcd' / Serial) with an onboard interactive touch screen and 4-way HC-SR04 ultrasonic proximity sensors, "
        "a 6-DOF Robot Arm powered by a Waveshare ESP32-S3-Touch-LCD-7C (7.0-inch 800x600 HD Capacitive Touchscreen with live telemetry & animation) via PCA9685, "
        "a Birds & LED stage controller powered by a unified Waveshare ESP32-S3-Touch-LCD-7 (7.0-inch 800x480 Capacitive Touchscreen with MCP23017 I/O Expander, Dual PCA9685 Servo Drivers, animated parrot mascot, and light shows), "
        "a 4-Motor AWD Mobile Platform powered by ESP32 (with 2 x LM298 reversing dual motor drivers for Front Left/Right and Rear Left/Right wheels, eye LEDs, pan-tilt camera, 4-way HC-SR04 ultrasonic proximity sensors, and MAX98357A I2S audio), "
        "a Tello drone, Leviton smart lights, and eWeLink (Sonoff) devices. All 5 ESP32-S3 touchscreen controllers are equipped with MAX98357A I2S Class-D mono audio amplifiers.\n\n"
        "1. VISUAL MODALITY AWARENESS:\n"
        "   - You are receiving a continuous, real-time video stream (from the user's webcam or screen share).\n"
        "   - Pay close attention to what you see. You MUST proactively notice, react to, and comment on objects, gestures, text, or visual changes shown in the video feed. Do NOT wait for the user to prompt you or say they are showing you something; describe what you see naturally as part of the conversation.\n"
        "   - For example, if you see the user holding a coffee cup, showing a phone, or displaying any object, refer to it and ask about it or comment on it immediately.\n\n"
        "2. TIME PERCEPTION CALIBRATION:\n"
        "   - The video stream is sent to you at exactly 1 frame per second (1 FPS). Each frame you receive represents exactly 1 second of real time.\n"
        "   - When estimating time or counting seconds (e.g., if the user asks you to wait 5 seconds, count seconds, or track time), use the number of incoming frames as your clock (e.g. 5 frames = 5 seconds). Do not rush or estimate time based on text-generation speeds; wait for the appropriate amount of time to pass.\n\n"
        "3. HARDWARE CONTROL:\n"
        "   - HC-SR04 Proximity Sensors: You MUST use the `get_proximity_sensors` tool when the user asks verbally or visually about distances, surrounding obstacles, or clear paths around the WaveRover or Hexapod platform.\n"
        "   - MAX98357A Hardware Audio: You MUST use the `play_hardware_sound` tool when the user asks verbally to play hardware sounds, bird chirps/songs, hexapod droid chatter/steps, robot arm grab/release chimes, rover engine/horn sounds, or drone radar pings/takeoff audio on any of the ESP32 touchscreen modules.\n"
        "   - Bird Routines & Animations: You MUST use the `trigger_bird_routine` tool when the user asks verbally or visually to perform singing ('sing'), spotlight sweeping ('sweep'), turntable dancing ('dance'), light shows ('lightshow'), bird symphony ('symphony'), or return to home ('home') on the Waveshare 7-inch Touch LCD controller.\n"
        "   - 4WD Rover Platform: You MUST use the `control_wave_rover` tool when the user asks verbally or visually to drive forward/back, turn, spin, patrol, move pan-tilt camera, control headlights, toggle Eye LEDs, or display status messages on the 4-Motor AWD Mobile Platform powered by dual LM298 motor drivers.\n"
        "   - Robot Arm: You MUST use the `control_robot_arm` tool when the user asks verbally or visually to control the 6-DOF robot arm powered by the Waveshare ESP32-S3-Touch-LCD-7C, perform gestures like 'yes', 'no', 'high_five', 'wave', 'bow', 'dance', execute pick & place, or set arm joint angles.\n"
        "   - Hexapod Robot: You MUST use the `control_hexapod` tool when the user asks verbally or visually to control the 6-leg hexapod robot powered by the ESP-32-Touch-LCD (e.g. walk, run, wave left arm, wave right arm, dance, sit, stand, flat to floor, turn left, turn right, bow, set leg joints, or display messages on the robot's screen).\n"
        "   - ESP32 PCA9685 Servos: You MUST use the `set_servo_angle` tool when the user asks verbally or visually to move, position, turn, or adjust any of the servos on the Birds ESP32-S3 Touchscreen (Left/Right sides) or Arm ESP32 (e.g. Left/Right Parrot Up/Dn, Right Spotlight Rotate, Center Bird Up/Dn, Center Turntable Rotate, Base/Shoulder/Elbow, etc.) to a specific degree angle (0 to 180 degrees).\n"
        "   - ESP32 LED: You MUST use the `set_led_state` tool to turn the LED on or off. If the user asks you to pulse, blink, or flash the LED a certain number of times (e.g., to match the count of fingers you see in the frame), you MUST use the `pulse_led` tool with the appropriate count.\n"
        "   - Tello Drone: You MUST use the `send_tello_command` tool to control the Tello drone when the user asks you to perform actions like takeoff, landing, moving, flipping, or rotating.\n"
        "   - Leviton Lights: You MUST use the `set_leviton_light_state` tool when the user asks you to turn smart home lights on, off, or change their brightness level.\n"
        "   - eWeLink Devices: You MUST use the `set_ewelink_device_state` tool when the user asks you to turn eWeLink or Sonoff devices (plugs, switches, fans, etc.) on or off.\n\n"
        "If a physical device is not connected or configured, the application will automatically run "
        "the command in simulated/fallback mode, so always call the tools anyway. "
        "Never tell the user that you cannot control the hardware, as you are fully equipped with tools to do so."
    )

    return types.LiveConnectConfig(
        response_modalities=[
            "AUDIO",
        ],
        media_resolution="MEDIA_RESOLUTION_MEDIUM",
        speech_config=types.SpeechConfig(
            voice_config=types.VoiceConfig(
                prebuilt_voice_config=types.PrebuiltVoiceConfig(voice_name=voice_name)
            )
        ),
        tools=tools,
        system_instruction=system_instruction,
        context_window_compression=types.ContextWindowCompressionConfig(
            trigger_tokens=104857,
            sliding_window=types.SlidingWindow(target_tokens=52428),
        ),
    )

pya = pyaudio.PyAudio()


def count_fingers(hand_landmarks) -> int:
    """
    Counts the number of extended fingers using MediaPipe hand landmarks.
    """
    landmarks = hand_landmarks.landmark
    fingers_open = 0

    # Index, Middle, Ring, Pinky
    tips = [8, 12, 16, 20]
    pips = [6, 10, 14, 18]
    for tip, pip in zip(tips, pips):
        if landmarks[tip].y < landmarks[pip].y:
            fingers_open += 1

    # Thumb
    # index mcp (5), pinky mcp (17), thumb tip (4), thumb ip (3)
    if landmarks[5].x > landmarks[17].x:
        # Left hand or right hand back
        if landmarks[4].x > landmarks[3].x:
            fingers_open += 1
    else:
        # Right hand or left hand back
        if landmarks[4].x < landmarks[3].x:
            fingers_open += 1

SERVO_CONFIG = {
    "left": [
        {"name": "Left Parrot Up/Dn", "channel": 0, "default": 90},
        {"name": "Left Parrot Right/Left", "channel": 1, "default": 90},
        {"name": "Left Parrot Rotate", "channel": 2, "default": 90},
        {"name": "Left Spotlight Up/Dn", "channel": 3, "default": 90},
        {"name": "Left Spotlight Rotate", "channel": 4, "default": 90},
        {"name": "Center Bird Up/Dn", "channel": 5, "default": 90},
        {"name": "Center Bird Right/Left", "channel": 6, "default": 90},
        {"name": "Center Bird Rotate", "channel": 7, "default": 90},
    ],
    "right": [
        {"name": "Right Parrot Up/Dn", "channel": 0, "default": 90},
        {"name": "Right Parrot Right/Left", "channel": 1, "default": 90},
        {"name": "Right Parrot Rotate", "channel": 2, "default": 90},
        {"name": "Right Spotlight Up/Dn", "channel": 3, "default": 90},
        {"name": "Right Spotlight Rotate", "channel": 4, "default": 90},
        {"name": "Center Turntable Rotate", "channel": 5, "default": 90},
    ],
    "arm": [
        {"name": "Base / Waist Rotate", "channel": 0, "default": 90},
        {"name": "Shoulder Pitch", "channel": 1, "default": 90},
        {"name": "Elbow Pitch", "channel": 2, "default": 90},
        {"name": "Wrist Pitch", "channel": 3, "default": 90},
        {"name": "Wrist Roll", "channel": 4, "default": 90},
        {"name": "Gripper / Claw", "channel": 5, "default": 40},
    ],
}


def load_esp32_button_config():
    """
    Loads button functions and GPIO mappings for ESP32 Left and ESP32 Right.
    Tries to read 'Birds On_Off Buttons ESP32.xlsx' if present; otherwise uses fallback data.
    """
    fallback_config = {
        "left": [
            {"name": "L Parrot Mouth", "gpio": 0},
            {"name": "L Parrot Eyes", "gpio": 1},
            {"name": "L Parrot Body", "gpio": 2},
            {"name": "L Parrot Light", "gpio": 3},
            {"name": "L Parrot Mouth Select", "gpio": 4},
            {"name": "L Rear Bird Rear Move", "gpio": 5},
            {"name": "L Rear Bird Rear Light", "gpio": 12},
            {"name": "L Front Bird Move", "gpio": 13},
            {"name": "L Front Bird Light", "gpio": 14},
            {"name": "L Bird Front Chirp", "gpio": 15},
            {"name": "Center Bird Move", "gpio": 16},
        ],
        "right": [
            {"name": "R Parrot Mouth", "gpio": 0},
            {"name": "R Parrot Eyes", "gpio": 1},
            {"name": "R Parrot Body", "gpio": 2},
            {"name": "R Parrot Light", "gpio": 3},
            {"name": "R Parrot Mouth Select", "gpio": 4},
            {"name": "R Rear Bird Rear Move", "gpio": 5},
            {"name": "R Rear Bird Rear Light", "gpio": 12},
            {"name": "R Front Bird Move", "gpio": 13},
            {"name": "R Front Bird Light", "gpio": 14},
            {"name": "R Bird Front Chirp", "gpio": 15},
            {"name": "Center Bird Move", "gpio": 16},
        ],
    }

    excel_path = "Birds On_Off Buttons ESP32.xlsx"
    if not os.path.exists(excel_path):
        return fallback_config

    try:
        import zipfile
        import xml.etree.ElementTree as ET

        z = zipfile.ZipFile(excel_path)
        shared_strings = []
        if 'xl/sharedStrings.xml' in z.namelist():
            tree = ET.fromstring(z.read('xl/sharedStrings.xml'))
            for elem in tree.iter():
                if elem.tag.endswith('t') and elem.text:
                    shared_strings.append(elem.text)

        sf = 'xl/worksheets/sheet1.xml'
        if sf not in z.namelist():
            return fallback_config

        tree = ET.fromstring(z.read(sf))
        ns = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}

        left_items = []
        right_items = []
        current_section = None

        for row in tree.findall('.//s:row', ns):
            cells = {}
            for cell in row.findall('s:c', ns):
                r = cell.attrib.get('r')
                t = cell.attrib.get('t')
                v_elem = cell.find('s:v', ns)
                val = ""
                if v_elem is not None and v_elem.text:
                    if t == 's':
                        idx = int(v_elem.text)
                        val = shared_strings[idx] if idx < len(shared_strings) else ""
                    else:
                        val = v_elem.text
                col = ''.join([c for c in r if c.isalpha()])
                cells[col] = val.strip()

            val_a = cells.get('A', '')
            val_b = cells.get('B', '')

            if 'ESP32 Left' in val_a:
                current_section = 'left'
                continue
            elif 'ESP32 Right' in val_a:
                current_section = 'right'
                continue

            if val_a and val_b and val_a not in ('Outputs', 'Bottango Driver ESP 32s'):
                try:
                    gpio_val = int(float(val_b))
                    item = {"name": val_a, "gpio": gpio_val}
                    if current_section == 'left':
                        left_items.append(item)
                    elif current_section == 'right':
                        right_items.append(item)
                except ValueError:
                    pass

        if left_items or right_items:
            return {"left": left_items or fallback_config["left"], "right": right_items or fallback_config["right"]}
    except Exception as e:
        print(f"[Spreadsheet] Notice: Using default button configuration ({e})")

    return fallback_config


def scan_and_autodetect_esp32_ports():
    """
    Scans system serial ports for connected ESP32, Waveshare 7" Touch-LCD (Birds / Arm / Tello / Wave Rover), or Hexapod ESP-32-Touch-LCD devices.
    Returns:
      display_options: list of human-readable labels for Comboboxes
      device_map: dict mapping label -> raw device name (e.g. 'COM3')
      detected_birds_label: label for auto-detected Birds ESP32-S3 Touchscreen (Waveshare 7")
      detected_hexapod_label: label for auto-detected Hexapod ESP-32-Touch-LCD
      detected_arm_label: label for auto-detected Arm ESP32
      detected_tello_label: label for auto-detected Tello ESP32-S3 7" Touch LCD Screen
      detected_rover_label: label for auto-detected Waveshare Wave Rover Mobile Platform
    """
    esp32_keywords = [
        "cp210", "ch340", "ch341", "ft232", "esp32", "usb-serial", "usb serial",
        "silicon labs", "uart", "serial port", "prolific", "waveshare"
    ]

    display_options = ["None (Simulation Mode)"]
    device_map = {"None (Simulation Mode)": None}
    esp32_candidate_labels = []
    tello_specific_label = None
    hexapod_specific_label = None
    birds_specific_label = None
    arm_specific_label = None
    rover_specific_label = None

    try:
        import serial.tools.list_ports
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            dev = p.device
            desc = p.description or ""
            comb = f"{dev} {desc}".lower()
            if "tello" in comb or "drone" in comb:
                label = f"{dev} - Tello / Waveshare 7\" Touch-LCD ({desc})"
                tello_specific_label = label
            elif "hexapod" in comb or "hexipod" in comb or "shobots" in comb:
                label = f"{dev} - Hexapod ESP-32-Touch-LCD ({desc})"
                hexapod_specific_label = label
            elif "arm" in comb or "robot arm" in comb:
                label = f"{dev} - ESP32 Robot Arm ({desc})"
                arm_specific_label = label
            elif "rover" in comb or "waverover" in comb:
                label = f"{dev} - Waveshare Wave Rover ({desc})"
                rover_specific_label = label
            elif "bird" in comb:
                label = f"{dev} - Birds ESP32-S3 Touchscreen ({desc})"
                birds_specific_label = label
            elif "touch" in comb or "waveshare" in comb or "esp32-s3" in comb or "esp32s3" in comb:
                label = f"{dev} - Waveshare 7\" Touch-LCD ({desc})"
            elif desc and desc != dev:
                label = f"{dev} ({desc})"
            else:
                label = dev
            display_options.append(label)
            device_map[label] = dev

            if any(k in comb for k in esp32_keywords):
                esp32_candidate_labels.append(label)
            else:
                esp32_candidate_labels.append(label)
    except Exception as e:
        print(f"[COM Scan Error] {e}")

    detected_birds_label = birds_specific_label or (esp32_candidate_labels[0] if len(esp32_candidate_labels) > 0 else "None (Simulation Mode)")
    detected_hexapod_label = hexapod_specific_label or (esp32_candidate_labels[1] if len(esp32_candidate_labels) > 1 else (esp32_candidate_labels[0] if len(esp32_candidate_labels) > 0 else "None (Simulation Mode)"))
    detected_arm_label = arm_specific_label or (esp32_candidate_labels[2] if len(esp32_candidate_labels) > 2 else (esp32_candidate_labels[1] if len(esp32_candidate_labels) > 1 else "None (Simulation Mode)"))
    detected_tello_label = tello_specific_label or (esp32_candidate_labels[3] if len(esp32_candidate_labels) > 3 else (esp32_candidate_labels[0] if len(esp32_candidate_labels) > 0 else "None (Simulation Mode)"))
    detected_rover_label = rover_specific_label or (esp32_candidate_labels[4] if len(esp32_candidate_labels) > 4 else (esp32_candidate_labels[0] if len(esp32_candidate_labels) > 0 else "None (Simulation Mode)"))

    return display_options, device_map, detected_birds_label, detected_hexapod_label, detected_arm_label, detected_tello_label, detected_rover_label


def scan_bluetooth_ports():
    """
    Scans system serial / Bluetooth ports for connected ESP-32-Touch-LCD Hexapod devices (broadcast: hexapod-touch-lcd / hexapod).
    Returns:
      display_options: list of human-readable labels for Comboboxes
      device_map: dict mapping label -> raw device name or BT broadcast identifier
      detected_bt_label: label for auto-detected Bluetooth / Serial device
    """
    display_options = ["hexapod-touch-lcd (ESP-32-Touch-LCD BT/Serial)", "hexapod (Legacy BT Broadcast)", "None (Simulation Mode)"]
    device_map = {
        "hexapod-touch-lcd (ESP-32-Touch-LCD BT/Serial)": "hexapod-touch-lcd",
        "hexapod (Legacy BT Broadcast)": "hexapod",
        "None (Simulation Mode)": "None"
    }

    try:
        import serial.tools.list_ports
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            dev = p.device
            desc = p.description or ""
            label = f"{dev} - ESP-32-Touch-LCD ({desc})" if ("touch" in desc.lower() or "esp32" in desc.lower()) else (f"{dev} - Bluetooth ({desc})" if "bluetooth" in desc.lower() else f"{dev} ({desc})")
            display_options.append(label)
            device_map[label] = dev
    except Exception as e:
        print(f"[Bluetooth/Serial Scan Error] {e}")

    detected_bt_label = display_options[0]
    return display_options, device_map, detected_bt_label


def choose_hexapod_bt_port():
    options, dev_map, auto_lbl = scan_bluetooth_ports()
    print("\nScanning for ESP-32-Touch-LCD Hexapod connections...")
    for idx, opt in enumerate(options):
        print(f"  [{idx + 1}] {opt}")
    choice = input(f"Select Hexapod ESP-32-Touch-LCD connection [1-{len(options)}, default: 1 ({auto_lbl})]: ").strip()
    if not choice:
        return dev_map.get(auto_lbl, "hexapod-touch-lcd")
    try:
        idx = int(choice) - 1
        if 0 <= idx < len(options):
            return dev_map.get(options[idx], options[idx])
    except ValueError:
        return choice
    return dev_map.get(auto_lbl, "hexapod-touch-lcd")


# Compatibility alias
choose_shobots_bt_port = choose_hexapod_bt_port


def scan_rover_bluetooth_ports():
    """
    Scans system serial / Bluetooth ports for connected Waveshare Wave Rover devices (broadcast: waverover / wave-rover).
    Returns:
      display_options: list of human-readable labels for Comboboxes
      device_map: dict mapping label -> raw device name or BT broadcast identifier
      detected_bt_label: label for auto-detected Bluetooth / Serial device
    """
    display_options = ["waverover (Waveshare Wave Rover BT)", "wave-rover (Legacy BT Broadcast)", "None (Simulation Mode)"]
    device_map = {
        "waverover (Waveshare Wave Rover BT)": "waverover",
        "wave-rover (Legacy BT Broadcast)": "wave-rover",
        "None (Simulation Mode)": "None"
    }

    try:
        import serial.tools.list_ports
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            dev = p.device
            desc = p.description or ""
            comb = f"{dev} {desc}".lower()
            if "rover" in comb or "waverover" in comb:
                label = f"{dev} - Waveshare Wave Rover ({desc})"
            elif "bluetooth" in comb:
                label = f"{dev} - Bluetooth ({desc})"
            elif "esp32" in comb or "touch" in comb:
                label = f"{dev} - ESP32 ({desc})"
            elif desc and desc != dev:
                label = f"{dev} ({desc})"
            else:
                label = dev
            display_options.append(label)
            device_map[label] = dev
    except Exception as e:
        print(f"[Rover Bluetooth/Serial Scan Error] {e}")

    detected_bt_label = display_options[0]
    return display_options, device_map, detected_bt_label


def choose_rover_bt_port():
    options, dev_map, auto_lbl = scan_rover_bluetooth_ports()
    print("\nScanning for Waveshare Wave Rover Bluetooth / Serial connections...")
    for idx, opt in enumerate(options):
        print(f"  [{idx + 1}] {opt}")
    choice = input(f"Select Waveshare Wave Rover connection [1-{len(options)}, default: 1 ({auto_lbl})]: ").strip()
    if not choice:
        return dev_map.get(auto_lbl, "waverover")
    try:
        idx = int(choice) - 1
        if 0 <= idx < len(options):
            return dev_map.get(options[idx], options[idx])
    except ValueError:
        return choice
    return dev_map.get(auto_lbl, "waverover")


class HexapodController:
    """
    Controller for a 6-Leg Hexapod Robot having an ESP-32-Touch-LCD connected to 2 PCA9685 Servo Drivers over Bluetooth or USB-CDC Serial.
    Bluetooth Broadcast Name: 'hexapod-touch-lcd' / 'hexapod'

    6 Legs with 3 Degrees of Freedom (DoF) each = 18 Servos total:
      Legs:
        FL: Front Left  (Driver 1 / I2C 0x40 - Channels 0, 1, 2: Coxa, Femur, Tibia)
        ML: Middle Left (Driver 1 / I2C 0x40 - Channels 3, 4, 5: Coxa, Femur, Tibia)
        RL: Rear Left   (Driver 1 / I2C 0x40 - Channels 6, 7, 8: Coxa, Femur, Tibia)
        FR: Front Right (Driver 2 / I2C 0x41 - Channels 0, 1, 2: Coxa, Femur, Tibia)
        MR: Middle Right(Driver 2 / I2C 0x41 - Channels 3, 4, 5: Coxa, Femur, Tibia)
        RR: Rear Right  (Driver 2 / I2C 0x41 - Channels 6, 7, 8: Coxa, Femur, Tibia)
    """

    LEGS = {
        "FL": {"name": "Front Left",   "driver": 1, "channels": {"coxa": 0, "femur": 1, "tibia": 2}},
        "ML": {"name": "Middle Left",  "driver": 1, "channels": {"coxa": 3, "femur": 4, "tibia": 5}},
        "RL": {"name": "Rear Left",    "driver": 1, "channels": {"coxa": 6, "femur": 7, "tibia": 8}},
        "FR": {"name": "Front Right",  "driver": 2, "channels": {"coxa": 0, "femur": 1, "tibia": 2}},
        "MR": {"name": "Middle Right", "driver": 2, "channels": {"coxa": 3, "femur": 4, "tibia": 5}},
        "RR": {"name": "Rear Right",   "driver": 2, "channels": {"coxa": 6, "femur": 7, "tibia": 8}},
    }

    def __init__(self, bt_port="hexapod-touch-lcd"):
        self.bt_port = bt_port
        self.serial_conn = None
        self.simulated = True
        self.connected_device = "Not Connected"

        # Track joint positions for 18 servos: (leg_code, joint) -> angle (0-180)
        self.servo_angles = {}
        for leg_code in self.LEGS:
            self.servo_angles[(leg_code, "coxa")] = 90
            self.servo_angles[(leg_code, "femur")] = 90
            self.servo_angles[(leg_code, "tibia")] = 90

        self.current_motion = "idle"
        self.motion_thread = None
        self.stop_motion_flag = False
        self.sonar_distances = {"front": 50, "rear": 110, "left": 35, "right": 90}
        self.gui_window = None

    def parse_sonar_telemetry(self, line: str):
        line = line.strip()
        if line.startswith("SONAR:"):
            parts = line.split(":")
            try:
                if len(parts) >= 9:
                    self.sonar_distances["front"] = int(parts[2])
                    self.sonar_distances["rear"] = int(parts[4])
                    self.sonar_distances["left"] = int(parts[6])
                    self.sonar_distances["right"] = int(parts[8])
            except Exception:
                pass

    def get_sonar_distances(self) -> dict:
        if self.simulated:
            import random
            return {
                "front_cm": random.randint(30, 160),
                "rear_cm": random.randint(70, 180),
                "left_cm": random.randint(20, 100),
                "right_cm": random.randint(45, 120),
                "status": "simulated",
                "device": "Hexapod"
            }
        return {
            "front_cm": self.sonar_distances.get("front", 999),
            "rear_cm": self.sonar_distances.get("rear", 999),
            "left_cm": self.sonar_distances.get("left", 999),
            "right_cm": self.sonar_distances.get("right", 999),
            "status": "connected",
            "device": "Hexapod"
        }

    def connect(self, bt_port=None):
        import serial
        if bt_port is not None:
            self.bt_port = bt_port

        if self.bt_port in (None, "None (Simulation Mode)", "None"):
            self.simulated = True
            self.connected_device = "Simulation Mode"
            if self.serial_conn and self.serial_conn.is_open:
                try:
                    self.serial_conn.close()
                except Exception:
                    pass
            self.serial_conn = None
            return True, "Hexapod ESP-32-Touch-LCD set to Simulation Mode"

        try:
            target = self.bt_port
            if not target.upper().startswith("COM") and not target.startswith("/dev/"):
                opts, d_map, auto_lbl = scan_bluetooth_ports()
                if target in d_map:
                    target = d_map[target]
                else:
                    target = "hexapod-touch-lcd"

            if target.upper().startswith("COM") or target.startswith("/dev/"):
                conn = serial.Serial()
                conn.port = target
                conn.baudrate = 115200
                conn.timeout = 1
                conn.open()
                self.serial_conn = conn
                self.simulated = False
                self.connected_device = f"ESP-32-Touch-LCD ({target})"
                return True, f"Connected to Hexapod ESP-32-Touch-LCD on {target}"
            else:
                self.simulated = True
                self.connected_device = f"ESP-32-Touch-LCD '{target}' (Simulated)"
                return True, f"Hexapod ESP-32-Touch-LCD ('{target}') connected (Simulated mode)"
        except Exception as e:
            self.simulated = True
            self.connected_device = f"Simulation Mode (Error: {e})"
            return True, f"Hexapod ESP-32-Touch-LCD fallback to Simulation Mode ({e})"

    def send_bt_command(self, cmd_str: str):
        if self.serial_conn and self.serial_conn.is_open:
            try:
                self.serial_conn.write(cmd_str.encode("utf-8"))
                self.serial_conn.flush()
                print(f"[Hexapod BT Sent] {cmd_str.strip()}")
            except Exception as e:
                print(f"[Hexapod BT Error] {e}")
        else:
            print(f"[Simulated Hexapod BT] {cmd_str.strip()}")

    def set_joint_angle(self, leg_code: str, joint: str, angle: int):
        leg_code = leg_code.upper()
        joint = joint.lower()
        if leg_code in self.LEGS and joint in ("coxa", "femur", "tibia"):
            angle = max(0, min(180, int(angle)))
            self.servo_angles[(leg_code, joint)] = angle
            leg_info = self.LEGS[leg_code]
            driver = leg_info["driver"]
            chan = leg_info["channels"][joint]
            cmd = f"HEX:SERVO:{driver}:{chan}:{angle}\r\n"
            self.send_bt_command(cmd)

            if self.gui_window:
                if hasattr(self.gui_window, 'update_hexapod_joint_slider'):
                    self.gui_window.update_hexapod_joint_slider(leg_code, joint, angle)
                elif hasattr(self.gui_window, 'update_shobots_joint_slider'):
                    self.gui_window.update_shobots_joint_slider(leg_code, joint, angle)
            return {"status": "success", "leg": leg_code, "joint": joint, "angle": angle}
        return {"status": "error", "message": f"Invalid leg '{leg_code}' or joint '{joint}'"}

    def move_leg_ik(self, leg_code: str, x: float, y: float, z: float, duration_ms: int = 200) -> dict:
        leg_code = leg_code.upper()
        if leg_code in self.LEGS:
            cmd = f"HEX:IK:{leg_code}:{x}:{y}:{z}:{duration_ms}\r\n"
            self.send_bt_command(cmd)
            return {"status": "success", "leg": leg_code, "x": x, "y": y, "z": z, "duration": duration_ms}
        return {"status": "error", "message": f"Invalid leg code '{leg_code}'"}

    def stop_current_motion(self):
        self.stop_motion_flag = True
        if self.motion_thread and self.motion_thread.is_alive():
            self.motion_thread.join(timeout=0.5)
        self.stop_motion_flag = False

    def execute_action(self, action_name: str) -> dict:
        action = action_name.lower().replace(" ", "_")
        self.stop_current_motion()

        if action in ("stand", "sit", "flat_to_floor", "flat"):
            self.current_motion = action
            if action == "stand":
                self._apply_posture({"coxa": 90, "femur": 90, "tibia": 90})
                msg = "Hexapod Standing Upright (3 DoF per leg)"
            elif action == "sit":
                self._apply_posture({"coxa": 90, "femur": 30, "tibia": 150})
                msg = "Hexapod Sitting Down"
            else: # flat
                self._apply_posture({"coxa": 90, "femur": 0, "tibia": 0})
                msg = "Hexapod Flat to Floor"

            self.send_bt_command(f"HEX:{action}\r\n")
            return {"status": "success", "action": action, "message": msg}

        elif action in ("walk", "run", "wave_left_arm", "wave_right_arm", "dance", "turn_left", "turn_right", "bow"):
            self.current_motion = action
            import threading
            self.stop_motion_flag = False

            if action == "walk":
                target_func = self._loop_walk
            elif action == "run":
                target_func = self._loop_run
            elif action == "wave_left_arm":
                target_func = self._loop_wave_left
            elif action == "wave_right_arm":
                target_func = self._loop_wave_right
            elif action == "dance":
                target_func = self._loop_dance
            elif action == "turn_left":
                target_func = self._loop_turn_left
            elif action == "turn_right":
                target_func = self._loop_turn_right
            elif action == "bow":
                target_func = self._loop_bow

            self.motion_thread = threading.Thread(target=target_func, daemon=True)
            self.motion_thread.start()
            self.send_bt_command(f"HEX:{action}\r\n")
            return {"status": "success", "action": action, "message": f"Executing motion '{action_name}' on Hexapod"}

        elif action == "stop":
            self.current_motion = "idle"
            self.send_bt_command("HEX:stop\r\n")
            return {"status": "success", "action": "stop", "message": "Hexapod motion stopped"}

        elif action == "set_lcd_message":
            return {"status": "success", "action": "set_lcd_message", "message": "ESP-32-Touch-LCD message updated"}

        else:
            return {"status": "error", "message": f"Unknown hexapod action '{action_name}'"}

    def set_lcd_message(self, message: str) -> dict:
        """Sends a custom status/message string to display on the ESP-32-Touch-LCD screen."""
        self.send_bt_command(f"HEX:LCD:MSG:{message}\r\n")
        return {"status": "success", "message": f"Sent LCD Message to ESP-32-Touch-LCD: '{message}'"}

    def _apply_posture(self, joint_angles: dict):
        for leg_code in self.LEGS:
            for joint, angle in joint_angles.items():
                self.set_joint_angle(leg_code, joint, angle)

    def _loop_walk(self):
        import time
        group_a = ["FL", "MR", "RL"]
        group_b = ["FR", "ML", "RR"]
        delay = 0.2
        while not self.stop_motion_flag:
            for leg in group_a:
                self.set_joint_angle(leg, "femur", 120)
                self.set_joint_angle(leg, "coxa", 120)
            for leg in group_b:
                self.set_joint_angle(leg, "femur", 90)
                self.set_joint_angle(leg, "coxa", 60)
            time.sleep(delay)
            if self.stop_motion_flag: break

            for leg in group_a:
                self.set_joint_angle(leg, "femur", 90)
            time.sleep(delay)
            if self.stop_motion_flag: break

            for leg in group_b:
                self.set_joint_angle(leg, "femur", 120)
                self.set_joint_angle(leg, "coxa", 120)
            for leg in group_a:
                self.set_joint_angle(leg, "femur", 90)
                self.set_joint_angle(leg, "coxa", 60)
            time.sleep(delay)
            if self.stop_motion_flag: break

            for leg in group_b:
                self.set_joint_angle(leg, "femur", 90)
            time.sleep(delay)

    def _loop_run(self):
        import time
        group_a = ["FL", "MR", "RL"]
        group_b = ["FR", "ML", "RR"]
        delay = 0.08
        while not self.stop_motion_flag:
            for leg in group_a:
                self.set_joint_angle(leg, "femur", 135)
                self.set_joint_angle(leg, "coxa", 130)
            for leg in group_b:
                self.set_joint_angle(leg, "femur", 85)
                self.set_joint_angle(leg, "coxa", 50)
            time.sleep(delay)
            if self.stop_motion_flag: break

            for leg in group_a:
                self.set_joint_angle(leg, "femur", 85)
            time.sleep(delay)
            if self.stop_motion_flag: break

            for leg in group_b:
                self.set_joint_angle(leg, "femur", 135)
                self.set_joint_angle(leg, "coxa", 130)
            for leg in group_a:
                self.set_joint_angle(leg, "femur", 85)
                self.set_joint_angle(leg, "coxa", 50)
            time.sleep(delay)
            if self.stop_motion_flag: break

            for leg in group_b:
                self.set_joint_angle(leg, "femur", 85)
            time.sleep(delay)

    def _loop_wave_left(self):
        import time
        self.set_joint_angle("FL", "femur", 140)
        self.set_joint_angle("FL", "tibia", 40)
        while not self.stop_motion_flag:
            self.set_joint_angle("FL", "coxa", 60)
            time.sleep(0.25)
            if self.stop_motion_flag: break
            self.set_joint_angle("FL", "coxa", 120)
            time.sleep(0.25)

    def _loop_wave_right(self):
        import time
        self.set_joint_angle("FR", "femur", 140)
        self.set_joint_angle("FR", "tibia", 40)
        while not self.stop_motion_flag:
            self.set_joint_angle("FR", "coxa", 60)
            time.sleep(0.25)
            if self.stop_motion_flag: break
            self.set_joint_angle("FR", "coxa", 120)
            time.sleep(0.25)

    def _loop_dance(self):
        import time
        while not self.stop_motion_flag:
            for leg in ["FL", "ML", "RL"]:
                self.set_joint_angle(leg, "femur", 60)
                self.set_joint_angle(leg, "coxa", 110)
            for leg in ["FR", "MR", "RR"]:
                self.set_joint_angle(leg, "femur", 120)
                self.set_joint_angle(leg, "coxa", 70)
            time.sleep(0.3)
            if self.stop_motion_flag: break

            self._apply_posture({"coxa": 90, "femur": 50, "tibia": 130})
            time.sleep(0.3)
            if self.stop_motion_flag: break

            for leg in ["FL", "ML", "RL"]:
                self.set_joint_angle(leg, "femur", 120)
                self.set_joint_angle(leg, "coxa", 70)
            for leg in ["FR", "MR", "RR"]:
                self.set_joint_angle(leg, "femur", 60)
                self.set_joint_angle(leg, "coxa", 110)
            time.sleep(0.3)
            if self.stop_motion_flag: break

            self._apply_posture({"coxa": 90, "femur": 120, "tibia": 60})
            time.sleep(0.3)

    def _loop_turn_left(self):
        import time
        while not self.stop_motion_flag:
            for leg in ["FL", "ML", "RL", "FR", "MR", "RR"]:
                self.set_joint_angle(leg, "femur", 120)
                self.set_joint_angle(leg, "coxa", 120)
            time.sleep(0.2)
            if self.stop_motion_flag: break
            for leg in ["FL", "ML", "RL", "FR", "MR", "RR"]:
                self.set_joint_angle(leg, "femur", 90)
                self.set_joint_angle(leg, "coxa", 60)
            time.sleep(0.2)

    def _loop_turn_right(self):
        import time
        while not self.stop_motion_flag:
            for leg in ["FL", "ML", "RL", "FR", "MR", "RR"]:
                self.set_joint_angle(leg, "femur", 120)
                self.set_joint_angle(leg, "coxa", 60)
            time.sleep(0.2)
            if self.stop_motion_flag: break
            for leg in ["FL", "ML", "RL", "FR", "MR", "RR"]:
                self.set_joint_angle(leg, "femur", 90)
                self.set_joint_angle(leg, "coxa", 120)
            time.sleep(0.2)

    def _loop_bow(self):
        import time
        while not self.stop_motion_flag:
            self.set_joint_angle("FL", "femur", 30)
            self.set_joint_angle("FR", "femur", 30)
            self.set_joint_angle("RL", "femur", 130)
            self.set_joint_angle("RR", "femur", 130)
            time.sleep(0.5)
            if self.stop_motion_flag: break
            self._apply_posture({"coxa": 90, "femur": 90, "tibia": 90})
            time.sleep(0.5)


# Compatibility alias
ShobotsController = HexapodController


class RobotArmController:
    """
    Controller for 6-DOF Robot Arm attached to ESP32 via PCA9685 I2C Servo Driver.
    Servos:
      - Ch 0: Base / Waist Rotation (0 - 180 deg)
      - Ch 1: Shoulder Pitch (0 - 180 deg)
      - Ch 2: Elbow Pitch (0 - 180 deg)
      - Ch 3: Wrist Pitch (0 - 180 deg)
      - Ch 4: Wrist Roll (0 - 180 deg)
      - Ch 5: Gripper / Claw (0 - 180 deg)
    """
    def __init__(self, audio_loop):
        self.audio_loop = audio_loop
        self.gui_window = None
        self.motion_thread = None
        self.stop_motion_flag = False
        self.current_motion = "idle"
        # Track current servo positions (channel -> angle)
        self.current_angles = {0: 90, 1: 90, 2: 90, 3: 90, 4: 90, 5: 40}

    def set_gui_window(self, gui_window):
        self.gui_window = gui_window

    def set_joint_angle(self, channel: int, angle: int):
        angle = max(0, min(180, int(angle)))
        self.current_angles[channel] = angle
        res = self.audio_loop.set_servo_angle(board="arm", channel=channel, angle=angle)
        if self.gui_window and hasattr(self.gui_window, 'update_arm_joint_slider'):
            self.gui_window.update_arm_joint_slider(channel, angle)
        return res

    def move_arm_ik(self, x: float, y: float, z: float, pitch: float = 0, roll: float = 90, claw: float = 40, duration_ms: int = 250) -> dict:
        cmd_str = f"ARM:IK:{x}:{y}:{z}:{pitch}:{roll}:{claw}:{duration_ms}\r\n"
        if self.audio_loop and self.audio_loop.serial_arm and self.audio_loop.serial_arm.is_open:
            try:
                self.audio_loop.serial_arm.write(cmd_str.encode("utf-8"))
                self.audio_loop.serial_arm.flush()
                print(f"[Robot Arm IK Sent] {cmd_str.strip()}")
                return {"status": "success", "x": x, "y": y, "z": z, "pitch": pitch, "roll": roll, "claw": claw, "duration": duration_ms}
            except Exception as e:
                print(f"[Robot Arm IK Error] {e}")
        print(f"[Simulated Robot Arm IK] {cmd_str.strip()}")
        return {"status": "success", "simulated": True, "x": x, "y": y, "z": z, "pitch": pitch, "roll": roll, "claw": claw}

    def stop_current_motion(self):
        self.stop_motion_flag = True
        if self.motion_thread and self.motion_thread.is_alive():
            self.motion_thread.join(timeout=0.5)
        self.stop_motion_flag = False

    def send_arm_hardware_command(self, cmd_str: str):
        if self.audio_loop and self.audio_loop.serial_arm and self.audio_loop.serial_arm.is_open:
            try:
                self.audio_loop.serial_arm.write(f"{cmd_str}\r\n".encode("utf-8"))
                self.audio_loop.serial_arm.flush()
                print(f"[Robot Arm HW Sent] {cmd_str.strip()}")
            except Exception as e:
                print(f"[Robot Arm HW Error] {e}")
        else:
            print(f"[Simulated Robot Arm HW] {cmd_str.strip()}")

    def execute_action(self, action_name: str) -> dict:
        action = action_name.lower().replace(" ", "_")
        self.stop_current_motion()

        # Send direct hardware command to ESP32 Firmware for hardware-interpolated execution
        self.send_arm_hardware_command(f"ARM:{action}")

        if action in ("home", "rest", "reach", "open_gripper", "close_gripper"):
            self.current_motion = action
            if action == "home":
                target = {0: 90, 1: 90, 2: 90, 3: 90, 4: 90, 5: 40}
                msg = "Robot Arm Home Position (Default 90°)"
            elif action == "rest":
                target = {0: 90, 1: 30, 2: 150, 3: 120, 4: 90, 5: 10}
                msg = "Robot Arm Rest / Standby Position"
            elif action == "reach":
                target = {0: 90, 1: 120, 2: 60, 3: 90, 4: 90, 5: 60}
                msg = "Robot Arm Reach Forward Position"
            elif action == "open_gripper":
                target = {5: 20}
                msg = "Robot Arm Gripper Opened"
            elif action == "close_gripper":
                target = {5: 100}
                msg = "Robot Arm Gripper Closed"

            import threading
            self.motion_thread = threading.Thread(target=self._smooth_move, args=(target,), daemon=True)
            self.motion_thread.start()
            return {"status": "success", "action": action, "message": msg}

        elif action in ("yes", "no", "high_five", "wave", "bow", "dance", "pick_and_place"):
            self.current_motion = action
            import threading
            self.stop_motion_flag = False

            if action == "yes":
                target_func = self._loop_yes
            elif action == "no":
                target_func = self._loop_no
            elif action == "high_five":
                target_func = self._loop_high_five
            elif action == "wave":
                target_func = self._loop_wave
            elif action == "bow":
                target_func = self._loop_bow
            elif action == "dance":
                target_func = self._loop_dance
            elif action == "pick_and_place":
                target_func = self._loop_pick_and_place

            self.motion_thread = threading.Thread(target=target_func, daemon=True)
            self.motion_thread.start()
            return {"status": "success", "action": action, "message": f"Executing arm routine '{action_name}'"}

        elif action == "stop":
            self.current_motion = "idle"
            return {"status": "success", "action": "stop", "message": "Robot arm motion stopped"}
        else:
            return {"status": "error", "message": f"Unknown robot arm action '{action_name}'"}

    def _smooth_move(self, target_angles: dict, steps=10, delay=0.03):
        import time
        start_angles = {ch: self.current_angles.get(ch, 90) for ch in target_angles}
        for s in range(1, steps + 1):
            if self.stop_motion_flag:
                break
            fraction = s / float(steps)
            for ch, end_a in target_angles.items():
                start_a = start_angles[ch]
                curr_a = int(start_a + (end_a - start_a) * fraction)
                self.set_joint_angle(ch, curr_a)
            time.sleep(delay)

    def _loop_yes(self):
        """Gesture 'Yes': Nods arm up and down smoothly 3 times."""
        import time
        self._smooth_move({0: 90, 1: 90, 2: 90, 3: 90, 4: 90, 5: 40}, steps=8, delay=0.02)
        for _ in range(3):
            if self.stop_motion_flag:
                break
            self._smooth_move({1: 110, 3: 60}, steps=8, delay=0.025)
            time.sleep(0.1)
            if self.stop_motion_flag:
                break
            self._smooth_move({1: 75, 3: 120}, steps=8, delay=0.025)
            time.sleep(0.1)
        self._smooth_move({0: 90, 1: 90, 2: 90, 3: 90, 4: 90, 5: 40}, steps=8, delay=0.02)
        self.current_motion = "idle"

    def _loop_no(self):
        """Gesture 'No': Rotates arm base left and right smoothly 3 times."""
        import time
        self._smooth_move({0: 90, 1: 90, 2: 90, 3: 90, 4: 90, 5: 40}, steps=8, delay=0.02)
        for _ in range(3):
            if self.stop_motion_flag:
                break
            self._smooth_move({0: 50, 4: 60}, steps=8, delay=0.025)
            time.sleep(0.1)
            if self.stop_motion_flag:
                break
            self._smooth_move({0: 130, 4: 120}, steps=8, delay=0.025)
            time.sleep(0.1)
        self._smooth_move({0: 90, 1: 90, 2: 90, 3: 90, 4: 90, 5: 40}, steps=8, delay=0.02)
        self.current_motion = "idle"

    def _loop_high_five(self):
        """Gesture 'High Five': Raises arm up, extends palm open facing forward, holds, then returns."""
        import time
        self._smooth_move({0: 90, 1: 140, 2: 40, 3: 90, 4: 90, 5: 110}, steps=15, delay=0.03)
        time.sleep(2.0)
        self._smooth_move({0: 90, 1: 90, 2: 90, 3: 90, 4: 90, 5: 40}, steps=12, delay=0.03)
        self.current_motion = "idle"

    def _loop_wave(self):
        """Gesture 'Wave': Lifts arm high and rotates wrist roll/waist side to side 3 times."""
        import time
        self._smooth_move({0: 90, 1: 130, 2: 50, 3: 90, 4: 90, 5: 80}, steps=12, delay=0.025)
        for _ in range(3):
            if self.stop_motion_flag:
                break
            self._smooth_move({0: 75, 4: 45}, steps=6, delay=0.02)
            time.sleep(0.08)
            if self.stop_motion_flag:
                break
            self._smooth_move({0: 105, 4: 135}, steps=6, delay=0.02)
            time.sleep(0.08)
        self._smooth_move({0: 90, 1: 90, 2: 90, 3: 90, 4: 90, 5: 40}, steps=10, delay=0.025)
        self.current_motion = "idle"

    def _loop_bow(self):
        """Gesture 'Bow': Gracefully lowers arm forward in a bow, holds, and returns up."""
        import time
        self._smooth_move({0: 90, 1: 45, 2: 135, 3: 45, 4: 90, 5: 40}, steps=15, delay=0.03)
        time.sleep(1.2)
        self._smooth_move({0: 90, 1: 90, 2: 90, 3: 90, 4: 90, 5: 40}, steps=12, delay=0.03)
        self.current_motion = "idle"

    def _loop_dance(self):
        """Gesture 'Dance': Playful multi-joint motion routine."""
        import time
        for _ in range(2):
            if self.stop_motion_flag:
                break
            self._smooth_move({0: 45, 1: 110, 2: 70, 3: 110, 4: 45, 5: 100}, steps=8, delay=0.02)
            time.sleep(0.1)
            if self.stop_motion_flag:
                break
            self._smooth_move({0: 135, 1: 70, 2: 110, 3: 70, 4: 135, 5: 20}, steps=8, delay=0.02)
            time.sleep(0.1)
        self._smooth_move({0: 90, 1: 90, 2: 90, 3: 90, 4: 90, 5: 40}, steps=10, delay=0.025)
        self.current_motion = "idle"

    def _loop_pick_and_place(self):
        """Demonstration: Automated Pick & Place sequence."""
        import time
        self._smooth_move({0: 50, 1: 115, 2: 65, 3: 90, 4: 90, 5: 20}, steps=12, delay=0.03)
        time.sleep(0.3)
        self._smooth_move({5: 100}, steps=8, delay=0.03)
        time.sleep(0.4)
        self._smooth_move({1: 75, 2: 105}, steps=10, delay=0.03)
        time.sleep(0.3)
        self._smooth_move({0: 130}, steps=12, delay=0.03)
        time.sleep(0.3)
        self._smooth_move({1: 115, 2: 65}, steps=10, delay=0.03)
        time.sleep(0.2)
        self._smooth_move({5: 20}, steps=8, delay=0.03)
        time.sleep(0.3)
        self._smooth_move({0: 90, 1: 90, 2: 90, 3: 90, 4: 90, 5: 40}, steps=12, delay=0.03)
        self.current_motion = "idle"


class WaveRoverController:
    """
    Controller for Standard 4-Motor AWD Mobile Platform powered by ESP32 (with 2 x LM298 Reversing Dual Motor Drivers).
    Features:
      - Dual LM298 Motor Drivers:
          * Front LM298: Channel A (Front Left Motor), Channel B (Front Right Motor)
          * Rear LM298: Channel A (Rear Left Motor), Channel B (Rear Right Motor)
      - 4WD AWD Differential Drive (forward, back, turn_left, turn_right, spin_left, spin_right, stop)
      - Eye LEDs Output: Digital GPIO Output for Eye LEDs (ON, OFF, Blink, Pulse)
      - Pan-Tilt Camera Servos (Pan 0-180°, Tilt 0-180°)
      - Front Headlights LED Output
      - Onboard Status Messages
      - Preset Routines: patrol, spin_360, dance, obstacle_avoidance
      - Onboard MAX98357A I2S Audio Synthesis
    """
    def __init__(self, port=None, audio_loop=None):
        self.port = port
        self.audio_loop = audio_loop
        self.serial_conn = None
        self.simulated = True
        self.connected_device = "Not Connected"
        
        self.mouth_state = False       # False = Closed, True = Open
        self.body_motor_state = False  # False = Off, True = On
        self.eye_leds_state = True     # True = ON, False = OFF
        self.headlight_state = False  # True = ON, False = OFF
        self.speed = 75                # 0 - 100%
        self.pan_angle = 90            # 0 - 180 deg
        self.tilt_angle = 90           # 0 - 180 deg
        self.current_motion = "idle"
        self.sonar_distances = {"front": 45, "rear": 120, "left": 30, "right": 85}
        self.gui_window = None

    def parse_sonar_telemetry(self, line: str):
        line = line.strip()
        if line.startswith("SONAR:"):
            parts = line.split(":")
            try:
                if len(parts) >= 9:
                    self.sonar_distances["front"] = int(parts[2])
                    self.sonar_distances["rear"] = int(parts[4])
                    self.sonar_distances["left"] = int(parts[6])
                    self.sonar_distances["right"] = int(parts[8])
            except Exception:
                pass

    def get_sonar_distances(self) -> dict:
        if self.simulated:
            import random
            return {
                "front_cm": random.randint(35, 150),
                "rear_cm": random.randint(80, 200),
                "left_cm": random.randint(25, 90),
                "right_cm": random.randint(40, 110),
                "status": "simulated",
                "device": "WaveRover"
            }
        return {
            "front_cm": self.sonar_distances.get("front", 999),
            "rear_cm": self.sonar_distances.get("rear", 999),
            "left_cm": self.sonar_distances.get("left", 999),
            "right_cm": self.sonar_distances.get("right", 999),
            "status": "connected",
            "device": "WaveRover"
        }

    def connect(self, port=None):
        import serial
        if port is not None:
            self.port = port

        if self.port in (None, "None (Simulation Mode)", "None"):
            self.simulated = True
            self.connected_device = "Simulation Mode"
            if self.serial_conn and self.serial_conn.is_open:
                try:
                    self.serial_conn.close()
                except Exception:
                    pass
            self.serial_conn = None
            return True, "4WD Rover set to Simulation Mode"

        try:
            target = self.port
            if not target.upper().startswith("COM") and not target.startswith("/dev/"):
                opts, d_map, auto_lbl = scan_rover_bluetooth_ports()
                if target in d_map:
                    target = d_map[target]
                elif target.lower() in ("waverover", "wave-rover", "rover", "waverover-touch-7c"):
                    # Attempt to resolve Bluetooth or serial port matching rover
                    for opt, dev_name in d_map.items():
                        if dev_name and (dev_name.upper().startswith("COM") or dev_name.startswith("/dev/")):
                            target = dev_name
                            break

            if target.upper().startswith("COM") or target.startswith("/dev/"):
                conn = serial.Serial()
                conn.port = target
                conn.baudrate = 115200
                conn.timeout = 1
                conn.open()
                self.serial_conn = conn
                self.simulated = False
                self.connected_device = f"4WD Rover Dual LM298 ({target})"
                return True, f"Connected to 4WD Rover (Dual LM298) via Bluetooth / Serial on {target}"
            else:
                self.simulated = True
                self.connected_device = f"4WD Rover '{target}' (Simulated)"
                return True, f"4WD Rover ('{target}') connected (Simulated mode)"
        except Exception as e:
            self.simulated = True
            self.connected_device = f"Simulation Mode (Error: {e})"
            return True, f"4WD Rover fallback to Simulation Mode ({e})"

    def send_command(self, cmd_str: str):
        if not cmd_str.endswith("\r\n"):
            cmd_str += "\r\n"
        if self.serial_conn and self.serial_conn.is_open:
            try:
                self.serial_conn.write(cmd_str.encode("utf-8"))
                self.serial_conn.flush()
                print(f"[Wave Rover Sent] {cmd_str.strip()}")
            except Exception as e:
                print(f"[Wave Rover Error] {e}")
        else:
            print(f"[Simulated Wave Rover] {cmd_str.strip()}")

    def set_mouth_state(self, open_mouth: bool):
        self.mouth_state = bool(open_mouth)
        cmd = f"ROVER:MOUTH:{1 if self.mouth_state else 0}\r\n"
        self.send_command(cmd)
        return {"status": "success", "mouth_open": self.mouth_state, "message": f"DC Mouth Motor {'Power ON (Open)' if self.mouth_state else 'Power OFF (Closed)'}"}

    def set_body_motor_state(self, enable_body: bool):
        self.body_motor_state = bool(enable_body)
        cmd = f"ROVER:BODY:{1 if self.body_motor_state else 0}\r\n"
        self.send_command(cmd)
        return {"status": "success", "body_motor": self.body_motor_state, "message": f"DC Body Up/Down Motor {'Power ON' if self.body_motor_state else 'Power OFF'}"}

    def set_eye_leds(self, enable_eyes: bool):
        self.eye_leds_state = bool(enable_eyes)
        cmd = f"ROVER:EYES:{1 if self.eye_leds_state else 0}\r\n"
        self.send_command(cmd)
        return {"status": "success", "eye_leds": self.eye_leds_state, "message": f"Eye LEDs {'ON' if self.eye_leds_state else 'OFF'}"}

    def set_headlight(self, enable_headlight: bool):
        self.headlight_state = bool(enable_headlight)
        cmd = f"ROVER:LED:{1 if self.headlight_state else 0}\r\n"
        self.send_command(cmd)
        return {"status": "success", "headlight": self.headlight_state, "message": f"Headlights {'ON' if self.headlight_state else 'OFF'}"}

    def set_speed(self, speed: int):
        self.speed = max(0, min(100, int(speed)))
        cmd = f"ROVER:SPEED:{self.speed}\r\n"
        self.send_command(cmd)
        return {"status": "success", "speed": self.speed}

    def set_pan_tilt(self, pan: int = None, tilt: int = None):
        if pan is not None: self.pan_angle = max(0, min(180, int(pan)))
        if tilt is not None: self.tilt_angle = max(0, min(180, int(tilt)))
        cmd = f"ROVER:PANTILT:{self.pan_angle}:{self.tilt_angle}\r\n"
        self.send_command(cmd)
        return {"status": "success", "pan": self.pan_angle, "tilt": self.tilt_angle}

    def set_lcd_message(self, message: str):
        cmd = f"ROVER:LCD:MSG:{message}\r\n"
        self.send_command(cmd)
        return {"status": "success", "message": message}

    def execute_action(self, action_name: str, **kwargs) -> dict:
        action = action_name.lower().replace(" ", "_")
        self.current_motion = action

        # Check for L298N DC mouth motor / body motor / eye LED actions
        if action in ("mouth_open", "open_mouth"):
            return self.set_mouth_state(True)
        elif action in ("mouth_close", "close_mouth"):
            return self.set_mouth_state(False)
        elif action in ("body_on", "start_body"):
            return self.set_body_motor_state(True)
        elif action in ("body_off", "stop_body"):
            return self.set_body_motor_state(False)
        elif action in ("eyes_on", "turn_on_eyes"):
            return self.set_eye_leds(True)
        elif action in ("eyes_off", "turn_off_eyes"):
            return self.set_eye_leds(False)
        elif action in ("headlight_on", "turn_on_headlight"):
            return self.set_headlight(True)
        elif action in ("headlight_off", "turn_off_headlight"):
            return self.set_headlight(False)

        # Check for drive motion actions
        if action in ("forward", "back", "backward", "turn_left", "turn_right", "spin_left", "spin_right", "stop", "patrol", "spin_360", "dance", "obstacle_avoidance"):
            cmd = f"ROVER:{action}\r\n"
            self.send_command(cmd)
            return {"status": "success", "action": action, "message": f"Executed Wave Rover action '{action}'"}
        
        return {"status": "error", "message": f"Unknown Wave Rover action '{action_name}'"}


class ESP32PulseWindow:
    def __init__(self, audio_loop_instance):
        self.audio_loop = audio_loop_instance
        self.audio_loop.gui_window = self
        self.root = None
        self.status_label = None
        self.config = load_esp32_button_config()
        self.birds_combo = None
        self.hexapod_combo = None
        self.left_combo = None
        self.right_combo = None
        self.button_states = {}
        self.buttons = {}
        self.servo_sliders = {}
        self.servo_labels = {}
        self.port_device_map = {}

    def start_gui(self):
        import threading
        thread = threading.Thread(target=self._run, daemon=True)
        thread.start()

    def _run(self):
        import tkinter as tk
        from tkinter import ttk
        import threading

        self.root = tk.Tk()
        self.root.title("Shobots")
        self.root.geometry("960x780")
        self.root.configure(bg="#0f172a")

        # Styling
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("TFrame", background="#0f172a")
        style.configure("TLabelframe", background="#0f172a", foreground="#38bdf8", bordercolor="#334155")
        style.configure("TLabelframe.Label", background="#0f172a", foreground="#38bdf8", font=("Segoe UI", 11, "bold"))
        style.configure("TLabel", background="#0f172a", foreground="#f8fafc", font=("Segoe UI", 10))
        style.configure("Header.TLabel", font=("Segoe UI", 14, "bold"), foreground="#38bdf8")
        style.configure("SubHeader.TLabel", font=("Segoe UI", 9), foreground="#94a3b8")
        style.configure("TNotebook", background="#0f172a", borderwidth=0)
        style.configure("TNotebook.Tab", background="#1e293b", foreground="#94a3b8", font=("Segoe UI", 10, "bold"), padding=[12, 6])
        style.map("TNotebook.Tab", background=[("selected", "#0284c7")], foreground=[("selected", "#ffffff")])

        # Header Frame
        header_frame = ttk.Frame(self.root, padding=12)
        header_frame.pack(fill="x")

        title_lbl = ttk.Label(header_frame, text="🤖 Shobots", style="Header.TLabel")
        title_lbl.pack(anchor="w")

        # COM Ports Selection Bar
        ports_frame = ttk.Frame(header_frame, padding=(0, 8, 0, 0))
        ports_frame.pack(fill="x")

        # Birds Touchscreen COM selector (unified ESP32-S3 Touchscreen controlling both sides)
        ttk.Label(ports_frame, text="Birds Screen:").grid(row=0, column=0, sticky="w", padx=(0, 2))
        self.birds_combo = ttk.Combobox(ports_frame, state="readonly", width=13)
        self.birds_combo.grid(row=0, column=1, sticky="w", padx=(0, 4))
        self.left_combo = self.birds_combo
        self.right_combo = self.birds_combo

        # Hexapod COM selector (Auto-detected COM / BT port)
        ttk.Label(ports_frame, text="Hexapod:").grid(row=0, column=2, sticky="w", padx=(0, 2))
        self.hexapod_combo = ttk.Combobox(ports_frame, state="normal", width=13)
        self.hexapod_combo.grid(row=0, column=3, sticky="w", padx=(0, 4))

        # Arm COM selector
        ttk.Label(ports_frame, text="Arm:").grid(row=0, column=4, sticky="w", padx=(0, 2))
        self.arm_combo = ttk.Combobox(ports_frame, state="readonly", width=11)
        self.arm_combo.grid(row=0, column=5, sticky="w", padx=(0, 4))

        # Tello 7" LCD Screen COM selector
        ttk.Label(ports_frame, text="Tello Screen:").grid(row=0, column=6, sticky="w", padx=(0, 2))
        self.tello_combo = ttk.Combobox(ports_frame, state="readonly", width=12)
        self.tello_combo.grid(row=0, column=7, sticky="w", padx=(0, 4))

        def refresh_com_ports(auto_connect=False):
            options, dev_map, auto_birds, auto_hexapod, auto_arm, auto_tello = scan_and_autodetect_esp32_ports()
            self.port_device_map = dev_map

            hexapod_options = list(options)
            if "hexapod-touch-lcd (ESP-32-Touch-LCD BT/Serial)" not in hexapod_options:
                hexapod_options.insert(1, "hexapod-touch-lcd (ESP-32-Touch-LCD BT/Serial)")
                self.port_device_map["hexapod-touch-lcd (ESP-32-Touch-LCD BT/Serial)"] = "hexapod-touch-lcd"

            self.birds_combo['values'] = options
            self.hexapod_combo['values'] = hexapod_options
            self.arm_combo['values'] = options
            self.tello_combo['values'] = options

            curr_birds_dev = getattr(self.audio_loop, 'esp32_birds_port', None) or self.audio_loop.esp32_left_port
            curr_hexapod_dev = getattr(self.audio_loop, 'hexapod_port', getattr(self.audio_loop, 'hexapod_bt_port', None))
            curr_arm_dev = self.audio_loop.esp32_arm_port
            curr_tello_dev = getattr(self.audio_loop, 'esp32_tello_port', None)

            match_birds = [lbl for lbl, dev in dev_map.items() if dev == curr_birds_dev] if curr_birds_dev else []
            if match_birds:
                self.birds_combo.set(match_birds[0])
            elif auto_birds in options:
                self.birds_combo.set(auto_birds)
            else:
                self.birds_combo.set("None (Simulation Mode)")

            match_hexapod = [lbl for lbl, dev in self.port_device_map.items() if dev == curr_hexapod_dev] if curr_hexapod_dev else []
            if match_hexapod:
                self.hexapod_combo.set(match_hexapod[0])
            elif auto_hexapod in hexapod_options:
                self.hexapod_combo.set(auto_hexapod)
            else:
                self.hexapod_combo.set(hexapod_options[0] if hexapod_options else "None (Simulation Mode)")

            match_arm = [lbl for lbl, dev in dev_map.items() if dev == curr_arm_dev] if curr_arm_dev else []
            if match_arm:
                self.arm_combo.set(match_arm[0])
            elif auto_arm in options:
                self.arm_combo.set(auto_arm)
            else:
                self.arm_combo.set("None (Simulation Mode)")

            match_tello = [lbl for lbl, dev in dev_map.items() if dev == curr_tello_dev] if curr_tello_dev else []
            if match_tello:
                self.tello_combo.set(match_tello[0])
            elif auto_tello in options:
                self.tello_combo.set(auto_tello)
            else:
                self.tello_combo.set("None (Simulation Mode)")

            if hasattr(self, 'hexapod_bt_combo') and self.hexapod_bt_combo:
                self.hexapod_bt_combo['values'] = hexapod_options
                self.hexapod_bt_combo.set(self.hexapod_combo.get())

            if auto_connect:
                on_update_ports()
            else:
                self.update_status(f"COM Ports scanned. Found {len(options)-1} serial port(s).")

        def on_update_ports():
            sel_birds_lbl = self.birds_combo.get()
            sel_hexapod_lbl = self.hexapod_combo.get()
            sel_arm_lbl = self.arm_combo.get()
            sel_tello_lbl = self.tello_combo.get()
            dev_birds = self.port_device_map.get(sel_birds_lbl, sel_birds_lbl)
            dev_hexapod = self.port_device_map.get(sel_hexapod_lbl, sel_hexapod_lbl)
            dev_arm = self.port_device_map.get(sel_arm_lbl, sel_arm_lbl)
            dev_tello = self.port_device_map.get(sel_tello_lbl, sel_tello_lbl)
            _, msg_birds = self.audio_loop.connect_esp32("birds", dev_birds)
            _, msg_hexapod = self.audio_loop.connect_esp32("hexapod", dev_hexapod)
            _, msg_arm = self.audio_loop.connect_esp32("arm", dev_arm)
            _, msg_tello = self.audio_loop.connect_esp32("tello", dev_tello)
            if hasattr(self, 'hexapod_bt_combo') and self.hexapod_bt_combo:
                self.hexapod_bt_combo.set(sel_hexapod_lbl)
            if hasattr(self, 'drone_status_lbl') and self.drone_status_lbl:
                t_port_str = getattr(self.audio_loop, 'esp32_tello_port', None) or 'Simulation Mode'
                self.drone_status_lbl.config(text=f"ESP32-S3 7\" Touch LCD Status: {t_port_str}")
            self.update_status(f"{msg_birds} | {msg_hexapod} | {msg_arm} | {msg_tello}")

        def on_connect_birds_port():
            options, dev_map, auto_birds, _, _, _ = scan_and_autodetect_esp32_ports()
            self.port_device_map = dev_map
            self.birds_combo['values'] = options
            sel_birds_lbl = self.birds_combo.get()
            if not sel_birds_lbl or sel_birds_lbl not in options:
                sel_birds_lbl = auto_birds if auto_birds in options else "None (Simulation Mode)"
                self.birds_combo.set(sel_birds_lbl)
            dev_birds = self.port_device_map.get(sel_birds_lbl, sel_birds_lbl)
            _, msg_birds = self.audio_loop.connect_esp32("birds", dev_birds)
            self.update_status(f"[Birds ESP32-S3 Screen Connection] {msg_birds}")

        def on_connect_hexapod_port():
            options, dev_map, _, auto_hexapod, _, _ = scan_and_autodetect_esp32_ports()
            self.port_device_map = dev_map
            hexapod_options = list(options)
            if "hexapod-touch-lcd (ESP-32-Touch-LCD BT/Serial)" not in hexapod_options:
                hexapod_options.insert(1, "hexapod-touch-lcd (ESP-32-Touch-LCD BT/Serial)")
                self.port_device_map["hexapod-touch-lcd (ESP-32-Touch-LCD BT/Serial)"] = "hexapod-touch-lcd"
            self.hexapod_combo['values'] = hexapod_options
            sel_hexapod_lbl = self.hexapod_combo.get()
            if not sel_hexapod_lbl or sel_hexapod_lbl not in hexapod_options:
                sel_hexapod_lbl = auto_hexapod if auto_hexapod in hexapod_options else hexapod_options[0]
                self.hexapod_combo.set(sel_hexapod_lbl)
            dev_hexapod = self.port_device_map.get(sel_hexapod_lbl, sel_hexapod_lbl)
            _, msg_hexapod = self.audio_loop.connect_esp32("hexapod", dev_hexapod)
            if hasattr(self, 'hexapod_bt_combo') and self.hexapod_bt_combo:
                self.hexapod_bt_combo.set(sel_hexapod_lbl)
            self.update_status(f"[Hexapod Connection] {msg_hexapod}")

        def on_connect_arm_port():
            options, dev_map, _, _, auto_arm, _ = scan_and_autodetect_esp32_ports()
            self.port_device_map = dev_map
            self.arm_combo['values'] = options
            sel_arm_lbl = self.arm_combo.get()
            if not sel_arm_lbl or sel_arm_lbl not in options:
                sel_arm_lbl = auto_arm if auto_arm in options else "None (Simulation Mode)"
                self.arm_combo.set(sel_arm_lbl)
            dev_arm = self.port_device_map.get(sel_arm_lbl, sel_arm_lbl)
            _, msg_arm = self.audio_loop.connect_esp32("arm", dev_arm)
            if hasattr(self, 'arm_status_lbl') and self.arm_status_lbl:
                self.arm_status_lbl.config(text=f"Status: ESP32 Arm Port - {self.audio_loop.esp32_arm_port or 'Simulation Mode'}")
            self.update_status(f"[Arm ESP32 Connection] {msg_arm}")

        def on_connect_tello_port():
            options, dev_map, _, _, _, auto_tello = scan_and_autodetect_esp32_ports()
            self.port_device_map = dev_map
            self.tello_combo['values'] = options
            sel_tello_lbl = self.tello_combo.get()
            if not sel_tello_lbl or sel_tello_lbl not in options:
                sel_tello_lbl = auto_tello if auto_tello in options else "None (Simulation Mode)"
                self.tello_combo.set(sel_tello_lbl)
            dev_tello = self.port_device_map.get(sel_tello_lbl, sel_tello_lbl)
            _, msg_tello = self.audio_loop.connect_esp32("tello", dev_tello)
            if hasattr(self, 'drone_status_lbl') and self.drone_status_lbl:
                t_port_str = getattr(self.audio_loop, 'esp32_tello_port', None) or 'Simulation Mode'
                self.drone_status_lbl.config(text=f"ESP32-S3 7\" Touch LCD Status: {t_port_str}")
            self.update_status(f"[Tello ESP32 Screen Connection] {msg_tello}")

        connect_btn = tk.Button(
            ports_frame,
            text="⚡ Connect All",
            font=("Segoe UI", 9, "bold"),
            bg="#0284c7",
            fg="#ffffff",
            activebackground="#0369a1",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=5,
            command=on_update_ports
        )
        connect_btn.grid(row=0, column=8, padx=2)

        connect_birds_btn = tk.Button(
            ports_frame,
            text="🦜 Birds",
            font=("Segoe UI", 9, "bold"),
            bg="#0284c7",
            fg="#ffffff",
            activebackground="#0369a1",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=5,
            command=on_connect_birds_port
        )
        connect_birds_btn.grid(row=0, column=9, padx=2)

        connect_hexapod_btn = tk.Button(
            ports_frame,
            text="🤖 Hexapod",
            font=("Segoe UI", 9, "bold"),
            bg="#10b981",
            fg="#ffffff",
            activebackground="#059669",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=5,
            command=on_connect_hexapod_port
        )
        connect_hexapod_btn.grid(row=0, column=10, padx=2)

        connect_arm_btn = tk.Button(
            ports_frame,
            text="🔌 Arm",
            font=("Segoe UI", 9, "bold"),
            bg="#7c3aed",
            fg="#ffffff",
            activebackground="#6d28d9",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=5,
            command=on_connect_arm_port
        )
        connect_arm_btn.grid(row=0, column=11, padx=2)

        connect_tello_btn = tk.Button(
            ports_frame,
            text="🚁 Tello LCD",
            font=("Segoe UI", 9, "bold"),
            bg="#059669",
            fg="#ffffff",
            activebackground="#047857",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=5,
            command=on_connect_tello_port
        )
        connect_tello_btn.grid(row=0, column=12, padx=2)

        scan_btn = tk.Button(
            ports_frame,
            text="🔄 Rescan",
            font=("Segoe UI", 9, "bold"),
            bg="#334155",
            fg="#f8fafc",
            activebackground="#475569",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=5,
            command=lambda: refresh_com_ports(auto_connect=True)
        )
        scan_btn.grid(row=0, column=13, padx=2)

        # Status readout
        self.status_label = ttk.Label(header_frame, text="Status: Ready for commands", font=("Segoe UI", 10, "italic"), foreground="#a855f7")
        self.status_label.pack(anchor="w", pady=(6, 0))

        # Perform initial scan & auto-connect
        refresh_com_ports(auto_connect=True)

        # Divider
        ttk.Separator(self.root, orient="horizontal").pack(fill="x", padx=15, pady=2)

        # AI Speech Reactivity & Multi-Robot Dispatch Bar
        speech_frame = ttk.LabelFrame(self.root, text=" 🎙️ AI Speech Reactivity & Multi-Robot Dispatch (Birds • Hexapod • Robot Arm) ", padding=6)
        speech_frame.pack(fill="x", padx=15, pady=(2, 4))

        self.parrot_mode_var = tk.StringVar(value="left")
        self.mic_react_var = tk.BooleanVar(value=True)
        self.speech_birds_var = tk.BooleanVar(value=getattr(self.audio_loop, 'speech_react_birds', True))
        self.speech_hexapod_var = tk.BooleanVar(value=getattr(self.audio_loop, 'speech_react_hexapod', True))
        self.speech_arm_var = tk.BooleanVar(value=getattr(self.audio_loop, 'speech_react_arm', True))

        def on_toggle_robot_speech(robot_name):
            if robot_name == "birds":
                en = self.speech_birds_var.get()
                self.audio_loop.set_speech_reactivity("birds", en)
            elif robot_name == "hexapod":
                en = self.speech_hexapod_var.get()
                self.audio_loop.set_speech_reactivity("hexapod", en)
            elif robot_name == "arm":
                en = self.speech_arm_var.get()
                self.audio_loop.set_speech_reactivity("arm", en)

        def on_preset_speech_react(preset):
            if preset == "all":
                self.speech_birds_var.set(True)
                self.speech_hexapod_var.set(True)
                self.speech_arm_var.set(True)
                self.audio_loop.set_speech_reactivity("all", True)
            elif preset == "birds":
                self.speech_birds_var.set(True)
                self.speech_hexapod_var.set(False)
                self.speech_arm_var.set(False)
                self.audio_loop.set_speech_reactivity("birds", True)
                self.audio_loop.set_speech_reactivity("hexapod", False)
                self.audio_loop.set_speech_reactivity("arm", False)
            elif preset == "hexapod":
                self.speech_birds_var.set(False)
                self.speech_hexapod_var.set(True)
                self.speech_arm_var.set(False)
                self.audio_loop.set_speech_reactivity("birds", False)
                self.audio_loop.set_speech_reactivity("hexapod", True)
                self.audio_loop.set_speech_reactivity("arm", False)
            elif preset == "arm":
                self.speech_birds_var.set(False)
                self.speech_hexapod_var.set(False)
                self.speech_arm_var.set(True)
                self.audio_loop.set_speech_reactivity("birds", False)
                self.audio_loop.set_speech_reactivity("hexapod", False)
                self.audio_loop.set_speech_reactivity("arm", True)
            elif preset == "mute":
                self.speech_birds_var.set(False)
                self.speech_hexapod_var.set(False)
                self.speech_arm_var.set(False)
                self.audio_loop.set_speech_reactivity("all", False)

        def on_parrot_select(choice):
            self.parrot_mode_var.set(choice)
            self.audio_loop.set_active_parrot(choice)
            for key, btn in p_btns_dict.items():
                if key == choice:
                    btn.configure(relief="sunken", bd=2)
                else:
                    btn.configure(relief="flat", bd=1)

        def on_toggle_mic_react():
            en = self.mic_react_var.get()
            self.audio_loop.set_parrot_sound_reactivity(en)

        def on_test_speech_pulse():
            self.audio_loop.dispatch_speech_event(True)
            self.root.after(1500, lambda: self.audio_loop.dispatch_speech_event(False))
            self.update_status("Tested 1.5s speech animatronics pulse across all enabled robots")

        # Row 0: Multi-Robot Reactivity Checkboxes & Test Pulse
        row0_frame = ttk.Frame(speech_frame)
        row0_frame.pack(fill="x", pady=(0, 3))

        ttk.Label(row0_frame, text="Active Talking Robots:", font=("Segoe UI", 9, "bold"), foreground="#38bdf8").pack(side="left", padx=(0, 6))

        birds_chk = tk.Checkbutton(
            row0_frame,
            text="🦜 Birds (Mouth & Eyes)",
            variable=self.speech_birds_var,
            font=("Segoe UI", 9, "bold"),
            bg="#0f172a",
            fg="#34d399",
            selectcolor="#1e293b",
            activebackground="#0f172a",
            activeforeground="#34d399",
            cursor="hand2",
            command=lambda: on_toggle_robot_speech("birds")
        )
        birds_chk.pack(side="left", padx=4)

        hex_chk = tk.Checkbutton(
            row0_frame,
            text="🤖 Hexapod (Eyes & Sway)",
            variable=self.speech_hexapod_var,
            font=("Segoe UI", 9, "bold"),
            bg="#0f172a",
            fg="#38bdf8",
            selectcolor="#1e293b",
            activebackground="#0f172a",
            activeforeground="#38bdf8",
            cursor="hand2",
            command=lambda: on_toggle_robot_speech("hexapod")
        )
        hex_chk.pack(side="left", padx=4)

        arm_chk = tk.Checkbutton(
            row0_frame,
            text="🦾 Robot Arm (Gestures)",
            variable=self.speech_arm_var,
            font=("Segoe UI", 9, "bold"),
            bg="#0f172a",
            fg="#c084fc",
            selectcolor="#1e293b",
            activebackground="#0f172a",
            activeforeground="#c084fc",
            cursor="hand2",
            command=lambda: on_toggle_robot_speech("arm")
        )
        arm_chk.pack(side="left", padx=4)

        mic_chk = tk.Checkbutton(
            row0_frame,
            text="🎤 Mic Sound Reactivity",
            variable=self.mic_react_var,
            font=("Segoe UI", 9, "bold"),
            bg="#0f172a",
            fg="#fbbf24",
            selectcolor="#1e293b",
            activebackground="#0f172a",
            activeforeground="#fbbf24",
            cursor="hand2",
            command=on_toggle_mic_react
        )
        mic_chk.pack(side="left", padx=6)

        test_pulse_btn = tk.Button(
            row0_frame,
            text="⚡ Test Speech Pulse (1.5s)",
            font=("Segoe UI", 9, "bold"),
            bg="#0284c7",
            fg="#ffffff",
            activebackground="#0369a1",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=8,
            pady=2,
            command=on_test_speech_pulse
        )
        test_pulse_btn.pack(side="right", padx=2)

        # Row 1: Parrot Choice & Quick Presets
        row1_frame = ttk.Frame(speech_frame)
        row1_frame.pack(fill="x")

        ttk.Label(row1_frame, text="Parrot Speaker:", font=("Segoe UI", 9, "bold"), foreground="#94a3b8").pack(side="left", padx=(0, 4))

        p_btns_dict = {}
        p_choices = [
            ("👈 Left", "left", "#0284c7"),
            ("🦜 Both", "both", "#d97706"),
            ("👉 Right", "right", "#7c3aed"),
        ]
        for p_lbl, p_val, p_col in p_choices:
            p_btn = tk.Button(
                row1_frame,
                text=p_lbl,
                font=("Segoe UI", 8, "bold"),
                bg=p_col,
                fg="#ffffff",
                relief="sunken" if p_val == "left" else "flat",
                bd=2 if p_val == "left" else 1,
                cursor="hand2",
                padx=6,
                pady=1,
                command=lambda pv=p_val: on_parrot_select(pv)
            )
            p_btn.pack(side="left", padx=2)
            p_btns_dict[p_val] = p_btn

        ttk.Label(row1_frame, text="  Quick Presets:", font=("Segoe UI", 9, "bold"), foreground="#94a3b8").pack(side="left", padx=(6, 4))

        presets = [
            ("✨ All Robots", "all", "#059669"),
            ("🦜 Birds Only", "birds", "#0284c7"),
            ("🤖 Hexapod Only", "hexapod", "#0ea5e9"),
            ("🦾 Arm Only", "arm", "#7c3aed"),
            ("🔇 Mute All", "mute", "#475569"),
        ]
        for pr_lbl, pr_key, pr_col in presets:
            pr_btn = tk.Button(
                row1_frame,
                text=pr_lbl,
                font=("Segoe UI", 8, "bold"),
                bg=pr_col,
                fg="#ffffff",
                relief="flat",
                cursor="hand2",
                padx=5,
                pady=1,
                command=lambda pk=pr_key: on_preset_speech_react(pk)
            )
            pr_btn.pack(side="left", padx=2)

        # Choreography & Light Routines Bar
        routine_frame = ttk.LabelFrame(self.root, text=" 🎭 Waveshare 7\" Touch-LCD Birds Choreography & Light Routines (esp32_Birds.ino) ", padding=6)
        routine_frame.pack(fill="x", padx=15, pady=(2, 6))

        def trigger_routine(r_name):
            self.audio_loop.execute_bird_routine(r_name)

        r_btns = [
            ("🦜 Parrot Sing", "sing", "#0284c7"),
            ("💡 Spotlight Sweep", "sweep", "#059669"),
            ("🔄 Turntable Dance", "dance", "#d97706"),
            ("🌟 Light Show", "lightshow", "#7c3aed"),
            ("🎶 Bird Symphony", "sing", "#db2777"),
            ("🏠 All Home (90°)", "home", "#475569"),
        ]
        for col_idx, (r_label, r_code, r_color) in enumerate(r_btns):
            r_btn = tk.Button(
                routine_frame,
                text=r_label,
                font=("Segoe UI", 9, "bold"),
                bg=r_color,
                fg="#ffffff",
                relief="flat",
                cursor="hand2",
                padx=8,
                pady=3,
                command=lambda rc=r_code: trigger_routine(rc)
            )
            r_btn.grid(row=0, column=col_idx, padx=3, pady=2, sticky="ew")
            routine_frame.columnconfigure(col_idx, weight=1)

        # Tabbed Notebook Interface
        notebook = ttk.Notebook(self.root, padding=6)
        notebook.pack(fill="both", expand=True, padx=10, pady=5)

        # --- TAB 1: GPIO BUTTONS ---
        tab_gpio = ttk.Frame(notebook, padding=10)
        notebook.add(tab_gpio, text=" 🦜 Birds & Lights Outputs ")

        tab_gpio.columnconfigure(0, weight=1)
        tab_gpio.columnconfigure(1, weight=1)

        left_box = ttk.LabelFrame(tab_gpio, text=" 👈 Left Birds & Outputs (esp32_Birds.ino / MCP23017) ", padding=10)
        left_box.grid(row=0, column=0, sticky="nsew", padx=5, pady=5)
        left_box.columnconfigure((0, 1), weight=1)

        self.buttons = {}
        self.button_states = {}

        for idx, item in enumerate(self.config.get("left", [])):
            r = idx // 2
            c = idx % 2
            board = "left"
            gpio = item['gpio']
            name = item['name']
            self.button_states[(board, gpio)] = False
            btn_text = f"{name}\n(GPIO {gpio}) [OFF]"
            btn = tk.Button(
                left_box,
                text=btn_text,
                font=("Segoe UI", 9, "bold"),
                bg="#1e293b",
                fg="#38bdf8",
                activebackground="#0284c7",
                activeforeground="#ffffff",
                relief="flat",
                bd=1,
                cursor="hand2",
                height=2,
                command=lambda b=board, g=gpio, n=name: self.on_button_clicked(b, g, n)
            )
            btn.grid(row=r, column=c, padx=3, pady=3, sticky="nsew")
            self.buttons[(board, gpio)] = btn

        right_box = ttk.LabelFrame(tab_gpio, text=" 👉 Right Birds & Outputs (esp32_Birds.ino / MCP23017) ", padding=10)
        right_box.grid(row=0, column=1, sticky="nsew", padx=5, pady=5)
        right_box.columnconfigure((0, 1), weight=1)

        for idx, item in enumerate(self.config.get("right", [])):
            r = idx // 2
            c = idx % 2
            board = "right"
            gpio = item['gpio']
            name = item['name']
            self.button_states[(board, gpio)] = False
            btn_text = f"{name}\n(GPIO {gpio}) [OFF]"
            btn = tk.Button(
                right_box,
                text=btn_text,
                font=("Segoe UI", 9, "bold"),
                bg="#1e293b",
                fg="#a855f7",
                activebackground="#7e22ce",
                activeforeground="#ffffff",
                relief="flat",
                bd=1,
                cursor="hand2",
                height=2,
                command=lambda b=board, g=gpio, n=name: self.on_button_clicked(b, g, n)
            )
            btn.grid(row=r, column=c, padx=3, pady=3, sticky="nsew")
            self.buttons[(board, gpio)] = btn

        # --- TAB 2: PCA9685 SERVO SLIDERS ---
        tab_servos = ttk.Frame(notebook, padding=10)
        notebook.add(tab_servos, text=" ⚙️ Birds PCA9685 Servos ")

        tab_servos.columnconfigure(0, weight=1)
        tab_servos.columnconfigure(1, weight=1)

        # Left ESP32-S3 Servos Panel (Driver 1)
        servo_left_box = ttk.LabelFrame(tab_servos, text=" 👈 Left Side Servos (Driver 1 - 0x40 / ESP32-S3 Touchscreen) ", padding=10)
        servo_left_box.grid(row=0, column=0, sticky="nsew", padx=5, pady=5)

        for idx, s in enumerate(SERVO_CONFIG.get("left", [])):
            chan = s["channel"]
            name = s["name"]
            default_deg = s.get("default", 90)

            row_frame = ttk.Frame(servo_left_box, padding=2)
            row_frame.pack(fill="x", pady=2)

            name_lbl = ttk.Label(row_frame, text=f"{name} (Ch {chan}):", width=22)
            name_lbl.pack(side="left", padx=(0, 5))

            val_lbl = ttk.Label(row_frame, text=f"{default_deg}°", width=5, font=("Segoe UI", 9, "bold"), foreground="#38bdf8")
            val_lbl.pack(side="right", padx=(5, 0))

            scale = tk.Scale(
                row_frame,
                from_=0,
                to=180,
                orient="horizontal",
                showvalue=False,
                bg="#1e293b",
                fg="#38bdf8",
                troughcolor="#0f172a",
                activebackground="#0284c7",
                highlightthickness=0,
                bd=0,
                length=180,
                command=lambda val, b="left", c=chan, n=name, l=val_lbl: self.on_servo_slider_moved(b, c, n, val, l)
            )
            scale.set(default_deg)
            scale.pack(side="left", fill="x", expand=True)

            self.servo_sliders[("left", chan)] = scale
            self.servo_labels[("left", chan)] = val_lbl

        # Right ESP32-S3 Servos Panel (Driver 2)
        servo_right_box = ttk.LabelFrame(tab_servos, text=" 👉 Right Side Servos (Driver 2 - 0x41 / ESP32-S3 Touchscreen) ", padding=10)
        servo_right_box.grid(row=0, column=1, sticky="nsew", padx=5, pady=5)

        for idx, s in enumerate(SERVO_CONFIG.get("right", [])):
            chan = s["channel"]
            name = s["name"]
            default_deg = s.get("default", 90)

            row_frame = ttk.Frame(servo_right_box, padding=2)
            row_frame.pack(fill="x", pady=2)

            name_lbl = ttk.Label(row_frame, text=f"{name} (Ch {chan}):", width=24)
            name_lbl.pack(side="left", padx=(0, 5))

            val_lbl = ttk.Label(row_frame, text=f"{default_deg}°", width=5, font=("Segoe UI", 9, "bold"), foreground="#a855f7")
            val_lbl.pack(side="right", padx=(5, 0))

            scale = tk.Scale(
                row_frame,
                from_=0,
                to=180,
                orient="horizontal",
                showvalue=False,
                bg="#1e293b",
                fg="#a855f7",
                troughcolor="#0f172a",
                activebackground="#7e22ce",
                highlightthickness=0,
                bd=0,
                length=180,
                command=lambda val, b="right", c=chan, n=name, l=val_lbl: self.on_servo_slider_moved(b, c, n, val, l)
            )
            scale.set(default_deg)
            scale.pack(side="left", fill="x", expand=True)

            self.servo_sliders[("right", chan)] = scale
            self.servo_labels[("right", chan)] = val_lbl

        # --- TAB 3: HEXAPOD BOT CONTROL ---
        tab_hexapod = ttk.Frame(notebook, padding=10)
        notebook.add(tab_hexapod, text=" 🤖 Hexapod Bot ")

        # Bluetooth / Serial Header Frame inside Tab 3
        hexapod_bt_frame = ttk.LabelFrame(tab_hexapod, text=" 📶 Hexapod ESP-32-Touch-LCD Connection (Bluetooth / USB-CDC Serial) ", padding=10)
        hexapod_bt_frame.pack(fill="x", padx=5, pady=(0, 8))

        ttk.Label(hexapod_bt_frame, text="ESP-32-Touch-LCD Connection:").grid(row=0, column=0, sticky="w", padx=(0, 5))
        self.hexapod_bt_combo = ttk.Combobox(hexapod_bt_frame, state="normal", width=35)
        self.hexapod_bt_combo.grid(row=0, column=1, sticky="w", padx=(0, 10))

        def refresh_bt_ports(auto_conn=False):
            bt_opts, bt_map, auto_bt = scan_bluetooth_ports()
            self.hexapod_bt_device_map = bt_map
            self.hexapod_bt_combo['values'] = bt_opts

            curr_dev = getattr(self.audio_loop, 'hexapod_bt_port', getattr(self.audio_loop, 'shobots_bt_port', 'hexapod-touch-lcd'))
            match = [lbl for lbl, dev in bt_map.items() if dev == curr_dev] if curr_dev else []
            if match:
                self.hexapod_bt_combo.set(match[0])
            elif auto_bt in bt_opts:
                self.hexapod_bt_combo.set(auto_bt)
            else:
                self.hexapod_bt_combo.set("hexapod-touch-lcd (ESP-32-Touch-LCD BT/Serial)")

            if auto_conn:
                on_connect_hexapod_bt()

        def on_connect_hexapod_bt():
            sel_lbl = self.hexapod_bt_combo.get()
            dev = self.hexapod_bt_device_map.get(sel_lbl, sel_lbl)
            if hasattr(self.audio_loop, 'hexapod') and self.audio_loop.hexapod:
                _, msg = self.audio_loop.hexapod.connect(dev)
                self.audio_loop.hexapod_bt_port = dev
                self.audio_loop.hexapod_port = dev
                self.audio_loop.shobots_bt_port = dev
                if hasattr(self, 'hexapod_combo') and self.hexapod_combo:
                    self.hexapod_combo.set(sel_lbl)
                self.update_status(f"[ESP-32-Touch-LCD Hexapod] {msg}")

        bt_conn_btn = tk.Button(
            hexapod_bt_frame,
            text="Connect Device",
            font=("Segoe UI", 9, "bold"),
            bg="#10b981",
            fg="#ffffff",
            activebackground="#059669",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=8,
            command=on_connect_hexapod_bt
        )
        bt_conn_btn.grid(row=0, column=2, padx=4)

        bt_scan_btn = tk.Button(
            hexapod_bt_frame,
            text="🔄 Rescan BT",
            font=("Segoe UI", 9, "bold"),
            bg="#334155",
            fg="#f8fafc",
            activebackground="#475569",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=8,
            command=lambda: refresh_bt_ports(auto_conn=True)
        )
        bt_scan_btn.grid(row=0, column=3, padx=4)

        refresh_bt_ports(auto_conn=False)

        # Hexapod Speech Reactivity Frame
        hex_speech_frame = ttk.LabelFrame(tab_hexapod, text=" 🎙️ Hexapod Speech Reactivity (LED Eyes & Expressive Body Sway) ", padding=8)
        hex_speech_frame.pack(fill="x", padx=5, pady=(0, 8))

        def on_hexapod_tab_speech_toggle():
            en = self.speech_hexapod_var.get()
            self.audio_loop.set_speech_reactivity("hexapod", en)

        hex_speech_chk = tk.Checkbutton(
            hex_speech_frame,
            text="🎙️ Hexapod Speech Reactivity (LED Eyes Blink & Body Sways on AI Speech)",
            variable=self.speech_hexapod_var,
            font=("Segoe UI", 9, "bold"),
            bg="#0f172a",
            fg="#38bdf8",
            selectcolor="#1e293b",
            activebackground="#0f172a",
            activeforeground="#38bdf8",
            cursor="hand2",
            command=on_hexapod_tab_speech_toggle
        )
        hex_speech_chk.pack(side="left", padx=5)

        hex_test_speech_btn = tk.Button(
            hex_speech_frame,
            text="⚡ Test Hexapod Speech (1.5s)",
            font=("Segoe UI", 9, "bold"),
            bg="#0284c7",
            fg="#ffffff",
            activebackground="#0369a1",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=10,
            command=lambda: self.audio_loop.dispatch_speech_event(True) or self.root.after(1500, lambda: self.audio_loop.dispatch_speech_event(False))
        )
        hex_test_speech_btn.pack(side="right", padx=5)

        # Preset Motion Buttons Frame
        hexapod_actions_frame = ttk.LabelFrame(tab_hexapod, text=" 🤖 Hexapod Motion Functions & Preset Movements ", padding=10)
        hexapod_actions_frame.pack(fill="x", padx=5, pady=(0, 8))
        hexapod_actions_frame.columnconfigure((0, 1, 2, 3), weight=1)

        action_buttons = [
            ("🚶 Walk", "walk", "#0284c7", "#0369a1"),
            ("🏃 Run", "run", "#0284c7", "#0369a1"),
            ("👋 Wave Left Arm", "wave_left_arm", "#8b5cf6", "#7c3aed"),
            ("🖐️ Wave Right Arm", "wave_right_arm", "#a855f7", "#9333ea"),
            ("💃 Dance", "dance", "#ec4899", "#db2777"),
            ("🪑 Sit", "sit", "#eab308", "#ca8a04"),
            ("🧍 Stand", "stand", "#10b981", "#059669"),
            ("🛌 Flat to Floor", "flat_to_floor", "#64748b", "#475569"),
            ("🛑 Stop", "stop", "#ef4444", "#dc2626"),
            ("↪️ Turn Left", "turn_left", "#0284c7", "#0369a1"),
            ("↩️ Turn Right", "turn_right", "#0284c7", "#0369a1"),
            ("🙇 Bow", "bow", "#10b981", "#059669"),
        ]

        for idx, (btn_label, action_key, bg_col, act_bg) in enumerate(action_buttons):
            r = idx // 4
            c = idx % 4
            btn = tk.Button(
                hexapod_actions_frame,
                text=btn_label,
                font=("Segoe UI", 9, "bold"),
                bg=bg_col,
                fg="#ffffff",
                activebackground=act_bg,
                activeforeground="#ffffff",
                relief="flat",
                cursor="hand2",
                height=2,
                command=lambda a=action_key: self.on_hexapod_action(a)
            )
            btn.grid(row=r, column=c, padx=3, pady=3, sticky="nsew")

        # 6-Leg Servo Sliders Panel (2 PCA9685 Drivers, 18 Servos - 3 DoF per leg)
        servo_main_box = ttk.LabelFrame(tab_hexapod, text=" 🦾 6-Leg Servo Drivers (2 PCA9685 Drivers, 18 Servos - 3 Degrees of Freedom) ", padding=10)
        servo_main_box.pack(fill="both", expand=True, padx=5, pady=2)

        canvas = tk.Canvas(servo_main_box, bg="#0f172a", highlightthickness=0)
        scrollbar = ttk.Scrollbar(servo_main_box, orient="vertical", command=canvas.yview)
        scrollable_frame = ttk.Frame(canvas)

        scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )

        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        scrollable_frame.columnconfigure(0, weight=1)
        scrollable_frame.columnconfigure(1, weight=1)

        left_legs_box = ttk.LabelFrame(scrollable_frame, text=" 👈 Driver 1 (Left Legs: FL, ML, RL) ", padding=8)
        left_legs_box.grid(row=0, column=0, sticky="nsew", padx=5, pady=5)

        right_legs_box = ttk.LabelFrame(scrollable_frame, text=" 👉 Driver 2 (Right Legs: FR, MR, RR) ", padding=8)
        right_legs_box.grid(row=0, column=1, sticky="nsew", padx=5, pady=5)

        self.hexapod_sliders = {}
        self.hexapod_labels = {}
        self.shobots_sliders = self.hexapod_sliders  # Alias
        self.shobots_labels = self.hexapod_labels    # Alias

        leg_groups = [
            ("left", left_legs_box, ["FL", "ML", "RL"], "#38bdf8", "#0284c7"),
            ("right", right_legs_box, ["FR", "MR", "RR"], "#a855f7", "#7e22ce")
        ]

        for side, parent_box, legs_list, fg_col, act_col in leg_groups:
            for leg_code in legs_list:
                leg_info = HexapodController.LEGS[leg_code]
                leg_name = leg_info["name"]
                driver_num = leg_info["driver"]

                leg_frame = ttk.LabelFrame(parent_box, text=f" 🦵 {leg_name} ({leg_code}) - Driver {driver_num} ", padding=6)
                leg_frame.pack(fill="x", expand=True, pady=4)

                for joint in ["coxa", "femur", "tibia"]:
                    chan = leg_info["channels"][joint]
                    joint_title = f"{joint.title()} (Hip)" if joint == "coxa" else (f"{joint.title()} (Thigh)" if joint == "femur" else f"{joint.title()} (Knee)")

                    r_frame = ttk.Frame(leg_frame, padding=1)
                    r_frame.pack(fill="x", pady=1)

                    lbl = ttk.Label(r_frame, text=f"{joint_title} [Ch {chan}]:", width=20)
                    lbl.pack(side="left", padx=(0, 4))

                    val_lbl = ttk.Label(r_frame, text="90°", width=5, font=("Segoe UI", 9, "bold"), foreground=fg_col)
                    val_lbl.pack(side="right", padx=(4, 0))

                    scale = tk.Scale(
                        r_frame,
                        from_=0,
                        to=180,
                        orient="horizontal",
                        showvalue=False,
                        bg="#1e293b",
                        fg=fg_col,
                        troughcolor="#0f172a",
                        activebackground=act_col,
                        highlightthickness=0,
                        bd=0,
                        length=140,
                        command=lambda val, lc=leg_code, j=joint, vl=val_lbl: self.on_hexapod_joint_moved(lc, j, val, vl)
                    )
                    scale.set(90)
                    scale.pack(side="left", fill="x", expand=True)

                    self.hexapod_sliders[(leg_code, joint)] = scale
                    self.hexapod_labels[(leg_code, joint)] = val_lbl

        # --- TAB 4: TELLO DRONE CONTROL (ESP32-S3 7" TOUCH LCD SCREEN BRIDGE) ---
        tab_drone = ttk.Frame(notebook, padding=10)
        notebook.add(tab_drone, text=" 🚁 Tello Drone ")

        # 1. ESP32-S3 7-Inch Touch LCD Connection Header
        drone_conn_frame = ttk.LabelFrame(tab_drone, text=" 🖥️ ESP32-S3 7-Inch Touch LCD Screen Bridge & Animations ", padding=8)
        drone_conn_frame.pack(fill="x", padx=5, pady=(0, 6))

        t_port_str = getattr(self.audio_loop, 'esp32_tello_port', None) or 'Simulation Mode'
        self.drone_status_lbl = ttk.Label(
            drone_conn_frame,
            text=f"ESP32-S3 7\" Touch LCD Status: {t_port_str}",
            font=("Segoe UI", 9, "bold"),
            foreground="#10b981" if getattr(self.audio_loop, 'esp32_tello_port', None) else "#38bdf8"
        )
        self.drone_status_lbl.pack(side="left", padx=5)

        def on_reconnect_drone_tab():
            on_connect_tello_port()

        drone_tab_conn_btn = tk.Button(
            drone_conn_frame,
            text="🔌 Connect Tello Screen",
            font=("Segoe UI", 9, "bold"),
            bg="#059669",
            fg="#ffffff",
            activebackground="#047857",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=8,
            pady=2,
            command=on_reconnect_drone_tab
        )
        drone_tab_conn_btn.pack(side="right", padx=5)

        ttk.Label(
            drone_conn_frame,
            text="Commands from this PC window, Gemini AI, and the 7\" Touchscreen are animated on the LCD & sent to Tello over UDP.",
            font=("Segoe UI", 8, "italic"),
            foreground="#94a3b8"
        ).pack(side="left", padx=10)

        # 2. Flight Essentials & Mode Box
        drone_top_box = ttk.LabelFrame(tab_drone, text=" 🎮 Flight Essentials & Control Mode ", padding=8)
        drone_top_box.pack(fill="x", padx=5, pady=(0, 6))

        mode_btn_frame = ttk.Frame(drone_top_box)
        mode_btn_frame.pack(fill="x", pady=2)

        cmd_mode_btn = tk.Button(
            mode_btn_frame,
            text="📡 Command Mode",
            font=("Segoe UI", 9, "bold"),
            bg="#0284c7",
            fg="#ffffff",
            activebackground="#0369a1",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=10,
            pady=4,
            command=lambda: self.send_drone_command("command")
        )
        cmd_mode_btn.pack(side="left", padx=3)

        takeoff_btn = tk.Button(
            mode_btn_frame,
            text="🛫 Takeoff",
            font=("Segoe UI", 9, "bold"),
            bg="#10b981",
            fg="#ffffff",
            activebackground="#059669",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=10,
            pady=4,
            command=lambda: self.send_drone_command("takeoff")
        )
        takeoff_btn.pack(side="left", padx=3)

        land_btn = tk.Button(
            mode_btn_frame,
            text="🛬 Land",
            font=("Segoe UI", 9, "bold"),
            bg="#eab308",
            fg="#ffffff",
            activebackground="#ca8a04",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=10,
            pady=4,
            command=lambda: self.send_drone_command("land")
        )
        land_btn.pack(side="left", padx=3)

        emergency_btn = tk.Button(
            mode_btn_frame,
            text="🚨 Emergency",
            font=("Segoe UI", 9, "bold"),
            bg="#ef4444",
            fg="#ffffff",
            activebackground="#dc2626",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=10,
            pady=4,
            command=lambda: self.send_drone_command("emergency")
        )
        emergency_btn.pack(side="left", padx=3)

        bat_btn = tk.Button(
            mode_btn_frame,
            text="🔋 Battery?",
            font=("Segoe UI", 9, "bold"),
            bg="#334155",
            fg="#38bdf8",
            activebackground="#475569",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=8,
            pady=4,
            command=lambda: self.send_drone_command("battery?")
        )
        bat_btn.pack(side="left", padx=3)

        # 3. Directional Movement (Inches)
        dir_box = ttk.LabelFrame(tab_drone, text=" 📐 Directional Flight Movement (Distances in Inches) ", padding=8)
        dir_box.pack(fill="x", padx=5, pady=(0, 6))
        dir_box.columnconfigure((0, 1), weight=1)

        # Up
        row_up = ttk.Frame(dir_box, padding=2)
        row_up.grid(row=0, column=0, sticky="ew", padx=4, pady=2)
        up_btn = tk.Button(
            row_up, text="⬆️ Up", font=("Segoe UI", 9, "bold"), bg="#1e293b", fg="#38bdf8",
            activebackground="#0284c7", activeforeground="#ffffff", relief="flat", cursor="hand2", width=10,
            command=lambda: self.on_drone_directional_move("up", self.up_entry.get())
        )
        up_btn.pack(side="left", padx=(0, 4))
        ttk.Label(row_up, text="Dist:").pack(side="left")
        self.up_entry = ttk.Entry(row_up, width=5)
        self.up_entry.insert(0, "20")
        self.up_entry.pack(side="left", padx=3)
        ttk.Label(row_up, text="in").pack(side="left")

        # Down
        row_down = ttk.Frame(dir_box, padding=2)
        row_down.grid(row=0, column=1, sticky="ew", padx=4, pady=2)
        down_btn = tk.Button(
            row_down, text="⬇️ Down", font=("Segoe UI", 9, "bold"), bg="#1e293b", fg="#38bdf8",
            activebackground="#0284c7", activeforeground="#ffffff", relief="flat", cursor="hand2", width=10,
            command=lambda: self.on_drone_directional_move("down", self.down_entry.get())
        )
        down_btn.pack(side="left", padx=(0, 4))
        ttk.Label(row_down, text="Dist:").pack(side="left")
        self.down_entry = ttk.Entry(row_down, width=5)
        self.down_entry.insert(0, "20")
        self.down_entry.pack(side="left", padx=3)
        ttk.Label(row_down, text="in").pack(side="left")

        # Left
        row_left = ttk.Frame(dir_box, padding=2)
        row_left.grid(row=1, column=0, sticky="ew", padx=4, pady=2)
        left_btn = tk.Button(
            row_left, text="⬅️ Left", font=("Segoe UI", 9, "bold"), bg="#1e293b", fg="#38bdf8",
            activebackground="#0284c7", activeforeground="#ffffff", relief="flat", cursor="hand2", width=10,
            command=lambda: self.on_drone_directional_move("left", self.left_entry.get())
        )
        left_btn.pack(side="left", padx=(0, 4))
        ttk.Label(row_left, text="Dist:").pack(side="left")
        self.left_entry = ttk.Entry(row_left, width=5)
        self.left_entry.insert(0, "20")
        self.left_entry.pack(side="left", padx=3)
        ttk.Label(row_left, text="in").pack(side="left")

        # Right
        row_right = ttk.Frame(dir_box, padding=2)
        row_right.grid(row=1, column=1, sticky="ew", padx=4, pady=2)
        right_btn = tk.Button(
            row_right, text="➡️ Right", font=("Segoe UI", 9, "bold"), bg="#1e293b", fg="#38bdf8",
            activebackground="#0284c7", activeforeground="#ffffff", relief="flat", cursor="hand2", width=10,
            command=lambda: self.on_drone_directional_move("right", self.right_entry.get())
        )
        right_btn.pack(side="left", padx=(0, 4))
        ttk.Label(row_right, text="Dist:").pack(side="left")
        self.right_entry = ttk.Entry(row_right, width=5)
        self.right_entry.insert(0, "20")
        self.right_entry.pack(side="left", padx=3)
        ttk.Label(row_right, text="in").pack(side="left")

        # Forward
        row_fwd = ttk.Frame(dir_box, padding=2)
        row_fwd.grid(row=2, column=0, sticky="ew", padx=4, pady=2)
        fwd_btn = tk.Button(
            row_fwd, text="🔼 Forward", font=("Segoe UI", 9, "bold"), bg="#1e293b", fg="#38bdf8",
            activebackground="#0284c7", activeforeground="#ffffff", relief="flat", cursor="hand2", width=10,
            command=lambda: self.on_drone_directional_move("forward", self.fwd_entry.get())
        )
        fwd_btn.pack(side="left", padx=(0, 4))
        ttk.Label(row_fwd, text="Dist:").pack(side="left")
        self.fwd_entry = ttk.Entry(row_fwd, width=5)
        self.fwd_entry.insert(0, "20")
        self.fwd_entry.pack(side="left", padx=3)
        ttk.Label(row_fwd, text="in").pack(side="left")

        # Back
        row_back = ttk.Frame(dir_box, padding=2)
        row_back.grid(row=2, column=1, sticky="ew", padx=4, pady=2)
        back_btn = tk.Button(
            row_back, text="🔽 Back", font=("Segoe UI", 9, "bold"), bg="#1e293b", fg="#38bdf8",
            activebackground="#0284c7", activeforeground="#ffffff", relief="flat", cursor="hand2", width=10,
            command=lambda: self.on_drone_directional_move("back", self.back_entry.get())
        )
        back_btn.pack(side="left", padx=(0, 4))
        ttk.Label(row_back, text="Dist:").pack(side="left")
        self.back_entry = ttk.Entry(row_back, width=5)
        self.back_entry.insert(0, "20")
        self.back_entry.pack(side="left", padx=3)
        ttk.Label(row_back, text="in").pack(side="left")

        # 4. Rotation Control (Degrees)
        rot_box = ttk.LabelFrame(tab_drone, text=" 🔄 Yaw & Rotation Control (Degrees) ", padding=8)
        rot_box.pack(fill="x", padx=5, pady=(0, 6))
        rot_box.columnconfigure((0, 1), weight=1)

        # Turn Left (ccw)
        row_tl = ttk.Frame(rot_box, padding=2)
        row_tl.grid(row=0, column=0, sticky="ew", padx=4, pady=2)
        tl_btn = tk.Button(
            row_tl, text="↩️ Rotate CCW", font=("Segoe UI", 9, "bold"), bg="#1e293b", fg="#a855f7",
            activebackground="#7e22ce", activeforeground="#ffffff", relief="flat", cursor="hand2", width=12,
            command=lambda: self.on_drone_turn("ccw", self.turn_left_entry.get())
        )
        tl_btn.pack(side="left", padx=(0, 4))
        ttk.Label(row_tl, text="Angle:").pack(side="left")
        self.turn_left_entry = ttk.Entry(row_tl, width=5)
        self.turn_left_entry.insert(0, "90")
        self.turn_left_entry.pack(side="left", padx=3)
        ttk.Label(row_tl, text="deg").pack(side="left")

        # Turn Right (cw)
        row_tr = ttk.Frame(rot_box, padding=2)
        row_tr.grid(row=0, column=1, sticky="ew", padx=4, pady=2)
        tr_btn = tk.Button(
            row_tr, text="↪️ Rotate CW", font=("Segoe UI", 9, "bold"), bg="#1e293b", fg="#a855f7",
            activebackground="#7e22ce", activeforeground="#ffffff", relief="flat", cursor="hand2", width=12,
            command=lambda: self.on_drone_turn("cw", self.turn_right_entry.get())
        )
        tr_btn.pack(side="left", padx=(0, 4))
        ttk.Label(row_tr, text="Angle:").pack(side="left")
        self.turn_right_entry = ttk.Entry(row_tr, width=5)
        self.turn_right_entry.insert(0, "90")
        self.turn_right_entry.pack(side="left", padx=3)
        ttk.Label(row_tr, text="deg").pack(side="left")

        # 5. Acrobatic Stunts & Choreography Routines Bar
        stunt_box = ttk.LabelFrame(tab_drone, text=" 🎪 Acrobatic 360° Flips & Autonomous Choreography ", padding=8)
        stunt_box.pack(fill="x", padx=5, pady=(0, 6))

        stunt_btn_frame = ttk.Frame(stunt_box)
        stunt_btn_frame.pack(fill="x", pady=2)

        flips = [
            ("🔄 Flip Fwd", "f", "#7c3aed"),
            ("🔄 Flip Back", "b", "#7c3aed"),
            ("↺ Flip Left", "l", "#7c3aed"),
            ("↻ Flip Right", "r", "#7c3aed"),
        ]
        for f_label, f_dir, f_color in flips:
            f_btn = tk.Button(
                stunt_btn_frame,
                text=f_label,
                font=("Segoe UI", 9, "bold"),
                bg=f_color,
                fg="#ffffff",
                relief="flat",
                cursor="hand2",
                padx=8,
                pady=3,
                command=lambda d=f_dir: self.on_drone_flip(d)
            )
            f_btn.pack(side="left", padx=3)

        routines = [
            ("🔲 Square Box", "square", "#0284c7"),
            ("🌀 360 Scan", "scan360", "#059669"),
            ("🎈 Bounce Wave", "bounce", "#d97706"),
        ]
        for r_label, r_code, r_color in routines:
            r_btn = tk.Button(
                stunt_btn_frame,
                text=r_label,
                font=("Segoe UI", 9, "bold"),
                bg=r_color,
                fg="#ffffff",
                relief="flat",
                cursor="hand2",
                padx=8,
                pady=3,
                command=lambda rc=r_code: self.on_drone_routine(rc)
            )
            r_btn.pack(side="left", padx=3)

        # Tab 5: 6-DOF Robot Arm
        tab_arm = ttk.Frame(notebook, padding=10)
        notebook.add(tab_arm, text=" 🦾 6-DOF Robot Arm ")

        # 1. Arm Connection Header inside Tab 5
        arm_conn_frame = ttk.LabelFrame(tab_arm, text=" 🔌 Arm ESP32 Serial Connection (PCA9685 Driver) ", padding=10)
        arm_conn_frame.pack(fill="x", padx=5, pady=(0, 8))

        self.arm_status_lbl = ttk.Label(
            arm_conn_frame,
            text=f"Status: ESP32 Arm Port - {self.audio_loop.esp32_arm_port or 'Simulation Mode'}",
            font=("Segoe UI", 9, "bold"),
            foreground="#38bdf8"
        )
        self.arm_status_lbl.pack(side="left", padx=5)

        arm_tab_conn_btn = tk.Button(
            arm_conn_frame,
            text="🔌 Scan & Connect Arm ESP32",
            font=("Segoe UI", 9, "bold"),
            bg="#0284c7",
            fg="#ffffff",
            activebackground="#0369a1",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=10,
            command=on_connect_arm_port
        )
        arm_tab_conn_btn.pack(side="right", padx=5)

        # Robot Arm Speech Conversational Gesturing Frame
        arm_speech_frame = ttk.LabelFrame(tab_arm, text=" 🎙️ Robot Arm Speech Gesturing (Conversational Swivel & Nodding) ", padding=8)
        arm_speech_frame.pack(fill="x", padx=5, pady=(0, 8))

        def on_arm_tab_speech_toggle():
            en = self.speech_arm_var.get()
            self.audio_loop.set_speech_reactivity("arm", en)

        arm_speech_chk = tk.Checkbutton(
            arm_speech_frame,
            text="🎙️ Conversational Speech Gestures (Base Swivel, Shoulder/Elbow Nods on AI Speech)",
            variable=self.speech_arm_var,
            font=("Segoe UI", 9, "bold"),
            bg="#0f172a",
            fg="#c084fc",
            selectcolor="#1e293b",
            activebackground="#0f172a",
            activeforeground="#c084fc",
            cursor="hand2",
            command=on_arm_tab_speech_toggle
        )
        arm_speech_chk.pack(side="left", padx=5)

        arm_test_speech_btn = tk.Button(
            arm_speech_frame,
            text="⚡ Test Arm Speech Gestures (1.5s)",
            font=("Segoe UI", 9, "bold"),
            bg="#0284c7",
            fg="#ffffff",
            activebackground="#0369a1",
            activeforeground="#ffffff",
            relief="flat",
            cursor="hand2",
            padx=10,
            command=lambda: self.audio_loop.dispatch_speech_event(True) or self.root.after(1500, lambda: self.audio_loop.dispatch_speech_event(False))
        )
        arm_test_speech_btn.pack(side="right", padx=5)

        # 2. Robot Arm Gestures & Demonstrations Frame
        arm_gestures_frame = ttk.LabelFrame(tab_arm, text=" 🎭 Robot Arm Gestures & Preset Movements ", padding=10)
        arm_gestures_frame.pack(fill="x", padx=5, pady=(0, 8))

        # Gestures row
        ttk.Label(arm_gestures_frame, text="Gestures:", font=("Segoe UI", 9, "bold"), foreground="#a855f7").grid(row=0, column=0, sticky="w", padx=5, pady=2)
        gestures_box = ttk.Frame(arm_gestures_frame)
        gestures_box.grid(row=0, column=1, sticky="w", padx=5, pady=2)

        gesture_buttons = [
            ("🙋 Yes (Nod)", "yes", "#059669"),
            ("🙅 No (Shake)", "no", "#dc2626"),
            ("✋ High Five", "high_five", "#7c3aed"),
            ("👋 Wave", "wave", "#0284c7"),
            ("🙇 Bow", "bow", "#d97706"),
            ("🕺 Dance", "dance", "#ec4899"),
        ]

        for g_idx, (g_text, g_action, g_color) in enumerate(gesture_buttons):
            btn = tk.Button(
                gestures_box,
                text=g_text,
                font=("Segoe UI", 9, "bold"),
                bg=g_color,
                fg="#ffffff",
                activebackground="#334155",
                activeforeground="#ffffff",
                relief="flat",
                cursor="hand2",
                padx=8,
                pady=3,
                command=lambda act=g_action: self.on_arm_action(act)
            )
            btn.grid(row=0, column=g_idx, padx=3, pady=2)

        # Commands row
        ttk.Label(arm_gestures_frame, text="Commands:", font=("Segoe UI", 9, "bold"), foreground="#38bdf8").grid(row=1, column=0, sticky="w", padx=5, pady=6)
        commands_box = ttk.Frame(arm_gestures_frame)
        commands_box.grid(row=1, column=1, sticky="w", padx=5, pady=6)

        demo_buttons = [
            ("🏠 Home Position", "home", "#334155"),
            ("💤 Rest Position", "rest", "#334155"),
            ("🎯 Reach Forward", "reach", "#334155"),
            ("📦 Pick & Place Demo", "pick_and_place", "#2563eb"),
            ("🖐️ Open Gripper", "open_gripper", "#16a34a"),
            ("✊ Close Gripper", "close_gripper", "#ca8a04"),
            ("🛑 Stop Motion", "stop", "#b91c1c"),
        ]

        for c_idx, (c_text, c_action, c_color) in enumerate(demo_buttons):
            btn = tk.Button(
                commands_box,
                text=c_text,
                font=("Segoe UI", 9, "bold"),
                bg=c_color,
                fg="#ffffff",
                activebackground="#475569",
                activeforeground="#ffffff",
                relief="flat",
                cursor="hand2",
                padx=8,
                pady=3,
                command=lambda act=c_action: self.on_arm_action(act)
            )
            btn.grid(row=0, column=c_idx, padx=3, pady=2)

        # 3. 6-DOF Manual Servo Control Sliders
        arm_servos_box = ttk.LabelFrame(tab_arm, text=" 🦾 6 Degrees of Freedom Manual Servo Drivers (PCA9685 Channels 0-5) ", padding=10)
        arm_servos_box.pack(fill="x", padx=5, pady=(0, 8))

        self.arm_servo_sliders = {}
        self.arm_servo_labels = {}

        arm_servos = SERVO_CONFIG.get("arm", [])
        for idx, s in enumerate(arm_servos):
            r = idx // 2
            c = (idx % 2) * 3

            lbl = ttk.Label(arm_servos_box, text=f"Ch {s['channel']} ({s['name']}):", font=("Segoe UI", 9, "bold"))
            lbl.grid(row=r, column=c, sticky="w", padx=5, pady=6)

            val_lbl = ttk.Label(arm_servos_box, text=f"{s['default']}°", font=("Segoe UI", 9), foreground="#38bdf8", width=5)
            val_lbl.grid(row=r, column=c+2, sticky="w", padx=5, pady=6)

            slider = ttk.Scale(
                arm_servos_box,
                from_=0,
                to=180,
                value=s['default'],
                orient="horizontal",
                command=lambda val, ch=s['channel'], vl=val_lbl: self.on_arm_joint_moved(ch, val, vl)
            )
            slider.grid(row=r, column=c+1, sticky="ew", padx=5, pady=6)
            arm_servos_box.columnconfigure(c+1, weight=1)

            self.arm_servo_sliders[s['channel']] = slider
            self.arm_servo_labels[s['channel']] = val_lbl

        # Center window on screen
        self.root.update_idletasks()
        w = self.root.winfo_width()
        h = self.root.winfo_height()
        x = (self.root.winfo_screenwidth() // 2) - (w // 2)
        y = (self.root.winfo_screenheight() // 2) - (h // 2)
        self.root.geometry(f"{w}x{h}+{x}+{y}")

        self.root.mainloop()

    def update_status(self, text):
        if self.status_label and self.root:
            try:
                self.root.after(0, lambda: self.status_label.config(text=f"Status: {text}"))
            except Exception:
                pass

    def sync_speech_react_switches(self):
        """Synchronizes GUI checkboxes with audio loop reactivity state."""
        if self.root:
            def _sync():
                if hasattr(self, 'speech_birds_var') and self.speech_birds_var:
                    self.speech_birds_var.set(getattr(self.audio_loop, 'speech_react_birds', True))
                if hasattr(self, 'speech_hexapod_var') and self.speech_hexapod_var:
                    self.speech_hexapod_var.set(getattr(self.audio_loop, 'speech_react_hexapod', True))
                if hasattr(self, 'speech_arm_var') and self.speech_arm_var:
                    self.speech_arm_var.set(getattr(self.audio_loop, 'speech_react_arm', True))
            try:
                self.root.after(0, _sync)
            except Exception:
                pass

    def update_servo_slider(self, board, channel, angle):
        if self.root:
            def _update():
                key = (board.lower(), int(channel))
                slider = self.servo_sliders.get(key)
                lbl = self.servo_labels.get(key)
                if slider:
                    slider.set(int(angle))
                if lbl:
                    lbl.config(text=f"{int(angle)}°")
            try:
                self.root.after(0, _update)
            except Exception:
                pass

    def update_hexapod_joint_slider(self, leg_code, joint, angle):
        if self.root:
            def _update():
                key = (leg_code.upper(), joint.lower())
                slider = self.hexapod_sliders.get(key)
                lbl = self.hexapod_labels.get(key)
                if slider:
                    slider.set(int(angle))
                if lbl:
                    lbl.config(text=f"{int(angle)}°")
            try:
                self.root.after(0, _update)
            except Exception:
                pass

    # Compatibility alias
    update_shobots_joint_slider = update_hexapod_joint_slider

    def update_arm_joint_slider(self, channel, angle):
        if self.root:
            def _update():
                channel = int(channel)
                slider = self.arm_servo_sliders.get(channel)
                lbl = self.arm_servo_labels.get(channel)
                if slider:
                    slider.set(int(angle))
                if lbl:
                    lbl.config(text=f"{int(angle)}°")
            try:
                self.root.after(0, _update)
            except Exception:
                pass

    def on_arm_action(self, action_name):
        import threading
        self.update_status(f"Executing Robot Arm Action: '{action_name}'...")
        def _execute():
            if hasattr(self.audio_loop, 'robot_arm') and self.audio_loop.robot_arm:
                res = self.audio_loop.robot_arm.execute_action(action_name)
                msg = res.get("message", f"Robot Arm action: {action_name}")
                self.update_status(msg)
        threading.Thread(target=_execute, daemon=True).start()

    def on_arm_joint_moved(self, channel, value, label_widget):
        try:
            deg = int(float(value))
            label_widget.config(text=f"{deg}°")
            def _execute():
                if hasattr(self.audio_loop, 'robot_arm') and self.audio_loop.robot_arm:
                    self.audio_loop.robot_arm.set_joint_angle(channel, deg)
            import threading
            threading.Thread(target=_execute, daemon=True).start()
        except Exception:
            pass

    def on_hexapod_action(self, action_name):
        import threading
        self.update_status(f"Executing Hexapod Action: '{action_name}'...")
        def _execute():
            if hasattr(self.audio_loop, 'hexapod') and self.audio_loop.hexapod:
                res = self.audio_loop.hexapod.execute_action(action_name)
                msg = res.get("message", f"Hexapod action: {action_name}")
                self.update_status(msg)
        threading.Thread(target=_execute, daemon=True).start()

    # Compatibility alias
    on_shobots_action = on_hexapod_action

    def on_hexapod_joint_moved(self, leg_code, joint, value, label_widget):
        try:
            deg = int(float(value))
            label_widget.config(text=f"{deg}°")
            def _execute():
                if hasattr(self.audio_loop, 'hexapod') and self.audio_loop.hexapod:
                    res = self.audio_loop.hexapod.set_joint_angle(leg_code, joint, deg)
                    msg = res.get("message", f"Set Leg {leg_code} {joint} to {deg}°")
                    self.update_status(msg)
            import threading
            threading.Thread(target=_execute, daemon=True).start()
        except Exception:
            pass

    # Compatibility alias
    on_shobots_joint_moved = on_hexapod_joint_moved

    def send_drone_command(self, cmd_str):
        import threading
        self.update_status(f"Sending Tello Drone command: '{cmd_str}'...")
        def _execute():
            if hasattr(self.audio_loop, 'tello') and self.audio_loop.tello:
                res = self.audio_loop.tello.send_cmd(cmd_str)
                resp_text = res.get("response", res.get("message", "Executed"))
                self.update_status(f"[Tello Drone] Command '{cmd_str}' -> {resp_text}")
        threading.Thread(target=_execute, daemon=True).start()

    def on_drone_directional_move(self, direction, inches_str):
        try:
            val_in = float(inches_str.strip())
            # Convert inches to centimeters (1 inch = 2.54 cm). Tello SDK valid range: 20-500 cm
            cm = max(20, min(500, int(round(val_in * 2.54))))
            cmd = f"{direction} {cm}"
            self.send_drone_command(cmd)
        except ValueError:
            self.update_status(f"Invalid distance '{inches_str}'. Please enter a number in inches.")

    def on_drone_turn(self, turn_dir, degrees_str):
        try:
            deg = int(float(degrees_str.strip()))
            deg = max(1, min(360, deg))
            cmd = f"{turn_dir} {deg}"
            self.send_drone_command(cmd)
        except ValueError:
            self.update_status(f"Invalid angle '{degrees_str}'. Please enter degrees (1-360).")

    def on_drone_flip(self, direction: str):
        cmd = f"flip {direction}"
        self.send_drone_command(cmd)

    def on_drone_routine(self, routine_name: str):
        cmd = f"ROUTINE:{routine_name.upper()}"
        self.send_drone_command(cmd)

    def on_servo_slider_moved(self, board, channel, name, value, label_widget):
        try:
            deg = int(float(value))
            label_widget.config(text=f"{deg}°")
            def _execute():
                res = self.audio_loop.set_servo_angle(board=board, channel=channel, servo_name=name, angle=deg)
                msg = res.get("message", f"Set {name} to {deg}°")
                self.update_status(msg)
            import threading
            threading.Thread(target=_execute, daemon=True).start()
        except Exception:
            pass

    def on_button_clicked(self, board, gpio, name):
        import threading
        current_state = self.button_states.get((board, gpio), False)
        new_state = not current_state
        self.button_states[(board, gpio)] = new_state

        state_label = "ON" if new_state else "OFF"
        btn = self.buttons.get((board, gpio))

        if btn:
            if new_state:
                btn.config(
                    text=f"{name}\n(GPIO {gpio}) [ON]",
                    bg="#16a34a",
                    fg="#ffffff",
                    activebackground="#15803d",
                    activeforeground="#ffffff"
                )
            else:
                default_fg = "#38bdf8" if board == "left" else "#a855f7"
                default_active_bg = "#0284c7" if board == "left" else "#7e22ce"
                btn.config(
                    text=f"{name}\n(GPIO {gpio}) [OFF]",
                    bg="#1e293b",
                    fg=default_fg,
                    activebackground=default_active_bg,
                    activeforeground="#ffffff"
                )

        self.update_status(f"Turning {board.title()}: '{name}' (GPIO {gpio}) -> {state_label}...")

        def _execute():
            res = self.audio_loop.trigger_esp32_gpio(board, gpio, name, state=new_state)
            status_str = res.get("message", f"Triggered {name} -> {state_label}")
            self.update_status(status_str)

        threading.Thread(target=_execute, daemon=True).start()

    def on_pulse_clicked(self, count):
        pass

    def on_turn_on(self):
        pass

    def on_turn_off(self):
        pass


class AudioLoop:
    def __init__(self, video_mode=DEFAULT_MODE, camera_idx=0, mic_idx=None, speaker_idx=None, voice_name="Zephyr", esp32_port=None, esp32_birds_port=None, esp32_left_port=None, esp32_right_port=None, esp32_arm_port=None, esp32_tello_port=None, esp32_rover_port=None, hexapod_port=None, hexapod_bt_port=None, shobots_bt_port=None, tello_ip="192.168.10.1", tello_port=8889):
        self.video_mode = video_mode
        self.camera_idx = camera_idx
        self.mic_idx = mic_idx
        self.speaker_idx = speaker_idx
        self.voice_name = voice_name
        self.esp32_birds_port = esp32_birds_port or esp32_left_port or esp32_port
        self.esp32_left_port = self.esp32_birds_port
        self.esp32_right_port = self.esp32_birds_port
        self.esp32_arm_port = esp32_arm_port
        self.esp32_tello_port = esp32_tello_port
        self.esp32_rover_port = esp32_rover_port
        self.esp32_port = self.esp32_birds_port
        self.hexapod_port = hexapod_port or hexapod_bt_port or shobots_bt_port or "hexapod"
        self.hexapod_bt_port = self.hexapod_port
        self.shobots_bt_port = self.hexapod_port
        self.hexapod = HexapodController(bt_port=self.hexapod_port)
        self.shobots = self.hexapod  # Alias for backward compatibility
        self.robot_arm = RobotArmController(self)
        self.waverover = WaveRoverController(port=self.esp32_rover_port, audio_loop=self)
        self.serial_birds = None
        self.serial_left = None
        self.serial_right = None
        self.serial_arm = None
        self.serial_tello = None
        self.serial_rover = None
        self.serial_conn = None
        self.tello = TelloController(ip=tello_ip, port=tello_port, audio_loop=self)
        self.leviton = LevitonController()
        self.ewelink = EwelinkController()

        self.audio_in_queue = None
        self.out_queue = None

        self.session = None

        self.send_text_task = None
        self.receive_audio_task = None
        self.play_audio_task = None

        self.audio_stream = None
        self.playing_audio = False

        # Multi-Robot Speech Reactivity Flags
        self.speech_react_birds = True
        self.speech_react_hexapod = True
        self.speech_react_arm = True
        self.speech_react_rover = True

    def connect_esp32(self, board: str, port_name: str):
        import serial
        board = board.lower()
        if port_name in (None, "None (Simulation Mode)", "None"):
            if board in ("birds", "left", "right", "unified", "birdsscreen", "touchscreen", "screen", "led"):
                if self.serial_birds and self.serial_birds.is_open:
                    try:
                        self.serial_birds.close()
                    except Exception:
                        pass
                elif self.serial_left and self.serial_left.is_open:
                    try:
                        self.serial_left.close()
                    except Exception:
                        pass
                self.serial_birds = None
                self.serial_left = None
                self.serial_right = None
                self.esp32_birds_port = None
                self.esp32_left_port = None
                self.esp32_right_port = None
                self.esp32_port = None
                self.serial_conn = None
            elif board in ("hexapod", "hexipod", "shobots", "bot"):
                if hasattr(self, 'hexapod') and self.hexapod:
                    self.hexapod.connect("None (Simulation Mode)")
                self.hexapod_port = None
                self.hexapod_bt_port = None
                self.shobots_bt_port = None
                return True, "Hexapod ESP-32-Touch-LCD set to Simulation Mode"
            elif board == "arm":
                if self.serial_arm and self.serial_arm.is_open:
                    try:
                        self.serial_arm.close()
                    except Exception:
                        pass
                self.serial_arm = None
                self.esp32_arm_port = None
            elif board in ("tello", "drone", "telloscreen"):
                if self.serial_tello and self.serial_tello.is_open:
                    try:
                        self.serial_tello.close()
                    except Exception:
                        pass
                self.serial_tello = None
                self.esp32_tello_port = None
            elif board in ("rover", "waverover", "wave_rover"):
                if self.serial_rover and self.serial_rover.is_open:
                    try:
                        self.serial_rover.close()
                    except Exception:
                        pass
                self.serial_rover = None
                self.esp32_rover_port = None
                if hasattr(self, 'waverover') and self.waverover:
                    self.waverover.connect("None (Simulation Mode)")
            board_desc = "Birds ESP32-S3 Touchscreen" if board in ("birds", "left", "right", "unified", "birdsscreen", "touchscreen", "screen", "led") else f"ESP32 {board.title()}"
            return True, f"{board_desc} set to Simulation Mode"

        if board in ("hexapod", "hexipod", "shobots", "bot"):
            if hasattr(self, 'hexapod') and self.hexapod:
                ok, msg = self.hexapod.connect(port_name)
                self.hexapod_port = port_name
                self.hexapod_bt_port = port_name
                self.shobots_bt_port = port_name
                return ok, msg
            return True, f"Hexapod set to {port_name}"

        if board in ("rover", "waverover", "wave_rover"):
            if hasattr(self, 'waverover') and self.waverover:
                ok, msg = self.waverover.connect(port_name)
                self.esp32_rover_port = port_name
                self.serial_rover = self.waverover.serial_conn
                return ok, msg

        try:
            conn = serial.Serial()
            conn.port = port_name
            conn.baudrate = 115200
            conn.timeout = 1
            conn.dtr = False
            conn.rts = False
            conn.open()

            if board in ("birds", "left", "right", "led", "unified", "birdsscreen", "touchscreen", "screen"):
                if self.serial_birds and self.serial_birds.is_open:
                    try:
                        self.serial_birds.close()
                    except Exception:
                        pass
                elif self.serial_left and self.serial_left.is_open:
                    try:
                        self.serial_left.close()
                    except Exception:
                        pass
                self.serial_birds = conn
                self.serial_left = conn
                self.serial_right = conn
                self.esp32_birds_port = port_name
                self.esp32_left_port = port_name
                self.esp32_right_port = port_name
                self.esp32_port = port_name
                self.serial_conn = conn
                return True, f"Connected Birds ESP32-S3 Touchscreen (esp32_Birds.ino) on {port_name}"
            elif board == "arm":
                if self.serial_arm and self.serial_arm.is_open:
                    try:
                        self.serial_arm.close()
                    except Exception:
                        pass
                self.serial_arm = conn
                self.esp32_arm_port = port_name
                if not self.serial_conn or not self.serial_conn.is_open:
                    self.serial_conn = conn
                return True, f"Connected ESP32 Arm (esp32_arm.ino) on {port_name}"
            elif board in ("tello", "drone", "telloscreen"):
                if self.serial_tello and self.serial_tello.is_open:
                    try:
                        self.serial_tello.close()
                    except Exception:
                        pass
                self.serial_tello = conn
                self.esp32_tello_port = port_name
                return True, f"Connected ESP32-S3 7\" Touch LCD Tello Screen (esp32_tello.ino) on {port_name}"
            return True, f"Connected {board.title()} on {port_name}"
        except Exception as e:
            return False, f"Failed {board.title()} ({port_name}): {e}"

    def trigger_esp32_gpio(self, board: str, gpio: int, function_name: str, state: bool = None) -> dict:
        board = board.lower()
        if board == "arm":
            conn = self.serial_arm
            prefix = ""
        elif board == "left":
            conn = self.serial_left
            prefix = "L:"
        else:
            conn = self.serial_right or self.serial_left
            prefix = "R:"
        if not conn or not conn.is_open:
            conn = self.serial_conn

        if state is True or state == 1 or state == "1":
            cmd_str = f"{prefix}{gpio}:1\r\n"
        elif state is False or state == 0 or state == "0":
            cmd_str = f"{prefix}{gpio}:0\r\n"
        elif state == "PULSE" or state == "pulse":
            cmd_str = f"{prefix}{gpio}:PULSE\r\n"
        else:
            cmd_str = f"{prefix}{gpio}\r\n"

        if conn and conn.is_open:
            try:
                cmd = cmd_str.encode("utf-8")
                conn.write(cmd)
                conn.flush()
                state_desc = "PULSE" if (state == "PULSE" or state == "pulse") else ("ON" if (state is True or state == 1 or state == "1") else ("OFF" if (state is False or state == 0 or state == "0") else "TOGGLE"))
                msg = f"Sent trigger to ESP32 {board.title()}: '{function_name}' ({state_desc} GPIO {gpio})"
                print(f"\n[ESP32-{board.upper()}] {msg}")
                return {"status": "success", "board": board, "gpio": gpio, "message": msg, "state": state}
            except Exception as e:
                err_msg = f"Error writing to ESP32 {board.title()} (GPIO {gpio}): {e}"
                print(f"\n[ESP32-{board.upper()}] {err_msg}")
                return {"status": "error", "message": err_msg}
        else:
            state_desc = "PULSE" if (state == "PULSE" or state == "pulse") else ("ON" if (state is True or state == 1 or state == "1") else ("OFF" if (state is False or state == 0 or state == "0") else "TOGGLE"))
            sim_msg = f"[Simulated ESP32 {board.title()}] Triggered '{function_name}' ({state_desc}) on GPIO {gpio}"
            print(f"\n{sim_msg}")
            return {"status": "success", "board": board, "gpio": gpio, "simulated": True, "message": sim_msg, "state": state}

    def set_led_state(self, state: bool) -> dict:
        return self.trigger_esp32_gpio("left", 2, "Builtin LED ON" if state else "Builtin LED OFF")

    def pulse_led(self, count: int = 1, gpio: int = None, duration_ms: int = 500) -> dict:
        target_gpio = gpio if gpio is not None else 2
        for _ in range(count):
            self.trigger_esp32_gpio("left", target_gpio, f"Pulse GPIO {target_gpio}", state="PULSE")
        return {"status": "success", "pulse_count": count, "gpio": target_gpio}

    async def pulse_led_async(self, count: int = 1, gpio: int = None, duration_ms: int = 500) -> dict:
        return await asyncio.to_thread(self.pulse_led, count, gpio, duration_ms)

    def execute_bird_routine(self, routine: str) -> dict:
        routine = routine.lower().strip()
        cmd_str = f"ROUTINE:{routine}\r\n"
        conn = self.serial_left or self.serial_conn
        if conn and conn.is_open:
            try:
                conn.write(cmd_str.encode("utf-8"))
                conn.flush()
                msg = f"Triggered routine '{routine.upper()}' on Waveshare 7-inch Touch-LCD ESP32"
                print(f"\n[ESP32-TOUCH-7] {msg}")
                if hasattr(self, 'gui_window') and self.gui_window and hasattr(self.gui_window, 'update_status'):
                    self.gui_window.update_status(f"Routine: {routine.upper()}")
                return {"status": "success", "routine": routine, "message": msg}
            except Exception as e:
                err_msg = f"Error sending routine '{routine}': {e}"
                print(f"\n[ESP32-TOUCH-7] {err_msg}")
                return {"status": "error", "message": err_msg}
        else:
            sim_msg = f"[Simulated Waveshare 7\" Touch-LCD] Triggered routine '{routine.upper()}'"
            print(f"\n{sim_msg}")
            if hasattr(self, 'gui_window') and self.gui_window and hasattr(self.gui_window, 'update_status'):
                self.gui_window.update_status(f"Simulated Routine: {routine.upper()}")
            return {"status": "success", "routine": routine, "simulated": True, "message": sim_msg}

    async def execute_bird_routine_async(self, routine: str) -> dict:
        return await asyncio.to_thread(self.execute_bird_routine, routine)

        conn = None
        if device in ("birds", "bird", "left", "stage"):
            conn = self.serial_left or self.serial_conn
        elif device in ("arm", "robot_arm"):
            conn = self.serial_arm
        elif device in ("hexapod", "hex", "shobots"):
            conn = getattr(self.hexapod, 'serial_hexapod', None) or self.serial_conn
        elif device in ("tello", "drone", "telloscreen"):
            conn = self.serial_tello
        elif device in ("rover", "waverover", "wave_rover"):
            conn = getattr(self, 'serial_rover', None) or getattr(self.waverover, 'serial_conn', None)

        if conn and conn.is_open:
            try:
                if vol_cmd:
                    conn.write(vol_cmd.encode("utf-8"))
                conn.write(cmd_str.encode("utf-8"))
                conn.flush()
                msg = f"Sent sound command '{sound.upper()}' to {device.upper()} over MAX98357A I2S"
                print(f"\n[MAX98357A-{device.upper()}] {msg}")
                if hasattr(self, 'gui_window') and self.gui_window and hasattr(self.gui_window, 'update_status'):
                    self.gui_window.update_status(f"Sound [{device.upper()}]: {sound.upper()}")
                return {"status": "success", "device": device, "sound": sound, "message": msg}
            except Exception as e:
                err_msg = f"Error sending sound to {device}: {e}"
                print(f"\n[MAX98357A-ERROR] {err_msg}")
                return {"status": "error", "message": err_msg}
        else:
            sim_msg = f"[Simulated MAX98357A Audio] Played sound '{sound.upper()}' on {device.upper()}"
            print(f"\n{sim_msg}")
            if hasattr(self, 'gui_window') and self.gui_window and hasattr(self.gui_window, 'update_status'):
                self.gui_window.update_status(f"Simulated Sound: {sound.upper()} ({device.upper()})")
            return {"status": "success", "simulated": True, "device": device, "sound": sound, "message": sim_msg}

    async def play_hardware_sound_async(self, device: str, sound: str, volume: int = None) -> dict:
        return await asyncio.to_thread(self.play_hardware_sound, device, sound, volume)

    def set_active_parrot(self, parrot: str) -> dict:
        parrot = parrot.lower().strip()
        cmd_str = f"PARROT_SEL:{parrot.upper()}\r\n"
        conn = self.serial_left or self.serial_conn
        if conn and conn.is_open:
            try:
                conn.write(cmd_str.encode("utf-8"))
                conn.flush()
                msg = f"Set speaking parrot to '{parrot.upper()}' on Waveshare 7-inch Touch-LCD ESP32"
                print(f"\n[ESP32-TOUCH-7] {msg}")
                if hasattr(self, 'gui_window') and self.gui_window and hasattr(self.gui_window, 'update_status'):
                    self.gui_window.update_status(f"Active Parrot: {parrot.upper()}")
                return {"status": "success", "parrot": parrot, "message": msg}
            except Exception as e:
                err_msg = f"Error setting active parrot: {e}"
                print(f"\n[ESP32-TOUCH-7] {err_msg}")
                return {"status": "error", "message": err_msg}
        else:
            sim_msg = f"[Simulated Waveshare 7\" Touch-LCD] Set speaking parrot to '{parrot.upper()}'"
            print(f"\n{sim_msg}")
            if hasattr(self, 'gui_window') and self.gui_window and hasattr(self.gui_window, 'update_status'):
                self.gui_window.update_status(f"Simulated Parrot: {parrot.upper()}")
            return {"status": "success", "parrot": parrot, "simulated": True, "message": sim_msg}

    async def set_active_parrot_async(self, parrot: str) -> dict:
        return await asyncio.to_thread(self.set_active_parrot, parrot)

    def set_parrot_sound_reactivity(self, enabled: bool) -> dict:
        cmd_str = f"MIC_REACT:{'1' if enabled else '0'}\r\n"
        conn = self.serial_left or self.serial_conn
        if conn and conn.is_open:
            try:
                conn.write(cmd_str.encode("utf-8"))
                conn.flush()
                msg = f"Set parrot sound reactivity to {'ENABLED' if enabled else 'DISABLED'}"
                print(f"\n[ESP32-TOUCH-7] {msg}")
                return {"status": "success", "enabled": enabled, "message": msg}
            except Exception as e:
                return {"status": "error", "message": str(e)}
        else:
            sim_msg = f"[Simulated Waveshare 7\" Touch-LCD] Parrot sound reactivity: {'ENABLED' if enabled else 'DISABLED'}"
            print(f"\n{sim_msg}")
            return {"status": "success", "enabled": enabled, "simulated": True, "message": sim_msg}

    async def set_parrot_sound_reactivity_async(self, enabled: bool) -> dict:
        return await asyncio.to_thread(self.set_parrot_sound_reactivity, enabled)

    def dispatch_speech_event(self, speaking: bool):
        """Broadcasts AI_SPEAKING:1 / AI_SPEAKING:0 over serial/BT to all enabled robots in real time."""
        cmd_bytes = b"AI_SPEAKING:1\r\n" if speaking else b"AI_SPEAKING:0\r\n"
        cmd_str = "AI_SPEAKING:1\r\n" if speaking else "AI_SPEAKING:0\r\n"

        # 1. Birds / Parrot Controller
        if getattr(self, 'speech_react_birds', True):
            conn = getattr(self, 'serial_birds', None) or getattr(self, 'serial_left', None) or getattr(self, 'serial_conn', None)
            if conn and conn.is_open:
                try:
                    conn.write(cmd_bytes)
                    conn.flush()
                except Exception:
                    pass

        # 2. Hexapod Controller
        if getattr(self, 'speech_react_hexapod', True):
            if hasattr(self, 'hexapod') and self.hexapod:
                try:
                    self.hexapod.send_bt_command(cmd_str)
                except Exception:
                    pass

        # 3. 6-DOF Robot Arm Controller
        if getattr(self, 'speech_react_arm', True):
            conn_arm = getattr(self, 'serial_arm', None)
            if conn_arm and conn_arm.is_open:
                try:
                    conn_arm.write(cmd_bytes)
                    conn_arm.flush()
                except Exception:
                    pass

        # 4. Waveshare Wave Rover Controller (DC Mouth & Body Motion Motors, Eye LEDs)
        if getattr(self, 'speech_react_rover', True):
            if hasattr(self, 'waverover') and self.waverover:
                try:
                    self.waverover.send_command(cmd_str)
                except Exception:
                    pass

    def set_speech_reactivity(self, robot: str = "all", enabled: bool = True) -> dict:
        """Enables or disables speech reactivity for specific robots ('birds', 'hexapod', 'arm', 'rover', 'all')."""
        robot = (robot or "all").lower().strip()
        if robot in ("birds", "bird", "parrot", "parrots"):
            self.speech_react_birds = enabled
            msg = f"Birds speech animatronics (mouth & eyes) {'ENABLED' if enabled else 'DISABLED'}"
        elif robot in ("hexapod", "hexipod", "shobots", "bot"):
            self.speech_react_hexapod = enabled
            msg = f"Hexapod speech reactivity (eyes & body sway) {'ENABLED' if enabled else 'DISABLED'}"
        elif robot in ("arm", "robot_arm", "robotarm"):
            self.speech_react_arm = enabled
            msg = f"Robot Arm conversational gesturing {'ENABLED' if enabled else 'DISABLED'}"
        elif robot in ("rover", "waverover", "wave_rover"):
            self.speech_react_rover = enabled
            msg = f"Waveshare Wave Rover speech animatronics (DC mouth & body up/down motor) {'ENABLED' if enabled else 'DISABLED'}"
        elif robot in ("all", "both", "every", "robots"):
            self.speech_react_birds = enabled
            self.speech_react_hexapod = enabled
            self.speech_react_arm = enabled
            self.speech_react_rover = enabled
            msg = f"All robots speech reactivity {'ENABLED' if enabled else 'DISABLED'}"
        else:
            return {"status": "error", "message": f"Unknown robot '{robot}'. Options: 'all', 'birds', 'hexapod', 'arm', 'rover'"}

        print(f"\n[Speech Reactivity] {msg}")
        if hasattr(self, 'gui_window') and self.gui_window and hasattr(self.gui_window, 'sync_speech_react_switches'):
            self.gui_window.sync_speech_react_switches()
        return {
            "status": "success",
            "message": msg,
            "birds": self.speech_react_birds,
            "hexapod": self.speech_react_hexapod,
            "arm": self.speech_react_arm,
            "rover": self.speech_react_rover
        }

    async def set_speech_reactivity_async(self, robot: str = "all", enabled: bool = True) -> dict:
        return await asyncio.to_thread(self.set_speech_reactivity, robot, enabled)

    async def send_tello_command(self, command: str) -> dict:
        return await asyncio.to_thread(self.tello.send_cmd, command)

    async def set_leviton_light_state(self, switch_name: str, state: bool, brightness: int = None) -> dict:
        return await asyncio.to_thread(self.leviton.set_light_state, switch_name, state, brightness)

    def set_servo_angle(self, board: str = "left", channel: int = None, servo_name: str = None, angle: int = 90) -> dict:
        board = board.lower() if board else "left"
        if board == "arm":
            conn = self.serial_arm
            cmd_str = f"SERVO:{channel}:{angle}\r\n"
        elif board == "left":
            conn = self.serial_left
            cmd_str = f"SERVO:L:{channel}:{angle}\r\n"
        elif board == "right":
            conn = self.serial_right or self.serial_left
            cmd_str = f"SERVO:R:{channel}:{angle}\r\n"
        else:
            conn = self.serial_left
            cmd_str = f"SERVO:L:{channel}:{angle}\r\n"

        if not conn or not conn.is_open:
            conn = self.serial_conn

        if channel is None and servo_name:
            servos = SERVO_CONFIG.get(board, [])
            for s in servos:
                if s["name"].lower() in servo_name.lower() or servo_name.lower() in s["name"].lower():
                    channel = s["channel"]
                    break
        if channel is None:
            channel = 0

        angle = max(0, min(180, int(angle)))

        if conn and conn.is_open:
            try:
                conn.write(cmd_str.encode("utf-8"))
                conn.flush()
                msg = f"Set PCA9685 Servo on {board.title()} ESP32 channel {channel} ('{servo_name or 'Servo'}') to {angle}°"
                print(f"\n[ESP32-{board.upper()}] {msg}")
                return {"status": "success", "board": board, "channel": channel, "angle": angle, "message": msg}
            except Exception as e:
                err_msg = f"Error sending servo command to {board.title()} ESP32: {e}"
                print(f"\n[ESP32-{board.upper()}] {err_msg}")
                return {"status": "error", "message": err_msg}
        else:
            sim_msg = f"[Simulated ESP32 {board.title()}] Set PCA9685 Servo Channel {channel} ('{servo_name or 'Servo'}') to {angle}°"
            print(f"\n{sim_msg}")
            return {"status": "success", "board": board, "channel": channel, "angle": angle, "simulated": True, "message": sim_msg}

    async def set_servo_angle_async(self, board: str = "left", channel: int = None, servo_name: str = None, angle: int = 90) -> dict:
        return await asyncio.to_thread(self.set_servo_angle, board, channel, servo_name, angle)

    async def set_ewelink_device_state(self, device_name: str, state: bool) -> dict:
        return await asyncio.to_thread(self.ewelink.set_device_state, device_name, state)


    async def send_text(self):
        while True:
            text = await asyncio.to_thread(
                input,
                "message > ",
            )
            if text.lower() == "q":
                break
            if self.session is not None:
                await self.session.send(input=text or ".", end_of_turn=True)

    def _camera_thread_loop(self, loop):
        import time
        import mediapipe as mp
        
        mp_hands = mp.solutions.hands
        hands = mp_hands.Hands(
            static_image_mode=False,
            max_num_hands=1,
            min_detection_confidence=0.5,
            min_tracking_confidence=0.5
        )
        
        hand_detected_start_time = None
        hand_lost_start_time = None
        prompt_sent = False
        
        cap = cv2.VideoCapture(self.camera_idx)
        last_send_time = 0
        while cap.isOpened():
            ret, frame = cap.read()
            if not ret:
                break
            
            cv2.imshow("Camera", frame)
            # waitKey is required to update the cv2 window. 
            # If 'q' is pressed, it will break but we won't exit the program, 
            # just stop the camera thread.
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break
                
            # Convert BGR to RGB color space for MediaPipe (and PIL later)
            frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            
            # Run hand detection
            results = hands.process(frame_rgb)
            hand_present = results.multi_hand_landmarks is not None
            
            current_time = time.time()
            if hand_present:
                hand_lost_start_time = None
                if hand_detected_start_time is None:
                    hand_detected_start_time = current_time
                
                # If hand has been detected continuously for 0.5 seconds and prompt hasn't been sent yet
                if not prompt_sent and (current_time - hand_detected_start_time >= 0.5):
                    finger_count = count_fingers(results.multi_hand_landmarks[0])
                    print(f"\n[Camera Thread] Hand detected with {finger_count} fingers! Pulsing GPIO {finger_count} on and off 1 time.")
                    
                    # Direct hardware pulse on target GPIO pin equal to finger_count
                    self.pulse_led(count=1, gpio=finger_count)

                    trigger_msg = {
                        "type": "text",
                        "data": f"System: The user just held up their hand to the camera showing exactly {finger_count} finger{'s' if finger_count != 1 else ''}. GPIO pin {finger_count} was pulsed on and off 1 time. Please confirm to the user that {finger_count} finger{'s' if finger_count != 1 else ''} was detected and GPIO pin {finger_count} was pulsed on and off 1 time."
                    }
                    if self.out_queue is not None and not self.out_queue.full():
                        loop.call_soon_threadsafe(self.out_queue.put_nowait, trigger_msg)
                    prompt_sent = True
                    last_send_time = 0  # Force an immediate frame send
            else:
                hand_detected_start_time = None
                if hand_lost_start_time is None:
                    hand_lost_start_time = current_time
                
                # If hand has been gone continuously for 1.0 seconds, reset prompt_sent
                if prompt_sent and (current_time - hand_lost_start_time >= 1.0):
                    print("\n[Camera Thread] Hand removed for 1.0 seconds. Resetting gesture trigger.")
                    prompt_sent = False
                    
            if current_time - last_send_time >= 1.0:
                last_send_time = current_time
                
                img = PIL.Image.fromarray(frame_rgb)
                img.thumbnail([1024, 1024])

                image_io = io.BytesIO()
                img.save(image_io, format="jpeg")
                image_io.seek(0)

                mime_type = "image/jpeg"
                image_bytes = image_io.read()
                msg = {"type": "video", "mime_type": mime_type, "data": image_bytes}
                
                if self.out_queue is not None and not self.out_queue.full():
                    loop.call_soon_threadsafe(self.out_queue.put_nowait, msg)

        cap.release()
        cv2.destroyAllWindows()

    async def get_frames(self):
        loop = asyncio.get_running_loop()
        await asyncio.to_thread(self._camera_thread_loop, loop)

    def _get_screen(self):
        try:
            import mss  # pytype: disable=import-error # pylint: disable=g-import-not-at-top
        except ImportError as e:
            raise ImportError("Please install mss package using 'pip install mss'") from e
        sct = mss.mss()
        monitor = sct.monitors[0]

        i = sct.grab(monitor)

        mime_type = "image/jpeg"
        image_bytes = mss.tools.to_png(i.rgb, i.size)
        img = PIL.Image.open(io.BytesIO(image_bytes))

        image_io = io.BytesIO()
        img.save(image_io, format="jpeg")
        image_io.seek(0)

        image_bytes = image_io.read()
        return {"type": "video", "mime_type": mime_type, "data": image_bytes}

    async def get_screen(self):

        while True:
            frame = await asyncio.to_thread(self._get_screen)
            if frame is None:
                break

            await asyncio.sleep(1.0)

            if self.out_queue is not None:
                await self.out_queue.put(frame)

    async def send_realtime(self):
        while True:
            if self.out_queue is not None:
                msg = await self.out_queue.get()
                if self.session is not None:
                    if msg.get("type") == "audio":
                        await self.session.send_realtime_input(audio=types.Blob(data=msg["data"], mime_type=msg["mime_type"]))
                    elif msg.get("type") == "video":
                        await self.session.send_realtime_input(video=types.Blob(data=msg["data"], mime_type=msg["mime_type"]))
                    elif msg.get("type") == "text":
                        await self.session.send(input=msg["data"], end_of_turn=True)
                    else:
                        await self.session.send(input=msg)

    async def listen_audio(self):
        mic_info = pya.get_default_input_device_info()
        input_device_index = self.mic_idx if self.mic_idx is not None else mic_info["index"]
        self.audio_stream = await asyncio.to_thread(
            pya.open,
            format=FORMAT,
            channels=CHANNELS,
            rate=SEND_SAMPLE_RATE,
            input=True,
            input_device_index=input_device_index,
            frames_per_buffer=CHUNK_SIZE,
        )
        if __debug__:
            kwargs = {"exception_on_overflow": False}
        else:
            kwargs = {}
        while True:
            data = await asyncio.to_thread(self.audio_stream.read, CHUNK_SIZE, **kwargs)
            if self.out_queue is not None and not self.playing_audio:
                await self.out_queue.put({"type": "audio", "data": data, "mime_type": "audio/pcm;rate=16000"})

    async def receive_audio(self):
        "Background task to reads from the websocket and write pcm chunks to the output queue"
        while True:
            if self.session is not None:
                turn = self.session.receive()
                async for response in turn:
                    if response.tool_call:
                        function_responses = []
                        for fc in response.tool_call.function_calls:
                            if fc.name == "set_led_state":
                                state = fc.args.get("state")
                                result = self.set_led_state(state)
                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "send_tello_command":
                                command = fc.args.get("command")
                                result = await self.send_tello_command(command)
                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "set_leviton_light_state":
                                switch_name = fc.args.get("switch_name")
                                state = fc.args.get("state")
                                brightness = fc.args.get("brightness")
                                result = await self.set_leviton_light_state(switch_name, state, brightness)
                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "set_ewelink_device_state":
                                device_name = fc.args.get("device_name")
                                state = fc.args.get("state")
                                result = await self.set_ewelink_device_state(device_name, state)
                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "set_servo_angle":
                                board = fc.args.get("board", "left")
                                channel = fc.args.get("channel")
                                servo_name = fc.args.get("servo_name")
                                angle = fc.args.get("angle", 90)
                                result = await self.set_servo_angle_async(board=board, channel=channel, servo_name=servo_name, angle=angle)
                                if hasattr(self, 'gui_window') and self.gui_window:
                                    if board.lower() == "arm":
                                        self.gui_window.update_arm_joint_slider(result.get("channel", 0), angle)
                                    else:
                                        self.gui_window.update_servo_slider(result.get("board", board), result.get("channel", 0), angle)
                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name in ("control_hexapod", "control_shobots"):
                                action = fc.args.get("action")
                                leg_name = fc.args.get("leg_name")
                                joint_name = fc.args.get("joint_name")
                                angle = fc.args.get("angle")
                                x = fc.args.get("x")
                                y = fc.args.get("y")
                                z = fc.args.get("z")
                                duration = fc.args.get("duration", 200)
                                lcd_msg = fc.args.get("lcd_message")

                                if lcd_msg:
                                    await asyncio.to_thread(self.hexapod.set_lcd_message, lcd_msg)

                                if action == "set_ik" or (leg_name and x is not None and y is not None and z is not None):
                                    result = await asyncio.to_thread(self.hexapod.move_leg_ik, leg_name, x, y, z, duration)
                                elif action == "set_joint" or (leg_name and joint_name and angle is not None):
                                    result = await asyncio.to_thread(self.hexapod.set_joint_angle, leg_name, joint_name, angle)
                                elif action == "set_lcd_message" and lcd_msg:
                                    result = {"status": "success", "action": "set_lcd_message", "message": f"Displayed '{lcd_msg}' on ESP-32-Touch-LCD"}
                                elif action:
                                    result = await asyncio.to_thread(self.hexapod.execute_action, action)
                                else:
                                    result = {"status": "error", "message": "Specify action, set_joint (leg+joint+angle), set_ik (leg+x+y+z), or set_lcd_message"}

                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "control_robot_arm":
                                action = fc.args.get("action")
                                channel = fc.args.get("channel")
                                angle = fc.args.get("angle")
                                x = fc.args.get("x")
                                y = fc.args.get("y")
                                z = fc.args.get("z")
                                pitch = fc.args.get("pitch", 0)
                                roll = fc.args.get("roll", 90)
                                claw = fc.args.get("claw", 40)
                                duration = fc.args.get("duration", 250)

                                if action == "move_ik" or (x is not None and y is not None and z is not None):
                                    result = await asyncio.to_thread(self.robot_arm.move_arm_ik, x, y, z, pitch, roll, claw, duration)
                                elif action:
                                    result = await asyncio.to_thread(self.robot_arm.execute_action, action)
                                elif channel is not None and angle is not None:
                                    result = await asyncio.to_thread(self.robot_arm.set_joint_angle, channel, angle)
                                else:
                                    result = {"status": "error", "message": "Specify action, channel+angle, or x+y+z IK coordinates"}

                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "control_wave_rover":
                                action = fc.args.get("action", "stop")
                                speed = fc.args.get("speed")
                                pan = fc.args.get("pan")
                                tilt = fc.args.get("tilt")
                                mouth = fc.args.get("mouth")
                                body_motion = fc.args.get("body_motion")
                                eye_leds = fc.args.get("eye_leds")
                                headlight = fc.args.get("headlight")
                                lcd_msg = fc.args.get("lcd_message")

                                if mouth is not None:
                                    await asyncio.to_thread(self.waverover.set_mouth_state, mouth)
                                if body_motion is not None:
                                    await asyncio.to_thread(self.waverover.set_body_motor_state, body_motion)
                                if eye_leds is not None:
                                    await asyncio.to_thread(self.waverover.set_eye_leds, eye_leds)
                                if headlight is not None:
                                    await asyncio.to_thread(self.waverover.set_headlight, headlight)
                                if speed is not None:
                                    await asyncio.to_thread(self.waverover.set_speed, speed)
                                if pan is not None or tilt is not None:
                                    await asyncio.to_thread(self.waverover.set_pan_tilt, pan, tilt)
                                if lcd_msg:
                                    await asyncio.to_thread(self.waverover.set_lcd_message, lcd_msg)

                                result = await asyncio.to_thread(self.waverover.execute_action, action)

                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "get_proximity_sensors":
                                robot = (fc.args.get("robot") or "rover").lower()
                                if "hex" in robot:
                                    result = await asyncio.to_thread(self.hexapod.get_sonar_distances)
                                else:
                                    result = await asyncio.to_thread(self.waverover.get_sonar_distances)

                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "trigger_bird_routine":
                                routine = fc.args.get("routine", "home")
                                result = await self.execute_bird_routine_async(routine)
                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "select_active_parrot":
                                parrot = fc.args.get("parrot", "left")
                                result = await self.set_active_parrot_async(parrot)
                                if hasattr(self, 'gui_window') and self.gui_window and hasattr(self.gui_window, 'parrot_mode_var'):
                                    self.gui_window.parrot_mode_var.set(parrot.lower())
                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "set_parrot_sound_reactivity":
                                enabled = fc.args.get("enabled", True)
                                result = await self.set_parrot_sound_reactivity_async(enabled)
                                if hasattr(self, 'gui_window') and self.gui_window and hasattr(self.gui_window, 'mic_react_var'):
                                    self.gui_window.mic_react_var.set(bool(enabled))
                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "set_speech_reactivity":
                                robot = fc.args.get("robot", "all")
                                enabled = fc.args.get("enabled", True)
                                result = await self.set_speech_reactivity_async(robot, enabled)
                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "pulse_led":
                                count = fc.args.get("count", 1)
                                gpio = fc.args.get("gpio", 2)
                                duration_ms = fc.args.get("duration_ms", 500)
                                result = await self.pulse_led_async(count=count, gpio=gpio, duration_ms=duration_ms)
                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                            elif fc.name == "play_hardware_sound":
                                device = fc.args.get("device", "birds")
                                sound = fc.args.get("sound", "chirp")
                                volume = fc.args.get("volume")
                                result = await self.play_hardware_sound_async(device=device, sound=sound, volume=volume)
                                function_responses.append(
                                    types.FunctionResponse(
                                        name=fc.name,
                                        response=result,
                                        id=fc.id
                                    )
                                )
                        if function_responses:
                            await self.session.send_tool_response(function_responses=function_responses)
                        continue
                    if data := response.data:
                        self.audio_in_queue.put_nowait(data)
                        continue
                    if text := response.text:
                        print(text, end="")

                # If you interrupt the model, it sends a turn_complete.
                # For interruptions to work, we need to stop playback.
                # So empty out the audio queue because it may have loaded
                # much more audio than has played yet.
                while not self.audio_in_queue.empty():
                    self.audio_in_queue.get_nowait()
                if self.playing_audio:
                    self.playing_audio = False
                    self.dispatch_speech_event(False)

    async def play_audio(self):
        output_device_index = self.speaker_idx
        kwargs = {"output_device_index": output_device_index} if output_device_index is not None else {}
        stream = await asyncio.to_thread(
            pya.open,
            format=FORMAT,
            channels=CHANNELS,
            rate=RECEIVE_SAMPLE_RATE,
            output=True,
            **kwargs
        )
        while True:
            if self.audio_in_queue is not None:
                bytestream = await self.audio_in_queue.get()
                if not self.playing_audio:
                    self.playing_audio = True
                    self.dispatch_speech_event(True)
                await asyncio.to_thread(stream.write, bytestream)
                if self.audio_in_queue.empty():
                    self.playing_audio = False
                    self.dispatch_speech_event(False)

    async def run(self):
        birds_port = getattr(self, 'esp32_birds_port', None) or self.esp32_left_port
        if birds_port:
            self.connect_esp32("birds", birds_port)

        if hasattr(self, 'hexapod') and self.hexapod:
            self.hexapod.connect(self.hexapod_bt_port)

        if not birds_port:
            print("No ESP32 Birds Touchscreen port specified. Running in simulation mode.")

        # Launch Thinker GUI window
        try:
            gui_window = ESP32PulseWindow(self)
            gui_window.start_gui()
            print("[GUI] Thinker Window launched.")
        except Exception as gui_err:
            print(f"[GUI] Could not launch Thinker window: {gui_err}")

        try:
            enable_esp32 = True
            async with (
                client.aio.live.connect(model=MODEL, config=get_config(self.voice_name, enable_esp32=enable_esp32)) as session,
                asyncio.TaskGroup() as tg,
            ):
                self.session = session

                self.audio_in_queue = asyncio.Queue()
                self.out_queue = asyncio.Queue(maxsize=5)

                send_text_task = tg.create_task(self.send_text())
                tg.create_task(self.send_realtime())
                tg.create_task(self.listen_audio())
                if self.video_mode == "camera":
                    tg.create_task(self.get_frames())
                elif self.video_mode == "screen":
                    tg.create_task(self.get_screen())

                tg.create_task(self.receive_audio())
                tg.create_task(self.play_audio())

                await send_text_task
                raise asyncio.CancelledError("User requested exit")

        except asyncio.CancelledError:
            pass
        except ExceptionGroup as EG:
            if self.audio_stream is not None:
                self.audio_stream.close()
                traceback.print_exception(EG)
        finally:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.close()
                print("ESP32 serial connection closed.")


def choose_audio_device(pya_instance, is_input):
    devices = []
    for i in range(pya_instance.get_device_count()):
        info = pya_instance.get_device_info_by_index(i)
        if is_input and info.get('maxInputChannels') > 0:
            devices.append((i, info.get('name')))
        elif not is_input and info.get('maxOutputChannels') > 0:
            devices.append((i, info.get('name')))
            
    device_type = "microphone" if is_input else "speaker"
    print(f"\nAvailable {device_type}s:")
    for idx, name in devices:
        print(f"[{idx}] {name}")
        
    try:
        default_info = pya_instance.get_default_input_device_info() if is_input else pya_instance.get_default_output_device_info()
        default_idx = default_info["index"]
    except Exception:
        default_idx = devices[0][0] if devices else None

    if default_idx is not None:
        print(f"Default is [{default_idx}].")
        
    while True:
        try:
            choice = input(f"Select {device_type} by index [default: press Enter]: ").strip()
            if not choice:
                return default_idx
            choice = int(choice)
            if any(choice == d[0] for d in devices):
                return choice
            else:
                print("Invalid index, try again.")
        except ValueError:
            print("Please enter a valid number.")

def choose_camera():
    print("\nSearching for available cameras (this may take a moment)...")
    available_cameras = []
    # Test first 4 indices
    for i in range(4):
        cap = cv2.VideoCapture(i)
        if cap.isOpened():
            ret, _ = cap.read()
            if ret:
                available_cameras.append(i)
            cap.release()
            
    if not available_cameras:
        print("No cameras found.")
        return None
        
    print("Available cameras:")
    for idx in available_cameras:
        print(f"[{idx}] Camera {idx}")
        
    while True:
        try:
            choice = input("Select camera by index [default: press Enter]: ").strip()
            if not choice:
                return available_cameras[0]
            choice = int(choice)
            if choice in available_cameras:
                return choice
            else:
                print("Invalid index, try again.")
        except ValueError:
            print("Please enter a valid number.")

def choose_voice():
    voices = ["Aoede", "Charon", "Kore", "Puck", "Zephyr"]
    print("\nAvailable voices:")
    for idx, name in enumerate(voices):
        print(f"[{idx}] {name}")
    print("Default is [4] (Zephyr).")
    
    while True:
        try:
            choice = input("Select voice by index [default: press Enter]: ").strip()
            if not choice:
                return "Zephyr"
            choice = int(choice)
            if 0 <= choice < len(voices):
                return voices[choice]
            else:
                print("Invalid index, try again.")
        except ValueError:
            print("Please enter a valid number.")

def choose_esp32_port():
    try:
        import serial.tools.list_ports
        ports = list(serial.tools.list_ports.comports())
    except ImportError:
        print("pyserial is not installed or not available. Skipping Birds / ESP32 port selection.")
        return None

    print("\nAvailable serial ports for Birds / Waveshare 7-inch Touch LCD Controller (esp32_Birds.ino):")
    print("[N] None (Skip Birds connection)")
    for idx, p in enumerate(ports):
        print(f"[{idx}] {p.device} - {p.description}")
    
    while True:
        try:
            choice = input("Select Birds / ESP32 port by index [default: None]: ").strip()
            if not choice or choice.upper() == 'N':
                return None
            choice_idx = int(choice)
            if 0 <= choice_idx < len(ports):
                return ports[choice_idx].device
            else:
                print("Invalid index, try again.")
        except ValueError:
            print("Please enter a valid number or 'N'.")

def choose_tello_port():
    while True:
        port_input = input("Enter Tello drone port [default: 8889]: ").strip()
        if not port_input:
            return 8889
        try:
            port = int(port_input)
            if 1 <= port <= 65535:
                return port
            else:
                print("Invalid port number. Must be between 1 and 65535.")
        except ValueError:
            print("Please enter a valid integer port number.")

def choose_tello_ip(port=8889):
    print("\nScanning local network for active devices...")
    import socket
    import subprocess
    import re
    import concurrent.futures
    import time

    # Find local subnets
    local_ips = []
    try:
        hostname = socket.gethostname()
        local_ips = socket.gethostbyname_ex(hostname)[2]
    except Exception:
        pass

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        primary_ip = s.getsockname()[0]
        s.close()
        if primary_ip not in local_ips:
            local_ips.append(primary_ip)
    except Exception:
        pass

    subnets = set()
    for ip in local_ips:
        if ip.startswith("127."):
            continue
        parts = ip.split(".")
        if len(parts) == 4:
            subnets.add(f"{parts[0]}.{parts[1]}.{parts[2]}.")

    # Common subnets to search
    subnets.add("192.168.10.")
    subnets.add("192.168.1.")
    subnets.add("192.168.0.")

    target_ips = []
    for subnet in subnets:
        for i in range(1, 255):
            target_ips.append(f"{subnet}{i}")

    def ping_udp(ip):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(0.1)
            sock.sendto(b'', (ip, 9))
            sock.close()
        except Exception:
            pass

    # Rapid UDP ping to populate OS ARP cache
    with concurrent.futures.ThreadPoolExecutor(max_workers=80) as executor:
        executor.map(ping_udp, target_ips)

    time.sleep(0.4)

    discovered = []
    tello_drones = []
    try:
        output = subprocess.check_output(["arp", "-a"]).decode('utf-8', errors='ignore')
        ip_mac_pattern = re.compile(
            r"^\s*(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})\s+([0-9a-fA-F:-]{17})\s+(\w+)",
            re.MULTILINE
        )

        def check_if_tello(ip):
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.settimeout(0.3)
                sock.sendto(b'command', (ip, port))
                data, _ = sock.recvfrom(1024)
                if b'ok' in data.lower():
                    return ip
            except Exception:
                pass
            finally:
                try:
                    sock.close()
                except Exception:
                    pass
            return None

        candidate_ips = []
        for match in ip_mac_pattern.finditer(output):
            ip, mac, link_type = match.groups()
            if ip.startswith("224.") or ip.startswith("239.") or ip.endswith(".255") or ip == "255.255.255.255":
                continue

            in_subnet = False
            for subnet in subnets:
                if ip.startswith(subnet):
                    in_subnet = True
                    break
            if in_subnet:
                candidate_ips.append((ip, mac))

        tello_detected = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=30) as checker:
            check_results = checker.map(check_if_tello, [ip for ip, _ in candidate_ips])
            for res in check_results:
                if res:
                    tello_detected.append(res)

        for ip, mac in candidate_ips:
            if ip in tello_detected:
                discovered.append(f"{ip} ({mac}) [Tello Drone]")
                tello_drones.append(ip)
            else:
                discovered.append(f"{ip} ({mac})")
    except Exception as e:
        print(f"Error scanning network: {e}")

    if discovered:
        print("\nActive devices found on the local network:")
        for idx, dev in enumerate(discovered, 1):
            print(f"  {idx}. {dev}")
    else:
        print("\nNo active devices found on the local network.")

    while True:
        default_ip = tello_drones[0] if tello_drones else "192.168.10.1"
        ip_input = input(f"\nEnter Tello drone IP address [default: {default_ip}]: ").strip()
        if not ip_input:
            return default_ip
        # In case the user typed/selected something with a MAC address suffix
        match = re.match(r"^(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})", ip_input)
        if match:
            return match.group(1)
        else:
            print("Invalid IP address format. Please enter a valid IPv4 address (e.g., 192.168.10.1).")

def show_settings_dialog(pya_instance, default_mode="camera"):
    try:
        import tkinter as tk
        from tkinter import ttk
        import re
    except ImportError:
        print("Tkinter not available. Falling back to command-line prompts.")
        return "fallback"

    # 1. Microphones
    mic_devices = []
    default_mic_idx = None
    try:
        default_mic_info = pya_instance.get_default_input_device_info()
        default_mic_idx = default_mic_info["index"]
    except Exception:
        pass

    for i in range(pya_instance.get_device_count()):
        try:
            info = pya_instance.get_device_info_by_index(i)
            if info.get('maxInputChannels') > 0:
                mic_devices.append((i, info.get('name')))
        except Exception:
            pass

    # 2. Speakers
    speaker_devices = []
    default_speaker_idx = None
    try:
        default_speaker_info = pya_instance.get_default_output_device_info()
        default_speaker_idx = default_speaker_info["index"]
    except Exception:
        pass

    for i in range(pya_instance.get_device_count()):
        try:
            info = pya_instance.get_device_info_by_index(i)
            if info.get('maxOutputChannels') > 0:
                speaker_devices.append((i, info.get('name')))
        except Exception:
            pass

    # 3. Cameras
    print("Scanning for available cameras for the settings window...")
    available_cameras = []
    for i in range(4):
        cap = cv2.VideoCapture(i)
        if cap.isOpened():
            ret, _ = cap.read()
            if ret:
                available_cameras.append(i)
            cap.release()

    # 4. Voices
    voices = ["Aoede", "Charon", "Kore", "Puck", "Zephyr"]

    # 5. COM/Serial ports
    com_ports = []
    try:
        import serial.tools.list_ports
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            com_ports.append(p.device)
    except Exception:
        pass

    result = {}
    started = False

    root = tk.Tk()
    root.title("Shobots Setup & Settings")
    root.geometry("480x500")
    root.resizable(False, False)

    # Use native style theme if available
    style = ttk.Style()
    try:
        style.theme_use('vista' if 'vista' in style.theme_names() else 'clam')
    except Exception:
        pass

    main_frame = ttk.Frame(root, padding="20 20 20 20")
    main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
    root.columnconfigure(0, weight=1)
    root.rowconfigure(0, weight=1)

    title_label = ttk.Label(main_frame, text="Configure Live Session Preferences", font=("Helvetica", 14, "bold"))
    title_label.grid(row=0, column=0, columnspan=2, pady=(0, 15), sticky=tk.W)

    # Microphone Selector
    ttk.Label(main_frame, text="Microphone:").grid(row=1, column=0, sticky=tk.W, pady=8)
    mic_options = [f"{name} (Index {idx})" for idx, name in mic_devices]
    mic_combo = ttk.Combobox(main_frame, values=mic_options, state="readonly", width=42)
    mic_combo.grid(row=1, column=1, sticky=tk.W, pady=8)
    
    default_mic_str = ""
    for idx, name in mic_devices:
        if idx == default_mic_idx:
            default_mic_str = f"{name} (Index {idx})"
            break
    if default_mic_str:
        mic_combo.set(default_mic_str)
    elif mic_options:
        mic_combo.current(0)

    # Speaker Selector
    ttk.Label(main_frame, text="Speaker:").grid(row=2, column=0, sticky=tk.W, pady=8)
    speaker_options = [f"{name} (Index {idx})" for idx, name in speaker_devices]
    speaker_combo = ttk.Combobox(main_frame, values=speaker_options, state="readonly", width=42)
    speaker_combo.grid(row=2, column=1, sticky=tk.W, pady=8)
    
    default_speaker_str = ""
    for idx, name in speaker_devices:
        if idx == default_speaker_idx:
            default_speaker_str = f"{name} (Index {idx})"
            break
    if default_speaker_str:
        speaker_combo.set(default_speaker_str)
    elif speaker_options:
        speaker_combo.current(0)

    # Video Mode Selector
    ttk.Label(main_frame, text="Video Mode:").grid(row=3, column=0, sticky=tk.W, pady=8)
    video_modes = ["Camera", "Screen Share", "None"]
    mode_combo = ttk.Combobox(main_frame, values=video_modes, state="readonly", width=42)
    mode_combo.grid(row=3, column=1, sticky=tk.W, pady=8)
    
    if default_mode == "camera":
        mode_combo.set("Camera")
    elif default_mode == "screen":
        mode_combo.set("Screen Share")
    else:
        mode_combo.set("None")

    # Camera Selector
    ttk.Label(main_frame, text="Camera Feed:").grid(row=4, column=0, sticky=tk.W, pady=8)
    camera_options = [f"Camera {idx}" for idx in available_cameras]
    if not camera_options:
        camera_options = ["No cameras detected"]
    camera_combo = ttk.Combobox(main_frame, values=camera_options, state="readonly", width=42)
    camera_combo.grid(row=4, column=1, sticky=tk.W, pady=8)
    if available_cameras:
        camera_combo.current(0)
    else:
        camera_combo.current(0)
        camera_combo.configure(state="disabled")

    # Voice Selector
    ttk.Label(main_frame, text="Gemini Voice:").grid(row=5, column=0, sticky=tk.W, pady=8)
    voice_combo = ttk.Combobox(main_frame, values=voices, state="readonly", width=42)
    voice_combo.grid(row=5, column=1, sticky=tk.W, pady=8)
    if "Zephyr" in voices:
        voice_combo.set("Zephyr")
    else:
        voice_combo.current(0)

    # 5. COM/Serial ports (Auto-Detected)
    port_options, port_device_map, auto_birds_lbl, auto_hexapod_lbl, auto_arm_lbl, auto_tello_lbl, auto_rover_lbl = scan_and_autodetect_esp32_ports()

    # Birds Touchscreen COM Port Selector (Unified ESP32-S3 controlling Left & Right)
    ttk.Label(main_frame, text="Birds ESP32-S3 Touchscreen (esp32_Birds.ino):").grid(row=6, column=0, sticky=tk.W, pady=6)
    birds_port_combo = ttk.Combobox(main_frame, values=port_options, state="readonly", width=42)
    birds_port_combo.grid(row=6, column=1, sticky=tk.W, pady=6)
    birds_port_combo.set(auto_birds_lbl)

    # Hexapod COM / Bluetooth Port Selector (Auto-Detected)
    bt_options, bt_dev_map, auto_bt_lbl = scan_bluetooth_ports()
    hexapod_options = list(port_options)
    for opt in bt_options:
        if opt not in hexapod_options:
            hexapod_options.insert(1, opt)
            port_device_map[opt] = bt_dev_map.get(opt, opt)

    ttk.Label(main_frame, text="Hexapod COM / Bluetooth Port:").grid(row=7, column=0, sticky=tk.W, pady=6)
    hexapod_bt_frame = ttk.Frame(main_frame)
    hexapod_bt_frame.grid(row=7, column=1, sticky=tk.W, pady=6)
    
    hexapod_bt_combo = ttk.Combobox(hexapod_bt_frame, values=hexapod_options, state="normal", width=25)
    hexapod_bt_combo.pack(side=tk.LEFT, padx=(0, 5))
    hexapod_bt_combo.set(auto_hexapod_lbl if (auto_hexapod_lbl in hexapod_options and auto_hexapod_lbl != "None (Simulation Mode)") else auto_bt_lbl)

    def on_scan_bt_hexapod():
        scan_bt_btn.configure(state="disabled", text="Scanning...")
        def run_bt_scan():
            p_opts, p_map, _, a_hex, _, _, _ = scan_and_autodetect_esp32_ports()
            opts, d_map, auto_lbl = scan_bluetooth_ports()
            comb_opts = list(p_opts)
            for o in opts:
                if o not in comb_opts:
                    comb_opts.insert(1, o)
                    p_map[o] = d_map.get(o, o)
            def update_ui():
                scan_bt_btn.configure(state="normal", text="Scan BT Hexapod")
                hexapod_bt_combo['values'] = comb_opts
                nonlocal bt_dev_map, port_device_map
                bt_dev_map = d_map
                port_device_map.update(p_map)
                if a_hex in comb_opts and a_hex != "None (Simulation Mode)":
                    hexapod_bt_combo.set(a_hex)
                elif auto_lbl in comb_opts:
                    hexapod_bt_combo.set(auto_lbl)
                print(f"[Bluetooth/Serial Scan] Found {len(comb_opts)} device option(s).")
            root.after(0, update_ui)
        import threading
        threading.Thread(target=run_bt_scan, daemon=True).start()

    scan_bt_btn = ttk.Button(hexapod_bt_frame, text="Scan BT Hexapod", command=on_scan_bt_hexapod)
    scan_bt_btn.pack(side=tk.LEFT)

    # ESP32 Arm COM Port Selector
    ttk.Label(main_frame, text="ESP32 Arm COM Port (esp32_arm.ino):").grid(row=8, column=0, sticky=tk.W, pady=6)
    arm_port_combo = ttk.Combobox(main_frame, values=port_options, state="readonly", width=42)
    arm_port_combo.grid(row=8, column=1, sticky=tk.W, pady=6)
    arm_port_combo.set(auto_arm_lbl)

    # ESP32 Tello Screen COM Port Selector
    ttk.Label(main_frame, text="Tello Screen COM (esp32_tello.ino):").grid(row=9, column=0, sticky=tk.W, pady=6)
    tello_port_combo = ttk.Combobox(main_frame, values=port_options, state="readonly", width=42)
    tello_port_combo.grid(row=9, column=1, sticky=tk.W, pady=6)
    tello_port_combo.set(auto_tello_lbl)

    # 4WD Rover COM / Bluetooth Port Selector
    ttk.Label(main_frame, text="4WD Rover COM / BT (esp32_waverover.ino):").grid(row=10, column=0, sticky=tk.W, pady=6)
    rover_port_combo = ttk.Combobox(main_frame, values=port_options, state="readonly", width=42)
    rover_port_combo.grid(row=10, column=1, sticky=tk.W, pady=6)
    rover_port_combo.set(auto_rover_lbl)

    # Tello Drone IP Entry
    ttk.Label(main_frame, text="Tello Drone IP:").grid(row=11, column=0, sticky=tk.W, pady=8)
    
    tello_ip_frame = ttk.Frame(main_frame)
    tello_ip_frame.grid(row=11, column=1, sticky=tk.W, pady=8)
    
    tello_ip_combo = ttk.Combobox(tello_ip_frame, width=28, state="normal")
    tello_ip_combo.pack(side=tk.LEFT, padx=(0, 5))
    tello_ip_combo.set("192.168.10.1")
    
    # Tello Drone Port Entry
    ttk.Label(main_frame, text="Tello Drone Port:").grid(row=12, column=0, sticky=tk.W, pady=8)
    tello_port_entry = ttk.Entry(main_frame, width=45)
    tello_port_entry.grid(row=12, column=1, sticky=tk.W, pady=8)
    tello_port_entry.insert(0, "8889")
    
    def on_scan_network():
        scan_btn.configure(state="disabled", text="Scanning...")
        
        tello_port_str = tello_port_entry.get().strip()
        try:
            scan_port = int(tello_port_str)
            if not (1 <= scan_port <= 65535):
                scan_port = 8889
        except ValueError:
            scan_port = 8889
        
        def run_scan():
            import socket
            import subprocess
            import re
            import concurrent.futures
            import time
            
            # Find local subnets
            local_ips = []
            try:
                hostname = socket.gethostname()
                local_ips = socket.gethostbyname_ex(hostname)[2]
            except Exception:
                pass
            
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                s.connect(("8.8.8.8", 80))
                primary_ip = s.getsockname()[0]
                s.close()
                if primary_ip not in local_ips:
                    local_ips.append(primary_ip)
            except Exception:
                pass
                
            subnets = set()
            for ip in local_ips:
                if ip.startswith("127."):
                    continue
                parts = ip.split(".")
                if len(parts) == 4:
                    subnets.add(f"{parts[0]}.{parts[1]}.{parts[2]}.")
            
            # Common subnets to search
            subnets.add("192.168.10.")
            subnets.add("192.168.1.")
            subnets.add("192.168.0.")
            
            target_ips = []
            for subnet in subnets:
                for i in range(1, 255):
                    target_ips.append(f"{subnet}{i}")
                    
            def ping_udp(ip):
                try:
                    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    sock.settimeout(0.1)
                    sock.sendto(b'', (ip, 9))
                    sock.close()
                except Exception:
                    pass

            with concurrent.futures.ThreadPoolExecutor(max_workers=80) as executor:
                executor.map(ping_udp, target_ips)
                
            time.sleep(0.4)
            
            discovered = []
            tello_drones = []
            try:
                output = subprocess.check_output(["arp", "-a"]).decode('utf-8', errors='ignore')
                ip_mac_pattern = re.compile(
                    r"^\s*(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})\s+([0-9a-fA-F:-]{17})\s+(\w+)",
                    re.MULTILINE
                )
                
                def check_if_tello(ip):
                    try:
                        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        sock.settimeout(0.3)
                        sock.sendto(b'command', (ip, scan_port))
                        data, _ = sock.recvfrom(1024)
                        if b'ok' in data.lower():
                            return ip
                    except Exception:
                        pass
                    finally:
                        try:
                            sock.close()
                        except Exception:
                            pass
                    return None

                candidate_ips = []
                for match in ip_mac_pattern.finditer(output):
                    ip, mac, link_type = match.groups()
                    if ip.startswith("224.") or ip.startswith("239.") or ip.endswith(".255") or ip == "255.255.255.255":
                        continue
                    
                    in_subnet = False
                    for subnet in subnets:
                        if ip.startswith(subnet):
                            in_subnet = True
                            break
                    if in_subnet:
                        candidate_ips.append((ip, mac))

                tello_detected = []
                with concurrent.futures.ThreadPoolExecutor(max_workers=30) as checker:
                    check_results = checker.map(check_if_tello, [ip for ip, _ in candidate_ips])
                    for res in check_results:
                        if res:
                            tello_detected.append(res)
                
                for ip, mac in candidate_ips:
                    if ip in tello_detected:
                        discovered.append(f"{ip} ({mac}) [Tello Drone]")
                        tello_drones.append(ip)
                    else:
                        discovered.append(f"{ip} ({mac})")
            except Exception as e:
                print(f"[Network Scan] Error: {e}")
                
            root.after(0, lambda: scan_complete(discovered, tello_drones))
            
        def scan_complete(discovered, tello_drones):
            scan_btn.configure(state="normal", text="Scan Network")
            if discovered:
                print(f"\n[Network Scan] Found {len(discovered)} devices on local network:")
                for d in discovered:
                    print(f"  - {d}")
                tello_ip_combo.configure(values=discovered)
                
                if tello_drones:
                    # Select the first Tello drone automatically
                    matching_opt = [d for d in discovered if tello_drones[0] in d]
                    if matching_opt:
                        tello_ip_combo.set(matching_opt[0])
                        from tkinter import messagebox
                        messagebox.showinfo("Drone IP Found", f"Successfully found Tello drone at {tello_drones[0]}!")
            else:
                from tkinter import messagebox
                messagebox.showwarning("Scan Complete", "No active devices detected on the local network.")
                
        import threading
        threading.Thread(target=run_scan, daemon=True).start()

    scan_btn = ttk.Button(tello_ip_frame, text="Scan Network", command=on_scan_network)
    scan_btn.pack(side=tk.LEFT)


    def on_mode_change(event):
        mode = mode_combo.get()
        if mode == "Camera" and available_cameras:
            camera_combo.configure(state="readonly")
        else:
            camera_combo.configure(state="disabled")

    mode_combo.bind("<<ComboboxSelected>>", on_mode_change)
    # Initialize correct camera dropdown state
    on_mode_change(None)

    # Buttons
    button_frame = ttk.Frame(main_frame, padding=(0, 25, 0, 0))
    button_frame.grid(row=13, column=0, columnspan=2, sticky=tk.E)

    def on_start():
        nonlocal started
        
        # Extract selections
        sel_mic = mic_combo.get()
        if sel_mic:
            match = re.search(r"\(Index (\d+)\)", sel_mic)
            result["mic_idx"] = int(match.group(1)) if match else None
        else:
            result["mic_idx"] = None

        sel_speaker = speaker_combo.get()
        if sel_speaker:
            match = re.search(r"\(Index (\d+)\)", sel_speaker)
            result["speaker_idx"] = int(match.group(1)) if match else None
        else:
            result["speaker_idx"] = None

        mode_str = mode_combo.get()
        if mode_str == "Camera":
            result["video_mode"] = "camera"
        elif mode_str == "Screen Share":
            result["video_mode"] = "screen"
        else:
            result["video_mode"] = "none"

        sel_cam = camera_combo.get()
        if sel_cam and "Camera" in sel_cam:
            try:
                result["camera_idx"] = int(sel_cam.split()[-1])
            except ValueError:
                result["camera_idx"] = 0
        else:
            result["camera_idx"] = 0

        result["voice_name"] = voice_combo.get()

        sel_birds_lbl = birds_port_combo.get()
        birds_dev = port_device_map.get(sel_birds_lbl)
        result["esp32_birds_port"] = birds_dev
        result["esp32_left_port"] = birds_dev
        result["esp32_right_port"] = birds_dev
        result["esp32_port"] = birds_dev

        sel_arm_lbl = arm_port_combo.get()
        result["esp32_arm_port"] = port_device_map.get(sel_arm_lbl)

        sel_tello_lbl = tello_port_combo.get()
        result["esp32_tello_port"] = port_device_map.get(sel_tello_lbl)

        sel_rover_lbl = rover_port_combo.get()
        result["esp32_rover_port"] = port_device_map.get(sel_rover_lbl)

        sel_bt_lbl = hexapod_bt_combo.get()
        dev_hex = port_device_map.get(sel_bt_lbl, bt_dev_map.get(sel_bt_lbl, sel_bt_lbl))
        result["hexapod_bt_port"] = dev_hex
        result["hexapod_port"] = dev_hex
        result["shobots_bt_port"] = dev_hex

        tello_ip = tello_ip_combo.get().strip()
        match = re.match(r"^(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})", tello_ip)
        tello_ip = match.group(1) if match else "192.168.10.1"
        result["tello_ip"] = tello_ip

        tello_port_str = tello_port_entry.get().strip()
        try:
            tello_port = int(tello_port_str)
            if not (1 <= tello_port <= 65535):
                raise ValueError()
        except ValueError:
            from tkinter import messagebox
            messagebox.showerror("Invalid Port", "Tello Port must be an integer between 1 and 65535.")
            return
        result["tello_port"] = tello_port

        started = True
        root.destroy()

    def on_cancel():
        root.destroy()

    start_btn = ttk.Button(button_frame, text="Start Session", command=on_start)
    start_btn.grid(row=0, column=0, padx=5)

    cancel_btn = ttk.Button(button_frame, text="Cancel", command=on_cancel)
    start_btn.grid(row=0, column=0, padx=5)
    cancel_btn.grid(row=0, column=1, padx=5)

    root.protocol("WM_DELETE_WINDOW", on_cancel)

    # Center window
    root.update_idletasks()
    width = root.winfo_width()
    height = root.winfo_height()
    x = (root.winfo_screenwidth() // 2) - (width // 2)
    y = (root.winfo_screenheight() // 2) - (height // 2)
    root.geometry(f'520x600+{x}+{y}')

    # Automatically start network scan at startup
    root.after(100, on_scan_network)

    root.mainloop()

    if started:
        return result
    return None

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--mode",
        type=str,
        default=DEFAULT_MODE,
        help="pixels to stream from",
        choices=["camera", "screen", "none"],
    )
    parser.add_argument(
        "--cli",
        action="store_true",
        help="Force CLI configuration prompts instead of GUI window",
    )
    args = parser.parse_args()
    
    import sys
    
    settings = None
    if not args.cli:
        print("Opening settings window...")
        settings = show_settings_dialog(pya, default_mode=args.mode)
        if settings is None:
            print("Session canceled by user.")
            sys.exit(0)
            
    if settings == "fallback" or args.cli:
        print("Checking available devices via CLI...")
        mic_idx = choose_audio_device(pya, is_input=True)
        speaker_idx = choose_audio_device(pya, is_input=False)
        
        camera_idx = 0
        video_mode = args.mode
        if video_mode == "camera":
            camera_idx = choose_camera()
            if camera_idx is None:
                print("No camera found. Exiting.")
                sys.exit(1)
        voice_name = choose_voice()
        esp32_birds_port = choose_esp32_port()
        esp32_left_port = esp32_birds_port
        esp32_right_port = esp32_birds_port
        esp32_arm_port = None
        esp32_tello_port = None
        esp32_rover_port = choose_rover_bt_port()
        hexapod_port = choose_hexapod_bt_port()
        hexapod_bt_port = hexapod_port
        tello_port = choose_tello_port()
        tello_ip = choose_tello_ip(tello_port)
    else:
        mic_idx = settings["mic_idx"]
        speaker_idx = settings["speaker_idx"]
        video_mode = settings["video_mode"]
        camera_idx = settings["camera_idx"]
        voice_name = settings["voice_name"]
        esp32_birds_port = settings.get("esp32_birds_port", settings.get("esp32_left_port"))
        esp32_left_port = esp32_birds_port
        esp32_right_port = esp32_birds_port
        esp32_arm_port = settings.get("esp32_arm_port")
        esp32_tello_port = settings.get("esp32_tello_port")
        esp32_rover_port = settings.get("esp32_rover_port")
        hexapod_port = settings.get("hexapod_port", settings.get("hexapod_bt_port", settings.get("shobots_bt_port", "hexapod")))
        hexapod_bt_port = hexapod_port
        tello_ip = settings.get("tello_ip", "192.168.10.1")
        tello_port = settings.get("tello_port", 8889)

    print("\nConnecting to Gemini...")
    main = AudioLoop(
        video_mode=video_mode,
        camera_idx=camera_idx,
        mic_idx=mic_idx,
        speaker_idx=speaker_idx,
        voice_name=voice_name,
        esp32_birds_port=esp32_birds_port,
        esp32_left_port=esp32_left_port,
        esp32_right_port=esp32_right_port,
        esp32_arm_port=esp32_arm_port,
        esp32_tello_port=esp32_tello_port,
        esp32_rover_port=esp32_rover_port,
        hexapod_port=hexapod_port,
        hexapod_bt_port=hexapod_bt_port,
        tello_ip=tello_ip,
        tello_port=tello_port
    )
    asyncio.run(main.run())
