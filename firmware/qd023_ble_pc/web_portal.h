#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "cfg_store.h"

// The hotspot never goes away: a wrong home-WiFi password must not lock you
// out of the panel, so client mode is additive (AP+STA) instead of a mode swap.
extern bool  bleConnected;
extern float twistAngle;
extern float sweepAngle;
extern char  g_scanHit[32];   // K1 pin-scanner edge latch (fw 2.7 diagnostic)
extern bool  g_stripLink;     // wireless LED strip BLE link (fw 2.9)
extern char  g_stripName[24]; // advertised strip name (fw 3.0, variant check)
extern StripState g_stripState; // 0 idle 1 scanning 2 connecting 3 connected
extern uint8_t g_stripFails;  // connect-fail/drop counter
extern uint8_t ledRawMode;    // raw DMX effect code (portal dropdown); 0 = glove modes
extern uint8_t ledDir;        // effect direction: 0 forward, 1 backward
extern char    g_lastGesture[20]; // last classified pose (gestures page)

int  g_livePct[5]      = {0, 0, 0, 0, 0};
bool g_recalRequested  = false;
static unsigned long g_rebootAt = 0;
static bool g_updateOK = false;
static volatile bool g_otaInProgress = false;

WebServer webServer(80);

// ---- home-WiFi client state -------------------------------------------------
static uint8_t  g_wifiState  = 0;      // 0 idle, 1 joining, 2 joined, 3 failed
static unsigned long g_wifiT0 = 0;
static bool     g_wifiReq    = false;
static char     g_wifiReqSsid[33];
static char     g_wifiReqPass[64];
#define WIFI_JOIN_TIMEOUT_MS 15000UL

static void wifiRequestJoin(const char* s, const char* p) {
  strncpy(g_wifiReqSsid, s, sizeof(g_wifiReqSsid) - 1);
  g_wifiReqSsid[sizeof(g_wifiReqSsid) - 1] = '\0';
  strncpy(g_wifiReqPass, p, sizeof(g_wifiReqPass) - 1);
  g_wifiReqPass[sizeof(g_wifiReqPass) - 1] = '\0';
  g_wifiReq   = true;
  g_wifiState = 1;
  g_wifiT0    = millis();
}

static void wifiService() {
  if (g_wifiReq) {
    g_wifiReq = false;
    WiFi.begin(g_wifiReqSsid, g_wifiReqPass);
  }
  if (g_wifiState == 1) {
    if (WiFi.status() == WL_CONNECTED) g_wifiState = 2;
    else if (millis() - g_wifiT0 > WIFI_JOIN_TIMEOUT_MS) g_wifiState = 3;
  }
}

// ---- calibration snapshot <-> live globals ---------------------------------
// doCalibration() fills the globals; these move them in and out of NVS so a
// stored calibration can be reused instead of redoing the two poses on boot.
static void calPersist() {
  CalBlob b;
  memset(&b, 0, sizeof(b));
  b.magic = CAL_MAGIC;
  b.ver   = CAL_VER;
  b.flags = CAL_F_VALID | (oriOK ? CAL_F_ORI : 0);
  for (int i = 0; i < 5; i++) {
    b.open[i]   = (int16_t)openVal[i];
    b.fist[i]   = (int16_t)fistVal[i];
    b.thresh[i] = (int16_t)thresh[i];
    b.deadb[i]  = (int16_t)deadb[i];
    if (inverted[i]) b.inverted |= (uint8_t)(1 << i);
    if (active[i])   b.active   |= (uint8_t)(1 << i);
  }
  b.twistAxis = (int8_t)twistAxis;
  b.twistSign = twistSign;
  for (int i = 0; i < 3; i++) {
    b.palm[i] = palmVec[i];
    b.back[i] = backVec[i];
    b.gyro[i] = gyroBias[i];
  }
  b.sum = calChecksum(&b);
  Serial.printf("cal: saved snapshot %s\n", calBlobSave(b) ? "OK" : "FAILED");
}

static bool calRestore() {
  CalBlob b;
  if (!calBlobLoad(b)) return false;
  if (!(b.flags & CAL_F_VALID)) return false;
  for (int i = 0; i < 5; i++) {
    openVal[i]  = b.open[i];
    fistVal[i]  = b.fist[i];
    thresh[i]   = b.thresh[i];
    deadb[i]    = b.deadb[i];
    inverted[i] = (b.inverted >> i) & 1;
    active[i]   = (b.active >> i) & 1;
  }
  twistAxis = b.twistAxis;
  twistSign = b.twistSign;
  for (int i = 0; i < 3; i++) {
    palmVec[i] = b.palm[i];
    backVec[i] = b.back[i];
    gyroBias[i] = b.gyro[i];
  }
  oriOK = (b.flags & CAL_F_ORI) != 0;
  Serial.println("cal: restored stored snapshot (skipping poses)");
  return true;
}

static String calJson() {
  CalBlob probe;
  bool stored = calBlobLoad(probe);

  // Report the snapshot when one exists and live RAM otherwise, so the badge
  // and the numbers can never disagree after a recalibration that was not saved.
  int      o[5], f[5], t[5], d[5];
  float    g[3];
  uint8_t  act = 0, inv = 0;
  bool     cOri  = oriOK;
  int      cAxis = twistAxis;
  float    cSign = twistSign;

  if (stored) {
    for (int i = 0; i < 5; i++) {
      o[i] = probe.open[i];  f[i] = probe.fist[i];
      t[i] = probe.thresh[i]; d[i] = probe.deadb[i];
    }
    for (int i = 0; i < 3; i++) g[i] = probe.gyro[i];
    act   = probe.active;
    inv   = probe.inverted;
    cOri  = (probe.flags & CAL_F_ORI) != 0;
    cAxis = probe.twistAxis;
    cSign = probe.twistSign;
  } else {
    for (int i = 0; i < 5; i++) {
      o[i] = openVal[i]; f[i] = fistVal[i];
      t[i] = thresh[i];  d[i] = deadb[i];
      if (active[i])   act |= (1u << i);
      if (inverted[i]) inv |= (1u << i);
    }
    for (int i = 0; i < 3; i++) g[i] = gyroBias[i];
  }

  char b[512];
  int n = snprintf(b, sizeof(b),
    "{\"stored\":%s,\"oriOK\":%s,\"axis\":%d,\"sign\":%.0f,"
    "\"gyro\":[%.3f,%.3f,%.3f],\"open\":[%d,%d,%d,%d,%d],"
    "\"fist\":[%d,%d,%d,%d,%d],\"thresh\":[%d,%d,%d,%d,%d],"
    "\"deadb\":[%d,%d,%d,%d,%d],\"active\":[%d,%d,%d,%d,%d],"
    "\"inv\":[%d,%d,%d,%d,%d]}",
    stored ? "true" : "false",
    cOri ? "true" : "false", cAxis, cSign,
    g[0], g[1], g[2],
    o[0], o[1], o[2], o[3], o[4],
    f[0], f[1], f[2], f[3], f[4],
    t[0], t[1], t[2], t[3], t[4],
    d[0], d[1], d[2], d[3], d[4],
    (act >> 0) & 1, (act >> 1) & 1, (act >> 2) & 1, (act >> 3) & 1, (act >> 4) & 1,
    (inv >> 0) & 1, (inv >> 1) & 1, (inv >> 2) & 1, (inv >> 3) & 1, (inv >> 4) & 1);
  if (n < 0) return String("{}");
  return String(b);
}

