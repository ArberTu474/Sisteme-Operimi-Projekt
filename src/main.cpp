#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <time.h>

// ==========================
// Wi-Fi
// ==========================
const char *WIFI_SSID = "Stupid";
const char *WIFI_PASS = "12345678910";

// Albania / Central Europe with DST
const char *TZ_INFO = "CET-1CEST,M3.5.0/2,M10.5.0/3";
const char *NTP_SERVER_1 = "pool.ntp.org";
const char *NTP_SERVER_2 = "time.nist.gov";

// ==========================
// Pins
// ==========================
constexpr uint8_t PIN_LED = 2;              // alarm LED
constexpr uint8_t PIN_READY_GREEN_LED = 25; // green indicator
constexpr uint8_t PIN_WARN_RED_LED = 26;    // red indicator
constexpr uint8_t PIN_CANCEL_BUTTON = 27;   // physical cancel/resume button
constexpr uint8_t PIN_MUG_SENSOR = 34;      // mug sensor input only
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;

// Most IR obstacle modules go LOW when object is detected
constexpr uint8_t MUG_PRESENT_LEVEL = LOW;

// ==========================
// Timing
// ==========================
constexpr unsigned long OLED_INTERVAL_MS = 500;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 50;
constexpr unsigned long BREW_OUTPUT_MS = 15000;
constexpr unsigned long MUG_GRACE_WINDOW_MS = 10UL * 60UL * 1000UL; // 10 min
constexpr unsigned long MUG_DETECTED_DELAY_MS = 5000;               // 5 sec
constexpr unsigned long MUG_STABLE_MS = 300;                        // anti-flicker

// ==========================
// Objects
// ==========================
WebServer server(80);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ==========================
// State
// ==========================
struct AlarmConfig
{
  bool autoEnabled = true;
  int hour = 8;
  int minute = 0;

  int lastHandledYear = -1;
  int lastHandledYday = -1;

  bool cancelScheduled = false;
  int cancelYear = -1;
  int cancelYday = -1;
};

AlarmConfig alarmCfg;

bool mugPresent = false;

bool alarmOutputActive = false;
unsigned long alarmOutputStartedMs = 0;

bool waitingForMug = false;
unsigned long waitingForMugStartedMs = 0;

bool mugDelayActive = false;
unsigned long mugDelayStartedMs = 0;
unsigned long mugStableStartMs = 0;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long lastButtonChangeMs = 0;

unsigned long lastOledMs = 0;

