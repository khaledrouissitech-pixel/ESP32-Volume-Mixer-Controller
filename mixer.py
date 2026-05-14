"""
Volume Mixer Controller — PC Bridge Script
==========================================
Windows : uses pycaw for per-app audio control
Linux   : uses pactl (PulseAudio / PipeWire)

Install dependencies:
    pip install pyserial pycaw comtypes psutil

Run:
    python mixer.py --port COM4          # Windows
    python mixer.py --port /dev/ttyUSB0  # Linux
"""

import serial
import time
import sys
import argparse
import threading
import platform
import subprocess

# ── Windows audio via pycaw ───────────────────────────────────────────────────
IS_WINDOWS = platform.system() == "Windows"
if IS_WINDOWS:
    from pycaw.pycaw import AudioUtilities, ISimpleAudioVolume
    from comtypes import CLSCTX_ALL

MAX_APPS = 8

# ── Audio helpers ─────────────────────────────────────────────────────────────


def get_audio_sessions():
    """Return list of (display_name, session) for active audio apps."""
    if not IS_WINDOWS:
        return get_pulse_apps()

    sessions = AudioUtilities.GetAllSessions()
    apps = []
    seen = set()
    for s in sessions:
        if s.Process and s.Process.name() not in seen:
            name = s.Process.name().replace(".exe", "")[:12]
            apps.append((name, s))
            seen.add(s.Process.name())
            if len(apps) >= MAX_APPS:
                break
    return apps


def get_app_volume(session):
    """Get current volume 0–100 for a session."""
    if not IS_WINDOWS:
        return 50
    try:
        vol = session._ctl.QueryInterface(ISimpleAudioVolume)
        return int(vol.GetMasterVolume() * 100)
    except Exception:
        return 50


def set_app_volume(session, percent):
    """Set volume 0–100 for a session."""
    if not IS_WINDOWS:
        return
    try:
        vol = session._ctl.QueryInterface(ISimpleAudioVolume)
        vol.SetMasterVolume(percent / 100.0, None)
    except Exception:
        pass


def toggle_mute(session):
    """Toggle mute state for a session."""
    if not IS_WINDOWS:
        return
    try:
        vol = session._ctl.QueryInterface(ISimpleAudioVolume)
        vol.SetMute(not vol.GetMute(), None)
    except Exception:
        pass


# ── Linux PulseAudio / PipeWire fallback ─────────────────────────────────────
def get_pulse_apps():
    try:
        out = subprocess.check_output(
            ["pactl", "list", "sink-inputs"], text=True)
        apps = []
        for line in out.splitlines():
            if "application.name" in line:
                name = line.split("=")[-1].strip().strip('"')[:12]
                apps.append((name, name))
                if len(apps) >= MAX_APPS:
                    break
        return apps if apps else [("No apps", None)]
    except Exception:
        return [("No apps", None)]


# ── Serial bridge ─────────────────────────────────────────────────────────────
class MixerBridge:
    def __init__(self, port, baud=115200):
        print(f"Connecting to ESP32 on {port}...")
        self.ser = serial.Serial(port, baud, timeout=1)
        self.apps = []
        self.running = True
        time.sleep(2)  # wait for ESP32 to boot and send READY

    def send_app_list(self):
        """Push current audio app list + volumes to ESP32."""
        self.apps = get_audio_sessions()
        if not self.apps:
            print("  No audio apps found.")
            return

        names = ",".join(name for name, _ in self.apps)
        self._write(f"APPS:{names}\n")

        for i, (name, session) in enumerate(self.apps):
            v = get_app_volume(session)
            self._write(f"VOL:{i}:{v}\n")

        print(f"  Sent {len(self.apps)} apps: {[n for n, _ in self.apps]}")

    def handle_command(self, line):
        """Handle a command string received from ESP32."""
        line = line.strip()
        if not line:
            return

        if line == "READY":
            print("ESP32 ready — sending app list.")
            self.send_app_list()

        elif line.startswith("SET_VOL:"):
            # format: SET_VOL:index:volume
            parts = line[8:].split(":")
            if len(parts) == 2:
                idx, vol = int(parts[0]), int(parts[1])
                if idx < len(self.apps):
                    set_app_volume(self.apps[idx][1], vol)
                    print(f"  {self.apps[idx][0]:12s} → {vol:3d}%")

        elif line.startswith("TOGGLE_MUTE:"):
            # format: TOGGLE_MUTE:index
            idx = int(line[12:])
            if idx < len(self.apps):
                toggle_mute(self.apps[idx][1])
                print(f"  Mute toggled: {self.apps[idx][0]}")

        else:
            # print anything else (useful for debugging ESP32 serial output)
            print(f"  ESP32: {line}")

    def _write(self, text):
        """Safe serial write."""
        try:
            self.ser.write(text.encode())
        except serial.SerialException as e:
            print(f"Write error: {e}")

    def refresh_loop(self):
        """Background thread — re-send app list every 10 s so new apps appear."""
        while self.running:
            time.sleep(10)
            try:
                self.send_app_list()
            except Exception:
                pass

    def run(self):
        t = threading.Thread(target=self.refresh_loop, daemon=True)
        t.start()
        print("Volume Mixer Bridge running. Ctrl+C to stop.\n")

        while self.running:
            try:
                raw = self.ser.readline()
                if raw:
                    line = raw.decode("utf-8", errors="ignore")
                    self.handle_command(line)
            except KeyboardInterrupt:
                print("\nStopping...")
                self.running = False
            except serial.SerialException as e:
                print(f"Serial error: {e} — retrying in 2s")
                time.sleep(2)


# ── Entry point ─s──────────────────────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Volume Mixer Bridge")
    parser.add_argument(
        "--port",
        default="COM4",
        help="Serial port, e.g. COM4 (Windows) or /dev/ttyUSB0 (Linux)"
    )
    args = parser.parse_args()

    try:
        bridge = MixerBridge(args.port)
        bridge.run()
    except serial.SerialException as e:
        print(f"Could not open port {args.port}: {e}")
        print("Check the port name and make sure no other program is using it.")
        sys.exit(1)
