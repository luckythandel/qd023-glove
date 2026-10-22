/*
  QD023 Glove -> PC controller  (v2.1, two-pose calibration)
  ----------------------------------------------------------
  Pin map is FIXED and verified on this unit (docs/PINMAP.md):
    Thumb=G36 Index=G39 Middle=G34 Ring=G35 Pinky=G32  (all ADC1)
  G33 is unconnected/floating (~604, twitches with noise) - not a sensor.
  v2.0 tried to LEARN pins at calibration by scanning G32..G39; floating
  pins could win the "biggest mover" contest and bind a finger to noise,
  which left some fingers dead or erratic. v2.1 trusts the measured map.

  Calibration is TWO POSES, not five per-finger steps:
    1. hand flat  -> OPEN baseline (median of 40 samples)
    2. full fist  -> FIST baseline (median of 40 samples)
  Median rejects single-sample spikes that used to poison thresholds
  (one bad capture gave thumb thr=794 while the real max is ~816, so the
  thumb never registered as bent and the GUI never curled it). Threshold
  sits at 32% of the measured travel from OPEN - a light bend already
  registers, like the vendor tutorial's fixed ">200" rule but scaled to
  YOUR fist. A finger whose fist-open travel is <80 counts is marked
  inactive instead of feeding garbage to the classifier.

  Detection: polarity auto (bent < open ? inverted : normal), hysteresis
  deadband = clamp(12% of travel, 15, 60). bentPct maps raw to 0..100%
  of travel for the GUI.

  BLE (Nordic UART Service) - unchanged, so pc_receiver.py still works:
    Service 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
    TX  6E400003-...  notify (glove -> PC)
    RX  6E400002-...  write  (PC -> glove, "CAL" = recalibrate)
  Messages: READY / GESTURE: / FINGERS: / FINGER:<name>:BENT|EXT / ROT: / TWIST: / SWEEP:
  Serial: 'c' recalibrate, 'd' raw pin dump, 's' ADC1 scan, 'z' zero twist+sweep, 'h' help
*/

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <WebServer.h>
#include "cfg_store.h"

#define FW_VERSION "3.4"

// K1 user button. Discovered empirically with the fw 2.7 pin scanner: a K1
// press latched "P23" edges (BOOT/GPIO0 stayed silent), so K1 is wired to
// GPIO23 - a normal GPIO, NOT the strap pin. BOOT is a separate button on
// GPIO0. Held LOW (button down) with an internal pull-up.
#define K1_PIN          23
#define K1_DEBOUNCE_MS  60

/* ---- K1 pin discovery scanner (diagnostic, fw 2.7) ----
 * Vendor docs never state which GPIO the K1 user button uses. All plausible
 * spare pins are sampled with internal pull-ups; every edge is latched into
 * g_scanHit (exposed via /api/status "scanhit") and printed to Serial.
 * BOOT(GPIO0) is included as a control: pressing BOOT must show P0 edges,
 * proving the scanner works while proving K1 is NOT on GPIO0.
 * Strapping pins (0/2/12/15) get their pull-ups at runtime only - never hold
 * a button across a power cycle while this build is running.
 */
static const uint8_t K1_SCAN_PINS[] = {0, 2, 4, 5, 12, 13, 14, 15, 16, 18, 19, 23, 25, 26, 27, 33};
static const int     K1_SCAN_N      = (int)(sizeof(K1_SCAN_PINS) / sizeof(K1_SCAN_PINS[0]));
static uint8_t       g_scanPrev[16];
char                 g_scanHit[32] = "";

static void k1ScanSetup() {
  for (int i = 0; i < K1_SCAN_N; i++) {
    pinMode(K1_SCAN_PINS[i], INPUT_PULLUP);
    g_scanPrev[i] = digitalRead(K1_SCAN_PINS[i]);
  }
  Serial.println("K1SCAN active - press K1 (or BOOT); edges -> serial + /api/status scanhit");
}

static void k1ScanPoll() {
  for (int i = 0; i < K1_SCAN_N; i++) {
    uint8_t s = digitalRead(K1_SCAN_PINS[i]);
    if (s != g_scanPrev[i]) {
      g_scanPrev[i] = s;
      snprintf(g_scanHit, sizeof(g_scanHit), "P%d:%s@%lus",
               K1_SCAN_PINS[i], s == LOW ? "DN" : "UP", millis() / 1000UL);
      Serial.printf("K1SCAN %s\n", g_scanHit);
    }
  }
}

// Onboard status LED. The vendor's own QD023 firmware (02Gloves.ino) drives
// GPIO17: pinMode(17, OUTPUT), digitalWrite(17, LOW) = LED ON, so the
// indicator is on GPIO17 and is ACTIVE-LOW. GPIO2 has nothing connected on
// this carrier. All writes below must use LOW for "lit" / HIGH for "dark".
#define LED_BUILTIN     17

#define DEVICE_NAME "QD023-Glove"
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

/* ---------------- finger configuration ---------------- */

#define N_FINGERS 5

// Fixed pin map, measured on THIS unit (see docs/PINMAP.md).
// Thumb=G36 Index=G39 Middle=G34 Ring=G35 Pinky=G32 - all ADC1 (safe with BLE).
// G33 is unconnected on this glove and floats ~604; G37/G38 are not broken out
// on the WROOM-32D module. v2.0 scanned these during calibration and could
// bind a finger to noise - that is why some fingers never moved in the GUI.
const int FINGER_PINS[N_FINGERS] = {36, 39, 34, 35, 32};
const char* FINGER_NAMES[N_FINGERS] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};

// overwritten by the two-pose calibration on every boot (seeded with last-good values)
int  openVal[N_FINGERS] = {  0,  40,   0,   0,   0};
int  fistVal[N_FINGERS] = {600, 750, 540, 950, 830};
int  thresh[N_FINGERS]  = {192, 289, 173, 316, 265};
int  deadb[N_FINGERS]   = { 60,  60,  60,  60,  60};
bool inverted[N_FINGERS]= {false,false,false,false,false};
bool active[N_FINGERS]  = {true, true, true, true, true};

// fraction of travel from OPEN at which a finger counts as bent.
// tutorial's fixed 200 on a 0..1000 swing is ~20-35% -> this matches it.
#define BEND_FRACTION 0.32f
#define MIN_TRAVEL    80      // below this the finger is ignored as unusable

/* ---------------- rotation ---------------- */

float palmVec[3] = {0, 0, 1};
float backVec[3] = {0, 0, -1};
bool  oriOK = false;

// ---- wrist twist (pronation / supination) ----
// The accelerometer can only say "palm or back"; the gyro can say how fast and
// which way. We integrate the calibrated axis to get a signed angle in degrees.
int   twistAxis   = 2;        // 0=x 1=y 2=z, chosen during calibration
float twistSign   = 1.0f;     // flips the axis so CLOCKWISE is positive
float gyroBias[3] = {0, 0, 0};
float twistAngle  = 0.0f;     // degrees since the last zero
float sweepAngle  = 0.0f;     // lateral-hand yaw sweep: degrees since last zero

/* ---- calibration live status (for web panel animation) ---- */
enum CalState { CAL_IDLE, CAL_FLAT, CAL_FIST, CAL_ROTATE_PALM, CAL_ROTATE_BACK, CAL_STILL, CAL_TWIST, CAL_DONE };
CalState g_calState = CAL_IDLE;
unsigned long g_calStepStartMs = 0;
unsigned long g_calStepDurationMs = 0;
const char* g_calStepName = "Idle";