// ==========================
// Web page
// ==========================
const char MAIN_PAGE[] PROGMEM = R"rawliteral(
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
      --color-bg: #f5f3ef;
      --color-surface: #faf9f6;
      --color-surface-2: #ffffff;
      --color-surface-offset: #ede9e3;
      --color-border: rgba(40,30,15,0.12);
      --color-divider: rgba(40,30,15,0.08);
      --color-text: #1c1a14;
      --color-text-muted: #6b6659;
      --color-text-faint: #b0ada5;
      --color-primary: #7c4b1a;
      --color-primary-hover: #5e3612;
      --color-primary-hi: #f0e8df;
      --color-success: #3a7022;
      --color-success-hi: #d6e8ce;
      --color-error: #b02a2a;
      --color-error-hi: #f5d9d9;
      --color-warning: #b06010;
      --color-warning-hi: #f5e6d4;
      --radius-sm: 0.375rem;
      --radius-md: 0.5rem;
      --radius-lg: 0.75rem;
      --radius-xl: 1rem;
      --radius-full: 9999px;
      --shadow-sm: 0 1px 3px rgba(28,20,8,0.08);
      --shadow-md: 0 4px 12px rgba(28,20,8,0.1);
      --shadow-lg: 0 12px 32px rgba(28,20,8,0.14);
      --font-body: 'DM Sans', sans-serif;
      --font-mono: 'DM Mono', monospace;
      --text-xs: clamp(0.75rem, 0.7rem + 0.25vw, 0.875rem);
      --text-sm: clamp(0.875rem, 0.8rem + 0.35vw, 1rem);
      --text-base: clamp(1rem, 0.95rem + 0.25vw, 1.125rem);
      --text-lg: clamp(1.125rem, 1rem + 0.75vw, 1.5rem);
      --text-xl: clamp(1.5rem, 1.2rem + 1.25vw, 2.25rem);
      --space-1: 0.25rem;
      --space-2: 0.5rem;
      --space-3: 0.75rem;
      --space-4: 1rem;
      --space-6: 1.5rem;
      --space-8: 2rem;
      --space-10: 2.5rem;
      --space-12: 3rem;
      --trans: 180ms cubic-bezier(0.16, 1, 0.3, 1);
    }

    [data-theme="dark"] {
      --color-bg: #121009;
      --color-surface: #1a1710;
      --color-surface-2: #211e16;
      --color-surface-offset: #272318;
      --color-border: rgba(255,240,200,0.1);
      --color-divider: rgba(255,240,200,0.06);
      --color-text: #e8e2d5;
      --color-text-muted: #9a9383;
      --color-text-faint: #5a5549;
      --color-primary: #d4883a;
      --color-primary-hover: #c07028;
      --color-primary-hi: #3a2a14;
      --color-success: #6ab845;
      --color-success-hi: #1e3213;
      --color-error: #e05555;
      --color-error-hi: #3a1010;
      --color-warning: #e09040;
      --color-warning-hi: #3a2408;
      --shadow-sm: 0 1px 3px rgba(0,0,0,0.3);
      --shadow-md: 0 4px 12px rgba(0,0,0,0.4);
      --shadow-lg: 0 12px 32px rgba(0,0,0,0.5);
    }

    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    html { scroll-behavior: smooth; -webkit-font-smoothing: antialiased; }
    body {
      font-family: var(--font-body);
      font-size: var(--text-base);
      color: var(--color-text);
      background: var(--color-bg);
      min-height: 100dvh;
      line-height: 1.6;
      transition: background var(--trans), color var(--trans);
    }
    button { cursor: pointer; border: none; background: none; font: inherit; color: inherit; }
    input { font: inherit; color: inherit; }
    a, button, input {
      transition: color var(--trans), background var(--trans), border-color var(--trans), box-shadow var(--trans), transform var(--trans);
    }
    :focus-visible {
      outline: 2px solid var(--color-primary);
      outline-offset: 3px;
      border-radius: var(--radius-sm);
    }

    .app {
      max-width: 980px;
      margin: 0 auto;
      padding: var(--space-6) var(--space-4);
      display: grid;
      gap: var(--space-6);
    }

    .header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: var(--space-4);
    }

    .logo {
      display: flex;
      align-items: center;
      gap: var(--space-3);
    }

    .logo-icon {
      width: 36px;
      height: 36px;
      color: var(--color-primary);
      flex-shrink: 0;
    }

    .logo h1 {
      font-size: var(--text-xl);
      font-weight: 700;
      letter-spacing: -0.02em;
      line-height: 1;
    }

    .logo span { color: var(--color-primary); }

    .header-actions {
      display: flex;
      gap: var(--space-2);
      align-items: center;
    }

    .btn-icon {
      width: 36px;
      height: 36px;
      border-radius: var(--radius-md);
      display: flex;
      align-items: center;
      justify-content: center;
      background: var(--color-surface);
      border: 1px solid var(--color-border);
      color: var(--color-text-muted);
    }

    .btn-icon:hover {
      background: var(--color-surface-offset);
      color: var(--color-text);
    }

    .banner {
      display: none;
      align-items: center;
      gap: var(--space-3);
      padding: var(--space-3) var(--space-4);
      border-radius: var(--radius-lg);
      font-weight: 600;
      font-size: var(--text-sm);
      border: 1px solid transparent;
    }

    .banner.visible { display: flex; }

    .banner.warn {
      background: var(--color-error-hi);
      border-color: var(--color-error);
      color: var(--color-error);
      animation: blink-border 1.5s ease-in-out infinite;
    }

    .banner.pending {
      background: var(--color-warning-hi);
      border-color: var(--color-warning);
      color: var(--color-warning);
    }

    @keyframes blink-border {
      0%, 100% { border-color: var(--color-error); }
      50% { border-color: transparent; }
    }

    .status-row {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
      gap: var(--space-4);
    }

    .stat-card {
      background: var(--color-surface);
      border: 1px solid var(--color-border);
      border-radius: var(--radius-xl);
      padding: var(--space-4);
      box-shadow: var(--shadow-sm);
    }

    .stat-label {
      font-size: var(--text-xs);
      color: var(--color-text-muted);
      text-transform: uppercase;
      letter-spacing: 0.06em;
      font-weight: 500;
      margin-bottom: var(--space-1);
    }

    .stat-value {
      font-size: var(--text-xl);
      font-weight: 700;
      letter-spacing: -0.02em;
      line-height: 1.1;
      font-family: var(--font-mono);
      color: var(--color-text);
    }

    .stat-value.accent { color: var(--color-primary); }

    .stat-sub {
      font-size: var(--text-xs);
      color: var(--color-text-muted);
      margin-top: var(--space-1);
    }

    .mug-chip,
    .led-chip,
    .brew-chip {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      font-size: var(--text-sm);
      font-weight: 600;
      padding: var(--space-1) var(--space-3);
      border-radius: var(--radius-full);
    }

    .mug-chip {
      background: var(--color-success-hi);
      color: var(--color-success);
    }

    .mug-chip.absent {
      background: var(--color-error-hi);
      color: var(--color-error);
    }

    .led-chip {
      background: var(--color-surface-offset);
      color: var(--color-text-muted);
      margin-right: 6px;
      margin-top: 4px;
    }

    .led-chip.on.green {
      background: var(--color-success-hi);
      color: var(--color-success);
    }

    .led-chip.on.red {
      background: var(--color-error-hi);
      color: var(--color-error);
    }

    .mug-dot,
    .led-dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background: currentColor;
    }

    .brew-chip {
      display: none;
      margin-top: var(--space-2);
      background: var(--color-primary-hi);
      color: var(--color-primary);
      animation: pulse 1.4s ease-in-out infinite;
    }

    .brew-chip.visible { display: inline-flex; }

    @keyframes pulse {
      0%,100% { opacity: 1; }
      50% { opacity: 0.55; }
    }

    .main-grid {
      display: grid;
      grid-template-columns: 1.1fr 0.9fr;
      gap: var(--space-6);
    }

    @media (max-width: 760px) {
      .main-grid { grid-template-columns: 1fr; }
      .header { align-items: flex-start; flex-direction: column; }
    }

    .panel {
      background: var(--color-surface);
      border: 1px solid var(--color-border);
      border-radius: var(--radius-xl);
      padding: var(--space-6);
      box-shadow: var(--shadow-sm);
    }

    .panel-title {
      font-size: var(--text-base);
      font-weight: 700;
      margin-bottom: var(--space-4);
      display: flex;
      align-items: center;
      gap: var(--space-2);
      color: var(--color-text);
    }

    .panel-icon {
      color: var(--color-primary);
      width: 18px;
      height: 18px;
      flex-shrink: 0;
    }

    .toggle-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: var(--space-4);
      padding-bottom: var(--space-4);
      border-bottom: 1px solid var(--color-divider);
      gap: var(--space-4);
    }

    .toggle-label {
      font-size: var(--text-sm);
      color: var(--color-text-muted);
    }

    .toggle {
      position: relative;
      width: 48px;
      height: 26px;
      background: var(--color-surface-offset);
      border: 1px solid var(--color-border);
      border-radius: var(--radius-full);
      cursor: pointer;
      transition: background var(--trans);
      flex-shrink: 0;
    }

    .toggle.on {
      background: var(--color-primary);
      border-color: var(--color-primary);
    }

    .toggle::after {
      content: '';
      position: absolute;
      top: 2px;
      left: 2px;
      width: 20px;
      height: 20px;
      border-radius: 50%;
      background: white;
      box-shadow: var(--shadow-sm);
      transition: left var(--trans);
    }

    .toggle.on::after { left: 24px; }

    .time-field label {
      display: block;
      font-size: var(--text-xs);
      color: var(--color-text-muted);
      text-transform: uppercase;
      letter-spacing: 0.06em;
      font-weight: 500;
      margin-bottom: var(--space-1);
    }

    .time-field input[type="time"] {
      width: 100%;
      padding: var(--space-3) var(--space-3);
      background: var(--color-surface-2);
      border: 1px solid var(--color-border);
      border-radius: var(--radius-md);
      font-size: var(--text-lg);
      font-family: var(--font-mono);
      font-weight: 600;
    }

    .time-field input[type="time"]:focus {
      border-color: var(--color-primary);
      outline: none;
      box-shadow: 0 0 0 3px rgba(124,75,26,0.2);
    }

    .btn {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: var(--space-2);
      padding: var(--space-2) var(--space-6);
      border-radius: var(--radius-md);
      font-size: var(--text-sm);
      font-weight: 600;
      border: none;
      min-height: 44px;
      width: 100%;
    }

    .btn-primary {
      background: var(--color-primary);
      color: white;
      margin-top: var(--space-4);
    }

    .btn-primary:hover {
      background: var(--color-primary-hover);
      box-shadow: var(--shadow-md);
    }

    .btn-cancel {
      background: var(--color-error-hi);
      color: var(--color-error);
      border: 1px solid var(--color-error);
      margin-top: var(--space-3);
    }

    .btn-cancel:hover {
      background: var(--color-error);
      color: white;
    }

    .btn-cancel.cancelled {
      background: var(--color-success-hi);
      color: var(--color-success);
      border-color: var(--color-success);
    }

    .btn-cancel.cancelled:hover {
      background: var(--color-success);
      color: white;
    }

    .log-list {
      list-style: none;
      display: flex;
      flex-direction: column;
      gap: var(--space-2);
      max-height: 260px;
      overflow-y: auto;
    }

    .log-item {
      display: flex;
      gap: var(--space-3);
      align-items: flex-start;
      font-size: var(--text-sm);
    }

    .log-time {
      font-family: var(--font-mono);
      font-size: var(--text-xs);
      color: var(--color-text-faint);
      white-space: nowrap;
      padding-top: 2px;
    }

    .log-msg {
      color: var(--color-text-muted);
    }

    .log-dot {
      width: 6px;
      height: 6px;
      border-radius: 50%;
      background: var(--color-primary);
      margin-top: 6px;
      flex-shrink: 0;
    }

    .log-dot.error { background: var(--color-error); }
    .log-dot.success { background: var(--color-success); }

    .log-empty {
      font-size: var(--text-sm);
      color: var(--color-text-faint);
      text-align: center;
      padding: var(--space-8) 0;
    }

    .status-note {
      margin-top: var(--space-4);
      padding-top: var(--space-4);
      border-top: 1px solid var(--color-divider);
      color: var(--color-text-muted);
      font-size: var(--text-sm);
    }
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
          <path d="M12 10 Q15 8 18 10" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" opacity="0.6"/>
          <path d="M14 7 Q15 5 16 7" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" opacity="0.4"/>
        </svg>
        <h1>Smart<span>Coffee</span></h1>
      </div>

      <div class="header-actions">
        <button class="btn-icon" id="themeBtn" aria-label="Toggle theme" title="Toggle theme">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>
          </svg>
        </button>
      </div>
    </header>

    <div class="banner warn" id="warningBox" role="alert">
      <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" aria-hidden="true">
        <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/>
        <line x1="12" y1="9" x2="12" y2="13"/>
        <line x1="12" y1="17" x2="12.01" y2="17"/>
      </svg>
      No mug detected — the next active alarm is blocked until a mug is placed.
    </div>

    <div class="banner pending" id="pendingBox" role="status">
      <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" aria-hidden="true">
        <circle cx="12" cy="12" r="10"/>
        <polyline points="12 6 12 12 16 14"/>
      </svg>
      Alarm triggered with no mug. Waiting up to 10 minutes. When a mug is detected, coffee starts after 5 seconds.
    </div>

    <div class="status-row">
      <div class="stat-card">
        <div class="stat-label">Current Time</div>
        <div class="stat-value accent" id="timeText">--:--:--</div>
      </div>

      <div class="stat-card">
        <div class="stat-label">Next Alarm</div>
        <div class="stat-value" id="alarmText">--</div>
        <div class="stat-sub" id="alarmSub">Loading…</div>
      </div>

      <div class="stat-card">
        <div class="stat-label">Mug Status</div>
        <div>
          <span class="mug-chip" id="mugChip">
            <span class="mug-dot"></span>
            <span id="mugText">Checking…</span>
          </span>
        </div>
        <div class="brew-chip" id="brewChip">
          <svg width="12" height="12" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
            <path d="M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zm1 11H11V7h2v6zm0 4H11v-2h2v2z"/>
          </svg>
          Alarm output active
        </div>
      </div>
    </div>

    <div class="main-grid">
      <div class="panel">
        <div class="panel-title">
          <svg class="panel-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true">
            <circle cx="12" cy="12" r="10"/>
            <polyline points="12 6 12 12 16 14"/>
          </svg>
          Alarm Schedule
        </div>

        <div class="toggle-row">
          <div>
            <div style="font-size:var(--text-sm);font-weight:600;">Automatic Alarm</div>
            <div class="toggle-label">Enable scheduled brewing</div>
          </div>
          <div class="toggle" id="autoToggleUI" role="switch" aria-checked="false" tabindex="0"></div>
        </div>

        <div class="time-field">
          <label for="alarmInput">Alarm Time</label>
          <input type="time" id="alarmInput" value="08:00">
        </div>

        <button class="btn btn-primary" onclick="setAlarm()">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" aria-hidden="true">
            <polyline points="20 6 9 17 4 12"/>
          </svg>
          Save Alarm
        </button>

        <button class="btn btn-cancel" id="toggleCancelBtn" onclick="toggleCancel()">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" aria-hidden="true">
            <line x1="18" y1="6" x2="6" y2="18"/>
            <line x1="6" y1="6" x2="18" y2="18"/>
          </svg>
          Cancel Next Alarm
        </button>

        <div class="status-note" id="statusLine">Loading...</div>
      </div>

      <div class="panel">
        <div class="panel-title">
          <svg class="panel-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true">
            <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
            <polyline points="14 2 14 8 20 8"/>
            <line x1="16" y1="13" x2="8" y2="13"/>
            <line x1="16" y1="17" x2="8" y2="17"/>
            <polyline points="10 9 9 9 8 9"/>
          </svg>
          Activity Log
        </div>

        <ul class="log-list" id="logList">
          <li class="log-empty">No activity yet</li>
        </ul>
      </div>
    </div>
  </div>

