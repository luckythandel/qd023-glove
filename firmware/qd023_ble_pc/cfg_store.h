#pragma once
#include <Arduino.h>
#include <Preferences.h>

// Glove settings persisted in NVS and shared with the PC bridge. The percent
// bend thresholds live here so the on-board portal and the bridge agree on one
// source of truth. bendFraction/minTravel are applied to the live measurement;
// twistAxis/twistSign default to "auto" (calibration decides) and only override
// when set to a real value.
struct GloveCfg {
  uint8_t  onPct[5];
  uint8_t  offPct[5];
  float    bendFraction;
  uint16_t minTravel;
  float    twistPauseDps;
  float    twistResumeDps;
  int8_t   twistAxis;
  float    twistSign;
  char     deviceName[24];
  uint16_t sendEveryMs;
  uint16_t ledSlowMs;
  uint16_t ledFastMs;
  char     wifiSsid[33];   // home network the glove joins as a client (2.4 GHz only)
  char     wifiPass[64];
  char     apPass[64];     // its own hotspot password
  bool     wifiEn;         // auto-join the home network on boot
};

GloveCfg g_cfg;

static const uint8_t CFG_DEF_ON[5]  = {56, 46, 46, 46, 46};
static const uint8_t CFG_DEF_OFF[5] = {30, 18, 18, 18, 18};
#define CFG_AP_PASS_DEF "glove1234"

// One NVS blob instead of ~20 keys: a calibration is all-or-nothing, so a
// torn write must invalidate the whole snapshot rather than leave a mix of
// old and new values that would silently misclassify fingers.
struct CalBlob {
  uint16_t magic;
  uint8_t  ver;
  uint8_t  flags;       // bit0 valid, bit1 oriOK
  int16_t  open[5];
  int16_t  fist[5];
  int16_t  thresh[5];
  int16_t  deadb[5];
  uint8_t  inverted;    // per-finger bitmask
  uint8_t  active;      // per-finger bitmask
  int8_t   twistAxis;
  uint8_t  pad;
  float    twistSign;
  float    palm[3];
  float    back[3];
  float    gyro[3];
  uint32_t sum;
};

#define CAL_MAGIC   0xCA11
#define CAL_VER     1
#define CAL_F_VALID 0x01
#define CAL_F_ORI   0x02

static uint32_t calChecksum(const CalBlob* b) {
  const uint8_t* p = (const uint8_t*)b;
  uint32_t s = 0;
  for (size_t i = 0; i < offsetof(CalBlob, sum); i++) s = s * 31u + p[i];
  return s;
}

static bool calBlobSave(const CalBlob& b) {
  Preferences p;
  if (!p.begin("glove", false)) return false;
  size_t n = p.putBytes("cal", &b, sizeof(b));
  p.end();
  return n == sizeof(b);
}

static bool calBlobLoad(CalBlob& b) {
  Preferences p;
  if (!p.begin("glove", true)) return false;
  size_t n = p.getBytes("cal", &b, sizeof(b));
  p.end();
  if (n != sizeof(b)) return false;
  if (b.magic != CAL_MAGIC || b.ver != CAL_VER) return false;
  return b.sum == calChecksum(&b);
}

static void calBlobErase() {
  Preferences p;
  if (!p.begin("glove", false)) return;
  p.remove("cal");
  p.end();
}

static void cfgDefaults(GloveCfg& c) {
  for (int i = 0; i < 5; i++) {
    c.onPct[i]  = CFG_DEF_ON[i];
    c.offPct[i] = CFG_DEF_OFF[i];
  }
  c.bendFraction   = 0.32f;
  c.minTravel      = 80;
  c.twistPauseDps  = 58.0f;
  c.twistResumeDps = 60.0f;
  c.twistAxis      = -1;
  c.twistSign      = 0.0f;
  strncpy(c.deviceName, "QD023-Glove", sizeof(c.deviceName) - 1);
  c.deviceName[sizeof(c.deviceName) - 1] = '\0';
  c.sendEveryMs = 600;
  c.ledSlowMs   = 900;
  c.ledFastMs   = 350;
  c.wifiSsid[0] = '\0';
  c.wifiPass[0] = '\0';
  strncpy(c.apPass, CFG_AP_PASS_DEF, sizeof(c.apPass) - 1);
  c.apPass[sizeof(c.apPass) - 1] = '\0';
  c.wifiEn = false;
}