/* ---------------- LED strip mode ----------------
   The strip is NOT wired to the glove. It is a separate ESP32 running
   Downloads/glove/ESP32_BLE_LED_Controller.ino, which advertises service
   0xFFE0 and renders effects from command frames written to char 0xFFE1
   (protocol recovered from the "ledble" app inside base.apk). The glove
   plays the app's role over BLE: scan -> connect -> push state frames.
   Effects are rendered strip-side, so the glove only transmits state
   changes (power / color+brightness / mode / speed). The control surface
   is unchanged: portal /api/led, serial LEDx: commands, gestures, K1. */
#define CTL_WINDOWS 0
#define CTL_LED     1
#define CTL_ANDROID 2
#define CTL_MUSIC   3
#define CTL_MODES   4
int gCtlMode = CTL_WINDOWS;

bool    ledPower  = false;
uint8_t ledBright = 60;   // 0-100
uint8_t ledR = 255, ledG = 255, ledB = 255;
uint8_t ledMode  = 0;     // 0 solid 1 breathe 2 flash 3 twinkle 4 stars 5 gradient
uint8_t ledSpeed = 50;    // 0-100
uint8_t ledRawMode = 0;   // raw DMX effect code from the portal dropdown (0 = glove modes 0-5)
uint8_t ledDir    = 0;    // effect direction: 0 forward, 1 backward (re-sent on each sync)
char    g_lastGesture[20] = ""; // last classified pose: /api/status + gestures page
static uint8_t ledPresetIdx = 0;
static const uint8_t LED_PRESETS[7][3] = {
  {255,255,255},{255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},{255,0,255}
};

static bool g_ledDirty = false;
bool g_stripLink = false;         // wireless strip BLE link (exposed via /api/status)

/* Commit point for every LED state change (portal /api/led, serial LEDx:
   commands, gestures). The strip is wireless: instead of PWM we mark the
   state dirty and stripPoll() pushes the frames to the strip over BLE. */
static void ledWrite(uint8_t r, uint8_t g, uint8_t b) {
  (void)r; (void)g; (void)b;      // callers already updated the state vars
  g_ledDirty = true;
}

static void ledSetHue(int h) {                  // HSV S=1 V=1 -> RGB
  h = ((h % 360) + 360) % 360;
  float f = (h % 60) / 60.0f;
  uint8_t r = 0, g = 0, b = 0;
  switch ((h / 60) % 6) {
    case 0: r = 255; g = (uint8_t)(255 * f); break;
    case 1: r = (uint8_t)(255 * (1 - f)); g = 255; break;
    case 2: g = 255; b = (uint8_t)(255 * f); break;
    case 3: g = (uint8_t)(255 * (1 - f)); b = 255; break;
    case 4: r = (uint8_t)(255 * f); b = 255; break;
    default: r = 255; b = (uint8_t)(255 * (1 - f)); break;
  }
  ledR = r; ledG = g; ledB = b;
  ledMode = 0;                                  // hue set -> solid color
  ledWrite(r, g, b);
}

static int ledClamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* signed "+10"/"-10" steps or absolute values, clamped */
static int ledParseStep(const String& s, int cur, int lo, int hi) {
  int v = (s.startsWith("+") || s.startsWith("-")) ? cur + s.toInt() : s.toInt();
  return ledClamp(v, lo, hi);
}

/* Gesture -> LED action map (fw 3.2), active only in LED control mode (the
   caller checks gCtlMode). Discrete pose -> discrete change; the wrist twist
   acts as a speed dial separately in loop(). Every action flips state and
   marks it dirty; stripPoll() pushes the frames to the wireless strip (or
   holds them until the link comes back up). Unknown poses are ignored. */
static void ledGestureAction(const String& g) {
  if (g == "OPEN_PALM")      ledPower = true;
  else if (g == "FIST")      ledPower = false;
  else if (g == "PEACE")     { ledMode = (ledMode + 1) % 6; ledRawMode = 0; }
  else if (g == "INDEX_UP") {
    ledPresetIdx = (ledPresetIdx + 1) % 7;
    ledR = LED_PRESETS[ledPresetIdx][0];
    ledG = LED_PRESETS[ledPresetIdx][1];
    ledB = LED_PRESETS[ledPresetIdx][2];
    ledMode = 0; ledRawMode = 0;    // explicit color -> solid
  }
  else if (g == "THUMBS_UP") ledBright = (uint8_t)ledClamp((int)ledBright + 25, 0, 100);
  else if (g == "PINKY_UP")  ledBright = (uint8_t)ledClamp((int)ledBright - 25, 0, 100);
  else return;
  ledWrite(ledR, ledG, ledB);
}

/* ---- wireless LED strip link (fw 3.1) ----
   The glove plays the "ledble" Android app's role (protocol recovered from
   base.apk, com.home.net.NetConnectBle). The user's strip advertises as
   "LEDDMX-03-..." - the DMX variant: 9-byte frames written to char 0xFFE1
   (service 0xFFE0), prefix 0x7B, terminator 0xBF:
     power   7B 04 04 01|00 FF FF FF FF BF
     bright  7B FF 01 (B*32/100) B 00 FF FF BF   B = 0-100
     color   7B FF 07 R   G   B   FF FF BF       (static color = "solid")
     mode    7B FF 03 M   FF FF FF FF BF         M = effect code (dmx_model)
     speed   7B FF 02 S   FF 00 FF FF BF         S = 1-100
   Effects render strip-side; the glove only pushes state changes. */
#define STRIP_SVC_UUID   0xFFE0
#define STRIP_CHAR_UUID  0xFFE1

enum StripState { STRIP_IDLE, STRIP_SCANNING, STRIP_CONNECTING, STRIP_CONNECTED };
StripState           g_stripState = STRIP_IDLE;   // enum in web_portal.h
uint8_t              g_stripFails = 0;            // connect-fail/drop counter: /api/status
static unsigned long g_stripScanStart = 0;
static unsigned long g_stripRetryAt = 0;
static bool          g_stripFound = false;
static BLEAddress    g_stripAddr;
static BLEClient*    g_stripClient = nullptr;
static BLERemoteCharacteristic* g_stripChar = nullptr;
char g_stripName[24] = "";        // advertised name (variant check), /api/status

/* glove mode 0-5 -> DMX effect codes (R.array.dmx03_model in the app,
   verified live on the LEDDMX-03 strip):
   0 solid = color frame only; 1 Horse Race 7 Colors; 2 Hop RD/GN/BU;
   3 Forward Flutter CN/VT/WH; 4 Forward Dreaming; 5 Forward Streaming 7 Colors */
static const uint8_t STRIP_MODE_MAP[6] = { 0, 81, 78, 75, 1, 39 };

static void stripWrite9(const uint8_t* f) {
  if (g_stripState != STRIP_CONNECTED || !g_stripClient || !g_stripChar) return;
  g_stripChar->writeValue((uint8_t*)f, 9);
  delay(30);                      // app throttles ~30-50 ms between frames
}

static void stripPowerFrame(bool on) {
  uint8_t f[9] = { 0x7B, 0x04, 0x04, on ? 0x01 : 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF };
  stripWrite9(f);
}

static void stripPowerOff() { stripPowerFrame(false); }

static void stripBrightFrame(uint8_t b) {
  uint8_t f[9] = { 0x7B, 0xFF, 0x01, (uint8_t)((b * 32) / 100), b, 0x00, 0xFF, 0xFF, 0xBF };
  stripWrite9(f);
}

static void stripColorFrame(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t f[9] = { 0x7B, 0xFF, 0x07, r, g, b, 0xFF, 0xFF, 0xBF };
  stripWrite9(f);
}

static void stripModeFrame(uint8_t code) {
  uint8_t f[9] = { 0x7B, 0xFF, 0x03, code, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF };
  stripWrite9(f);
}

static void stripSpeedFrame(uint8_t s) {
  uint8_t f[9] = { 0x7B, 0xFF, 0x02, s, 0xFF, 0x00, 0xFF, 0xFF, 0xBF };
  stripWrite9(f);
}