<script>
  let logEntries = [];
  let prevStateKey = "";
  let currentState = {
    autoEnabled: false,
    cancelled: false,
    mugPresent: false,
    waitingForMug: false,
    mugDelayActive: false,
    alarmOutputActive: false,
    greenLedOn: false,
    redLedOn: false
  };

  function addLog(msg, type = "info") {
    const now = new Date();
    const ts = now.toLocaleTimeString('en-GB', {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    });

    logEntries.unshift({ ts, msg, type });
    if (logEntries.length > 30) logEntries.pop();
    renderLog();
  }

  function renderLog() {
    const ul = document.getElementById('logList');
    if (!logEntries.length) {
      ul.innerHTML = '<li class="log-empty">No activity yet</li>';
      return;
    }

    ul.innerHTML = logEntries.map(e => `
      <li class="log-item">
        <span class="log-dot ${e.type === 'error' ? 'error' : e.type === 'success' ? 'success' : ''}"></span>
        <span class="log-time">${e.ts}</span>
        <span class="log-msg">${e.msg}</span>
      </li>
    `).join('');
  }

  function parseAlarmTimeForInput(nextAlarmText) {
    const match = String(nextAlarmText).match(/(\d{2}:\d{2})/);
    return match ? match[1] : null;
  }

  function updateStateLogs(s) {
    const key = JSON.stringify({
      autoEnabled: s.autoEnabled,
      cancelled: s.cancelled,
      mugPresent: s.mugPresent,
      waitingForMug: s.waitingForMug,
      mugDelayActive: s.mugDelayActive,
      alarmOutputActive: s.alarmOutputActive,
      greenLedOn: s.greenLedOn,
      redLedOn: s.redLedOn,
      nextAlarm: s.nextAlarm
    });

    if (!prevStateKey) {
      prevStateKey = key;
      addLog('Dashboard connected', 'success');
      return;
    }

    if (currentState.autoEnabled !== s.autoEnabled) {
      addLog(s.autoEnabled ? 'Automatic alarm enabled' : 'Automatic alarm disabled', s.autoEnabled ? 'success' : 'error');
    }

    if (currentState.cancelled !== s.cancelled) {
      addLog(s.cancelled ? 'Next alarm cancelled' : 'Next alarm restored', s.cancelled ? 'error' : 'success');
    }

    if (currentState.waitingForMug !== s.waitingForMug && s.waitingForMug) {
      addLog('Alarm triggered without mug — waiting up to 10 minutes', 'error');
    }

    if (currentState.mugDelayActive !== s.mugDelayActive && s.mugDelayActive) {
      addLog('Mug detected — starting in 5 seconds', 'success');
    }

    if (currentState.alarmOutputActive !== s.alarmOutputActive) {
      addLog(s.alarmOutputActive ? 'Alarm output turned ON' : 'Alarm output turned OFF', s.alarmOutputActive ? 'success' : 'info');
    }

    prevStateKey = key;
  }

  function applyStatus(s) {
    currentState = { ...s };

    document.getElementById('timeText').textContent = s.time || '--:--:--';
    document.getElementById('alarmText').textContent = s.nextAlarm || '--';

    const alarmSub = document.getElementById('alarmSub');
    if (!s.autoEnabled) alarmSub.textContent = 'Disabled';
    else if (s.cancelled) alarmSub.textContent = 'Cancelled';
    else if (s.waitingForMug) alarmSub.textContent = 'Waiting for mug';
    else if (s.mugDelayActive) alarmSub.textContent = 'Starting in 5s';
    else alarmSub.textContent = 'Scheduled';

    const mugChip = document.getElementById('mugChip');
    const mugText = document.getElementById('mugText');
    if (s.mugPresent) {
      mugChip.classList.remove('absent');
      mugText.textContent = 'Mug Ready';
    } else {
      mugChip.classList.add('absent');
      mugText.textContent = 'No Mug';
    }

    document.getElementById('warningBox').classList.toggle('visible', s.redLedOn);
    document.getElementById('pendingBox').classList.toggle('visible', s.waitingForMug);
    document.getElementById('brewChip').classList.toggle('visible', s.alarmOutputActive);

    const autoToggle = document.getElementById('autoToggleUI');
    autoToggle.classList.toggle('on', !!s.autoEnabled);
    autoToggle.setAttribute('aria-checked', s.autoEnabled ? 'true' : 'false');

    const btn = document.getElementById('toggleCancelBtn');
    if (s.cancelled) {
      btn.classList.add('cancelled');
      btn.innerHTML = `
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" aria-hidden="true">
          <polyline points="20 6 9 17 4 12"/>
        </svg>
        Resume Next Alarm
      `;
    } else {
      btn.classList.remove('cancelled');
      btn.innerHTML = `
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" aria-hidden="true">
          <line x1="18" y1="6" x2="6" y2="18"/>
          <line x1="6" y1="6" x2="18" y2="18"/>
        </svg>
        Cancel Next Alarm
      `;
    }

    let extra = [];
    extra.push('Auto: ' + (s.autoEnabled ? 'ON' : 'OFF'));
    extra.push('Cancelled: ' + (s.cancelled ? 'YES' : 'NO'));
    if (s.waitingForMug) extra.push('Waiting for mug');
    if (s.mugDelayActive) extra.push('Mug found, starting in 5s');
    if (s.alarmOutputActive) extra.push('Alarm output ON');
    document.getElementById('statusLine').textContent = extra.join(' | ');

    const parsed = parseAlarmTimeForInput(s.nextAlarm);
    if (parsed && !document.getElementById('alarmInput').matches(':focus')) {
      document.getElementById('alarmInput').value = parsed;
    }

    updateStateLogs(s);
  }

  async function refreshStatus() {
    try {
      const r = await fetch('/status', { cache: 'no-store' });
      if (!r.ok) throw new Error('HTTP error');
      const s = await r.json();
      applyStatus(s);
    } catch (e) {
      document.getElementById('statusLine').textContent = 'Connection error';
    }
  }

  async function setAlarm() {
    const t = document.getElementById('alarmInput').value;
    if (!t) {
      addLog('Invalid alarm time', 'error');
      return;
    }

    try {
      await fetch('/setAlarm?time=' + encodeURIComponent(t), { method: 'POST' });
      addLog('Alarm saved for ' + t, 'success');
      refreshStatus();
    } catch (e) {
      addLog('Failed to save alarm', 'error');
    }
  }

  async function toggleCancel() {
    try {
      await fetch('/toggleCancel', { method: 'POST' });
      refreshStatus();
    } catch (e) {
      addLog('Failed to change cancel state', 'error');
    }
  }

  async function toggleAuto() {
    const next = !currentState.autoEnabled;
    try {
      await fetch('/auto?enabled=' + (next ? '1' : '0'), { method: 'POST' });
      refreshStatus();
    } catch (e) {
      addLog('Failed to change auto mode', 'error');
    }
  }

  (function initTheme() {
    const root = document.documentElement;
    const btn = document.getElementById('themeBtn');

    function setIcon(theme) {
      btn.innerHTML = theme === 'dark'
        ? '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="5"/><path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/></svg>'
        : '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/></svg>';
    }

    let theme = localStorage.getItem('coffee-theme') || 'dark';
    root.setAttribute('data-theme', theme);
    setIcon(theme);

    btn.addEventListener('click', () => {
      theme = theme === 'dark' ? 'light' : 'dark';
      root.setAttribute('data-theme', theme);
      localStorage.setItem('coffee-theme', theme);
      setIcon(theme);
    });
  })();

  document.getElementById('autoToggleUI').addEventListener('click', toggleAuto);
  document.getElementById('autoToggleUI').addEventListener('keydown', (event) => {
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      toggleAuto();
    }
  });

  document.addEventListener('DOMContentLoaded', () => {
    addLog('Dashboard loaded');
    refreshStatus();
    setInterval(refreshStatus, 1000);
  });
