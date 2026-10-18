#!/usr/bin/env python3
import http.server
import socketserver
import json
import os
import sys
import subprocess
import socket
import time
import urllib.parse
import signal
import threading

PORT = int(os.environ.get("PORT", 7090))
HOST = os.environ.get("HOST", os.environ.get("IP", "127.0.0.1"))
DIRECTORY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")
os.makedirs(DIRECTORY, exist_ok=True)

CONFIG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")

DEFAULT_PROJECTS = {
    "hexapod": {
        "name": "Hexapod Robot",
        "dir": "/home/markirgang/hexapod-app",
        "script": "server.py",
        "host": "127.0.0.1",
        "port": 8090,
        "icon": "🕷️",
        "description": "6-Legged Kinematic Hexapod with Gemini AI & Servo Calibration"
    },
    "rover": {
        "name": "4WD Rover",
        "dir": "/home/markirgang/Rover",
        "script": "server.py",
        "host": "127.0.0.1",
        "port": 9090,
        "icon": "🏎️",
        "description": "4WD Rover with Robotic Head, MP3 Player & Beat Dance Sync"
    },
    "arm": {
        "name": "Robotic Arm",
        "dir": "/home/markirgang/Arm",
        "script": "server.py",
        "host": "127.0.0.1",
        "port": 1090,
        "icon": "🦾",
        "description": "6-DOF Servo Robotic Arm Workbench & Kinematics Control"
    },
    "birds": {
        "name": "Animatronic Birds",
        "dir": "/home/markirgang/Birds",
        "script": "server.py",
        "host": "127.0.0.1",
        "port": 1290,
        "icon": "🦜",
        "description": "Interactive Animatronic Bird Show & Audio Performance Engine"
    },
    "menorahfish": {
        "name": "Menorah Fish",
        "dir": "/home/markirgang/MenorahFish",
        "script": "server.py",
        "host": "127.0.0.1",
        "port": 1390,
        "icon": "🐟",
        "description": "Musical Menorah Fish Animatronic with WebGL Kinematics"
    },
    "drone": {
        "name": "Pixhawk 4 Drone",
        "dir": "/home/markirgang/Drone",
        "script": "server.py",
        "host": "127.0.0.1",
        "port": 1190,
        "icon": "🚁",
        "description": "Pixhawk 4 Drone Web Server & Multimodal MAVLink Control Hub"
    },
    "tello": {
        "name": "Tello Drone",
        "dir": "/home/markirgang/Tello",
        "script": "server.py",
        "host": "127.0.0.1",
        "port": 1490,
        "icon": "🛸",
        "description": "Tello Drone & Arm Vision Control Workbench"
    }
}

def load_config():
    projects = json.loads(json.dumps(DEFAULT_PROJECTS))
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                saved = json.load(f)
                if isinstance(saved, dict):
                    for k, v in saved.items():
                        if k in projects and isinstance(v, dict):
                            projects[k].update(v)
                        elif isinstance(v, dict) and "name" in v and "dir" in v:
                            projects[k] = v
        except Exception as e:
            print(f"Error reading {CONFIG_FILE}: {e}")
    return projects

def save_config():
    try:
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(PROJECTS, f, indent=4)
        return True, "Configuration saved successfully"
    except Exception as e:
        return False, f"Failed to save configuration: {str(e)}"

PROJECTS = load_config()
running_processes = {}

def is_port_open(port, host="127.0.0.1", timeout=0.8):
    try:
        check_host = "127.0.0.1" if str(host) in ["0.0.0.0", "", "*"] else host
        with socket.create_connection((check_host, int(port)), timeout=timeout):
            return True
    except Exception:
        return False

def kill_process_on_port(port):
    try:
        output = subprocess.check_output(["lsof", "-t", f"-i:{port}"], text=True)
        pids = [int(p) for p in output.strip().split("\n") if p.isdigit()]
        for pid in pids:
            try:
                os.kill(pid, signal.SIGTERM)
                time.sleep(0.2)
            except Exception:
                pass
    except Exception:
        pass