/* Effect direction (NetConnectBle.setDirection, LEDDMX-03 branch):
   7B FF 0D D FF FF FF FF BF - D 0 forward, 1 backward. */
static void stripDirectionFrame(uint8_t d) {
  uint8_t f[9] = { 0x7B, 0xFF, 0x0D, d, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF };
  stripWrite9(f);
}

/* Mic/voice-reactive effect (setMusicMicroMode / setVoiceCtlMode, LEDDMX-03):
   7B FF 0B M V FF FF FF BF - M 0-4 (gradual/trailing/jump/strobe/off),
   V 1 = microphone on. One-shot poke: mic effects are strip-side state, so
   they must NOT go through the dirty-sync path (a full sync would overwrite
   the mic effect with color/mode frames). */
static void stripMusicFrame(uint8_t m, uint8_t voice) {
  uint8_t f[9] = { 0x7B, 0xFF, 0x0B, m, voice, 0xFF, 0xFF, 0xFF, 0xBF };
  stripWrite9(f);
}

/* Music-mode brightness (setMusicBrightness, LEDDMX-03): like stripBrightFrame
   but with the voice flag (byte 5) = 1. One-shot poke, same reason. */
static void stripMusicBrightFrame(uint8_t b) {
  uint8_t f[9] = { 0x7B, 0xFF, 0x01, (uint8_t)((b * 32) / 100), b, 0x01, 0xFF, 0xFF, 0xBF };
  stripWrite9(f);
}

static void stripSyncAll() {     // color first: it forces static mode
  stripPowerFrame(ledPower);
  if (!ledPower) return;
  stripBrightFrame(ledBright);
  stripColorFrame(ledR, ledG, ledB);
  if (ledRawMode) stripModeFrame(ledRawMode);   // portal dropdown pick (raw DMX code)
  else if (ledMode > 0) stripModeFrame(STRIP_MODE_MAP[ledMode]);
  stripDirectionFrame(ledDir);                  // forward/backward persists strip-side
  stripSpeedFrame(ledSpeed < 1 ? 1 : ledSpeed);
}

class StripScanCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    BLEUUID svc((uint16_t)STRIP_SVC_UUID);
    String nm = advertisedDevice.getName();
    bool bySvc  = advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(svc);
    bool byName = nm.startsWith("LED") || nm.startsWith("BED");  // app filter: NAME_START_LED/BED
    if (bySvc || byName) {
      g_stripAddr = advertisedDevice.getAddress();
      g_stripFound = true;          // stripPoll stops the scan + transitions
      strlcpy(g_stripName, nm.c_str(), sizeof(g_stripName));
      Serial.printf("STRIP: found %s %s\n", g_stripName, g_stripAddr.toString().c_str());
    }
  }
};

static StripScanCB g_stripScanCB;

static void scanCompleteCB(BLEScanResults results) {
  (void)results;
  if (g_stripState != STRIP_SCANNING) return;
  if (g_stripFound) {
    g_stripState = STRIP_CONNECTING;
  } else {
    g_stripState = STRIP_IDLE;
    g_stripRetryAt = millis() + 10000;
  }
}

class StripClientCB : public BLEClientCallbacks {
  void onDisconnect(BLEClient* c) override {
    (void)c;
    g_stripState = STRIP_IDLE;
    g_stripRetryAt = millis() + 3000;
    g_stripChar = nullptr;
    g_stripLink = false;
    g_stripFails++;
    Serial.println("STRIP: link lost - rescanning");
  }
};

static StripClientCB g_stripClientCB;

/* Connection state machine + dirty-state push, run from loop() while the
   glove is in LED control mode. connect() blocks ~1 s per attempt. */
static void stripPoll() {
  if (gCtlMode != CTL_LED) return;
  switch (g_stripState) {
    case STRIP_IDLE:
      if (g_stripClient) {          // stale client from a dropped link
        delete g_stripClient;
        g_stripClient = nullptr;
        g_stripChar = nullptr;
      }
      if ((long)(millis() - g_stripRetryAt) >= 0) {
        g_stripFound = false;
        BLEScan* scan = BLEDevice::getScan();
        scan->setAdvertisedDeviceCallbacks(&g_stripScanCB, false);
        scan->setActiveScan(true);
        scan->clearResults();
        scan->start(3, scanCompleteCB, false);
        g_stripScanStart = millis();
        g_stripState = STRIP_SCANNING;
        Serial.println("STRIP: scanning for 0xFFE0 advertiser");
      }
      break;
    case STRIP_SCANNING:
      if (g_stripFound) {           // matched in onResult; stop from main loop
        BLEDevice::getScan()->stop();
        g_stripState = STRIP_CONNECTING;
      } else if ((long)(millis() - g_stripScanStart) >= 3500) {
        g_stripState = STRIP_IDLE;  // scan window elapsed, nothing matched
        g_stripRetryAt = millis() + 10000;
      }
      break;
    case STRIP_CONNECTING: {
      delay(100);                 // let the scan controller fully release
      g_stripClient = BLEDevice::createClient();
      g_stripClient->setClientCallbacks(&g_stripClientCB);
      if (!g_stripClient->connect(g_stripAddr)) {
        Serial.println("STRIP: connect failed");
        g_stripFails++;
        delete g_stripClient;
        g_stripClient = nullptr;
        g_stripState = STRIP_IDLE;
        g_stripRetryAt = millis() + 10000;
        break;
      }
      BLERemoteService* svc = g_stripClient->getService(BLEUUID((uint16_t)STRIP_SVC_UUID));
      g_stripChar = svc ? svc->getCharacteristic(BLEUUID((uint16_t)STRIP_CHAR_UUID)) : nullptr;
      if (!g_stripChar) {
        Serial.println("STRIP: 0xFFE1 characteristic missing");
        g_stripFails++;
        g_stripClient->disconnect();
        delete g_stripClient;
        g_stripClient = nullptr;
        g_stripState = STRIP_IDLE;
        g_stripRetryAt = millis() + 10000;
        break;
      }
      g_stripState = STRIP_CONNECTED;
      g_stripLink = true;
      Serial.println("STRIP: connected - pushing state");
      g_ledDirty = true;
      break;
    }
    case STRIP_CONNECTED:
      if (g_ledDirty) {
        g_ledDirty = false;
        stripSyncAll();
      }
      break;
    default:
      break;
  }
}

static void setCtlMode(int m);

Adafruit_MPU6050 mpu;
bool mpuOK = false;

/* ---------------- BLE ---------------- */

BLEServer* pServer = nullptr;
BLECharacteristic* pTx = nullptr;
bool bleConnected = false;

void doCalibration();
void printHelp();
static void cfgRxLine(const String& v);
static void k1Service();
static void advertiseForMode(int m);

#include "web_portal.h"

static String g_wsBuf;
static int    g_wsSeen = 0;
static int    g_wsTot  = 0;

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    bleConnected = true;
    Serial.println("BLE PC connected");
  }
  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    Serial.println("BLE central disconnected - re-advertising for current mode");
    advertiseForMode(gCtlMode);
  }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = String(c->getValue().c_str());
    v.trim();
    Serial.println("BLE RX: " + v);
    if (v.equalsIgnoreCase("CAL")) doCalibration();
    else cfgRxLine(v);
  }
};

void bleSend(const String& s) {
  if (bleConnected && pTx) {
    pTx->setValue((uint8_t*)s.c_str(), s.length());
    pTx->notify();
  }
}

/* One BLE radio link is shared by the PC bridge and the phone. The advertised
   name carries the active control mode, so each client only grabs the glove
   while it owns it: "-W" windows, "-L" LED strip, "-A" android. Switching mode
   drops the current central and re-tags the advert, which is the handoff. */