</script>
</body>
</html>
)rawliteral";

// ==========================
// Helpers
// ==========================
String twoDigits(int v)
{
  return (v < 10) ? "0" + String(v) : String(v);
}

String formatHHMM(int h, int m)
{
  return twoDigits(h) + ":" + twoDigits(m);
}

String currentTimeText()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 50))
    return "--:--:--";

  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

bool parseHHMM(const String &s, int &h, int &m)
{
  if (s.length() != 5 || s[2] != ':')
    return false;

  String hs = s.substring(0, 2);
  String ms = s.substring(3, 5);

  for (int i = 0; i < 2; i++)
  {
    if (!isDigit(hs[i]) || !isDigit(ms[i]))
      return false;
  }

  h = hs.toInt();
  m = ms.toInt();

  if (h < 0 || h > 23 || m < 0 || m > 59)
    return false;

  return true;
}

bool isCancelledForDate(const struct tm &t)
{
  return alarmCfg.cancelScheduled &&
         alarmCfg.cancelYear == t.tm_year &&
         alarmCfg.cancelYday == t.tm_yday;
}

void clearCancelState()
{
  alarmCfg.cancelScheduled = false;
  alarmCfg.cancelYear = -1;
  alarmCfg.cancelYday = -1;
}

void clearPendingMugState()
{
  waitingForMug = false;
  mugDelayActive = false;
  waitingForMugStartedMs = 0;
  mugDelayStartedMs = 0;
  mugStableStartMs = 0;
}

