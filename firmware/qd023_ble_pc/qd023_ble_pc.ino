/*
  QD023 Glove -> PC controller  (v2, calibration rebuilt)
  -------------------------------------------------------
  Reference: ACEBOTT tutorial ACB_Gloves (potentiometer pins + >200 = bent logic)
  Pin map verified against THIS glove's live readings:
      Thumb=G36  Index=G39  Middle=G34  Ring=G35  Pinky=G32
  (G33 was used earlier and is an UNCONNECTED pin on this glove - it idles
   around 604 and never follows the thumb. The vendor map is the correct one.)

  Detection model (from the tutorial, made adaptive):
    - tutorial uses a FIXED threshold of 200 with "bent = value above it".
      That works because every finger rests near 0 and rises above 500.
    - here the threshold is placed at ~32% of the measured travel from the
      OPEN position (not the midpoint), so a light bend already registers,
      and the whole thing is re-measured for YOUR fingers.
    - polarity is auto-detected (Thumb reads HIGH when open, LOW when bent;
      the other four read LOW when open, HIGH when bent). No hardcoding.

  BLE (Nordic UART Service) - unchanged, so pc_receiver.py still works:
    Service 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
    TX  6E400003-...  notify (glove -> PC)
    RX  6E400002-...  write  (PC -> glove, "CAL" = recalibrate)
  Messages: READY / GESTURE: / FINGERS: / FINGER:<name>:BENT|EXT / ROT:
  Serial: 'c' recalibrate, 'd' toggle raw pin diagnostics, 'h' help
*/

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define FW_VERSION "2.0"

#define DEVICE_NAME "QD023-Glove"
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

/* ---------------- finger configuration ---------------- */

#define N_FINGERS 5

const int  FINGER_PINS[N_FINGERS]   = {36, 39, 34, 35, 32};
const char* FINGER_NAMES[N_FINGERS] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};

// seeded from the vendor tutorial map (ACB_Gloves: 36/39/34/35/32);
// recalibrated on every boot. G33 is an unconnected pin on this glove.
int  openVal[N_FINGERS] = {  0,  40,   0,   0,   0};
int  fistVal[N_FINGERS] = {600, 750, 540, 950, 830};
int  thresh[N_FINGERS]  = {192, 289, 173, 316, 265};
int  deadb[N_FINGERS]   = { 60,  60,  60,  60,  60};
bool inverted[N_FINGERS]= {false,false,false,false,false};
bool active[N_FINGERS]  = {true, true, true, true, true};

// fraction of travel from OPEN at which a finger counts as bent.
// tutorial's fixed 200 on a 0..1000 swing is ~20-35% -> this matches it.
#define BEND_FRACTION 0.32f
#define MIN_TRAVEL    120     // below this the sensor is considered unusable
#define REST_TRAVEL   60      // tutorial's "NO_TRAVEL" guard for empty runs

/* ---------------- rotation ---------------- */

float palmVec[3] = {0, 0, 1};
float backVec[3] = {0, 0, -1};
bool  oriOK = false;

Adafruit_MPU6050 mpu;
bool mpuOK = false;

/* ---------------- BLE ---------------- */

BLEServer* pServer = nullptr;
BLECharacteristic* pTx = nullptr;
bool bleConnected = false;

void doCalibration();
void printHelp();

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    bleConnected = true;
    Serial.println("BLE PC connected");
  }
  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    Serial.println("BLE PC disconnected - advertising");
    BLEDevice::startAdvertising();
  }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = String(c->getValue().c_str());
    v.trim();
    Serial.println("BLE RX: " + v);
    if (v.equalsIgnoreCase("CAL")) doCalibration();
  }
};

void bleSend(const String& s) {
  if (bleConnected && pTx) {
    pTx->setValue((uint8_t*)s.c_str(), s.length());
    pTx->notify();
  }
}

/* ---------------- helpers ---------------- */

int readAvg(int pin, int n = 10) {
  long s = 0;
  for (int k = 0; k < n; k++) { s += analogRead(pin); delay(3); }
  return (int)(s / n);
}

int lean(int lo, int hi, int pct) { return lo + (hi - lo) * pct / 100; }

