/*
 * ============================================================
 *  Smart Coffee Machine — ESP32S
 *  PlatformIO / VS Code
 * ============================================================
 *  Wiring:
 *    OLED SDA        → GPIO 21
 *    OLED SCL        → GPIO 22
 *    DHT11 DATA      → GPIO 26
 *    LED anode (+)   → 220Ω → GPIO 2
 *    IR sensor OUT   → GPIO 34  (LOW = mug present)
 *    Cancel button   → GPIO 27  (INPUT_PULLUP, LOW = pressed)
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <time.h>
#include <ArduinoJson.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ────────────────────────────────────────────────────────────
//  CONFIG
// ────────────────────────────────────────────────────────────
const char *WIFI_SSID = "Home-Wifi";
const char *WIFI_PASSWORD = "password123";

const char *NTP_SERVER1 = "pool.ntp.org";
const char *NTP_SERVER2 = "time.nist.gov";
const char *TZ_INFO = "CET-1CEST,M3.5.0/2,M10.5.0/3";

const unsigned long BREW_DURATION_MS = 30000UL;

// ────────────────────────────────────────────────────────────
//  PINS
// ────────────────────────────────────────────────────────────
#define PIN_DHT 26
#define PIN_LED 2
#define PIN_MUG_SENSOR 34
#define PIN_BUTTON 27

// ────────────────────────────────────────────────────────────
//  OBJECTS
// ────────────────────────────────────────────────────────────
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
DHT dht(PIN_DHT, DHT11);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ────────────────────────────────────────────────────────────
//  STATE
// ────────────────────────────────────────────────────────────
int alarmHour = 7;
int alarmMin = 0;
bool alarmEnabled = false;
bool alarmCancelled = false;

float temperature = NAN;
float humidity = NAN;
bool mugPresent = false;
bool brewing = false;

unsigned long brewStartMs = 0;
unsigned long lastSensorRead = 0;
unsigned long lastBcast = 0;
unsigned long lastOLED = 0;
int lastTriggeredDay = -1;

// Button debounce
int lastButtonReading = HIGH;
int stableButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;

bool prevMug = false;

// ────────────────────────────────────────────────────────────
//  HELPERS
// ────────────────────────────────────────────────────────────
String fmtTime(int h, int m)
{
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
  return String(buf);
}

String nowStr()
{
  struct tm t;
  if (!getLocalTime(&t, 1000))
    return "--:--";
  return fmtTime(t.tm_hour, t.tm_min);
}

String getCurrentTime()
{
  struct tm t;
  if (!getLocalTime(&t, 1000))
    return "--:--:--";
  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M:%S", &t);
  return String(buf);
}

void setLED(bool state)
{
  brewing = state;
  digitalWrite(PIN_LED, state ? HIGH : LOW);
  if (state)
  {
    brewStartMs = millis();
    Serial.println("[Brew] LED ON — brewing started");
  }
  else
  {
    Serial.println("[Brew] LED OFF — brewing done");
  }
}

// ────────────────────────────────────────────────────────────
//  SENSOR READ
// ────────────────────────────────────────────────────────────
void readSensor()
{
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t))
  {
    humidity = h;
    temperature = t;
    Serial.print("[DHT] Temp: ");
    Serial.print(t);
    Serial.print(" C  Humidity: ");
    Serial.print(h);
    Serial.println(" %");
  }
  else
  {
    Serial.println("[DHT] Read failed");
  }
}

// ────────────────────────────────────────────────────────────
//  OLED
// ────────────────────────────────────────────────────────────
void drawOLED()
{
  u8g2.clearBuffer();

  // Row 1 — Current time (large font)
  u8g2.setFont(u8g2_font_logisoso16_tr);
  u8g2.drawStr(0, 16, nowStr().c_str());

  u8g2.setFont(u8g2_font_ncenB08_tr);

  // Row 2 — Temperature
  char tempStr[20];
  if (isnan(temperature))
    snprintf(tempStr, sizeof(tempStr), "Temp: --.- C");
  else
    snprintf(tempStr, sizeof(tempStr), "Temp: %.1f C", temperature);
  u8g2.drawStr(0, 30, tempStr);

  // Row 3 — Alarm
  String alarmStr;
  if (alarmEnabled && !alarmCancelled)
    alarmStr = "Alarm: " + fmtTime(alarmHour, alarmMin);
  else if (alarmCancelled)
    alarmStr = "Alarm: CANCELLED";
  else
    alarmStr = "Alarm: OFF";
  u8g2.drawStr(0, 43, alarmStr.c_str());

  // Row 4 — Mug
  u8g2.drawStr(0, 55, mugPresent ? "Mug: Ready" : "Mug: !MISSING!");

  // Row 5 — Brewing
  if (brewing)
    u8g2.drawStr(0, 64, ">> BREWING...");

  u8g2.sendBuffer();
}

// ────────────────────────────────────────────────────────────
//  WEBSOCKET BROADCAST
// ────────────────────────────────────────────────────────────
void broadcastState()
{
  JsonDocument doc;
  doc["time"] = getCurrentTime();
  doc["alarm"] = fmtTime(alarmHour, alarmMin);
  doc["alarmOn"] = alarmEnabled;
  doc["cancelled"] = alarmCancelled;
  doc["temp"] = isnan(temperature) ? 0.0f : temperature;
  doc["mug"] = mugPresent;
  doc["brewing"] = brewing;

  String payload;
  serializeJson(doc, payload);
  ws.textAll(payload);
}

// ────────────────────────────────────────────────────────────
//  WEBSOCKET EVENT HANDLER
// ────────────────────────────────────────────────────────────
void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *, AwsEventType type,
               void *arg, uint8_t *data, size_t len)
{

  if (type == WS_EVT_CONNECT)
  {
    broadcastState();
    return;
  }
  if (type != WS_EVT_DATA)
    return;

  AwsFrameInfo *fi = (AwsFrameInfo *)arg;
  if (!fi->final || fi->index != 0 || fi->len != len || fi->opcode != WS_TEXT)
    return;

  data[len] = '\0';
  JsonDocument doc;
  if (deserializeJson(doc, (char *)data))
    return;

  if (doc["alarmHour"].is<int>())
    alarmHour = doc["alarmHour"].as<int>();
  if (doc["alarmMinute"].is<int>())
    alarmMin = doc["alarmMinute"].as<int>();
  if (doc["alarmOn"].is<bool>())
    alarmEnabled = doc["alarmOn"].as<bool>();
  if (doc["cancel"].is<bool>())
    alarmCancelled = doc["cancel"].as<bool>();

  lastTriggeredDay = -1; // allow re-trigger if alarm time was changed
  drawOLED();
  broadcastState();
}

// ────────────────────────────────────────────────────────────
//  PHYSICAL BUTTON (debounced)
// ────────────────────────────────────────────────────────────
void handleButton()
{
  int reading = digitalRead(PIN_BUTTON);

  if (reading != lastButtonReading)
    lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY)
  {
    if (reading != stableButtonState)
    {
      stableButtonState = reading;
      if (stableButtonState == LOW)
      {
        alarmCancelled = !alarmCancelled;
        Serial.print("[Button] Alarm cancelled = ");
        Serial.println(alarmCancelled);
        drawOLED();
        broadcastState();
      }
    }
  }
  lastButtonReading = reading;
}

// ────────────────────────────────────────────────────────────
//  ALARM CHECK
// ────────────────────────────────────────────────────────────
void checkAlarm()
{
  if (!alarmEnabled || alarmCancelled || brewing)
    return;

  struct tm t;
  if (!getLocalTime(&t, 1000))
    return;

  bool timeMatch = (t.tm_hour == alarmHour) &&
                   (t.tm_min == alarmMin) &&
                   (lastTriggeredDay != t.tm_yday);

  if (timeMatch)
  {
    if (mugPresent)
    {
      Serial.println("[Alarm] Firing — LED ON");
      setLED(true);
      lastTriggeredDay = t.tm_yday;
      drawOLED();
      broadcastState();
    }
    else
    {
      Serial.println("[Alarm] Skipped — no mug detected");
    }
  }
}

// ────────────────────────────────────────────────────────────
//  HTML
// ────────────────────────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Smart Coffee</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=DM+Mono:wght@400;500&family=DM+Sans:wght@300..700&display=swap" rel="stylesheet">
  <style>
    :root, [data-theme="light"] {
      --color-bg:             #f5f3ef;
      --color-surface:        #faf9f6;
      --color-surface-2:      #ffffff;
      --color-surface-offset: #ede9e3;
      --color-border:         rgba(40,30,15,0.12);
      --color-divider:        rgba(40,30,15,0.08);
      --color-text:           #1c1a14;
      --color-text-muted:     #6b6659;
      --color-primary:        #7c4b1a;
      --color-primary-hover:  #5e3612;
      --color-primary-hi:     #f0e8df;
      --color-success:        #3a7022;
      --color-success-hi:     #d6e8ce;
      --color-error:          #b02a2a;
      --color-error-hi:       #f5d9d9;
      --radius-lg: 0.75rem;
      --radius-xl: 1rem;
      --radius-full: 9999px;
      --shadow-sm: 0 1px 3px rgba(28,20,8,0.08);
      --shadow-md: 0 4px 12px rgba(28,20,8,0.1);
      --font-body: 'DM Sans', sans-serif;
      --font-mono: 'DM Mono', monospace;
      --trans: 180ms cubic-bezier(0.16, 1, 0.3, 1);
    }
    [data-theme="dark"] {
      --color-bg:             #121009;
      --color-surface:        #1a1710;
      --color-surface-2:      #211e16;
      --color-surface-offset: #272318;
      --color-border:         rgba(255,240,200,0.1);
      --color-divider:        rgba(255,240,200,0.06);
      --color-text:           #e8e2d5;
      --color-text-muted:     #9a9383;
      --color-primary:        #d4883a;
      --color-primary-hover:  #c07028;
      --color-primary-hi:     #3a2a14;
      --color-success:        #6ab845;
      --color-success-hi:     #1e3213;
      --color-error:          #e05555;
      --color-error-hi:       #3a1010;
      --shadow-sm: 0 1px 3px rgba(0,0,0,0.3);
      --shadow-md: 0 4px 12px rgba(0,0,0,0.4);
    }
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    html { -webkit-font-smoothing: antialiased; }
    body {
      font-family: var(--font-body);
      color: var(--color-text);
      background: var(--color-bg);
      min-height: 100dvh;
      transition: background var(--trans), color var(--trans);
    }
    button { cursor: pointer; border: none; background: none; font: inherit; color: inherit; }
    input  { font: inherit; color: inherit; }
    .app { max-width: 960px; margin: 0 auto; padding: 1.5rem 1rem; display: grid; gap: 1.5rem; }
    .header { display: flex; align-items: center; justify-content: space-between; gap: 1rem; }
    .logo { display: flex; align-items: center; gap: 0.75rem; }
    .logo-icon { width: 36px; height: 36px; color: var(--color-primary); }
    .logo h1 { font-size: clamp(1.5rem,4vw,2.25rem); font-weight: 700; letter-spacing: -0.02em; line-height: 1; }
    .logo span { color: var(--color-primary); }
    .header-actions { display: flex; gap: 0.5rem; align-items: center; }
    .conn-badge { display: flex; align-items: center; gap: 0.5rem; font-size: 0.8rem; color: var(--color-text-muted); background: var(--color-surface); border: 1px solid var(--color-border); border-radius: var(--radius-full); padding: 0.25rem 0.75rem; }
    .conn-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--color-text-muted); transition: background var(--trans); }
    .conn-dot.connected { background: var(--color-success); box-shadow: 0 0 6px var(--color-success); }
    .conn-dot.error { background: var(--color-error); box-shadow: 0 0 6px var(--color-error); }
    .btn-icon { width: 36px; height: 36px; border-radius: 0.5rem; display: flex; align-items: center; justify-content: center; background: var(--color-surface); border: 1px solid var(--color-border); color: var(--color-text-muted); transition: background var(--trans); }
    .btn-icon:hover { background: var(--color-surface-offset); color: var(--color-text); }
    .mug-warning { display: none; align-items: center; gap: 0.75rem; padding: 0.75rem 1rem; background: var(--color-error-hi); border: 1px solid var(--color-error); border-radius: var(--radius-lg); color: var(--color-error); font-weight: 600; font-size: 0.9rem; animation: blink-border 1.5s ease-in-out infinite; }
    .mug-warning.visible { display: flex; }
    @keyframes blink-border { 0%, 100% { border-color: var(--color-error); } 50% { border-color: transparent; } }
    .status-row { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 1rem; }
    .stat-card { background: var(--color-surface); border: 1px solid var(--color-border); border-radius: var(--radius-xl); padding: 1rem; box-shadow: var(--shadow-sm); }
    .stat-label { font-size: 0.75rem; color: var(--color-text-muted); text-transform: uppercase; letter-spacing: 0.06em; font-weight: 500; margin-bottom: 0.25rem; }
    .stat-value { font-size: clamp(1.5rem,3vw,2.25rem); font-weight: 700; letter-spacing: -0.02em; line-height: 1.1; font-family: var(--font-mono); }
    .stat-value.accent { color: var(--color-primary); }
    .stat-sub { font-size: 0.75rem; color: var(--color-text-muted); margin-top: 0.25rem; }
    .mug-chip { display: inline-flex; align-items: center; gap: 6px; font-size: 0.875rem; font-weight: 600; padding: 0.25rem 0.75rem; border-radius: var(--radius-full); background: var(--color-success-hi); color: var(--color-success); transition: background var(--trans), color var(--trans); }
    .mug-chip.absent { background: var(--color-error-hi); color: var(--color-error); }
    .mug-dot { width: 8px; height: 8px; border-radius: 50%; background: currentColor; }
    .brew-chip { display: none; align-items: center; gap: 6px; font-size: 0.75rem; font-weight: 600; padding: 0.25rem 0.75rem; border-radius: var(--radius-full); background: var(--color-primary-hi); color: var(--color-primary); margin-top: 0.5rem; animation: pulse 1.4s ease-in-out infinite; }
    .brew-chip.visible { display: inline-flex; }
    @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.55; } }
    .main-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 1.5rem; }
    @media (max-width: 600px) { .main-grid { grid-template-columns: 1fr; } }
    .panel { background: var(--color-surface); border: 1px solid var(--color-border); border-radius: var(--radius-xl); padding: 1.5rem; box-shadow: var(--shadow-sm); }
    .panel-title { font-size: 1rem; font-weight: 700; margin-bottom: 1rem; display: flex; align-items: center; gap: 0.5rem; }
    .panel-icon { color: var(--color-primary); width: 18px; height: 18px; }
    .toggle-row { display: flex; align-items: center; justify-content: space-between; margin-bottom: 1rem; padding-bottom: 1rem; border-bottom: 1px solid var(--color-divider); }
    .toggle-label { font-size: 0.85rem; color: var(--color-text-muted); }
    .toggle { position: relative; width: 48px; height: 26px; background: var(--color-surface-offset); border: 1px solid var(--color-border); border-radius: var(--radius-full); cursor: pointer; transition: background var(--trans); }
    .toggle.on { background: var(--color-primary); border-color: var(--color-primary); }
    .toggle::after { content: ''; position: absolute; top: 2px; left: 2px; width: 20px; height: 20px; border-radius: 50%; background: white; box-shadow: var(--shadow-sm); transition: left var(--trans); }
    .toggle.on::after { left: 24px; }
    .time-input-row { display: flex; gap: 0.75rem; align-items: flex-end; margin-bottom: 1rem; }
    .time-field { flex: 1; }
    .time-field label { display: block; font-size: 0.75rem; color: var(--color-text-muted); text-transform: uppercase; letter-spacing: 0.06em; font-weight: 500; margin-bottom: 0.25rem; }
    .time-field input[type="number"] { width: 100%; padding: 0.5rem; background: var(--color-surface-2); border: 1px solid var(--color-border); border-radius: 0.5rem; font-size: 1.4rem; font-family: var(--font-mono); font-weight: 600; text-align: center; -moz-appearance: textfield; }
    .time-field input[type="number"]::-webkit-inner-spin-button, .time-field input[type="number"]::-webkit-outer-spin-button { -webkit-appearance: none; }
    .time-field input[type="number"]:focus { border-color: var(--color-primary); outline: none; box-shadow: 0 0 0 3px rgba(212,136,58,0.2); }
    .time-sep { font-size: 1.8rem; font-family: var(--font-mono); font-weight: 700; color: var(--color-text-muted); padding-bottom: 0.4rem; }
    .btn { display: inline-flex; align-items: center; justify-content: center; gap: 0.5rem; padding: 0.6rem 1.5rem; border-radius: 0.5rem; font-size: 0.9rem; font-weight: 600; cursor: pointer; border: none; min-height: 44px; transition: background var(--trans), color var(--trans), box-shadow var(--trans); }
    .btn-primary { background: var(--color-primary); color: white; width: 100%; }
    .btn-primary:hover { background: var(--color-primary-hover); box-shadow: var(--shadow-md); }
    .btn-cancel { background: var(--color-error-hi); color: var(--color-error); border: 1px solid var(--color-error); width: 100%; margin-top: 0.75rem; }
    .btn-cancel:hover { background: var(--color-error); color: white; }
    .btn-cancel.cancelled { background: var(--color-success-hi); color: var(--color-success); border-color: var(--color-success); }
    .btn-cancel.cancelled:hover { background: var(--color-success); color: white; }
    .log-list { list-style: none; display: flex; flex-direction: column; gap: 0.5rem; max-height: 220px; overflow-y: auto; }
    .log-item { display: flex; gap: 0.75rem; align-items: flex-start; font-size: 0.85rem; }
    .log-time { font-family: var(--font-mono); font-size: 0.75rem; color: var(--color-text-muted); white-space: nowrap; padding-top: 2px; }
    .log-msg { color: var(--color-text-muted); }
    .log-dot { width: 6px; height: 6px; border-radius: 50%; background: var(--color-primary); margin-top: 6px; flex-shrink: 0; }
    .log-dot.error { background: var(--color-error); }
    .log-dot.success { background: var(--color-success); }
    .log-empty { font-size: 0.85rem; color: var(--color-text-muted); text-align: center; padding: 3rem 0; }
  </style>
</head>
<body>
<div class="app">

  <header class="header">
    <div class="logo">
      <svg class="logo-icon" viewBox="0 0 36 36" fill="none" aria-hidden="true">
        <rect x="8" y="6" width="14" height="18" rx="3" stroke="currentColor" stroke-width="2"/>
        <path d="M22 11 C26 11 28 14 26 17 C24 20 22 19 22 19" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
        <path d="M11 28 C11 28 9 24 10 22" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
        <path d="M19 28 C19 28 21 24 20 22" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
        <line x1="10" y1="28" x2="20" y2="28" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
      </svg>
      <h1>Smart<span>Coffee</span></h1>
    </div>
    <div class="header-actions">
      <div class="conn-badge">
        <div class="conn-dot" id="connDot"></div>
        <span id="connLabel">Connecting…</span>
      </div>
      <button class="btn-icon" id="themeBtn" aria-label="Toggle theme">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/></svg>
      </button>
    </div>
  </header>

  <div class="mug-warning" id="mugWarning" role="alert">
    <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>
    No mug detected — alarm will not fire until a mug is placed!
  </div>

  <div class="status-row">
    <div class="stat-card">
      <div class="stat-label">Current Time</div>
      <div class="stat-value accent" id="dispTime">--:--</div>
    </div>
    <div class="stat-card">
      <div class="stat-label">Temperature</div>
      <div class="stat-value" id="dispTemp">--</div>
      <div class="stat-sub">°C · DHT11</div>
    </div>
    <div class="stat-card">
      <div class="stat-label">Next Alarm</div>
      <div class="stat-value" id="dispAlarm">--:--</div>
      <div class="stat-sub" id="dispAlarmSub">Disabled</div>
    </div>
    <div class="stat-card">
      <div class="stat-label">Mug Status</div>
      <div>
        <span class="mug-chip" id="mugChip"><span class="mug-dot"></span><span id="mugText">Checking…</span></span>
      </div>
      <div class="brew-chip" id="brewChip">☕ Brewing…</div>
    </div>
  </div>

  <div class="main-grid">
    <div class="panel">
      <div class="panel-title">
        <svg class="panel-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
        Alarm Schedule
      </div>
      <div class="toggle-row">
        <div>
          <div style="font-size:0.9rem;font-weight:600">Automatic Alarm</div>
          <div class="toggle-label">Enable scheduled brewing</div>
        </div>
        <div class="toggle" id="alarmToggle" role="switch" aria-checked="false" tabindex="0"
             onclick="toggleAlarm()" onkeydown="if(event.key==='Enter'||event.key===' ')toggleAlarm()"></div>
      </div>
      <div class="time-input-row">
        <div class="time-field">
          <label for="inHour">Hour</label>
          <input type="number" id="inHour" min="0" max="23" value="7">
        </div>
        <div class="time-sep">:</div>
        <div class="time-field">
          <label for="inMin">Minute</label>
          <input type="number" id="inMin" min="0" max="59" value="0">
        </div>
      </div>
      <button class="btn btn-primary" onclick="setAlarm()">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><polyline points="20 6 9 17 4 12"/></svg>
        Set Alarm
      </button>
      <button class="btn btn-cancel" id="cancelBtn" onclick="toggleCancel()">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
        Cancel Next Alarm
      </button>
    </div>

    <div class="panel">
      <div class="panel-title">
        <svg class="panel-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>
        Activity Log
      </div>
      <ul class="log-list" id="logList">
        <li class="log-empty">No activity yet</li>
      </ul>
    </div>
  </div>

</div>

<script>
// ── Theme ──────────────────────────────────────────────────
(function() {
  const root = document.documentElement;
  let theme = 'dark';
  const btn = document.getElementById('themeBtn');
  function setIcon(t) {
    btn.innerHTML = t === 'dark'
      ? '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="5"/><path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/></svg>'
      : '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/></svg>';
  }
  setIcon(theme);
  btn.addEventListener('click', () => {
    theme = theme === 'dark' ? 'light' : 'dark';
    root.setAttribute('data-theme', theme);
    setIcon(theme);
  });
})();

// ── State ──────────────────────────────────────────────────
let ws;
let alarmCancelled = false;
let alarmEnabled   = false;
let logs           = [];

// ── WebSocket ──────────────────────────────────────────────
function connect() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen    = () => { setConn('connected'); addLog('Connected to ESP32', 'success'); };
  ws.onclose   = () => { setConn('disconnected'); addLog('Disconnected — retrying…', 'error'); setTimeout(connect, 3000); };
  ws.onerror   = () => setConn('error');
  ws.onmessage = (e) => { try { applyState(JSON.parse(e.data)); } catch(err) {} };
}

function send(obj) {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
}

function setConn(state) {
  const dot   = document.getElementById('connDot');
  const label = document.getElementById('connLabel');
  dot.className = 'conn-dot' + (state === 'connected' ? ' connected' : state === 'error' ? ' error' : '');
  label.textContent = state === 'connected' ? 'Connected' : state === 'error' ? 'Error' : 'Disconnected';
}

// ── Apply State ────────────────────────────────────────────
function applyState(s) {
  if (s.time !== undefined) document.getElementById('dispTime').textContent  = s.time;
  if (s.temp !== undefined) document.getElementById('dispTemp').textContent  = Number(s.temp).toFixed(1);
  if (s.alarm !== undefined) document.getElementById('dispAlarm').textContent = s.alarm;

  alarmEnabled   = !!s.alarmOn;
  alarmCancelled = !!s.cancelled;

  const sub = document.getElementById('dispAlarmSub');
  sub.textContent = !alarmEnabled ? 'Disabled' : alarmCancelled ? 'Cancelled' : 'Scheduled';

  const tog = document.getElementById('alarmToggle');
  tog.classList.toggle('on', alarmEnabled);
  tog.setAttribute('aria-checked', alarmEnabled);

  const cb = document.getElementById('cancelBtn');
  if (alarmCancelled) {
    cb.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><polyline points="20 6 9 17 4 12"/></svg> Restore Next Alarm';
    cb.classList.add('cancelled');
  } else {
    cb.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg> Cancel Next Alarm';
    cb.classList.remove('cancelled');
  }

  const mug = !!s.mug;
  document.getElementById('mugText').textContent = mug ? 'Mug Ready' : 'No Mug!';
  document.getElementById('mugChip').classList.toggle('absent', !mug);
  document.getElementById('mugWarning').classList.toggle('visible', !mug);
  document.getElementById('brewChip').classList.toggle('visible', !!s.brewing);
}

// ── Controls ───────────────────────────────────────────────
function toggleAlarm() {
  alarmEnabled = !alarmEnabled;
  send({ alarmOn: alarmEnabled });
  addLog(alarmEnabled ? 'Alarm enabled' : 'Alarm disabled', alarmEnabled ? 'success' : 'info');
}

function setAlarm() {
  const h = parseInt(document.getElementById('inHour').value);
  const m = parseInt(document.getElementById('inMin').value);
  if (isNaN(h)||h<0||h>23||isNaN(m)||m<0||m>59) { addLog('Invalid time', 'error'); return; }
  send({ alarmHour: h, alarmMinute: m });
  const hh = String(h).padStart(2,'0'), mm = String(m).padStart(2,'0');
  addLog('Alarm set to ' + hh + ':' + mm, 'success');
}

function toggleCancel() {
  alarmCancelled = !alarmCancelled;
  send({ cancel: alarmCancelled });
  addLog(alarmCancelled ? 'Next alarm cancelled' : 'Alarm restored', alarmCancelled ? 'error' : 'success');
}

// ── Log ────────────────────────────────────────────────────
function addLog(msg, type) {
  const ts = new Date().toLocaleTimeString('en-GB');
  logs.unshift({ ts, msg, type: type || 'info' });
  if (logs.length > 30) logs.pop();
  const ul = document.getElementById('logList');
  ul.innerHTML = logs.map(l =>
    `<li class="log-item">
      <span class="log-dot ${l.type === 'error' ? 'error' : l.type === 'success' ? 'success' : ''}"></span>
      <span class="log-time">${l.ts}</span>
      <span class="log-msg">${l.msg}</span>
    </li>`
  ).join('');
}

// ── Boot ───────────────────────────────────────────────────
addLog('Dashboard loaded');
connect();
</script>
</body>
</html>
)=====";

// ────────────────────────────────────────────────────────────
//  SETUP
// ────────────────────────────────────────────────────────────
void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  pinMode(PIN_MUG_SENSOR, INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // OLED — I2C on default GPIO 21 (SDA) / 22 (SCL)
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 12, "Connecting WiFi...");
  u8g2.sendBuffer();

  // DHT11
  dht.begin();
  delay(2000); // stabilise before first read

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());

  // NTP — TZ string method (same as your working code)
  configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
    setenv("TZ",