def start_robot_server(key):
    if key not in PROJECTS:
        return False, "Unknown robot key"
    
    proj = PROJECTS[key]
    port = int(proj["port"])
    host = proj.get("host", "127.0.0.1")
    proj_dir = proj["dir"]
    script_name = proj.get("script", "server.py")
    script_file = os.path.join(proj_dir, script_name)

    if is_port_open(port, host=host):
        return True, f"{proj['name']} is already online on {host}:{port}"

    if not os.path.isfile(script_file):
        return False, f"Script not found at {script_file}"

    env = dict(os.environ)
    env["PORT"] = str(port)
    env["HOST"] = host
    env["IP"] = host

    try:
        proc = subprocess.Popen(
            [sys.executable, script_name, "--port", str(port), "--host", host],
            cwd=proj_dir,
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            preexec_fn=os.setsid
        )
        running_processes[key] = proc
        
        # Poll up to 3 seconds to confirm startup
        for _ in range(15):
            time.sleep(0.2)
            if is_port_open(port, host=host):
                return True, f"{proj['name']} started successfully on {host}:{port}"

        return True, f"{proj['name']} process spawned (port check pending)"
    except Exception as e:
        return False, f"Failed to start {proj['name']}: {str(e)}"

def stop_robot_server(key):
    if key not in PROJECTS:
        return False, "Unknown robot key"

    proj = PROJECTS[key]
    port = int(proj["port"])
    host = proj.get("host", "127.0.0.1")

    if key in running_processes:
        proc = running_processes[key]
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except Exception:
            pass
        del running_processes[key]

    kill_process_on_port(port)
    time.sleep(0.5)

    if not is_port_open(port, host=host):
        return True, f"{proj['name']} stopped successfully"
    else:
        kill_process_on_port(port)
        return True, f"{proj['name']} stop signal sent"

def start_all_servers():
    results = {}
    threads = []

    def _start(key):
        success, msg = start_robot_server(key)
        results[key] = {"success": success, "message": msg}

    for key in PROJECTS:
        t = threading.Thread(target=_start, args=(key,))
        t.start()
        threads.append(t)

    for t in threads:
        t.join(timeout=4.0)

    return results

def stop_all_servers():
    results = {}
    threads = []

    def _stop(key):
        success, msg = stop_robot_server(key)
        results[key] = {"success": success, "message": msg}

    for key in PROJECTS:
        t = threading.Thread(target=_stop, args=(key,))
        t.start()
        threads.append(t)

    for t in threads:
        t.join(timeout=3.0)

    return results

def get_all_status():
    status = {}
    for key, proj in PROJECTS.items():
        host = proj.get("host", "127.0.0.1")
        port = int(proj["port"])
        online = is_port_open(port, host=host)
        
        display_host = "localhost" if str(host) in ["127.0.0.1", "0.0.0.0", "", "localhost"] else host
        url = f"http://{display_host}:{port}"

        def_p = DEFAULT_PROJECTS.get(key, {})

        status[key] = {
            "name": proj["name"],
            "icon": proj["icon"],
            "dir": proj["dir"],
            "host": host,
            "port": port,
            "description": proj["description"],
            "online": online,
            "url": url,
            "default_dir": def_p.get("dir", proj["dir"]),
            "default_host": def_p.get("host", "127.0.0.1"),
            "default_port": def_p.get("port", port)
        }
    return status

class ShobotsRequestHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def log_message(self, format, *args):
        pass

    def send_json(self, data, status=200):
        body = json.dumps(data).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/api/status":
            self.send_json(get_all_status())
        elif parsed.path == "/api/config":
            self.send_json({"projects": PROJECTS, "defaults": DEFAULT_PROJECTS})
        else:
            super().do_GET()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        content_length = int(self.headers.get('Content-Length', 0))
        post_data = self.rfile.read(content_length) if content_length > 0 else b'{}'
        
        try:
            data = json.loads(post_data.decode('utf-8'))
        except Exception:
            data = {}

        robot = data.get("robot", data.get("key", "all"))

        if parsed.path == "/api/start":
            if robot == "all":
                results = start_all_servers()
                self.send_json({"success": True, "results": results})
            else:
                success, msg = start_robot_server(robot)
                self.send_json({"success": success, "message": msg})

        elif parsed.path == "/api/stop":
            if robot == "all":
                results = stop_all_servers()
                self.send_json({"success": True, "results": results})
            else:
                success, msg = stop_robot_server(robot)
                self.send_json({"success": success, "message": msg})

        elif parsed.path == "/api/restart":
            if robot == "all":
                stop_all_servers()
                time.sleep(1)
                results = start_all_servers()
                self.send_json({"success": True, "results": results})
            else:
                stop_robot_server(robot)
                time.sleep(0.5)
                success, msg = start_robot_server(robot)
                self.send_json({"success": success, "message": msg})

        elif parsed.path == "/api/config":
            key = data.get("key")
            new_dir = data.get("dir")
            new_host = data.get("host")
            new_port = data.get("port")
            restart = data.get("restart", False)

            if key and key in PROJECTS:
                old_port = PROJECTS[key]["port"]
                old_host = PROJECTS[key].get("host", "127.0.0.1")
                
                needs_restart = restart
                if (new_port and int(new_port) != int(old_port)) or (new_host and new_host != old_host):
                    needs_restart = True

                if is_port_open(old_port, old_host):
                    stop_robot_server(key)

                if new_dir:
                    PROJECTS[key]["dir"] = str(new_dir).strip()
                if new_host:
                    PROJECTS[key]["host"] = str(new_host).strip()
                if new_port:
                    PROJECTS[key]["port"] = int(new_port)

                save_success, save_msg = save_config()

                start_msg = ""
                if needs_restart or restart:
                    succ, start_msg = start_robot_server(key)

                self.send_json({
                    "success": save_success,
                    "message": f"{save_msg}. {start_msg}".strip(),
                    "status": get_all_status()
                })
            elif "projects" in data:
                for k, cfg in data["projects"].items():
                    if k in PROJECTS and isinstance(cfg, dict):
                        if is_port_open(PROJECTS[k]["port"], PROJECTS[k].get("host", "127.0.0.1")):
                            stop_robot_server(k)
                        if "dir" in cfg: PROJECTS[k]["dir"] = str(cfg["dir"]).strip()
                        if "host" in cfg: PROJECTS[k]["host"] = str(cfg["host"]).strip()
                        if "port" in cfg: PROJECTS[k]["port"] = int(cfg["port"])

                save_success, save_msg = save_config()
                if restart:
                    start_all_servers()

                self.send_json({
                    "success": save_success,
                    "message": save_msg,
                    "status": get_all_status()
                })
            else:
                self.send_json({"error": "Missing project key or projects payload"}, status=400)

        elif parsed.path == "/api/config/reset":
            key = data.get("key", "all")
            restart = data.get("restart", False)

            if key == "all":
                stop_all_servers()
                for k in DEFAULT_PROJECTS:
                    PROJECTS[k] = json.loads(json.dumps(DEFAULT_PROJECTS[k]))
                save_config()
                if restart:
                    start_all_servers()
                self.send_json({"success": True, "message": "All project configurations reset to default.", "status": get_all_status()})
            elif key in PROJECTS:
                stop_robot_server(key)
                if key in DEFAULT_PROJECTS:
                    PROJECTS[key] = json.loads(json.dumps(DEFAULT_PROJECTS[key]))
                save_config()
                if restart:
                    start_robot_server(key)
                self.send_json({"success": True, "message": f"{PROJECTS[key]['name']} reset to default combination.", "status": get_all_status()})
            else:
                self.send_json({"error": f"Unknown robot key '{key}'"}, status=400)

        else:
            self.send_json({"error": "Unknown endpoint"}, status=404)

class ThreadingReusableTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True

if __name__ == "__main__":
    for i in range(1, len(sys.argv)):
        if sys.argv[i] == "--port" and i + 1 < len(sys.argv):
            PORT = int(sys.argv[i + 1])
        elif sys.argv[i] == "--host" and i + 1 < len(sys.argv):
            HOST = sys.argv[i + 1]

    display_host = "localhost" if HOST in ["", "0.0.0.0", "127.0.0.1"] else HOST
    print(f"Starting SHOBOTS Master Control Center on http://{display_host}:{PORT}")
    print("Auto-starting all configured robot servers...")
    start_all_servers()
    
    bind_host = "" if HOST == "0.0.0.0" else HOST
    with ThreadingReusableTCPServer((bind_host, PORT), ShobotsRequestHandler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down SHOBOTS Master Control Center.")