// 0..100 = how far this finger has travelled from OPEN towards FIST,
// clamped so a value sitting on either end never goes out of range.
int bentPct(int i, int raw) {
  int a = inverted[i] ? fistVal[i] : openVal[i];
  int b = inverted[i] ? openVal[i] : fistVal[i];
  if (b <= a) return 0;
  int p = (raw - a) * 100 / (b - a);
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

/* ---------------- calibration ---------------- */

static void sampleOpen(int out[N_FINGERS]) {
  long sum[N_FINGERS] = {0};
  const int N = 40;
  for (int s = 0; s < N; s++) {
    for (int i = 0; i < N_FINGERS; i++) sum[i] += analogRead(FINGER_PINS[i]);
    delay(50);
  }
  for (int i = 0; i < N_FINGERS; i++) out[i] = (int)(sum[i] / N);
}

void doCalibration() {
  Serial.println();
  Serial.println("=== CALIBRATION v" FW_VERSION " (per finger, adaptive) ===");

  /* ---- stage A: OPEN ---- */
  Serial.println("Step 0: hand FLAT and relaxed, fingers STRAIGHT, in 3s...");
  delay(3000);
  int ext[N_FINGERS];
  sampleOpen(ext);
  Serial.printf("OPEN baseline: T=%d I=%d M=%d R=%d P=%d\n",
                ext[0], ext[1], ext[2], ext[3], ext[4]);

  /* ---- stage B: one finger at a time ---- */
  for (int f = 0; f < N_FINGERS; f++) {
    Serial.printf("Step %d/5: bend ONLY %s to a COMFORTABLE position, hold it, in 3s...\n",
                  f + 1, FINGER_NAMES[f]);
    delay(3000);

    int mn = 4095, mx = 0;
    long sum = 0;
    const int N = 40;
    for (int s = 0; s < N; s++) {
      int v = analogRead(FINGER_PINS[f]);
      if (v < mn) mn = v;
      if (v > mx) mx = v;
      sum += v;
      if (s % 8 == 0) Serial.printf("   %s = %4d\n", FINGER_NAMES[f], v);
      delay(50);
    }
    int med = (int)(sum / N);
    int travel = abs(med - ext[f]);
    int range  = mx - mn;

    // reject a window the user slept through, or a dead wire
    if (range < 40 || travel < REST_TRAVEL) {
      Serial.printf(" %s: NO_TRAVEL (open=%d bent=%d range=%d) -> KEEP_OLD thr=%d %s\n",
                    FINGER_NAMES[f], ext[f], med, range, thresh[f],
                    active[f] ? "still-OK" : "still-OFF");
      Serial.println("   back to FLAT...");
      delay(1200);
      continue;
    }
    if (travel < MIN_TRAVEL) {
      active[f] = false;
      Serial.printf(" %s: WEAK (travel=%d < %d) -> marked INACTIVE\n",
                    FINGER_NAMES[f], travel, MIN_TRAVEL);
      Serial.println("   back to FLAT...");
      delay(1200);
      continue;
    }

    active[f] = true;
    openVal[f]  = ext[f];
    fistVal[f]  = med;

    bool inv = (med < ext[f]);          // bent reads BELOW open -> inverted
    inverted[f] = inv;

    // threshold near the OPEN end, like the tutorial's fixed 200
    int t = inv ? (ext[f] - (int)((ext[f] - med) * BEND_FRACTION))
                : (ext[f] + (int)((med - ext[f]) * BEND_FRACTION));
    thresh[f] = t;

    int db = travel * 12 / 100;
    if (db < 15) db = 15;
    if (db > 60) db = 60;
    deadb[f] = db;

    Serial.printf(" %s: open=%d bent=%d travel=%d thr=%d db=%d %s OK\n",
                  FINGER_NAMES[f], openVal[f], fistVal[f], travel,
                  thresh[f], deadb[f], inv ? "(bent=LOW)" : "(bent=HIGH)");
    Serial.println("   back to FLAT...");
    delay(1200);
  }

  /* ---- stage C: rotation ---- */
  Serial.println("Rotation: hold hand FLAT, PALM towards you, in 3s...");
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

  Serial.println("=== calibration done. 'c' to redo, 'd' for raw diagnostics ===");
  Serial.println();
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

  BLEDevice::init(DEVICE_NAME);
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

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising as " DEVICE_NAME " (NUS)");

  doCalibration();
  bleSend("READY\n");
}

void printHelp() {
  Serial.println("commands:  c = recalibrate    d = toggle raw pin dump    h = help");
}

/* ---------------- main loop ---------------- */

void loop() {
  static bool rawDump = false;

  /* ---- serial console ---- */
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'c' || c == 'C') doCalibration();
    else if (c == 'd' || c == 'D') { rawDump = !rawDump; Serial.printf("raw dump %s\n", rawDump ? "ON" : "OFF"); }
    else if (c == 'h' || c == 'H') printHelp();
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

  if (changed) {
    bleSend("GESTURE:" + lastG + "\n");
    Serial.print(">>> "); Serial.print(lastG);
    if (lastRot.length()) Serial.print(" +" + lastRot);
    Serial.println(bleConnected ? " [BLE]" : " [local]");
#ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, HIGH); delay(60); digitalWrite(LED_BUILTIN, LOW);
#endif
  }

  /* ---- live line every 600ms ---- */
  static unsigned long lastLive = 0;
  if (millis() - lastLive > 600) {
    lastLive = millis();
    bleSend(String("FINGERS:") + pct[0] + "," + pct[1] + "," + pct[2] + "," +
            pct[3] + "," + pct[4] + "\n");
    Serial.printf("T:%d I:%d M:%d R:%d P:%d  raw[%4d %4d %4d %4d %4d]  %s %s\n",
                  bent[0], bent[1], bent[2], bent[3], bent[4],
                  raw[0], raw[1], raw[2], raw[3], raw[4],
                  lastG.c_str(), lastRot.c_str());
  }

#ifdef LED_BUILTIN
  static unsigned long lb = 0;
  static bool led = false;
  unsigned long iv = bleConnected ? 900 : 350;
  if (millis() - lb > iv) { lb = millis(); led = !led; digitalWrite(LED_BUILTIN, led); }
#endif

  delay(120);
}
