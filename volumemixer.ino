// ── Volume Mixer Controller — ESP32 Firmware ────────────────────────────────
// Libraries needed (install via Arduino Library Manager):
//   - Adafruit SSD1306
//   - Adafruit GFX Library
//   - ESP32Encoder
//
// Board: ESP32 Dev Module
// Upload speed: 115200

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Encoder.h>

// ── Pin definitions ──────────────────────────────────────────────────────────
#define ENC_CLK  34
#define ENC_DT   35
#define ENC_SW   32

// Green LEDs — one per pin, light up 1 by 1 from 10% to 60%
#define LED_G1   25
#define LED_G2   26
#define LED_G3   27
#define LED_G4   14
#define LED_G5   12
#define LED_G6   13

// Yellow LEDs — 70% and 80%
#define LED_Y1   4
#define LED_Y2   5

// Red LEDs — 90% and 100%
#define LED_R1   18
#define LED_R2   19

const int ALL_LEDS[] = { LED_G1, LED_G2, LED_G3, LED_G4, LED_G5, LED_G6,
                          LED_Y1, LED_Y2, LED_R1, LED_R2 };

// ── OLED 128×64 I2C ──────────────────────────────────────────────────────────
Adafruit_SSD1306 display(128, 64, &Wire, -1);
ESP32Encoder encoder;

// ── State ────────────────────────────────────────────────────────────────────
enum Mode { MENU, ADJUST };
Mode mode = MENU;

String apps[8];
int    volumes[8];
bool   muted[8];
int    appCount   = 0;
int    selectedApp = 0;

// ── Encoder state ─────────────────────────────────────────────────────────────
long lastEncVal = 0;

// ── Inactivity timeout ────────────────────────────────────────────────────────
unsigned long lastActivityTime = 0;
const unsigned long INACTIVITY_MS = 60000;  // 1 minute — change to taste

// ── Button state (with debounce) ─────────────────────────────────────────────
bool          btnDown        = false;
bool          btnHandled     = false;   // prevents double-fire on long press
unsigned long btnPressTime   = 0;
unsigned long btnReleaseTime = 0;
const int     LONG_PRESS_MS  = 600;
const int     DEBOUNCE_MS    = 50;     
// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true);  
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  encoder.attachHalfQuad(ENC_CLK, ENC_DT);
  encoder.setCount(0);

  pinMode(ENC_SW, INPUT_PULLUP);
  for (int i = 0; i < 10; i++) {
    pinMode(ALL_LEDS[i], OUTPUT);
    digitalWrite(ALL_LEDS[i], LOW);
  }

  showWelcome();
  Serial.println("READY");  

// ── Main loop ────────────────────────────────────────────────────────────────
void loop() {
  handleSerial();
  handleEncoder();
  handleButton();


  if (mode == ADJUST && (millis() - lastActivityTime >= INACTIVITY_MS)) {
    mode = MENU;
  }

  updateLEDs();
  updateDisplay();
  delay(20);
}


void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();

  if (line.startsWith("APPS:")) {
    parseApps(line.substring(5));
  } else if (line.startsWith("VOL:")) {
    int c1  = line.indexOf(':', 4);
    int idx = line.substring(4, c1).toInt();
    int vol = line.substring(c1 + 1).toInt();
    if (idx < appCount) volumes[idx] = vol;
  } else if (line.startsWith("MUTE:")) {
    int c1  = line.indexOf(':', 5);
    int idx = line.substring(5, c1).toInt();
    if (idx < appCount) muted[idx] = (line.substring(c1 + 1).toInt() == 1);
  }
}

void parseApps(String csv) {
  int newCount = 0;
  String newApps[8];
  int start = 0;
  for (int i = 0; i <= (int)csv.length() && newCount < 8; i++) {
    if (i == (int)csv.length() || csv[i] == ',') {
      newApps[newCount] = csv.substring(start, i);
      newCount++;
      start = i + 1;
    }
  }


  if (appCount == 0) {
    for (int i = 0; i < newCount; i++) {
      apps[i]    = newApps[i];
      volumes[i] = 50;
      muted[i]   = false;
    }
    appCount    = newCount;
    selectedApp = 0;
    mode        = MENU;
    return;
  }

  for (int i = 0; i < newCount; i++) {
    apps[i] = newApps[i];
    if (i >= appCount) {   
      volumes[i] = 50;
      muted[i]   = false;
    }
  }
  appCount = newCount;

  if (selectedApp >= appCount) selectedApp = appCount - 1;
}

// ── Encoder: scroll menu or adjust volume ────────────────────────────────────
// Turn RIGHT = increase volume 
void handleEncoder() {
  long val   = encoder.getCount() / 2;
  long delta = val - lastEncVal;
  if (delta == 0) return;
  lastEncVal = val;

  if (mode == MENU) {
    // scroll through apps 
    selectedApp = (selectedApp - delta + appCount) % appCount;
  } else {
    // turn right  = volume up
    int v = volumes[selectedApp] - delta * 5;
    volumes[selectedApp] = constrain(v, 0, 100);
    Serial.print("SET_VOL:");
    Serial.print(selectedApp);
    Serial.print(":");
    Serial.println(volumes[selectedApp]);
  }
  lastActivityTime = millis();  // reset inactivity timer on any rotation
}