void clearHandledState()
{
  alarmCfg.lastHandledYear = -1;
  alarmCfg.lastHandledYday = -1;
}

void clearExpiredCancel(const struct tm &now)
{
  if (!alarmCfg.cancelScheduled)
    return;

  bool futureDayPassed =
      (now.tm_year > alarmCfg.cancelYear) ||
      (now.tm_year == alarmCfg.cancelYear && now.tm_yday > alarmCfg.cancelYday);

  bool sameDayPastAlarm =
      (now.tm_year == alarmCfg.cancelYear &&
       now.tm_yday == alarmCfg.cancelYday &&
       (now.tm_hour > alarmCfg.hour ||
        (now.tm_hour == alarmCfg.hour && now.tm_min > alarmCfg.minute)));

  if (futureDayPassed || sameDayPastAlarm)
  {
    clearCancelState();
  }
}

bool getUpcomingAlarmTm(struct tm &target)
{
  struct tm now;
  if (!getLocalTime(&now, 50))
    return false;

  target = now;
  target.tm_hour = alarmCfg.hour;
  target.tm_min = alarmCfg.minute;
  target.tm_sec = 0;

  time_t nowEpoch = mktime(&now);
  time_t targetEpoch = mktime(&target);

  bool todayHandled =
      (alarmCfg.lastHandledYear == target.tm_year &&
       alarmCfg.lastHandledYday == target.tm_yday);

  if (targetEpoch <= nowEpoch || todayHandled)
  {
    targetEpoch += 24UL * 60UL * 60UL;
    localtime_r(&targetEpoch, &target);
  }

  return true;
}