static const char* ctlTag(int m) {
  if (m == CTL_LED) return "-L";
  if (m == CTL_ANDROID) return "-A";
  if (m == CTL_MUSIC) return "-M";
  return "-W";
}

static void advertiseForMode(int m) {
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->stop();

  String name = String(g_cfg.deviceName) + ctlTag(m);
  BLEAdvertisementData d;
  d.setFlags(0x06);  // LE general discoverable, BR/EDR not supported
  d.setName(name);
  char man[3] = {(char)0xFF, (char)0xFF, (char)m};  // company id 0xFFFF + mode
  d.setManufacturerData(String(man, 3));
  adv->setAdvertisementData(d);
  adv->start();
  Serial.println("BLE adv: " + name);
}

/* control-mode switch: 0 = WINDOWS (media keys), 1 = LED strip. Notifies the
   bridge ("MOD:x") so it can reroute gestures. Leaving LED mode powers the
   wireless strip off immediately; entering it re-syncs the full state once
   the strip link is up. */
static void setCtlMode(int m) {
  if (m < 0 || m >= CTL_MODES) return;
  if (m == gCtlMode) return;
  int prev = gCtlMode;
  gCtlMode = m;
  if (prev == CTL_LED) stripPowerOff();
  g_ledDirty = true;

  // Tell the current central which mode won before taking the link away.
  bleSend(String("MOD:") + m + "\n");

  bool wasConnected = bleConnected;
  if (wasConnected && pServer) {
    pServer->disconnect(pServer->getConnId());  // onDisconnect re-advertises
  }
  if (!wasConnected) advertiseForMode(m);

  const char* name = (m == CTL_LED)      ? "LED STRIP"
                     : (m == CTL_ANDROID) ? "ANDROID"
                     : (m == CTL_MUSIC)   ? "MUSIC"
                                          : "WINDOWS";
  Serial.printf("mode: %s\n", name);
}

// The PC bridge pulls the shared thresholds after it subscribes, and pushes
// them back when the UI changes one. Short lines keep every notify inside the
// smallest common ATT payload (20 bytes at the default MTU).
static bool parseCsv5(const String& s, uint8_t out[5]) {
  int start = 0;
  for (int i = 0; i < 5; i++) {
    int comma = s.indexOf(',', start);
    String tok = (comma < 0) ? s.substring(start) : s.substring(start, comma);
    long v = tok.toInt();
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    out[i] = (uint8_t)v;
    if (comma < 0) return i == 4;
    start = comma + 1;
  }
  return true;
}

static void cfgRxLine(const String& v) {
  // WiFi credentials do not fit one 20-byte notify, so they arrive as ordered
  // fragments: "WS:1/4:<text>" ... "WS:4/4:<text>". Reassembly is staged and
  // only committed once the last fragment lands and the count matches.
  if (v.startsWith("WS:")) {
    int a = v.indexOf(':', 3);
    int b = v.indexOf('/', 3);
    if (a < 0 || b < 0 || b > a) { bleSend("ERR:WSFMT\n"); return; }
    int idx = v.substring(3, b).toInt();
    int tot = v.substring(b + 1, a).toInt();
    String part = v.substring(a + 1);
    if (idx < 1 || tot < 1 || tot > 8 || idx > tot) { bleSend("ERR:WSFMT\n"); return; }
    if (idx == 1) { g_wsBuf = ""; g_wsTot = tot; }
    if (idx != g_wsSeen + 1) { bleSend("ERR:WSSEQ\n"); g_wsSeen = 0; return; }
    g_wsBuf += part;
    g_wsSeen = idx;
    if (idx < tot) { bleSend("OK:WS" + String(idx) + "\n"); return; }
    g_wsSeen = 0;

    int sep = g_wsBuf.indexOf('\n');
    if (sep < 0) { bleSend("ERR:WSCFG\n"); return; }
    String ssid = g_wsBuf.substring(0, sep);
    String pass = g_wsBuf.substring(sep + 1);
    if (ssid.length() == 0 || ssid.length() > 32) { bleSend("ERR:WSSSID\n"); return; }
    if (pass.length() > 63) { bleSend("ERR:WSPASS\n"); return; }

    strncpy(g_cfg.wifiSsid, ssid.c_str(), sizeof(g_cfg.wifiSsid) - 1);
    g_cfg.wifiSsid[sizeof(g_cfg.wifiSsid) - 1] = '\0';
    strncpy(g_cfg.wifiPass, pass.c_str(), sizeof(g_cfg.wifiPass) - 1);
    g_cfg.wifiPass[sizeof(g_cfg.wifiPass) - 1] = '\0';
    g_cfg.wifiEn = true;
    cfgSave(g_cfg);
    bleSend("OK:WS\n");
    wifiRequestJoin(g_cfg.wifiSsid, g_cfg.wifiPass);
    return;
  }
  if (v.equalsIgnoreCase("WIFISTAT")) {
    bleSend("WST:" + String(g_wifiState) + "," +
            (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("-")) + "\n");
    return;
  }
  if (v.equalsIgnoreCase("GETCFG")) {    char b[40];
    snprintf(b, sizeof(b), "THR:%u,%u,%u,%u,%u\n",
             g_cfg.onPct[0], g_cfg.onPct[1], g_cfg.onPct[2],
             g_cfg.onPct[3], g_cfg.onPct[4]);
    bleSend(String(b)); delay(20);
    snprintf(b, sizeof(b), "THO:%u,%u,%u,%u,%u\n",
             g_cfg.offPct[0], g_cfg.offPct[1], g_cfg.offPct[2],
             g_cfg.offPct[3], g_cfg.offPct[4]);
    bleSend(String(b)); delay(20);
    snprintf(b, sizeof(b), "TWD:%.1f,%.1f\n", g_cfg.twistPauseDps, g_cfg.twistResumeDps);
    bleSend(String(b));
    return;
  }
  if (v.startsWith("SO:")) {
    parseCsv5(v.substring(3), g_cfg.onPct);
    cfgSave(g_cfg);
    bleSend("OK:SO\n");
  } else if (v.startsWith("SF:")) {
    parseCsv5(v.substring(3), g_cfg.offPct);
    cfgSave(g_cfg);
    bleSend("OK:SF\n");
  } else if (v.startsWith("ST:")) {
    int c = v.indexOf(',', 3);
    if (c > 0) {
      g_cfg.twistPauseDps  = v.substring(3, c).toFloat();
      g_cfg.twistResumeDps = v.substring(c + 1).toFloat();
      cfgSave(g_cfg);
      bleSend("OK:ST\n");
    }
  } else if (v.startsWith("LEDP:")) {
    String a = v.substring(5);
    if (a == "T") ledPower = !ledPower;
    else ledPower = (a == "1");
    ledWrite(ledR, ledG, ledB);
    bleSend("OK:LED\n");
  } else if (v.startsWith("LEDC:")) {
    String a = v.substring(5);
    if (a == "cycle") {
      ledPresetIdx = (ledPresetIdx + 1) % 7;
      ledR = LED_PRESETS[ledPresetIdx][0];
      ledG = LED_PRESETS[ledPresetIdx][1];
      ledB = LED_PRESETS[ledPresetIdx][2];
    } else {
      int c1 = a.indexOf(',');
      int c2 = a.indexOf(',', c1 + 1);
      if (c1 > 0 && c2 > c1) {
        ledR = (uint8_t)ledClamp(a.substring(0, c1).toInt(), 0, 255);
        ledG = (uint8_t)ledClamp(a.substring(c1 + 1, c2).toInt(), 0, 255);
        ledB = (uint8_t)ledClamp(a.substring(c2 + 1).toInt(), 0, 255);
      }
    }
    ledMode = 0;                       // explicit color -> solid
    ledRawMode = 0;
    ledWrite(ledR, ledG, ledB);
    bleSend("OK:LED\n");
  } else if (v.startsWith("LEDB:")) {
    ledBright = (uint8_t)ledParseStep(v.substring(5), ledBright, 0, 100);
    ledWrite(ledR, ledG, ledB);
    bleSend("OK:LED\n");
  } else if (v.startsWith("LEDM:")) {
    String a = v.substring(5);
    int m;
    if (a.startsWith("+") || a.startsWith("-")) {
      m = ((int)ledMode + a.toInt()) % 6;   // relative steps wrap 0..5
      if (m < 0) m += 6;
    } else {
      m = ledClamp(a.toInt(), 0, 5);        // absolute: clamp
    }
    ledMode = (uint8_t)m;
    ledRawMode = 0;
    ledWrite(ledR, ledG, ledB);
    bleSend("OK:LED\n");
  } else if (v.startsWith("LEDS:")) {
    ledSpeed = (uint8_t)ledParseStep(v.substring(5), ledSpeed, 0, 100);
    ledWrite(ledR, ledG, ledB);
    bleSend("OK:LED\n");
  } else if (v.startsWith("LEDH:")) {
    ledSetHue(v.substring(5).toInt());
    ledRawMode = 0;
    bleSend("OK:LED\n");
  } else if (v.startsWith("MOD:")) {
    setCtlMode(v.substring(4).toInt());
  } else if (v.equalsIgnoreCase("GETMODE")) {
    bleSend("MOD:" + String(gCtlMode) + "\n");
  }
}