// ── Button: short click = select/mute, long hold = back to menu ──────────────
void handleButton() {
  bool reading = (digitalRead(ENC_SW) == LOW);


  if (reading && !btnDown) {
    // ignore if released less than DEBOUNCE_MS ago (mechanical bounce)
    if (millis() - btnReleaseTime < DEBOUNCE_MS) return;
    btnDown          = true;
    btnHandled       = false;
    btnPressTime     = millis();
    lastActivityTime = millis();  // reset inactivity timer on any button press
  }

  // ── held: fire long-press once without waiting for release ─────────────────
  if (btnDown && !btnHandled) {
    if (millis() - btnPressTime >= LONG_PRESS_MS) {
      btnHandled = true;   // mark so release doesn't also fire a short-click
      mode = MENU;         // long press → back to menu
    }
  }

  // ── release edge ───────────────────────────────────────────────────────────
  if (!reading && btnDown) {
    btnDown        = false;
    btnReleaseTime = millis();
    if (btnHandled) return;  

    // short click
    if (mode == MENU) {
      mode = ADJUST;  // enter volume adjust for selected app
    } else {
      // toggle mute
      muted[selectedApp] = !muted[selectedApp];
      Serial.print("TOGGLE_MUTE:");
      Serial.println(selectedApp);
    }
  }
}

// ── LEDs: VU meter ────────────────────────────────────────────────────────────
// 10% → G1,  20% → G1+G2,  30% → G1+G2+G3  ... 60% → all 6 green
// 70% → Y1,  80% → Y1+Y2
// 90% → R1, 100% → R1+R2
// Muted → all off except red blink
void updateLEDs() {
  int vol = (appCount > 0) ? volumes[selectedApp] : 0;

  // muted — blink both red, everything else off
  if (appCount > 0 && muted[selectedApp]) {
    bool blink = (millis() / 400) % 2;
    for (int i = 0; i < 8; i++) digitalWrite(ALL_LEDS[i], LOW);
    digitalWrite(LED_R1, blink);
    digitalWrite(LED_R2, blink);
    return;
  }

  // green: each LED covers a 10% band from 10% to 60%
  digitalWrite(LED_G1, vol >= 10  ? HIGH : LOW);
  digitalWrite(LED_G2, vol >= 20  ? HIGH : LOW);
  digitalWrite(LED_G3, vol >= 30  ? HIGH : LOW);
  digitalWrite(LED_G4, vol >= 40  ? HIGH : LOW);
  digitalWrite(LED_G5, vol >= 50  ? HIGH : LOW);
  digitalWrite(LED_G6, vol >= 60  ? HIGH : LOW);

  // yellow: 70% and 80%
  digitalWrite(LED_Y1, vol >= 70  ? HIGH : LOW);
  digitalWrite(LED_Y2, vol >= 80  ? HIGH : LOW);

  // red: 90% and 100%
  digitalWrite(LED_R1, vol >= 90  ? HIGH : LOW);
  digitalWrite(LED_R2, vol >= 100 ? HIGH : LOW);
}

// ── OLED display ──────────────────────────────────────────────────────────────
void updateDisplay() {
  display.clearDisplay();

  if (appCount == 0) {
    display.setTextSize(1);
    display.setCursor(8, 24);
    display.print("Waiting for PC...");
    display.display();
    return;
  }

  // Line 1: mode indicator
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(mode == MENU ? "[SELECT APP]" : "[ADJUST VOL]");

  // Line 2: app name (large)
  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print(apps[selectedApp].substring(0, 9));

  // Mute badge (top right)
  if (muted[selectedApp]) {
    display.setTextSize(1);
    display.setCursor(100, 14);
    display.print("MUTE");
  }

  // Line 3: numeric volume + app counter
  int vol = volumes[selectedApp];
  display.setTextSize(1);
  display.setCursor(0, 36);
  display.print("Vol: ");
  display.print(vol);
  display.print("%");

  display.setCursor(98, 36);
  display.print(String(selectedApp + 1) + "/" + String(appCount));

  // Volume bar
  display.drawRect(0, 48, 128, 12, SSD1306_WHITE);
  int fillW = (vol * 124) / 100;
  display.fillRect(2, 50, fillW, 8, SSD1306_WHITE);

  display.display();
}

// ── Welcome screen ────────────────────────────────────────────────────────────
void showWelcome() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 18);
  display.setTextColor(SSD1306_WHITE);
  display.print("Vol Mixer");
  display.setTextSize(1);
  display.setCursor(28, 46);
  display.print("Connecting...");
  display.display();
  delay(1200);
}