bool isUpcomingAlarmActive()
{
  if (!alarmCfg.autoEnabled)
    return false;

  struct tm target;
  if (!getUpcomingAlarmTm(target))
    return false;

  return !isCancelledForDate(target);
}

void updateIndicatorLeds()
{
  mugPresent = (digitalRead(PIN_MUG_SENSOR) == MUG_PRESENT_LEVEL);

  bool activeAlarmReady = isUpcomingAlarmActive();
  bool greenOn = activeAlarmReady && mugPresent;
  bool redOn = activeAlarmReady && !mugPresent;

  digitalWrite(PIN_READY_GREEN_LED, greenOn ? HIGH : LOW);
  digitalWrite(PIN_WARN_RED_LED, redOn ? HIGH : LOW);
}

String nextAlarmText()
{
  if (!alarmCfg.autoEnabled)
    return "OFF";

  struct tm now;
  if (!getLocalTime(&now, 50))
  {
    if (alarmCfg.cancelScheduled)
      return "Cancelled";
    return formatHHMM(alarmCfg.hour, alarmCfg.minute);
  }

  struct tm target;
  if (!getUpcomingAlarmTm(target))
    return formatHHMM(alarmCfg.hour, alarmCfg.minute);

  String label = (target.tm_yday == now.tm_yday) ? "Today " : "Tomorrow ";
  label += formatHHMM(target.tm_hour, target.tm_min);

  if (isCancelledForDate(target))
    label += " (cancelled)";

  return label;
}