static void cfgLoad(GloveCfg& c) {
  cfgDefaults(c);
  Preferences p;
  if (!p.begin("glove", true)) return;
  char k[8];
  for (int i = 0; i < 5; i++) {
    snprintf(k, sizeof(k), "on%d", i);
    c.onPct[i] = p.getUChar(k, c.onPct[i]);
    snprintf(k, sizeof(k), "off%d", i);
    c.offPct[i] = p.getUChar(k, c.offPct[i]);
  }
  c.bendFraction   = p.getFloat("bf", c.bendFraction);
  c.minTravel      = p.getUShort("mt", c.minTravel);
  c.twistPauseDps  = p.getFloat("tp", c.twistPauseDps);
  c.twistResumeDps = p.getFloat("tr", c.twistResumeDps);
  c.twistAxis      = p.getChar("ta", c.twistAxis);
  c.twistSign      = p.getFloat("ts", c.twistSign);
  c.sendEveryMs    = p.getUShort("sem", c.sendEveryMs);
  c.ledSlowMs      = p.getUShort("ls", c.ledSlowMs);
  c.ledFastMs      = p.getUShort("lf", c.ledFastMs);
  String nm = p.getString("name", String(c.deviceName));
  strncpy(c.deviceName, nm.c_str(), sizeof(c.deviceName) - 1);
  c.deviceName[sizeof(c.deviceName) - 1] = '\0';

  String ws = p.getString("wsid", "");
  strncpy(c.wifiSsid, ws.c_str(), sizeof(c.wifiSsid) - 1);
  c.wifiSsid[sizeof(c.wifiSsid) - 1] = '\0';
  String wp = p.getString("wpass", "");
  strncpy(c.wifiPass, wp.c_str(), sizeof(c.wifiPass) - 1);
  c.wifiPass[sizeof(c.wifiPass) - 1] = '\0';
  String ap = p.getString("appass", String(c.apPass));
  strncpy(c.apPass, ap.c_str(), sizeof(c.apPass) - 1);
  c.apPass[sizeof(c.apPass) - 1] = '\0';
  c.wifiEn = p.getBool("wifiEn", c.wifiEn);
  p.end();
}

static void cfgSave(const GloveCfg& c) {
  Preferences p;
  if (!p.begin("glove", false)) return;
  char k[8];
  for (int i = 0; i < 5; i++) {
    snprintf(k, sizeof(k), "on%d", i);
    p.putUChar(k, c.onPct[i]);
    snprintf(k, sizeof(k), "off%d", i);
    p.putUChar(k, c.offPct[i]);
  }
  p.putFloat("bf", c.bendFraction);
  p.putUShort("mt", c.minTravel);
  p.putFloat("tp", c.twistPauseDps);
  p.putFloat("tr", c.twistResumeDps);
  p.putChar("ta", c.twistAxis);
  p.putFloat("ts", c.twistSign);
  p.putUShort("sem", c.sendEveryMs);
  p.putUShort("ls", c.ledSlowMs);
  p.putUShort("lf", c.ledFastMs);
  p.putString("name", c.deviceName);
  p.putString("wsid", c.wifiSsid);
  p.putString("wpass", c.wifiPass);
  p.putString("appass", c.apPass);
  p.putBool("wifiEn", c.wifiEn);
  p.end();
}

// Passwords are never echoed back; the UI only learns whether one is set.
static String cfgToJson(const GloveCfg& c) {
  char b[640];
  snprintf(b, sizeof(b),
    "{\"on\":[%u,%u,%u,%u,%u],\"off\":[%u,%u,%u,%u,%u],"
    "\"bf\":%.3f,\"mt\":%u,\"tp\":%.1f,\"tr\":%.1f,\"ta\":%d,\"ts\":%.1f,"
    "\"name\":\"%s\",\"sem\":%u,\"ls\":%u,\"lf\":%u,"
    "\"wsid\":\"%s\",\"wifiEn\":%s,\"hasWpass\":%s,\"hasApass\":%s}",
    c.onPct[0], c.onPct[1], c.onPct[2], c.onPct[3], c.onPct[4],
    c.offPct[0], c.offPct[1], c.offPct[2], c.offPct[3], c.offPct[4],
    c.bendFraction, c.minTravel, c.twistPauseDps, c.twistResumeDps,
    (int)c.twistAxis, c.twistSign, c.deviceName,
    c.sendEveryMs, c.ledSlowMs, c.ledFastMs,
    c.wifiSsid,
    c.wifiEn ? "true" : "false",
    c.wifiPass[0] ? "true" : "false",
    c.apPass[0] ? "true" : "false");
  return String(b);
}

