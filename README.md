#  ESP32 Volume Mixer Controller

A physical volume mixer controller built with an ESP32, rotary encoder, OLED display, and LED VU meter. Scroll through your open audio apps, adjust their volume individually, mute them, and watch the LEDs react in real time — all from a small device sitting on your desk.

---

##  Features

- **Per-app volume control** — independently control Spotify, Chrome, Discord, and any other app playing audio on Windows
- **OLED display** — shows the current app name, volume percentage, a visual bar, and mode indicator
- **LED VU meter** — 10 LEDs light up progressively as volume increases
  - 🟢 Green × 6 → 10% to 60% (one LED per 10%)
  - 🟡 Yellow × 2 → 70% and 80%
  - 🔴 Red × 2 → 90% and 100%
  - Red LEDs blink when the app is muted
- **Rotary encoder navigation** — turn to scroll apps or adjust volume, click to select/mute, long hold to go back
- **Auto-return to menu** — returns to app selection after 1 minute of inactivity
- **Live refresh** — new audio apps appear automatically every 10 seconds without restarting anything

---

##  Hardware

| Component | Description |
|---|---|
| ESP32-WROOM-32D | Main microcontroller, connects to PC via Micro-USB |
| KY-040 EC11 Rotary Encoder | Navigation and volume control input |
| SSD1306 0.96" 128×64 OLED | I2C display (address 0x3C) |
|  Jumper wires| Connections between components |
| Green LEDs 5mm (×6) | VU meter low-mid range |
| Yellow LEDs 5mm (×2) | VU meter mid-high range |
| Red LEDs 5mm (×2) | VU meter high range + mute indicator |
| 220Ω resistors (×10) | One per LED, current limiting |

---

##  Wiring

### Rotary Encoder (KY-040)

| Encoder Pin | ESP32 GPIO |
|---|---|
| CLK | 34 |
| DT | 35 |
| SW | 32 |
| + | 3.3V |
| GND | GND |

### OLED Display (I2C)

| OLED Pin | ESP32 GPIO |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | 21 |
| SCL | 22 |

### LEDs

Each LED is wired: **GPIO pin → 220Ω resistor → LED anode (+, long leg) → LED cathode (−, short leg) → GND rail**

| LED | Color | GPIO Pin | Lights at |
|---|---|---|---|
| G1 | Green | 25 | ≥ 10% |
| G2 | Green | 26 | ≥ 20% |
| G3 | Green | 27 | ≥ 30% |
| G4 | Green | 14 | ≥ 40% |
| G5 | Green | 12 | ≥ 50% |
| G6 | Green | 13 | ≥ 60% |
| Y1 | Yellow | 4 | ≥ 70% |
| Y2 | Yellow | 5 | ≥ 80% |
| R1 | Red | 18 | ≥ 90% |
| R2 | Red | 19 | ≥ 100% |

All LED cathodes connect to the GND rail. One wire from the GND rail to any ESP32 GND pin covers all 10 LEDs.

---

##  Project Structure

```
esp32-volume-mixer/
├── volume_mixer.ino   # ESP32 firmware (Arduino)
├── mixer.py           # PC bridge script (Python)
└── README.md
```

---

##  Setup

### 1. Flash the ESP32

**Install Arduino IDE** and add ESP32 board support:
- Go to `File → Preferences → Additional Board Manager URLs`
- Add: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- Go to `Tools → Board → Board Manager`, search `esp32`, install

**Install libraries** via `Sketch → Include Library → Manage Libraries`:
- `Adafruit SSD1306`
- `Adafruit GFX Library`
- `ESP32Encoder`

**Flash:**
- Open `volume_mixer.ino` in Arduino IDE
- Set board to `ESP32 Dev Module` under `Tools → Board`
- Select the correct COM port under `Tools → Port`
- Click Upload

### 2. Run the Python bridge

**Install dependencies:**
```bash
pip install pyserial pycaw comtypes psutil
```

**Run:**
```bash
# Windows
python mixer.py --port COM3

# Linux
python mixer.py --port /dev/ttyUSB0
```

Replace `COM3` with your actual port. Check Device Manager on Windows or run `ls /dev/tty*` on Linux to find it.

The script will print the detected audio apps in the terminal when it connects successfully.

---

##  How to Use

| Action | What it does |
|---|---|
| **Rotate** (in menu) | Scroll through audio apps |
| **Short click** (in menu) | Select app and enter volume adjust mode |
| **Rotate** (in adjust) | Turn right = volume up, turn left = volume down (5% per click) |
| **Short click** (in adjust) | Toggle mute on/off |
| **Long hold** (anywhere) | Go back to app selection menu |
| **No input for 1 minute** | Automatically returns to menu |

---

##  Serial Protocol

The ESP32 and Python script communicate over USB serial at **115200 baud** using plain text messages:

| Direction | Message | Meaning |
|---|---|---|
| ESP32 → PC | `READY` | Device booted, ready to receive app list |
| PC → ESP32 | `APPS:Spotify,Chrome,Discord` | Send app name list |
| PC → ESP32 | `VOL:0:75` | App at index 0 is at 75% volume |
| PC → ESP32 | `MUTE:0:1` | App at index 0 is muted |
| ESP32 → PC | `SET_VOL:0:80` | User set app 0 to 80%, apply it |
| ESP32 → PC | `TOGGLE_MUTE:0` | User clicked mute on app 0 |

---

##  Configuration

You can change these constants at the top of `volume_mixer.ino`:

```cpp
const int     LONG_PRESS_MS  = 600;    // ms to hold for long press (back to menu)
const int     DEBOUNCE_MS    = 50;     // ms debounce window (increase if twitchy)
const unsigned long INACTIVITY_MS = 60000;  // ms before auto-returning to menu (60s)
```

And in `mixer.py`:

```python
time.sleep(10)  # how often to refresh the app list (seconds)
```

---

##  Troubleshooting

**OLED not showing anything**
- Check SDA/SCL wiring (GPIO 21/22)
- Make sure you're powering the OLED from 3.3V not 5V
- The code halts silently if the OLED isn't found — open Arduino Serial Monitor to check for output

**Encoder direction reversed**
- Swap the CLK and DT wires, or negate the delta in `handleEncoder()`

**Button twitching / jumping to menu unexpectedly**
- Increase `DEBOUNCE_MS` from 50 to 80 in the firmware

**No apps showing on device**
- Make sure `mixer.py` is running
- Open an app that plays audio (Spotify, YouTube in browser, etc.) — apps with no active audio session don't appear
- Check the terminal output of `mixer.py` to see which apps were detected

**Wrong COM port**
- Windows: check Device Manager → Ports (COM & LPT) while the ESP32 is plugged in
- Linux: run `ls /dev/ttyUSB*` or `ls /dev/ttyACM*`

---

##  Requirements

- **OS:** Windows 10/11 (for `pycaw` audio control) or Linux with PulseAudio/PipeWire
- **Python:** 3.7+
- **Arduino IDE:** 2.x recommended

---

## 📄 License

MIT License — do whatever you want with it.

---

*Built with an ESP32, some jumper wires, and too much free time.*