void startCoffeeCycle()
{
  digitalWrite(PIN_LED, HIGH);
  alarmOutputActive = true;
  alarmOutputStartedMs = millis();
  clearPendingMugState();
}

void stopCoffeeCycle()
{
  digitalWrite(PIN_LED, LOW);
  alarmOutputActive = false;
}

void updateAlarmOutput()
{
  if (alarmOutputActive && (millis() - alarmOutputStartedMs >= BREW_OUTPUT_MS))
  {
    stopCoffeeCycle();
  }
}

void updateMugPendingLogic()
{
  mugPresent = (digitalRead(PIN_MUG_SENSOR) == MUG_PRESENT_LEVEL);

  if (!waitingForMug)
    return;

  unsigned long nowMs = millis();

  if (nowMs - waitingForMugStartedMs >= MUG_GRACE_WINDOW_MS)
  {
    clearPendingMugState();
    return;
  }

  if (!mugDelayActive)
  {
    if (mugPresent)
    {
      if (mugStableStartMs == 0)
        mugStableStartMs = nowMs;

      if (nowMs - mugStableStartMs >= MUG_STABLE_MS)
      {
        mugDelayActive = true;
        mugDelayStartedMs = nowMs;
        mugStableStartMs = 0;
      }
    }
    else
    {
      mugStableStartMs = 0;
    }
  }
  else
  {
    if (nowMs - mugDelayStartedMs >= MUG_DETECTED_DELAY_MS)
    {
      startCoffeeCycle();
    }
  }
}

void updateOLED()
{
  if (millis() - lastOledMs < OLED_INTERVAL_MS)
    return;

  lastOledMs = millis();

  mugPresent = (digitalRead(PIN_MUG_SENSOR) == MUG_PRESENT_LEVEL);

  struct tm target;
  bool hasUpcoming = getUpcomingAlarmTm(target);
  bool cancelled = hasUpcoming && isCancelledForDate(target);

  String line1 = "Time: " + currentTimeText();
  String line2 = cancelled ? "Next: CANCEL" : "Next: " + formatHHMM(alarmCfg.hour, alarmCfg.minute);
  String line3 = mugPresent ? "Mug: OK" : "Mug: NO";
  String line4 = "Alarm: OFF";

  if (alarmOutputActive)
    line4 = "Alarm: ON";
  else if (mugDelayActive)
    line4 = "Start in 5 sec";
  else if (waitingForMug)
    line4 = "Waiting mug";

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 12, line1.c_str());
  u8g2.drawStr(0, 26, line2.c_str());
  u8g2.drawStr(0, 40, line3.c_str());
  u8g2.drawStr(0, 54, line4.c_str());
  u8g2.sendBuffer();
}

void toggleNextAlarmCancel()
{
  struct tm target;
  if (!getUpcomingAlarmTm(target))
  {
    alarmCfg.cancelScheduled = !alarmCfg.cancelScheduled;
    return;
  }

  if (isCancelledForDate(target))
  {
    clearCancelState();
  }
  else
  {
    alarmCfg.cancelScheduled = true;
    alarmCfg.cancelYear = target.tm_year;
    alarmCfg.cancelYday = target.tm_yday;
    clearPendingMugState();
  }
}

void handleCancelButton()
{
  bool reading = digitalRead(PIN_CANCEL_BUTTON);

  if (reading != lastButtonReading)
  {
    lastButtonChangeMs = millis();
    lastButtonReading = reading;
  }

  if ((millis() - lastButtonChangeMs) > BUTTON_DEBOUNCE_MS)
  {
    if (reading != stableButtonState)
    {
      stableButtonState = reading;

      if (stableButtonState == LOW)
      {
        toggleNextAlarmCancel();
      }
    }
  }
}

void checkAlarm()
{
  struct tm now;
  if (!getLocalTime(&now, 50))
    return;

  clearExpiredCancel(now);

  if (!alarmCfg.autoEnabled)
    return;

  if (now.tm_hour != alarmCfg.hour || now.tm_min != alarmCfg.minute)
    return;

  bool alreadyHandledToday =
      (alarmCfg.lastHandledYear == now.tm_year &&
       alarmCfg.lastHandledYday == now.tm_yday);

  if (alreadyHandledToday)
    return;

  alarmCfg.lastHandledYear = now.tm_year;
  alarmCfg.lastHandledYday = now.tm_yday;

  mugPresent = (digitalRead(PIN_MUG_SENSOR) == MUG_PRESENT_LEVEL);

  if (isCancelledForDate(now))
  {
    clearCancelState();
    clearPendingMugState();
    return;
  }

  if (mugPresent)
  {
    startCoffeeCycle();
  }
  else
  {
    waitingForMug = true;
    waitingForMugStartedMs = millis();
    mugDelayActive = false;
    mugDelayStartedMs = 0;
    mugStableStartMs = 0;
  }
}