// Tolerant flat scanner over the fixed self-produced schema; avoids adding a
// JSON library dependency for a handful of scalars and a 5-element array.
static bool jsonNum(const String& j, const char* key, float& out) {
  int k = j.indexOf(String("\"") + key + "\"");
  if (k < 0) return false;
  int c = j.indexOf(':', k);
  if (c < 0) return false;
  out = j.substring(c + 1).toFloat();
  return true;
}

static bool jsonStr(const String& j, const char* key, char* out, size_t n) {
  int k = j.indexOf(String("\"") + key + "\"");
  if (k < 0) return false;
  int q1 = j.indexOf('"', j.indexOf(':', k));
  if (q1 < 0) return false;
  int q2 = j.indexOf('"', q1 + 1);
  if (q2 < 0) return false;
  String v = j.substring(q1 + 1, q2);
  strncpy(out, v.c_str(), n - 1);
  out[n - 1] = '\0';
  return true;
}

static void jsonArr5(const String& j, const char* key, uint8_t out[5]) {
  int k = j.indexOf(String("\"") + key + "\"");
  if (k < 0) return;
  int lb = j.indexOf('[', k), rb = j.indexOf(']', lb);
  if (lb < 0 || rb < 0) return;
  String body = j.substring(lb + 1, rb);
  int start = 0;
  for (int i = 0; i < 5; i++) {
    int comma = body.indexOf(',', start);
    String tok = (comma < 0) ? body.substring(start) : body.substring(start, comma);
    long v = tok.toInt();
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    out[i] = (uint8_t)v;
    if (comma < 0) break;
    start = comma + 1;
  }
}

static void cfgFromJson(const String& j, GloveCfg& c) {
  jsonArr5(j, "on", c.onPct);
  jsonArr5(j, "off", c.offPct);
  float f;
  if (jsonNum(j, "bf", f)) c.bendFraction = f;
  if (jsonNum(j, "mt", f)) c.minTravel = (uint16_t)f;
  if (jsonNum(j, "tp", f)) c.twistPauseDps = f;
  if (jsonNum(j, "tr", f)) c.twistResumeDps = f;
  if (jsonNum(j, "ta", f)) c.twistAxis = (int8_t)f;
  if (jsonNum(j, "ts", f)) c.twistSign = f;
  if (jsonNum(j, "sem", f)) c.sendEveryMs = (uint16_t)f;
  if (jsonNum(j, "ls", f)) c.ledSlowMs = (uint16_t)f;
  if (jsonNum(j, "lf", f)) c.ledFastMs = (uint16_t)f;
  jsonStr(j, "name", c.deviceName, sizeof(c.deviceName));

  // Same rule as the passwords: an empty SSID means "keep what is stored", so
  // pressing Save with the dropdown still on "-- pick a network --" cannot
  // silently forget the home network.
  char tmp[64];
  if (jsonStr(j, "wsid", tmp, sizeof(tmp)) && tmp[0]) {
    strncpy(c.wifiSsid, tmp, sizeof(c.wifiSsid) - 1);
    c.wifiSsid[sizeof(c.wifiSsid) - 1] = '\0';
  }

  // Empty password field means "leave the stored one alone", so a save from
  // the UI (which never receives the password) cannot wipe it.
  if (jsonStr(j, "wpass", tmp, sizeof(tmp)) && tmp[0]) {
    strncpy(c.wifiPass, tmp, sizeof(c.wifiPass) - 1);
    c.wifiPass[sizeof(c.wifiPass) - 1] = '\0';
  }
  if (jsonStr(j, "appass", tmp, sizeof(tmp)) && strlen(tmp) >= 8) {
    strncpy(c.apPass, tmp, sizeof(c.apPass) - 1);
    c.apPass[sizeof(c.apPass) - 1] = '\0';
  }
  if (jsonNum(j, "wifiEn", f)) c.wifiEn = (f != 0.0f);
}