/* ---------------- helpers ---------------- */

// insertion-sort median over `n` samples; rejects single-sample ADC spikes
// (one bad sample used to poison a finger's threshold and dead-band)
static int medianOf(int* buf, int n) {
  for (int i = 1; i < n; i++) {
    int key = buf[i], j = i - 1;
    while (j >= 0 && buf[j] > key) { buf[j + 1] = buf[j]; j--; }
    buf[j + 1] = key;
  }
  return (n & 1) ? buf[n / 2] : (buf[n / 2 - 1] + buf[n / 2]) / 2;
}

// median of all five fingers at once (hand FLAT or FIST pose)
static void capturePose(int out[N_FINGERS], int samples = 40, int gapMs = 50) {
  static int buf[N_FINGERS][64];
  if (samples > 64) samples = 64;
  for (int s = 0; s < samples; s++) {
    for (int i = 0; i < N_FINGERS; i++) buf[i][s] = analogRead(FINGER_PINS[i]);
    delay(gapMs);
  }
  for (int i = 0; i < N_FINGERS; i++) out[i] = medianOf(buf[i], samples);
}

// 0..100 = how far this finger has travelled from OPEN towards FIST.
// v2.1 fix: span = fist - open so an inverted finger (bent reads LOW)
// still maps open->0 and fist->100. The v2.0 a/b swap reversed the
// scale for inverted fingers in the GUI.
int bentPct(int i, int raw) {
  int span = fistVal[i] - openVal[i];
  if (span == 0) return 0;
  int p = (raw - openVal[i]) * 100 / span;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

/* ---------------- calibration ---------------- */

void doCalibration() {
  Serial.println();
  Serial.println("=== CALIBRATION v" FW_VERSION " (two pose: FLAT + FIST) ===");

  /* ---- stage A: OPEN baseline (all five fingers straight) ---- */
  g_calState = CAL_FLAT;
  g_calStepStartMs = millis();
  g_calStepDurationMs = 3000;
  g_calStepName = "Hold hand FLAT, fingers straight and relaxed";
  Serial.println("Step 1/2: hand FLAT, fingers STRAIGHT and relaxed, in 3s...");
  delay(3000);
  int openRaw[N_FINGERS];
  capturePose(openRaw, 40, 50);
  for (int i = 0; i < N_FINGERS; i++) openVal[i] = openRaw[i];
  Serial.print("OPEN :");
  for (int i = 0; i < N_FINGERS; i++) Serial.printf(" %s=%d", FINGER_NAMES[i], openVal[i]);
  Serial.println();

  /* ---- stage B: FIST baseline (all five fingers curled) ---- */
  g_calState = CAL_FIST;
  g_calStepStartMs = millis();
  g_calStepDurationMs = 3000;
  g_calStepName = "Make a FIST - curl all five fingers";
  Serial.println("Step 2/2: make a FIST (curl ALL five fingers), hold it, in 3s...");
  delay(3000);
  int fistRaw[N_FINGERS];
  capturePose(fistRaw, 40, 50);
  Serial.print("FIST :");
  for (int i = 0; i < N_FINGERS; i++) Serial.printf(" %s=%d", FINGER_NAMES[i], fistRaw[i]);
  Serial.println();

  /* ---- derive per-finger thresholds from the two medians ---- */
  int nAct = 0;
  for (int i = 0; i < N_FINGERS; i++) {
    fistVal[i] = fistRaw[i];
    int travel = fistVal[i] - openVal[i];
    bool inv = (travel < 0);
    if (inv) travel = -travel;
    inverted[i] = inv;

    if (travel < g_cfg.minTravel) {
      active[i] = false;
      Serial.printf(" %s: travel=%d < %u -> INACTIVE (check sensor / redo cal)\n",
                    FINGER_NAMES[i], travel, g_cfg.minTravel);
      continue;
    }
    active[i] = true;
    nAct++;

    // threshold at the configured fraction of the way from OPEN to FIST
    int t = inv ? (openVal[i] - (int)(travel * g_cfg.bendFraction))
                : (openVal[i] + (int)(travel * g_cfg.bendFraction));
    thresh[i] = t;

    // deadband 12% of travel, clamped so tiny sensors still get hysteresis
    // and big ones don't need a huge swing to flip state
    int db = travel * 12 / 100;
    if (db < 15) db = 15;
    if (db > 60) db = 60;
    deadb[i] = db;

    Serial.printf(" %s: open=%d fist=%d travel=%d thr=%d db=%d %s OK\n",
                  FINGER_NAMES[i], openVal[i], fistVal[i], travel,
                  thresh[i], deadb[i], inv ? "(bent=LOW)" : "(bent=HIGH)");
  }

  Serial.print("MAPPING:");
  for (int i = 0; i < N_FINGERS; i++)
    Serial.printf(" %s=G%d%s", FINGER_NAMES[i], FINGER_PINS[i], active[i] ? "" : "(off)");
  Serial.printf("  active=%d/%d\n", nAct, N_FINGERS);

  /* ---- stage C: rotation ---- */
    Serial.println("Rotation: hold hand FLAT, PALM towards you, in 3s...");
    g_calState = CAL_ROTATE_PALM;
    g_calStepStartMs = millis();
    g_calStepDurationMs = 3000;
    g_calStepName = "Rotate palm toward you";
    delay(3000);
  if (mpuOK) {
    long ax = 0, ay = 0, az = 0;
    const int N = 30;
    for (int s = 0; s < N; s++) {
      sensors_event_t a, g, t;
      mpu.getEvent(&a, &g, &t);
      ax += (long)(a.acceleration.x * 100);
      ay += (long)(a.acceleration.y * 100);
      az += (long)(a.acceleration.z * 100);
      delay(80);
    }
    palmVec[0] = ax / N / 100.0f;
    palmVec[1] = ay / N / 100.0f;
    palmVec[2] = az / N / 100.0f;
    Serial.printf("PALM vec: (%.1f, %.1f, %.1f)\n", palmVec[0], palmVec[1], palmVec[2]);

    Serial.println("Rotation: rotate to BACK of hand towards you, in 3s...");
    g_calState = CAL_ROTATE_BACK;
    g_calStepStartMs = millis();
    g_calStepDurationMs = 3000;
    g_calStepName = "Rotate back of hand toward you";
    delay(3000);
    ax = ay = az = 0;
    for (int s = 0; s < N; s++) {
      sensors_event_t a, g, t;
      mpu.getEvent(&a, &g, &t);
      ax += (long)(a.acceleration.x * 100);
      ay += (long)(a.acceleration.y * 100);
      az += (long)(a.acceleration.z * 100);
      delay(80);
    }
    backVec[0] = ax / N / 100.0f;
    backVec[1] = ay / N / 100.0f;
    backVec[2] = az / N / 100.0f;
    Serial.printf("BACK vec: (%.1f, %.1f, %.1f)\n", backVec[0], backVec[1], backVec[2]);

    float dot = palmVec[0] * backVec[0] + palmVec[1] * backVec[1] + palmVec[2] * backVec[2];
    if (dot > 60) {
      // the two poses look almost identical -> templates are useless
      oriOK = false;
      Serial.printf("rotation templates too similar (dot=%.1f) -> palm/back disabled, tilt only\n", dot);
    } else {
      oriOK = true;
      Serial.printf("rotation templates OK (dot=%.1f)\n", dot);
    }
  } else {
    Serial.println("no MPU -> rotation disabled");
  }

  /* ---- stage D: wrist twist axis ---- */
  if (mpuOK) {
    Serial.println("Twist cal: hold STILL 2s (gyro zero reference)...");
    g_calState = CAL_STILL;
    g_calStepStartMs = millis();
    g_calStepDurationMs = 2000;
    g_calStepName = "Hold STILL - do not move (gyro zero)";
    delay(2000);
    float bx = 0, by = 0, bz = 0;
    const int NB = 150;
    for (int s = 0; s < NB; s++) {
      sensors_event_t a, g, t;
      mpu.getEvent(&a, &g, &t);
      bx += g.gyro.x; by += g.gyro.y; bz += g.gyro.z;
      delay(10);
    }
    gyroBias[0] = bx / NB;
    gyroBias[1] = by / NB;
    gyroBias[2] = bz / NB;
    Serial.printf("gyro zero: %.3f %.3f %.3f rad/s\n", gyroBias[0], gyroBias[1], gyroBias[2]);

    Serial.println("Twist cal: rotate wrist CLOCKWISE (like turning a key) 3s, starting in 2s...");
    g_calState = CAL_TWIST;
    g_calStepStartMs = millis();
    g_calStepDurationMs = 2000;
    g_calStepName = "Rotate wrist clockwise (like turning a key)";
    delay(2000);
    float sx = 0, sy = 0, sz = 0;
    const int NS = 150;
    for (int s = 0; s < NS; s++) {
      sensors_event_t a, g, t;
      mpu.getEvent(&a, &g, &t);
      sx += (g.gyro.x - gyroBias[0]);
      sy += (g.gyro.y - gyroBias[1]);
      sz += (g.gyro.z - gyroBias[2]);
      delay(20);
    }
    sx /= NS; sy /= NS; sz /= NS;
    float axv = fabs(sx), ayv = fabs(sy), azv = fabs(sz);
    if (azv >= axv && azv >= ayv) { twistAxis = 2; twistSign = (sz >= 0) ? 1.0f : -1.0f; }
    else if (ayv >= axv)          { twistAxis = 1; twistSign = (sy >= 0) ? 1.0f : -1.0f; }
    else                          { twistAxis = 0; twistSign = (sx >= 0) ? 1.0f : -1.0f; }
    twistAngle = 0.0f;
    sweepAngle = 0.0f;
    Serial.printf("twist axis=%c sign=%s (mean rates x=%.2f y=%.2f z=%.2f rad/s)\n",
                  twistAxis == 0 ? 'x' : (twistAxis == 1 ? 'y' : 'z'),
                  twistSign > 0 ? "+" : "-", sx, sy, sz);
  }

  if (g_cfg.twistAxis >= 0) {
    twistAxis = g_cfg.twistAxis;
    Serial.printf("twist axis overridden by config -> %d\n", twistAxis);
  }
  if (g_cfg.twistSign != 0.0f) {
    twistSign = (g_cfg.twistSign > 0) ? 1.0f : -1.0f;
    Serial.printf("twist sign overridden by config -> %s\n", twistSign > 0 ? "+" : "-");
  }

  Serial.println("=== calibration done. 'c' to redo, 'd' raw, 's' ADC scan, 'z' zero twist ===");
  g_calState = CAL_DONE;
  g_calStepDurationMs = 0;
  g_calStepName = "Calibration complete";
  Serial.println();

  // Persist so the next boot can skip the two poses entirely. Saving here (and
  // not only from the web button) means a calibration is never lost just
  // because nobody remembered to press Save.
  calPersist();
}

/* ---------------- gesture classification ---------------- */

String classify(const bool bent[N_FINGERS], int nActive) {
  int bentA = 0;
  for (int i = 0; i < N_FINGERS; i++)
    if (active[i] && bent[i]) bentA++;

  if (nActive < 3) return "NEED_CALIBRATION";
  if (bentA == 0)  return "OPEN_PALM";
  if (bentA == nActive) return "FIST";

  if (nActive == 5) {
    // t/i/m/r/p bent flags; the EXTENDED finger names the gesture
    if (!bent[1] && bent[0] && bent[2] && bent[3] && bent[4]) return "INDEX_UP";
    if (!bent[0] && bent[1] && bent[2] && bent[3] && bent[4]) return "THUMBS_UP";
    if (!bent[4] && bent[0] && bent[1] && bent[2] && bent[3]) return "PINKY_UP";
    if (!bent[1] && !bent[2] && bent[0] && bent[3] && bent[4]) return "PEACE";
  }
  switch (bentA) {
    case 1: return "ONE_FINGER";
    case 2: return "TWO_BENT";
    case 3: return "THREE_BENT";
    case 4: return "FOUR_BENT";
  }
  return "PARTIAL";
}

String rotationLabel() {
  if (!mpuOK) return "";
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  if (oriOK) {
    float ax = a.acceleration.x, ay = a.acceleration.y, az = a.acceleration.z;
    float dp = ax * palmVec[0] + ay * palmVec[1] + az * palmVec[2];
    float db = ax * backVec[0] + ay * backVec[1] + az * backVec[2];
    if (dp >= db) return "PALM_VIEW";
    return "BACK_VIEW";
  }
  float pitch = atan2(-a.acceleration.x,
                      sqrt(a.acceleration.y * a.acceleration.y +
                           a.acceleration.z * a.acceleration.z)) * 180.0f / PI;
  float roll = atan2(a.acceleration.y, a.acceleration.z) * 180.0f / PI;
  if (roll > 35)  return "TILT_RIGHT";
  if (roll < -35) return "TILT_LEFT";
  if (pitch > 35) return "TILT_BACK";
  if (pitch < -35) return "TILT_FWD";
  return "FLAT";
}

/* ---------------- setup ---------------- */

void setup() {
  Serial.begin(115200);
  delay(600);

  Serial.println();
  Serial.println("=== QD023 BLE-PC Controller v" FW_VERSION " ===");
  Serial.println("pins: T=36 I=39 M=34 R=35 P=32   (ADC1, 12-bit, 11dB)");
  printHelp();

  cfgLoad(g_cfg);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  for (int i = 0; i < N_FINGERS; i++) pinMode(FINGER_PINS[i], INPUT);

#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
#endif

  Wire.begin(21, 22);
  if (mpu.begin(0x68)) {
    mpuOK = true;
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println("MPU6050 OK");
  } else {
    Serial.println("MPU6050 missing - fingers only");
  }

  BLEDevice::init(g_cfg.deviceName);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCB());
  BLEService* svc = pServer->createService(SERVICE_UUID);

  pTx = svc->createCharacteristic(CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pTx->addDescriptor(new BLE2902());

  BLECharacteristic* pRx =
      svc->createCharacteristic(CHAR_RX_UUID,
                                BLECharacteristic::PROPERTY_WRITE |
                                BLECharacteristic::PROPERTY_WRITE_NR);
  pRx->setCallbacks(new RxCB());

  svc->start();

  advertiseForMode(gCtlMode);
  Serial.println("BLE advertising in mode: " + String(ctlTag(gCtlMode)) + " (NUS)");

  pinMode(K1_PIN, INPUT_PULLUP);

  webInit();

  // A stored snapshot replaces the two-pose routine, so power-cycling the
  // glove no longer costs a recalibration. K1 sits on GPIO23 (a normal GPIO,
  // not a strap pin), so holding it during reset is harmless - to force a
  // fresh calibration use 'c' on the console or the web button.
  if (!calRestore()) doCalibration();
  bleSend("READY\n");

  // Auto-join runs after BLE/AP are already up, so a slow or failing router
  // never delays the glove or blocks the hotspot.
  if (g_cfg.wifiEn && g_cfg.wifiSsid[0]) {
    Serial.printf("auto-join \"%s\"\n", g_cfg.wifiSsid);
    wifiRequestJoin(g_cfg.wifiSsid, g_cfg.wifiPass);
  }

  // LAST, so the scanner owns the spare pins (25-27 included).
  k1ScanSetup();
}

void printHelp() {
  Serial.println("commands:  c = recalibrate   d = raw 5-pin dump   s = ADC1 scan   z = zero twist   h = help");
  Serial.println("K1 button: tap = cycle WINDOWS/LED/ANDROID mode   hold 1s = join saved home WiFi");
}

/* K1: short press toggles the control mode (WINDOWS <-> LED strip); a hold of
   >=1 s joins the saved home network (2.4 GHz). The hotspot stays up
   regardless, so a failed join is never a lockout. */
static void k1Service() {
  static bool          stable    = false;   // debounced level
  static unsigned long lastEdge  = 0;
  static unsigned long downAt    = 0;
  static bool          longDone  = false;

  bool raw = (digitalRead(K1_PIN) == LOW);
  if (raw != stable) {
    if (millis() - lastEdge > K1_DEBOUNCE_MS) {
      stable   = raw;
      lastEdge = millis();
      if (stable) {                       // press: start the long-press window
        downAt   = millis();
        longDone = false;
      } else if (!longDone) {             // release before 1 s: mode cycle
        setCtlMode((gCtlMode + 1) % CTL_MODES);
      }
    }
  }
  if (stable && !longDone && millis() - downAt >= 1000) {
    longDone = true;                      // hold: join saved home WiFi
    if (!g_cfg.wifiSsid[0]) {
      Serial.println("K1: no saved home WiFi - set one in the portal");
    } else {
      Serial.printf("K1: joining \"%s\"\n", g_cfg.wifiSsid);
      wifiRequestJoin(g_cfg.wifiSsid, g_cfg.wifiPass);
    }
  }
}

/* ---------------- main loop ---------------- */

void loop() {
  static bool rawDump   = false;
  static bool scanMode  = false;

  webLoop();
  k1Service();
  extern volatile bool g_otaInProgress;
  if (!g_otaInProgress) stripPoll();

  if (g_recalRequested) {
    g_recalRequested = false;
    doCalibration();
    bleSend("READY\n");
  }

  /* ---- serial console ---- */
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'c' || c == 'C') doCalibration();
    else if (c == 'd' || c == 'D') { rawDump = !rawDump; Serial.printf("raw dump %s\n", rawDump ? "ON" : "OFF"); }
    else if (c == 's' || c == 'S') { scanMode = !scanMode; Serial.printf("ADC scan %s\n", scanMode ? "ON" : "OFF"); }
    else if (c == 'z' || c == 'Z') { twistAngle = 0.0f; sweepAngle = 0.0f; Serial.println("twist+sweep zeroed"); }
    else if (c == 'h' || c == 'H') printHelp();
  }

  /* ADC1 sweep of the 6 broken-out pins (G37/G38 absent on WROOM-32D).
     Use to sanity-check wiring; G33 is the floating spare and should twitch. */
  if (scanMode) {
    Serial.printf("SCAN 32=%4d 33=%4d 34=%4d 35=%4d 36=%4d 39=%4d\n",
                  analogRead(32), analogRead(33), analogRead(34),
                  analogRead(35), analogRead(36), analogRead(39));
    delay(200);
    return;
  }

  if (rawDump) {
    Serial.printf("RAWS T=%4d I=%4d M=%4d R=%4d P=%4d | G33(spare)=%4d (thr T=%d I=%d M=%d R=%d P=%d)\n",
                  analogRead(FINGER_PINS[0]), analogRead(FINGER_PINS[1]),
                  analogRead(FINGER_PINS[2]), analogRead(FINGER_PINS[3]),
                  analogRead(FINGER_PINS[4]), analogRead(33),
                  thresh[0], thresh[1], thresh[2], thresh[3], thresh[4]);
    delay(250);
    return;
  }

  /* ---- sample + smooth ---- */
  int raw[N_FINGERS];
  for (int i = 0; i < N_FINGERS; i++) raw[i] = analogRead(FINGER_PINS[i]);

  static float ema[N_FINGERS];
  static bool  emaInit = false;
  if (!emaInit) {
    for (int i = 0; i < N_FINGERS; i++) ema[i] = raw[i];
    emaInit = true;
  }

  int  pct[N_FINGERS];
  bool bent[N_FINGERS];
  int  nActive = 0;
  static bool prevBent[N_FINGERS] = {false, false, false, false, false};

  for (int i = 0; i < N_FINGERS; i++) {
    if (!active[i]) {
      pct[i] = 0; bent[i] = false; prevBent[i] = false; ema[i] = raw[i];
      continue;
    }
    ema[i] = 0.35f * raw[i] + 0.65f * ema[i];
    int v = (int)(ema[i] + 0.5f);
    nActive++;
    pct[i] = bentPct(i, v);

    int db = deadb[i];
    bool b;
    if (!inverted[i]) {              // bent = HIGH
      if (v > thresh[i] + db)      b = true;
      else if (v < thresh[i] - db) b = false;
      else                         b = prevBent[i];
    } else {                         // bent = LOW
      if (v < thresh[i] - db)      b = true;
      else if (v > thresh[i] + db) b = false;
      else                         b = prevBent[i];
    }
    bent[i] = b;
    prevBent[i] = b;
  }

  /* ---- per finger edges ---- */
  for (int i = 0; i < N_FINGERS; i++) {
    static bool lastB[N_FINGERS] = {false, false, false, false, false};
    static bool initB = false;
    if (!initB) { lastB[i] = bent[i]; if (i == N_FINGERS - 1) initB = true; continue; }
    if (active[i] && bent[i] != lastB[i]) {
      lastB[i] = bent[i];
      String m = String("FINGER:") + FINGER_NAMES[i] + ":" + (bent[i] ? "BENT" : "EXT") + "\n";
      bleSend(m);
      Serial.print(">>> "); Serial.print(m.substring(0, m.length() - 1));
      Serial.println(bleConnected ? " [BLE]" : " [local]");
    }
  }

  /* ---- gesture with 3-sample debounce ---- */
  String g = classify(bent, nActive);
  static String lastG = "", candG = "";
  static int nG = 0;
  if (g == candG) nG++; else { candG = g; nG = 1; }
  bool changed = false;
  if (nG >= 3 && candG != lastG) { lastG = candG; changed = true; }

  /* ---- rotation with 4-sample debounce ---- */
  String rot = rotationLabel();
  static String lastRot = "", candRot = "";
  static int nR = 0;
  if (rot == candRot) nR++; else { candRot = rot; nR = 1; }
  if (nR >= 4 && candRot != lastRot && candRot.length()) {
    lastRot = candRot;
    bleSend(String("ROT:") + lastRot + "\n");
    Serial.println(">>> ROT:" + lastRot + (bleConnected ? " [BLE]" : " [local]"));
  }

  /* ---- wrist twist: integrate the gyro about the calibrated axis ---- */
  static unsigned long lastTwistMs = 0;
  static float         lastTwistSent = 0.0f;
  static unsigned long lastTwistTx = 0;

  if (mpuOK) {
    unsigned long nowMs = millis();
    float dt = (nowMs - lastTwistMs) / 1000.0f;
    lastTwistMs = nowMs;
    if (dt > 0.5f) dt = 0.5f;

    sensors_event_t ta, tg, tt;
    mpu.getEvent(&ta, &tg, &tt);
    float rawRate = (twistAxis == 0) ? tg.gyro.x
                  : (twistAxis == 1) ? tg.gyro.y
                                     : tg.gyro.z;
    float rate = (rawRate - gyroBias[twistAxis]) * twistSign * 57.2958f;  // deg/s
    if (fabs(rate) < 3.0f) rate = 0.0f;      // deadband: no drift while still
    twistAngle += rate * dt;
    if (twistAngle >  720.0f) twistAngle =  720.0f;
    if (twistAngle < -720.0f) twistAngle = -720.0f;

    if (fabs(twistAngle - lastTwistSent) >= 1.0f || (millis() - lastTwistTx) > 500) {
      lastTwistSent = twistAngle;
      lastTwistTx = millis();
      char tb[24];
      snprintf(tb, sizeof(tb), "TWIST:%d\n", (int)(twistAngle + (twistAngle >= 0 ? 0.5f : -0.5f)));
      bleSend(String(tb));
      Serial.printf("TWIST %+d deg (%s) rate %.1f deg/s\n",
                    (int)(twistAngle + (twistAngle >= 0 ? 0.5f : -0.5f)),
                    twistAngle >= 0 ? "CW" : "CCW", rate);
    }

    /* ---- lateral sweep: yaw about the gravity axis (hand moves L/R) ----
       A sideways hand sweep changes the hand's HEADING (yaw), not wrist roll,
       so TWIST's calibrated axis cannot see it. Project the bias-corrected
       gyro onto the up vector: yaw rate regardless of how the glove is worn.
       The accel only counts as gravity inside 0.5-1.5 g; during a fast sweep
       the linear accel can leave that band, so the last good up vector is
       held (it barely rotates while the arm stays upright). */
    static float gVec[3] = {0, 0, 1};
    float amag = sqrtf(ta.acceleration.x * ta.acceleration.x +
                       ta.acceleration.y * ta.acceleration.y +
                       ta.acceleration.z * ta.acceleration.z);
    if (amag > 4.9f && amag < 14.7f) {
      gVec[0] = ta.acceleration.x / amag;
      gVec[1] = ta.acceleration.y / amag;
      gVec[2] = ta.acceleration.z / amag;
    }
    float yawRate = ((tg.gyro.x - gyroBias[0]) * gVec[0] +
                     (tg.gyro.y - gyroBias[1]) * gVec[1] +
                     (tg.gyro.z - gyroBias[2]) * gVec[2]) * 57.2958f;  // deg/s
    if (fabs(yawRate) < 3.0f) yawRate = 0.0f;   // deadband: no drift while still
    sweepAngle += yawRate * dt;
    if (sweepAngle >  720.0f) sweepAngle =  720.0f;
    if (sweepAngle < -720.0f) sweepAngle = -720.0f;

    static float         lastSweepSent = 0.0f;
    static unsigned long lastSweepTx = 0;
    if (fabs(sweepAngle - lastSweepSent) >= 1.0f || (millis() - lastSweepTx) > 500) {
      lastSweepSent = sweepAngle;
      lastSweepTx = millis();
      char sb[24];
      snprintf(sb, sizeof(sb), "SWEEP:%d\n", (int)(sweepAngle + (sweepAngle >= 0 ? 0.5f : -0.5f)));
      bleSend(String(sb));
      Serial.printf("SWEEP %+d deg yaw %.1f deg/s\n",
                    (int)(sweepAngle + (sweepAngle >= 0 ? 0.5f : -0.5f)), yawRate);
    }
  }

  /* ---- LED mode: wrist twist = speed dial. Crossing +/-60 deg makes one
     +/-10 speed step; the dial re-arms when the wrist returns near center,
     so holding a twist fires exactly once per flick. ---- */
  if (gCtlMode == CTL_LED && mpuOK) {
    static bool twistArm = true;
    float absTw = fabs(twistAngle);
    if (absTw > 60.0f && twistArm) {
      twistArm = false;
      ledSpeed = (uint8_t)ledClamp((int)ledSpeed + (twistAngle > 0 ? 10 : -10), 0, 100);
      ledWrite(ledR, ledG, ledB);
      Serial.printf("LED: twist dial -> speed %d\n", ledSpeed);
    } else if (absTw < 20.0f) twistArm = true;
  }

  if (changed) {
    strlcpy(g_lastGesture, lastG.c_str(), sizeof(g_lastGesture));
    if (gCtlMode == CTL_LED) ledGestureAction(lastG);
    bleSend("GESTURE:" + lastG + "\n");
    Serial.print(">>> "); Serial.print(lastG);
    if (lastRot.length()) Serial.print(" +" + lastRot);
    Serial.println(bleConnected ? " [BLE]" : " [local]");
#ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, LOW); delay(60); digitalWrite(LED_BUILTIN, HIGH);
#endif
  }

  /* ---- live line every sendEveryMs ---- */
  static unsigned long lastLive = 0;
  if (millis() - lastLive > g_cfg.sendEveryMs) {
    lastLive = millis();
    bleSend(String("FINGERS:") + pct[0] + "," + pct[1] + "," + pct[2] + "," +
            pct[3] + "," + pct[4] + "\n");
    for (int i = 0; i < N_FINGERS; i++) g_livePct[i] = pct[i];
    Serial.printf("T:%d I:%d M:%d R:%d P:%d  raw[%4d %4d %4d %4d %4d]  %s %s\n",
                  bent[0], bent[1], bent[2], bent[3], bent[4],
                  raw[0], raw[1], raw[2], raw[3], raw[4],
                  lastG.c_str(), lastRot.c_str());
  }

  /* ---- music mode: fast finger bitmask for the synth ----
     Finger events must land within a few tens of milliseconds to feel like
     an instrument, so music mode streams its own compact frame instead of
     waiting for the 1.2 s live line. Bit i = finger i closed (bent[] order:
     thumb, index, middle, ring, pinky). */
  static unsigned long lastMus = 0;
  static uint8_t lastMask = 0xFF;
  if (gCtlMode == CTL_MUSIC && bleConnected) {
    uint8_t mask = 0;
    for (int i = 0; i < N_FINGERS; i++) {
      if (bent[i]) mask |= (1 << i);
    }
    if (mask != lastMask || millis() - lastMus > 400) {
      lastMask = mask;
      lastMus = millis();
      bleSend(String("MUS:") + mask + "\n");
    }
  }

  static unsigned long lb = 0;
  static bool led = false;
  unsigned long iv = (gCtlMode == CTL_LED) ? 150 : (bleConnected ? 1200 : 250);
  if (millis() - lb > iv) { lb = millis(); led = !led; digitalWrite(LED_BUILTIN, led ? LOW : HIGH); }

  k1ScanPoll();
  delay(20);
}