// ==========================
// Web handlers
// ==========================
void handleRoot()
{
  server.send_P(200, "text/html", MAIN_PAGE);
}

void handleStatus()
{
  mugPresent = (digitalRead(PIN_MUG_SENSOR) == MUG_PRESENT_LEVEL);

  struct tm target;
  bool cancelled = getUpcomingAlarmTm(target) && isCancelledForDate(target);
  bool activeAlarmReady = isUpcomingAlarmActive();
  bool greenOn = activeAlarmReady && mugPresent;
  bool redOn = activeAlarmReady && !mugPresent;

  String json = "{";
  json += "\"time\":\"" + currentTimeText() + "\",";
  json += "\"nextAlarm\":\"" + nextAlarmText() + "\",";
  json += "\"mugPresent\":";
  json += (mugPresent ? "true" : "false");
  json += ",";
  json += "\"autoEnabled\":";
  json += (alarmCfg.autoEnabled ? "true" : "false");
  json += ",";
  json += "\"cancelled\":";
  json += (cancelled ? "true" : "false");
  json += ",";
  json += "\"waitingForMug\":";
  json += (waitingForMug ? "true" : "false");
  json += ",";
  json += "\"mugDelayActive\":";
  json += (mugDelayActive ? "true" : "false");
  json += ",";
  json += "\"alarmOutputActive\":";
  json += (alarmOutputActive ? "true" : "false");
  json += ",";
  json += "\"greenLedOn\":";
  json += (greenOn ? "true" : "false");
  json += ",";
  json += "\"redLedOn\":";
  json += (redOn ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

void handleSetAlarm()
{
  if (!server.hasArg("time"))
  {
    server.send(400, "text/plain", "Missing time");
    return;
  }

  int h, m;
  if (!parseHHMM(server.arg("time"), h, m))
  {
    server.send(400, "text/plain", "Invalid time");
    return;
  }

  alarmCfg.hour = h;
  alarmCfg.minute = m;

  clearCancelState();
  clearPendingMugState();
  clearHandledState();
  stopCoffeeCycle();

  server.send(200, "text/plain", "OK");
}

void handleToggleCancel()
{
  toggleNextAlarmCancel();
  server.send(200, "text/plain", "OK");
}

void handleAuto()
{
  if (!server.hasArg("enabled"))
  {
    server.send(400, "text/plain", "Missing enabled");
    return;
  }

  alarmCfg.autoEnabled = (server.arg("enabled") == "1");

  if (!alarmCfg.autoEnabled)
  {
    clearPendingMugState();
    stopCoffeeCycle();
  }

  server.send(200, "text/plain", "OK");
}

// ==========================
// Setup helpers
// ==========================
void connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }
}

void showBootIpScreen()
{
  String ip = WiFi.localIP().toString();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 12, "WiFi connected");
  u8g2.drawStr(0, 28, "IP address:");
  u8g2.drawStr(0, 44, ip.c_str());
  u8g2.sendBuffer();

  delay(5000);
}

void initTime()
{
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
  setenv("TZ", TZ_INFO, 1);
  tzset();

  struct tm timeinfo;
  for (int i = 0; i < 30; i++)
  {
    if (getLocalTime(&timeinfo, 1000))
      return;
    delay(200);
  }
}

void setupWebServer()
{
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/setAlarm", HTTP_POST, handleSetAlarm);
  server.on("/toggleCancel", HTTP_POST, handleToggleCancel);
  server.on("/auto", HTTP_POST, handleAuto);
  server.begin();
}

// ==========================
// Arduino
// ==========================
void setup()
{
  Serial.begin(115200);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  pinMode(PIN_READY_GREEN_LED, OUTPUT);
  digitalWrite(PIN_READY_GREEN_LED, LOW);

  pinMode(PIN_WARN_RED_LED, OUTPUT);
  digitalWrite(PIN_WARN_RED_LED, LOW);

  pinMode(PIN_CANCEL_BUTTON, INPUT_PULLUP);
  pinMode(PIN_MUG_SENSOR, INPUT);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  u8g2.begin();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 12, "Connecting WiFi...");
  u8g2.sendBuffer();

  connectWiFi();
  showBootIpScreen();
  initTime();
  setupWebServer();

  lastOledMs = millis() - OLED_INTERVAL_MS;

  Serial.print("Open this IP in browser: ");
  Serial.println(WiFi.localIP());
}

void loop()
{
  server.handleClient();
  handleCancelButton();
  checkAlarm();
  updateMugPendingLogic();
  updateAlarmOutput();
  updateIndicatorLeds();
  updateOLED();
}