static const char CFG_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>QD023 GLOVE // CONTROL DECK</title><style>
:root{
  --bg:#060d09; --paper:#0c1711; --ink:#e6fff0; --deep:#040a06; --edge:#1b3a29;
  --pink:#22c55e; --lime:#7dffb2; --cyan:#14b8a6;
  --purple:#0f7a4a; --yellow:#a3e635; --orange:#16a34a; --muted:#7f9c8b;
}
*{box-sizing:border-box}
html,body{margin:0;padding:0}
body{
  font-family:"Trebuchet MS","Segoe UI",system-ui,sans-serif;
  color:var(--ink);padding:18px 14px 60px;max-width:820px;margin:0 auto;
  background:
    radial-gradient(1100px 560px at 12% -10%,rgba(34,197,94,.16),transparent 62%),
    radial-gradient(900px 500px at 92% 112%,rgba(20,184,166,.13),transparent 62%),
    repeating-linear-gradient(45deg,rgba(125,255,178,.04) 0 12px,transparent 12px 24px),
    var(--bg);
  background-attachment:fixed;
  min-height:100vh;
}
h1{
  font-size:clamp(26px,7vw,42px);line-height:.95;margin:0 0 6px;
  text-transform:uppercase;letter-spacing:-1px;font-weight:900;
  color:var(--lime);
  text-shadow:0 0 20px rgba(125,255,178,.45),3px 3px 0 #0b2a18;
  transform:rotate(-1.5deg);
}
.tag{
  display:inline-block;background:var(--deep);color:var(--lime);
  font-weight:900;font-size:11px;letter-spacing:2px;text-transform:uppercase;
  padding:5px 12px;border-radius:999px;transform:rotate(1.5deg);margin-bottom:16px;
  border:2px solid var(--pink);box-shadow:0 0 14px rgba(34,197,94,.35);
}
.card{
  background:linear-gradient(180deg,#0e1c14,#0a1410);
  border:2px solid var(--edge);border-radius:20px;
  padding:16px;margin:16px 0;
  box-shadow:0 0 0 1px rgba(125,255,178,.06),0 10px 28px rgba(0,0,0,.55);
}
.card:nth-of-type(odd){border-color:#1b4a32}
.card:nth-of-type(even){border-color:#173f2b}
.card h2{
  font-size:13px;margin:0 0 14px;font-weight:900;text-transform:uppercase;
  letter-spacing:2px;display:flex;align-items:center;gap:9px;color:var(--lime);
}
.card h2::before{
  content:"";width:14px;height:14px;border-radius:4px;background:var(--pink);
  border:2px solid #0b2a18;flex:none;box-shadow:0 0 10px rgba(34,197,94,.7);
}
.row{display:flex;align-items:center;gap:10px;margin:11px 0}
.row label{width:112px;font-size:13px;font-weight:800;flex:none;text-transform:uppercase;letter-spacing:.4px;color:#bcd9c8}
input[type=range]{
  flex:1;min-width:0;-webkit-appearance:none;appearance:none;height:14px;
  background:#0a2a1b;border:2px solid var(--edge);border-radius:999px;
}
input[type=range]::-webkit-slider-thumb{
  -webkit-appearance:none;width:26px;height:26px;border-radius:50%;
  background:var(--yellow);border:2px solid #062012;cursor:pointer;
  box-shadow:0 0 10px rgba(163,230,53,.75);
}
input[type=range]::-moz-range-thumb{
  width:22px;height:22px;border-radius:50%;background:var(--yellow);
  border:2px solid #062012;cursor:pointer;
}
input[type=text],input[type=number],input[type=password],select{
  flex:1;min-width:0;padding:11px 12px;border-radius:12px;border:2px solid var(--edge);
  background:var(--deep);color:var(--ink);font-size:14px;font-weight:700;
  font-family:inherit;
}
input::placeholder{color:#4d6b5a}
input[type=checkbox]{
  appearance:none;-webkit-appearance:none;flex:none;width:26px;height:26px;
  border:2px solid var(--edge);border-radius:8px;background:var(--deep);
  cursor:pointer;position:relative;transition:background .12s,border-color .12s;
}
input[type=checkbox]:checked{background:var(--lime);border-color:#0b2a18;
  box-shadow:0 0 10px rgba(125,255,178,.5)}
input[type=checkbox]:checked::after{
  content:"";position:absolute;left:8px;top:3px;width:6px;height:12px;
  border:solid #04150b;border-width:0 3px 3px 0;transform:rotate(45deg);
}
input:focus,select:focus{outline:none;border-color:var(--lime);box-shadow:0 0 0 3px rgba(125,255,178,.22)}
.val{width:38px;text-align:right;font-weight:900;font-size:13px;flex:none;font-variant-numeric:tabular-nums;color:var(--lime)}
button{
  background:var(--lime);color:#04150b;border:2px solid #0b2a18;
  border-radius:12px;padding:12px 18px;font-weight:900;font-size:13px;
  text-transform:uppercase;letter-spacing:1px;cursor:pointer;
  box-shadow:0 4px 0 #0b2a18,0 0 14px rgba(125,255,178,.22);margin:4px 8px 4px 0;font-family:inherit;
  transition:transform .06s,box-shadow .06s,filter .12s;
}
button:hover{filter:brightness(1.08)}
button:active{transform:translateY(4px);box-shadow:0 0 0 #0b2a18}
button.pink{background:var(--pink);color:#04150b}
button.cyan{background:var(--cyan);color:#eafff4}
button.ghost{background:#0a1a11;color:var(--lime);border-color:#1d4a32}
.bar{height:16px;background:var(--deep);border:2px solid var(--edge);border-radius:999px;overflow:hidden;flex:1}
.bar>i{display:block;height:100%;width:0;transition:width .12s;
  background:linear-gradient(90deg,var(--orange),var(--lime));
  box-shadow:0 0 12px rgba(125,255,178,.5)}
.pill{display:inline-block;padding:5px 12px;border-radius:999px;font-size:11px;
  font-weight:900;letter-spacing:1px;text-transform:uppercase;border:2px solid var(--edge);color:#04150b}
.on{background:var(--lime);border-color:#0b2a18}
.off{background:#1a2b22;color:#8fb3a0;border-color:#24402f}
.hint{font-size:12px;line-height:1.55;color:var(--muted);font-weight:600;margin-top:10px}
table{width:100%;border-collapse:collapse;font-size:12px;font-weight:700}
th,td{padding:7px 5px;text-align:right;border-bottom:1px dashed rgba(125,255,178,.18)}
th:first-child,td:first-child{text-align:left}
th{font-size:10px;text-transform:uppercase;letter-spacing:1px;color:#9fc4ad}
td.dead{color:#4a6353;text-decoration:line-through}
pre{
  margin:12px 0 0;padding:12px;font-size:11px;font-weight:700;background:var(--deep);
  color:var(--lime);border-radius:12px;white-space:pre-wrap;word-break:break-word;
  max-height:150px;overflow:auto;border:2px solid var(--edge);
}
.badge{background:var(--yellow);color:#04150b;border:2px solid #0b2a18;border-radius:999px;
  padding:3px 9px;font-size:10px;font-weight:900;letter-spacing:1px;margin-left:6px}
.num{flex:none;width:54px;text-align:right;font-size:12px;font-weight:900;
  font-variant-numeric:tabular-nums;color:var(--lime);letter-spacing:.3px}
</style></head><body>

<h1>QD023 Glove</h1>
<div class="tag">Control Deck // v2.2</div>

<div class="card"><h2>Live telemetry</h2>
<div class="row"><label>Link</label><span id="bles" class="pill off">-</span>
<span style="margin-left:auto;font-weight:900;font-size:13px">twist <b id="tw">0</b>&deg;</span></div>
<div class="row"><label>Thumb</label><div class="bar"><i id="b0"></i></div><span class="num" id="n0">0%</span></div>
<div class="row"><label>Index</label><div class="bar"><i id="b1"></i></div><span class="num" id="n1">0%</span></div>
<div class="row"><label>Middle</label><div class="bar"><i id="b2"></i></div><span class="num" id="n2">0%</span></div>
<div class="row"><label>Ring</label><div class="bar"><i id="b3"></i></div><span class="num" id="n3">0%</span></div>
<div class="row"><label>Pinky</label><div class="bar"><i id="b4"></i></div><span class="num" id="n4">0%</span></div>
</div>

<div class="card"><h2>Stored calibration <span id="calBadge" class="badge">-</span></h2>
<table><thead><tr><th>Finger</th><th>Open</th><th>Fist</th><th>Travel</th><th>Thr</th><th>Db</th></tr></thead>
<tbody id="calBody"><tr><td colspan="6" style="text-align:left">loading...</td></tr></tbody></table>
<div class="hint" id="calHint">A saved snapshot is reloaded on boot, so the two-pose calibration does not run again.</div>
<div style="margin-top:12px">
<button class="cyan" onclick="act('/api/cal/save')">Save snapshot</button>
<button class="ghost" onclick="act('/api/cal/load')">Restore</button>
<button class="pink" onclick="act('/api/cal/clear')">Erase</button>
<button onclick="recal()">Recalibrate now</button>
</div>
</div>

<div class="card"><h2>Calibration guide</h2>
<div class="hint">Run <b>Recalibrate now</b> above when you change gloves or notice sweep drift. During calibration:</div>
<ol>
<li><b>Hand FLAT</b>, fingers straight and relaxed — sets the OPEN bend baseline.</li>
<li><b>FIST</b>, curl all five fingers — sets the FIST baseline and bend thresholds.</li>
<li><b>Rotate</b> palm toward you, then back of hand toward you — sets orientation templates.</li>
<li><b>Hold STILL</b> for ~2s — sets the gyro zero reference. <b>Do not move</b> during this step; motion here causes sweep drift.</li>
<li><b>Rotate wrist clockwise</b> (like turning a key) — detects the twist axis and sign.</li>
</ol>
<div class="hint">Good gyro bias: all three values near 0.000 rad/s. Values above 0.05 mean the glove moved during the still step — recalibrate. Brightness holds at your release level once calibrated.</div>
</div>

<div class="card"><h2>Live calibration <span id="calLiveBadge" class="badge">idle</span></h2>
<div id="calLiveStep" style="font-size:1.3em; font-weight:bold; margin:10px 0; color:var(--lime);">--</div>
<div id="calLiveInstr" style="margin:8px 0; color:var(--ink);">Press Recalibrate to begin</div>
<div style="background:var(--deep); height:26px; border-radius:13px; overflow:hidden; margin:12px 0; border:2px solid var(--edge);">
  <div id="calLiveBar" style="height:100%; width:0%; background:linear-gradient(90deg,var(--lime),var(--cyan)); transition:width 0.4s;"></div>
</div>
<div id="calLiveTimer" style="font-family:monospace; font-size:1.4em; color:var(--ink); text-align:center;">--</div>
<div id="calLiveHint" style="font-size:0.85em; color:var(--muted); margin-top:8px; text-align:center;"></div>
</div>

<div class="card"><h2>Firmware update <span id="fwBadge" class="badge">wifi OTA</span></h2>
<div class="hint">Pick a compiled .bin and push it over WiFi - no USB, no BOOT button. The glove flashes, verifies and reboots itself (~30s). If the flash fails, the old firmware keeps running.</div>
<div class="row"><input id="fwfile" type="file" accept=".bin" style="flex:1"></div>
<div style="margin-top:10px">
<button class="cyan" onclick="fwPush()">Push update</button>
</div>
<div id="fwOut" class="hint" style="margin-top:8px"></div>
</div>

<div class="card"><h2>LED strip <span id="ctlBadge" class="badge">WINDOWS</span></h2>
<div class="row"><label>Control mode</label>
<button class="cyan" style="margin:0" onclick="modeToggle()">Switch mode</button></div>
<div class="row"><label>Power</label>
<button id="ledPBtn" class="ghost" style="margin:0" onclick="ledSet('p','T')">Off</button></div>
<div class="row"><label>Color</label><input id="ledC" type="color" value="#ffffff" style="flex:none;width:64px;height:36px;padding:2px"></div>
<div class="row"><label>Brightness</label><input id="ledB" type="range" min="0" max="100" value="60"><span class="val" id="ledBv">60</span></div>
<div class="row"><label>Speed</label><input id="ledS" type="range" min="0" max="100" value="50"><span class="val" id="ledSv">50</span></div>
<div class="row" style="flex-wrap:wrap;gap:6px">
<button class="ghost" style="margin:0;padding:8px 10px" onclick="ledSet('m',0)">Solid</button>
<button class="ghost" style="margin:0;padding:8px 10px" onclick="ledSet('m',1)">Breathe</button>
<button class="ghost" style="margin:0;padding:8px 10px" onclick="ledSet('m',2)">Flash</button>
<button class="ghost" style="margin:0;padding:8px 10px" onclick="ledSet('m',3)">Twinkle</button>
<button class="ghost" style="margin:0;padding:8px 10px" onclick="ledSet('m',4)">Stars</button>
<button class="ghost" style="margin:0;padding:8px 10px" onclick="ledSet('m',5)">Gradient</button>
</div>
<div class="row"><label>Direction</label>
<button class="ghost" style="margin:0" onclick="ledSet('d',0)">Forward</button>
<button class="ghost" style="margin:0" onclick="ledSet('d',1)">Backward</button></div>
<div class="row"><label>All effects</label>
<select id="ledMc" style="flex:1"><option value="0">-- pick from the app's full list --</option></select></div>
<div class="row"><label>Mic reactive</label>
<select id="ledMm" style="flex:1">
<option value="-">-- mic mode --</option>
<option value="0">Gradual</option><option value="1">Trailing</option>
<option value="2">Jump</option><option value="3">Strobe</option>
<option value="4">Off</option></select></div>
<div class="row"><label>Mic level</label><input id="ledMb" type="range" min="0" max="100" value="60"><span class="val" id="ledMbv">60</span></div>
<div class="hint">K1 tap toggles WINDOWS/LED mode. <a href="/gestures" style="color:var(--cyan)">Gesture map &rarr;</a> open-palm=on, fist=off, peace=effect, index=color, thumbs/pinky=brightness, twist=speed. Mic modes are one-shot pokes.</div>
</div>

<div class="card"><h2>Home WiFi <span id="wBadge" class="badge">-</span></h2>
<div class="row"><label>Network</label>
<select id="wsid"><option value="">-- pick after scanning --</option></select>
<button class="ghost" style="margin:0;padding:11px 14px" onclick="scan()">Scan</button></div>
<div class="row"><label>Password</label><input id="wpass" type="password" placeholder="unchanged" autocomplete="new-password"></div>
<div class="row"><label>Auto-join</label><input id="wifiEn" type="checkbox" style="flex:none;width:24px;height:24px;accent-color:#22c55e"></div>
<div style="margin-top:10px">
<button class="cyan" onclick="joinWifi()">Connect</button>
<button class="ghost" onclick="saveAll()">Save &amp; reboot</button>
</div>
<div class="hint">The ESP32 radio is <b>2.4&nbsp;GHz only</b>, so 5&nbsp;GHz networks will never appear here.
The hotspot stays up either way &mdash; a wrong password cannot lock you out.
Press <b>K1</b> on the board to join the saved network without opening this page.</div>
<pre id="wif"></pre>
</div>

<div class="card"><h2>Bend thresholds (%)</h2>
<div class="row"><label>Thumb ON</label><input id="on0" type="range" min="0" max="100"><span class="val" id="on0v">0</span></div>
<div class="row"><label>Thumb OFF</label><input id="off0" type="range" min="0" max="100"><span class="val" id="off0v">0</span></div>
<div class="row"><label>Fingers ON</label><input id="on1" type="range" min="0" max="100"><span class="val" id="on1v">0</span></div>
<div class="row"><label>Fingers OFF</label><input id="off1" type="range" min="0" max="100"><span class="val" id="off1v">0</span></div>
<div class="hint">One ON/OFF pair covers index, middle, ring and pinky.</div>
</div>

<div class="card"><h2>Twist</h2>
<div class="row"><label>Pause dps</label><input id="tp" type="number" step="0.5" min="1" max="400"></div>
<div class="row"><label>Resume dps</label><input id="tr" type="number" step="0.5" min="1" max="400"></div>
<div class="row"><label>Axis</label><input id="ta" type="number" min="-1" max="2"></div>
<div class="row"><label>Sign</label><input id="ts" type="number" step="1" min="-1" max="1"></div>
<div class="hint">Axis -1 and sign 0 mean "let calibration decide".</div>
</div>

<div class="card"><h2>Device &amp; hotspot</h2>
<div class="row"><label>Name</label><input id="name" type="text" maxlength="23"></div>
<div class="row"><label>Hotspot pass</label><input id="appass" type="password" placeholder="unchanged" autocomplete="new-password"></div>
<div class="row"><label>Send every</label><input id="sem" type="number" min="50" max="5000" step="10"></div>
<div class="row"><label>LED slow ms</label><input id="ls" type="number" min="100" max="5000" step="50"></div>
<div class="row"><label>LED fast ms</label><input id="lf" type="number" min="50" max="5000" step="50"></div>
<div class="row"><label>Bend frac</label><input id="bf" type="number" step="0.01" min="0.05" max="0.95"></div>
<div class="row"><label>Min travel</label><input id="mt" type="number" min="0" max="1000" step="5"></div>
<div class="hint">Hotspot password must be at least 8 characters (WPA2).</div>
</div>

<div class="card">
<button class="pink" onclick="saveAll()">Save &amp; reboot</button>
<button class="ghost" onclick="act('/api/zero')">Zero twist</button>
<pre id="out">ready</pre>
</div>

<script>
function el(i){return document.getElementById(i);}
function pct(id){var e=el(id),o=el(id+'v');var u=function(){o.textContent=e.value;};e.addEventListener('input',u);u();}
function say(m){el('out').textContent=m;}
function act(p){
  fetch(p,{method:'POST'}).then(function(r){return r.json();})
  .then(function(j){say(p+' -> '+JSON.stringify(j)); if(p.indexOf('cal')>=0) calLoad();})
  .catch(function(e){say('failed: '+e);});
}
var CAL_STEPS = [
  {name:'Hold hand FLAT, fingers straight and relaxed', ms:5000},
  {name:'Make a FIST - curl all five fingers', ms:5000},
  {name:'Rotate palm toward you', ms:5400},
  {name:'Rotate back of hand toward you', ms:5400},
  {name:'Hold STILL - do not move (gyro zero)', ms:3500},
  {name:'Rotate wrist clockwise (like turning a key)', ms:5000}
];
var calAnimTimer = null;
function calAnimate(){
  var t0 = Date.now();
  var total = 0;
  CAL_STEPS.forEach(function(s){ total += s.ms; });
  function tick(){
    var e = Date.now() - t0;
    var acc = 0, step = null, idx = 0, rem = 0;
    for(var i=0;i<CAL_STEPS.length;i++){
      if(e < acc + CAL_STEPS[i].ms){ step = CAL_STEPS[i]; idx = i; rem = acc + CAL_STEPS[i].ms - e; break; }
      acc += CAL_STEPS[i].ms;
    }
    if(step){
      el('calLiveBadge').textContent = 'step ' + (idx+1) + '/' + CAL_STEPS.length;
      el('calLiveStep').textContent = step.name;
      el('calLiveInstr').textContent = step.name;
      el('calLiveBar').style.width = ((e/total)*100).toFixed(1) + '%';
      el('calLiveTimer').textContent = Math.ceil(rem/1000) + 's';
      el('calLiveHint').textContent = 'Follow this step on the glove now';
      calAnimTimer = setTimeout(tick, 200);
    } else {
      el('calLiveStep').textContent = 'Calibration complete!';
      el('calLiveInstr').textContent = 'Done - refreshing saved snapshot...';
      el('calLiveBar').style.width = '100%';
      el('calLiveTimer').textContent = 'done';
      el('calLiveHint').textContent = '';
      fetch('/api/cal/status').then(function(r){return r.json();}).then(function(s){
        el('calLiveBadge').textContent = s.state;
      }).catch(function(){});
      calLoad();
    }
  }
  tick();
}
function recal(){
  if(!confirm('Recalibrate now? Follow the LIVE CALIBRATION panel - it guides you through each step. Glove goes quiet ~30s.'))return;
  if(calAnimTimer) clearTimeout(calAnimTimer);
  fetch('/api/calibrate',{method:'POST'}).catch(function(e){say('calibrate failed: '+e);});
  say('calibrating - follow the live panel');
  calAnimate();
}
function fwPush(){
  var f = el('fwfile').files[0];
  if(!f){ el('fwOut').textContent = 'pick a .bin file first'; return; }
  if(!confirm('Push '+f.name+' ('+Math.round(f.size/1024)+' KB) to the glove? It reboots into the new firmware.'))return;
  var fd = new FormData(); fd.append('bin', f, f.name);
  var x = new XMLHttpRequest();
  x.open('POST', '/api/update');
  x.upload.onprogress = function(ev){
    if(ev.lengthComputable) el('fwOut').textContent = 'uploading... ' + Math.round(ev.loaded/ev.total*100) + '%';
  };
  x.onload = function(){
    if(x.status === 200){
      el('fwOut').textContent = 'flashed - glove is rebooting, page reloads in 12s';
      setTimeout(function(){ location.reload(); }, 12000);
    } else {
      el('fwOut').textContent = 'update failed: ' + x.responseText;
    }
  };
  x.onerror = function(){ el('fwOut').textContent = 'upload failed - glove may be rebooting'; };
  x.send(fd);
}
function ledSet(p,v){
  fetch('/api/led?'+p+'='+encodeURIComponent(v),{method:'POST'}).then(function(r){return r.json();}).then(function(j){ if(j.ok) modeLoad(); });
}
function modeToggle(){
  fetch('/api/mode',{method:'POST'}).then(function(r){return r.json();}).then(function(j){ if(j.ok) modeLoad(); });
}
function modeLoad(){
  fetch('/api/mode').then(function(r){return r.json();}).then(function(j){
    var b=el('ctlBadge');
    b.textContent = j.ctl ? 'LED MODE' : 'WINDOWS';
    b.style.background = j.ctl ? 'var(--pink)' : '';
    el('ledPBtn').textContent = j.led.p ? 'On' : 'Off';
    el('ledB').value = j.led.b; el('ledBv').textContent = j.led.b;
    el('ledS').value = j.led.s; el('ledSv').textContent = j.led.s;
    el('ledC').value = '#' + [j.led.r,j.led.g,j.led.bl].map(function(x){return ('0'+x.toString(16)).slice(-2);}).join('');
    var mc=el('ledMc'); if(mc && j.led.mc!==undefined) mc.value = String(j.led.mc);
    var mm=el('ledMm'); if(mm) mm.value = '-';   // one-shot pokes spring back
  });
}
function modesLoad(){
  fetch('/api/modes').then(function(r){return r.json();}).then(function(j){
    var s=el('ledMc'); var cur=s.value; s.textContent='';
    var o=document.createElement('option'); o.value='0'; o.textContent="-- pick from the app's full list --"; s.appendChild(o);
    j.modes.forEach(function(m){
      var op=document.createElement('option'); op.value=m.c; op.textContent=m.c+': '+m.n; s.appendChild(op);
    });
    s.value=cur;
  }).catch(function(){});
}
el('ledB').addEventListener('change',function(){ledSet('b',this.value);});
el('ledS').addEventListener('change',function(){ledSet('s',this.value);});
el('ledC').addEventListener('change',function(){
  var h=this.value;
  ledSet('c',parseInt(h.slice(1,3),16)+','+parseInt(h.slice(3,5),16)+','+parseInt(h.slice(5,7),16));
});
el('ledMc').addEventListener('change',function(){ if(+this.value) ledSet('mc',this.value); });
el('ledMm').addEventListener('change',function(){ var v=this.value; if(v==='-')return; fetch('/api/led?mm='+v,{method:'POST'}).catch(function(){}); });
el('ledMb').addEventListener('change',function(){ el('ledMbv').textContent=this.value; fetch('/api/led?mb='+this.value,{method:'POST'}).catch(function(){}); });
function calLoad(){
  fetch('/api/cal').then(function(r){return r.json();}).then(function(c){
    var names=['Thumb','Index','Middle','Ring','Pinky'];
    var tb=el('calBody'); tb.textContent='';
    for(var i=0;i<5;i++){
      var tr=document.createElement('tr');
      var tv=(c.fist[i]-c.open[i]); if(tv<0)tv=-tv;
      var cells=[names[i],c.open[i],c.fist[i],tv,c.thresh[i],c.deadb[i]];
      for(var k=0;k<cells.length;k++){
        var td=document.createElement('td');
        td.textContent=cells[k];
        if(!c.active[i]) td.className='dead';
        tr.appendChild(td);
      }
      tb.appendChild(tr);
    }
    el('calBadge').textContent=c.stored?'saved':'live only';
    el('calHint').textContent=(c.stored
      ? 'Snapshot in flash - reused on boot, no recalibration needed. '
      : 'Not saved yet - the poses will run again on next boot. ')+
      'Twist axis '+c.axis+', sign '+(c.sign>0?'+':'-')+', rotation '+(c.oriOK?'enabled':'tilt only')+'. '+
      'Gyro bias: x='+c.gyro[0]+' y='+c.gyro[1]+' z='+c.gyro[2]+' rad/s (near 0.000 = good; &gt;0.05 = drift risk, recalibrate).';
  }).catch(function(e){el('calHint').textContent='cal load failed: '+e;});
}
function scan(){
  el('wif').textContent='scanning 2.4GHz...';
  fetch('/api/wifi/scan').then(function(r){return r.json();}).then(function(j){
    var s=el('wsid'); s.textContent='';
    var o=document.createElement('option'); o.value=''; o.textContent='-- pick a network --';
    s.appendChild(o);
    if(!j.nets||!j.nets.length){el('wif').textContent='no 2.4GHz networks found';return;}
    j.nets.forEach(function(n){
      var op=document.createElement('option');
      op.value=n.s;
      op.textContent=n.s+'  ('+n.r+' dBm'+(n.e?' *':'')+')';
      s.appendChild(op);
    });
    el('wif').textContent='found '+j.nets.length+' network(s)';
  }).catch(function(e){el('wif').textContent='scan failed: '+e;});
}
function joinWifi(){
  var s=el('wsid').value, p=el('wpass').value;
  if(!s){el('wif').textContent='pick a network first';return;}
  el('wif').textContent='joining '+s+' ...';
  fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({wsid:s,wpass:p})})
  .then(function(r){return r.json();})
  .then(function(j){
    el('wif').textContent=(j.ok?'joining ':'rejected ')+s;
    setTimeout(wifiStat,4000);
  }).catch(function(e){el('wif').textContent='join failed: '+e;});
}
function wifiStat(){
  fetch('/api/wifi/status').then(function(r){return r.json();}).then(function(w){
    var b=el('wBadge');
    b.textContent=w.state;
    b.style.background=(w.state==='joined')?'#7dffb2':(w.state==='failed'?'#ff4d4d':'#a3e635');
    el('wif').textContent='state: '+w.state+(w.ip?('  ip: '+w.ip):'')+
      (w.ssid?('  ssid: '+w.ssid):'');
    if(w.state==='joining') setTimeout(wifiStat,2500);
  }).catch(function(){});
}
function load(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(c){
    el('on0').value=c.on[0]; el('off0').value=c.off[0];
    el('on1').value=c.on[1]; el('off1').value=c.off[1];
    el('tp').value=c.tp; el('tr').value=c.tr; el('ta').value=c.ta; el('ts').value=c.ts;
    el('name').value=c.name; el('sem').value=c.sem; el('ls').value=c.ls; el('lf').value=c.lf;
    el('bf').value=c.bf; el('mt').value=c.mt;
    el('wifiEn').checked=!!c.wifiEn;
    if(c.wsid){var o=document.createElement('option');o.value=c.wsid;o.textContent=c.wsid;el('wsid').appendChild(o);el('wsid').value=c.wsid;}
    el('wpass').placeholder=c.hasWpass?'stored - leave blank to keep':'not set';
    el('appass').placeholder=c.hasApass?'stored - leave blank to keep':'glove1234';
    pct('on0');pct('off0');pct('on1');pct('off1');
    say('loaded from NVS');
  }).catch(function(e){say('load failed: '+e);});
  calLoad(); wifiStat();
}
function collect(){
  var on=+el('on1').value, off=+el('off1').value;
  var m={on:[+el('on0').value,on,on,on,on], off:[+el('off0').value,off,off,off,off],
    tp:+el('tp').value, tr:+el('tr').value, ta:+el('ta').value, ts:+el('ts').value,
    name:el('name').value, sem:+el('sem').value, ls:+el('ls').value, lf:+el('lf').value,
    bf:+el('bf').value, mt:+el('mt').value,
    wsid:el('wsid').value, wifiEn:el('wifiEn').checked?1:0};
  if(el('wpass').value) m.wpass=el('wpass').value;
  if(el('appass').value) m.appass=el('appass').value;
  return m;
}
function saveAll(){
  say('saving...');
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(collect())})
  .then(function(r){return r.json();})
  .then(function(){say('saved, rebooting to apply');})
  .catch(function(e){say('save failed: '+e);});
}
function status(){
  fetch('/api/status').then(function(r){return r.json();}).then(function(s){
    var p=el('bles'); p.textContent=s.ble?'PC linked':'advertising';
    p.className='pill '+(s.ble?'on':'off');
    el('tw').textContent=s.twist;
    for(var i=0;i<5;i++){
      var v=Math.max(0,Math.min(100,Math.round(s.pct[i])));
      el('b'+i).style.width=v+'%';
      el('n'+i).textContent=v+'%';
    }
  }).catch(function(){});
}
  load(); status(); modeLoad(); modesLoad(); setInterval(status,500); setInterval(modeLoad,3000); setInterval(wifiStat,6000);
</script></body></html>)HTML";

/* ---- DMX03 effect catalogue (fw 3.2): R.array.dmx03_model in the "ledble"
   app, verbatim. Index 0 = AUTO (effect code 255); index i = effect code i.
   Served by /api/modes so the portal dropdown offers every effect the app
   has; the raw code goes out as the mode frame 7B FF 03 code FF FF FF FF BF. */
static const char* const DMX03_MODES[211] = {
  "Auto",
  "Forward Dreaming", "Backward Dreaming", "Forward 7 Colors", "Backward 7 Colors",
  "Forward RD/GN/BU", "Backward RD/GN/BU", "Forward YE/CN/VT", "Backward YE/CN/VT",
  "Forward 6 Colors RD", "Backward 6 Colors RD", "Forward 6 Colors GN", "Backward 6 Colors GN",
  "Forward 6 Colors BU", "Backward 6 Colors BU", "Forward 6 Colors CN", "Backward 6 Colors CN",
  "Forward 6 Colors YE", "Backward 6 Colors YE", "Forward 6 Colors VT", "Backward 6 Colors VT",
  "Forward 6 Colors WH", "Backward 6 Colors WH",
  "Forward Trailing 7 Colors", "Backward Trailing 7 Colors",
  "Forward Trailing RD", "Backward Trailing RD", "Forward Trailing GN", "Backward Trailing GN",
  "Forward Trailing BU", "Backward Trailing BU", "Forward Trailing YE", "Backward Trailing YE",
  "Forward Trailing CN", "Backward Trailing CN", "Forward Trailing VT", "Backward Trailing VT",
  "Forward Trailing WH", "Backward Trailing WH",
  "Forward Streaming 7 Colors", "Backward Streaming 7 Colors",
  "Forward Streaming RD/GN/BU", "Backward Streaming RD/GN/BU",
  "Forward Streaming YE/CN/VT", "Backward Streaming YE/CN/VT",
  "Forward Streaming RD/GN", "Backward Streaming RD/GN",
  "Forward Streaming GN/BU", "Backward Streaming GN/BU",
  "Forward Streaming YE/BU", "Backward Streaming YE/BU",
  "Forward Streaming YE/CN", "Backward Streaming YE/CN",
  "Forward Streaming CN/VT", "Backward Streaming CN/VT",
  "Forward Streaming BK/WH", "Backward Streaming BK/WH",
  "Open Curtain 7 Colors", "Close Curtain 7 Colors",
  "Open Curtain RD/GN/BU", "Close Curtain RD/GN/BU",
  "Open Curtain YE/CN/VT", "Close Curtain YE/CN/VT",
  "Forward Follow Spot 7 Colors", "Backward Follow Spot 7 Colors",
  "Forward Follow Spot RD/GN/BU", "Backward Follow Spot RD/GN/BU",
  "Forward Follow Spot YE/CN/VT", "Backward Follow Spot YE/CN/VT",
  "Forward Flutter 7 Colors", "Backward Flutter 7 Colors",
  "Forward Flutter RD/GN/BU", "Backward Flutter RD/GN/BU",
  "Forward Flutter BU/YE/CN", "Backward Flutter BU/YE/CN",
  "Forward Flutter CN/VT/WH", "Backward Flutter CN/VT/WH",
  "Hop 7 Colors", "Hop RD/GN/BU", "Hop YE/CN/VT", "Strobe White",
  "Forward Horse Race 7 Colors", "Backward Horse Race 7 Colors",
  "Forward Horse Race RD/GN/BU", "Backward Horse Race RD/GN/BU",
  "Forward Horse Race YE/CN/VT", "Backward Horse Race YE/CN/VT",
  "Forward Horse Race YE", "Backward Horse Race YE",
  "Forward Horse Race CN", "Backward Horse Race CN",
  "Forward Horse Race VT", "Backward Horse Race VT",
  "Forward Horse Race WH", "Backward Horse Race WH",
  "Forward Run 7 Colors", "Backward Run 7 Colors",
  "Forward Run RD", "Backward Run RD", "Forward Run GN", "Backward Run GN",
  "Forward Run BU", "Backward Run BU", "Forward Run YE", "Backward Run YE",
  "Forward Run CN", "Backward Run CN", "Forward Run VT", "Backward Run VT",
  "Forward Run WH", "Backward Run WH",
  "Forward Run RD/GN", "Backward Run RD/GN", "Forward Run GN/BU", "Backward Run GN/BU",
  "Forward Run BU/YE", "Backward Run BU/YE", "Forward Run YE/CN", "Backward Run YE/CN",
  "Forward Run CN/VT", "Backward Run CN/VT", "Forward Run VT/WH", "Backward Run VT/WH",
  "Forward Flow 7 Colors", "Backward Flow 7 Colors",
  "Forward Flow RD/GN/RD", "Backward Flow RD/GN/RD", "Forward Flow BU/GN/BU", "Backward Flow BU/GN/BU",
  "Forward Flow YE/GN/YE", "Backward Flow YE/GN/YE", "Forward Flow YE/CN/YE", "Backward Flow YE/CN/YE",
  "Forward Flow VT/CN/VT", "Backward Flow VT/CN/VT", "Forward Flow WH/VT/WH", "Backward Flow WH/VT/WH",
  "Forward Run 7 Colors ON WH", "Backward Run 7 Colors ON WH",
  "Forward Run RD ON WH", "Backward Run RD ON WH",
  "Forward Run GN ON WH", "Backward Run GN ON WH",
  "Forward Run BU ON WH", "Backward Run BU ON WH",
  "Forward Run YE ON GN", "Backward Run YE ON GN",
  "Forward Run CN ON RD", "Backward Run CN ON RD",
  "Forward Run VT ON BU", "Backward Run VT ON BU",
  "Forward Run YE ON WH", "Backward Run YE ON WH",
  "Close Curtain 7 COLOR", "Open Curtain 7 COLOR",
  "Close Curtain RD", "Open Curtain RD", "Close Curtain GN", "Open Curtain GN",
  "Close Curtain BU", "Open Curtain BU", "Close Curtain YE", "Open Curtain YE",
  "Close Curtain CN", "Open Curtain CN", "Close Curtain VT", "Open Curtain VT",
  "Close Curtain WH", "Open Curtain WH",
  "Close Curtain 7 Colors ON YE", "Open Curtain 7 Colors ON YE",
  "Close Curtain RD/GN/BU ON CN", "Open Curtain RD/GN/BU ON CN",
  "Close Curtain YE/CN/VT ON VT", "Open Curtain YE/CN/VT ON VT",
  "Forward Flow RD/GN/BU/YE", "Backward Flow RD/GN/BU/YE",
  "Forward Flow GN/BU/YE/CN", "Backward Flow GN/BU/YE/CN",
  "Forward Flow BU/YE/CN/VT", "Backward Flow BU/YE/CN/VT",
  "Forward Flow YE/CN/VT/WH", "Backward Flow YE/CN/VT/WH",
  "Forward Swab 7 COLORS", "Backward Swab 7 COLORS",
  "Forward Swab RD/GN/BU", "Backward Swab RD/GN/BU",
  "Forward Swab YE/CN/VT", "Backward Swab YE/CN/VT",
  "Forward Swab RD", "Backward Swab RD", "Forward Swab GN", "Backward Swab GN",
  "Forward Swab BU", "Backward Swab BU", "Forward Swab YE", "Backward Swab YE",
  "Forward Swab CN", "Forward Swab CN", "Forward Swab VT", "Forward Swab VT",
  "Forward Swab WH", "Forward Swab WH",
  "Open Curtain Swab 7 COLORS", "Close Curtain Swab 7 COLORS",
  "Open Curtain Swab RD/GN/BU", "Close Curtain Swab RD/GN/BU",
  "Open Curtain Swab BU/YE", "Close Curtain Swab BU/YE",
  "Open Curtain Swab CN/VT", "Close Curtain Swab CN/VT"
};

/* ---- Gestures page (fw 3.2): the pose -> LED-action map with a live pose
   readout. Served at /gestures; linked from the LED strip card. Reads
   /api/status (ctl / gest / pct / strip). ---- */
static const char GEST_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>QD023 Glove - LED Gestures</title>
<style>
body{font-family:system-ui,sans-serif;background:#0b1020;color:#e7ecf5;margin:0 auto;padding:16px;max-width:640px}
h1{font-size:1.35em}h2{font-size:1.05em;color:#7dd3fc;margin-top:22px}
table{width:100%;border-collapse:collapse;margin:8px 0}
td,th{padding:7px 8px;border-bottom:1px solid #1e293b;text-align:left;font-size:0.92em}
th{color:#94a3b8;font-weight:600}
.pose{font-family:monospace;color:#a3e635;font-weight:700;white-space:nowrap}
.pill{padding:3px 10px;border-radius:12px;font-size:0.8em;background:#334155;display:inline-block}
.on{background:#22c55e;color:#03210f}.off{background:#475569}
.bar{height:10px;background:#1e293b;border-radius:5px;overflow:hidden;margin:3px 0 10px}
.bar>div{height:100%;background:linear-gradient(90deg,#22c55e,#38bdf8)}
a{color:#38bdf8}
.hint{color:#94a3b8;font-size:0.85em;margin-top:14px;line-height:1.5}
</style></head><body>
<h1>LED strip gestures <a href="/" style="float:right;font-size:0.62em">&larr; config panel</a></h1>
<div>Mode <span id="gCtl" class="pill off">-</span>&nbsp;
Strip <span id="gStrip" class="pill off">-</span>&nbsp;
Pose <span id="gPose" class="pill on">-</span></div>
<h2>Gesture map &mdash; active in LED mode</h2>
<table>
<tr><th>Gesture</th><th>LED action</th></tr>
<tr><td class="pose">OPEN_PALM</td><td>Power ON</td></tr>
<tr><td class="pose">FIST</td><td>Power OFF</td></tr>
<tr><td class="pose">PEACE (2 fingers)</td><td>Next effect: solid &rarr; breathe &rarr; flash &rarr; twinkle &rarr; stars &rarr; gradient</td></tr>
<tr><td class="pose">INDEX_UP</td><td>Next color preset (white, red, green, blue, yellow, cyan, magenta)</td></tr>
<tr><td class="pose">THUMBS_UP</td><td>Brightness +25%</td></tr>
<tr><td class="pose">PINKY_UP</td><td>Brightness &minus;25%</td></tr>
<tr><td class="pose">TWIST clockwise</td><td>Speed +10 &mdash; flick the wrist past 60&deg;, return near center, repeat</td></tr>
<tr><td class="pose">TWIST counter-clockwise</td><td>Speed &minus;10, same flick-and-return cadence</td></tr>
</table>
<h2>Live finger bend</h2>
<div id="bars"></div>
<div class="hint">A pose must hold for 3 samples (~60&thinsp;ms) to register, so flings and transits are ignored. Gestures only change state; the glove pushes LEDDMX-03 frames to the strip over BLE (the same protocol the official app speaks). Twist uses the calibrated wrist-roll axis, so it works with the glove worn flat or tilted. Gestures work even when the strip link is down &mdash; changes apply as soon as it reconnects.</div>
<script>
function el(i){return document.getElementById(i)}
var names=['Thumb','Index','Middle','Ring','Pinky'];
var bars=el('bars');
names.forEach(function(n,i){
  var d=document.createElement('div');
  d.innerHTML=n+' <span id="v'+i+'">0%</span><div class="bar"><div id="b'+i+'" style="width:0%"></div></div>';
  bars.appendChild(d);
});
function poll(){
  fetch('/api/status').then(function(r){return r.json();}).then(function(s){
    var c=el('gCtl'); c.textContent=s.ctl?'LED MODE':'WINDOWS'; c.className='pill '+(s.ctl?'on':'off');
    var t=el('gStrip'); t.textContent=s.strip?'connected':'no link'; t.className='pill '+(s.strip?'on':'off');
    el('gPose').textContent=s.gest||'-';
    for(var i=0;i<5;i++){
      var v=Math.max(0,Math.min(100,Math.round(s.pct[i])));
      el('b'+i).style.width=v+'%'; el('v'+i).textContent=v+'%';
    }
  }).catch(function(){});
}
poll(); setInterval(poll,400);
</script></body></html>)HTML";

static void webJson(int code, const String& body) {
  webServer.send(code, "application/json", body);
}

static void webInit() {
  WiFi.mode(WIFI_AP_STA);
  bool ok = WiFi.softAP(g_cfg.deviceName, g_cfg.apPass);
  Serial.printf("AP %s \"%s\" -> %s\n", ok ? "up" : "FAILED",
                g_cfg.deviceName, WiFi.softAPIP().toString().c_str());

  webServer.on("/", []() { webServer.send_P(200, "text/html", CFG_HTML); });

  webServer.on("/gestures", []() { webServer.send_P(200, "text/html", GEST_HTML); });

  webServer.on("/api/modes", HTTP_GET, []() {
    String out = "{\"modes\":[";
    for (int i = 0; i < 211; i++) {
      char it[96];
      snprintf(it, sizeof(it), "%s{\"c\":%d,\"n\":\"%s\"}",
               i ? "," : "", i == 0 ? 255 : i, DMX03_MODES[i]);
      out += it;
    }
    out += "]}";
    webJson(200, out);
  });

  webServer.on("/api/config", HTTP_GET, []() { webJson(200, cfgToJson(g_cfg)); });

  webServer.on("/api/config", HTTP_POST, []() {
    cfgFromJson(webServer.arg("plain"), g_cfg);
    cfgSave(g_cfg);
    webJson(200, "{\"ok\":true,\"reboot\":true}");
    g_rebootAt = millis() + 500;
  });

  webServer.on("/api/status", HTTP_GET, []() {
    char b[384];
    snprintf(b, sizeof(b),
             "{\"ble\":%s,\"pct\":[%d,%d,%d,%d,%d],\"twist\":%d,\"k1\":%d,\"scanhit\":\"%s\",\"strip\":%s,\"stripname\":\"%s\",\"stripst\":%d,\"stripfail\":%d,\"ctl\":%d,\"gest\":\"%s\",\"fw\":\"%s\",\"up\":%lu,\"ap\":\"%s\"}",
             bleConnected ? "true" : "false",
             g_livePct[0], g_livePct[1], g_livePct[2], g_livePct[3], g_livePct[4],
             (int)twistAngle, digitalRead(K1_PIN) == LOW ? 1 : 0,
             g_scanHit, g_stripLink ? "true" : "false", g_stripName,
             (int)g_stripState, g_stripFails, gCtlMode, g_lastGesture,
             FW_VERSION, millis() / 1000UL,
             WiFi.softAPIP().toString().c_str());
    webJson(200, String(b));
  });

  webServer.on("/api/cal", HTTP_GET, []() { webJson(200, calJson()); });

  webServer.on("/api/cal/status", HTTP_GET, []() {
    const char* names[] = {"idle", "flat", "fist", "rotate_palm", "rotate_back", "still", "twist", "done"};
    unsigned long elapsed = millis() - g_calStepStartMs;
    unsigned long remaining = g_calStepDurationMs > elapsed ? g_calStepDurationMs - elapsed : 0;
    unsigned long progress = g_calStepDurationMs > 0 ? (elapsed * 100UL / g_calStepDurationMs) : 0;
    if (progress > 100) progress = 100;
    char b[320];
    snprintf(b, sizeof(b),
      "{\"state\":\"%s\",\"step\":\"%s\",\"elapsed\":%lu,\"duration\":%lu,\"remaining\":%lu,\"progress\":%lu}",
      names[g_calState], g_calStepName, elapsed, g_calStepDurationMs, remaining, progress);
    webJson(200, b);
  });

  webServer.on("/api/mode", HTTP_GET, []() {
    char b[224];
    snprintf(b, sizeof(b),
      "{\"ctl\":%d,\"led\":{\"p\":%d,\"b\":%u,\"m\":%u,\"s\":%u,\"mc\":%u,\"d\":%u,\"r\":%u,\"g\":%u,\"bl\":%u}}",
      gCtlMode, ledPower ? 1 : 0, ledBright, ledMode, ledSpeed, ledRawMode, ledDir, ledR, ledG, ledB);
    webJson(200, b);
  });

  webServer.on("/api/mode", HTTP_POST, []() {
    setCtlMode(gCtlMode == CTL_LED ? CTL_WINDOWS : CTL_LED);
    webJson(200, "{\"ok\":true}");
  });

  webServer.on("/api/led", HTTP_POST, []() {
    bool poke = false;   // mic-mode pokes skip the dirty sync (see stripMusicFrame)
    if (webServer.hasArg("p")) {
      String a = webServer.arg("p");
      if (a == "T") ledPower = !ledPower;
      else ledPower = (a == "1");
    }
    if (webServer.hasArg("b")) ledBright = (uint8_t)ledClamp(webServer.arg("b").toInt(), 0, 100);
    if (webServer.hasArg("m")) {
      ledRawMode = 0;    // glove-mode pick replaces a raw dropdown pick
      ledMode = (uint8_t)ledClamp(webServer.arg("m").toInt(), 0, 5);
    }
    if (webServer.hasArg("mc")) {   // raw DMX effect code 1-210 (dmx03_model)
      ledRawMode = (uint8_t)ledClamp(webServer.arg("mc").toInt(), 0, 255);
      if (ledRawMode) ledMode = 0;
    }
    if (webServer.hasArg("d")) ledDir = webServer.arg("d").toInt() ? 1 : 0;
    if (webServer.hasArg("s")) ledSpeed = (uint8_t)ledClamp(webServer.arg("s").toInt(), 0, 100);
    if (webServer.hasArg("c")) {
      String a = webServer.arg("c");
      int c1 = a.indexOf(',');
      int c2 = a.indexOf(',', c1 + 1);
      if (c1 > 0 && c2 > c1) {
        ledR = (uint8_t)ledClamp(a.substring(0, c1).toInt(), 0, 255);
        ledG = (uint8_t)ledClamp(a.substring(c1 + 1, c2).toInt(), 0, 255);
        ledB = (uint8_t)ledClamp(a.substring(c2 + 1).toInt(), 0, 255);
        ledMode = 0;
        ledRawMode = 0;  // explicit color -> solid
      }
    }
    if (webServer.hasArg("h")) { ledSetHue(webServer.arg("h").toInt()); ledRawMode = 0; }
    // mic-reactive pokes push directly; they must not trigger the full sync
    if (webServer.hasArg("mm")) { stripMusicFrame((uint8_t)ledClamp(webServer.arg("mm").toInt(), 0, 4), 1); poke = true; }
    if (webServer.hasArg("mb")) { stripMusicBrightFrame((uint8_t)ledClamp(webServer.arg("mb").toInt(), 0, 100)); poke = true; }
    if (!poke) ledWrite(ledR, ledG, ledB);
    webJson(200, "{\"ok\":true}");
  });

  webServer.on("/api/update", HTTP_POST,
    []() {
      if (g_updateOK) {
        g_rebootAt = millis() + 1500;
        webJson(200, "{\"ok\":true,\"note\":\"flashed - rebooting\"}");
      } else {
        webJson(500, "{\"ok\":false,\"error\":\"flash failed - old firmware still running\"}");
      }
    },
    []() {
      HTTPUpload& up = webServer.upload();
      if (up.status == UPLOAD_FILE_START) {
        g_updateOK = false;
        g_otaInProgress = true;
        Serial.printf("ota: start %s\n", up.filename.c_str());
        Update.begin();
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) g_updateOK = false;
      } else if (up.status == UPLOAD_FILE_END) {
        g_updateOK = Update.end(false); // skip verify: esp-idf compat
        Serial.println(g_updateOK ? "ota: verified, rebooting" : "ota: verify FAILED");
        g_otaInProgress = false;
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        g_updateOK = false;
        g_otaInProgress = false;
      }
    });

  webServer.on("/api/cal/save", HTTP_POST, []() {
    calPersist();
    webJson(200, "{\"ok\":true,\"saved\":true}");
  });

  webServer.on("/api/cal/load", HTTP_POST, []() {
    bool ok = calRestore();
    webJson(200, ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"no_snapshot\"}");
  });

  webServer.on("/api/cal/clear", HTTP_POST, []() {
    calBlobErase();
    webJson(200, "{\"ok\":true,\"cleared\":true}");
  });

  webServer.on("/api/zero", HTTP_POST, []() {
    twistAngle = 0.0f;
    sweepAngle = 0.0f;
    webJson(200, "{\"ok\":true,\"twist\":0,\"sweep\":0}");
  });

  webServer.on("/api/calibrate", HTTP_POST, []() {
    webJson(200, "{\"ok\":true,\"note\":\"glove goes quiet ~20s\"}");
    g_recalRequested = true;
  });

  webServer.on("/api/wifi/status", HTTP_GET, []() {
    const char* st = "idle";
    if (g_wifiState == 1) st = "joining";
    else if (g_wifiState == 2) st = "joined";
    else if (g_wifiState == 3) st = "failed";
    bool up = WiFi.status() == WL_CONNECTED;
    String ip = up ? WiFi.localIP().toString() : String("");
    String ss = up ? WiFi.SSID() : String(g_cfg.wifiSsid);
    char b[192];
    snprintf(b, sizeof(b), "{\"state\":\"%s\",\"ip\":\"%s\",\"ssid\":\"%s\",\"en\":%s}",
             st, ip.c_str(), ss.c_str(), g_cfg.wifiEn ? "true" : "false");
    webJson(200, String(b));
  });

  webServer.on("/api/wifi/scan", HTTP_GET, []() {
    int n = WiFi.scanNetworks(false, false);
    String out = "{\"nets\":[";
    int added = 0;
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      if (!s.length()) continue;
      bool enc = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      char item[96];
      snprintf(item, sizeof(item), "%s{\"s\":\"%s\",\"r\":%d,\"e\":%s}",
               added ? "," : "", s.c_str(), WiFi.RSSI(i), enc ? "true" : "false");
      out += item;
      added++;
    }
    out += "]}";
    WiFi.scanDelete();
    webJson(200, out);
  });

  webServer.on("/api/wifi/connect", HTTP_POST, []() {
    String body = webServer.arg("plain");
    char ssid[33], pass[64];
    if (!jsonStr(body, "wsid", ssid, sizeof(ssid)) || !ssid[0]) {
      webJson(400, "{\"ok\":false,\"error\":\"no_ssid\"}");
      return;
    }
    if (jsonStr(body, "wpass", pass, sizeof(pass)) && pass[0]) {
      strncpy(g_cfg.wifiPass, pass, sizeof(g_cfg.wifiPass) - 1);
      g_cfg.wifiPass[sizeof(g_cfg.wifiPass) - 1] = '\0';
    }
    strncpy(g_cfg.wifiSsid, ssid, sizeof(g_cfg.wifiSsid) - 1);
    g_cfg.wifiSsid[sizeof(g_cfg.wifiSsid) - 1] = '\0';
    cfgSave(g_cfg);
    wifiRequestJoin(g_cfg.wifiSsid, g_cfg.wifiPass);
    webJson(200, "{\"ok\":true,\"note\":\"joining\"}");
  });

  webServer.onNotFound([]() { webJson(404, "{\"error\":\"not_found\"}"); });
  webServer.begin();
}

static void webLoop() {
  webServer.handleClient();
  wifiService();
  if (g_rebootAt && millis() >= g_rebootAt) {
    Serial.println("web: rebooting to apply config");
    delay(100);
    ESP.restart();
  }
}
