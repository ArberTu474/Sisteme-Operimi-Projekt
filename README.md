# Dokumentacioni i Makinës së Kafesë Smart — ESP32S

Ky projekt është një kontrollues i automatizuar për makinat e kafesë, i ndërtuar mbi platformën **ESP32**. Ai përfshin një **Web Dashboard** interaktiv, monitorim në kohë reale të sensorëve dhe një sistem **Brewing** të aktivizuar me alarm.

## Funksionalitetet Kryesore

- **Scheduled Brewing**: Mundësia për të caktuar një orar specifik se kur makina duhet të fillojë procesin e bërjes së kafesë përmes ndërfaqes web.
- **Safety Interlock**: Një sensor **IR** kontrollon nëse kupa (mug) është e pranishme përpara se të fillojë procesi, duke parandaluar derdhjen e kafesë.
- **Monitorim në Kohë Reale**: Ndjekja e temperaturës, lagështisë, statusit të kupës dhe procesit të **brewing** përmes një ekrani **OLED** ose **Web Dashboard**.
- **Web Dashboard**: Një ndërfaqe moderne dhe **responsive** për kontrollin e alarmit, shikimin e log-eve dhe ndryshimin e temave (Dark/Light).
- **Kontroll Lokal**: Buton fizik për të anuluar ose rikthyer alarmin e radhës dhe feedback vizual në ekranin **OLED**.
- **Time Synchronization**: Përditësim automatik i orës përmes protokollit **NTP (Network Time Protocol)**.

## Komponentët dhe Pjesët Hardware

| Komponenti                | Përshkrimi                                                              | Lidhja në Pin (GPIO)       |
| :------------------------ | :---------------------------------------------------------------------- | :------------------------- |
| **ESP32 DevKit**          | Mikrokontrolluesi kryesor me mbështetje për **WiFi** dhe **Bluetooth**. | N/A                        |
| **SSD1306 OLED (128x64)** | Shfaq informacione lokale si ora, temperatura dhe statusi.              | SDA: GPIO 21, SCL: GPIO 22 |
| **DHT11 Sensor**          | Mat temperaturën e ambientit dhe lagështinë.                            | DATA: GPIO 26              |
| **IR Sensor**             | Detekton praninë e kupës (Mug Sensor).                                  | OUT: GPIO 34               |
| **LED / Relay**           | Simulon ose aktivizon procesin e **brewing** (Anoda në GPIO 2).         | GPIO 2                     |
| **Tactile Button**        | Shërben si buton "Cancel" për alarmin e radhës.                         | GPIO 27                    |
| **Resistor (220Ω)**       | Rezistencë për të limituar rrymën për **LED**.                          | Lidhur me GPIO 2           |

## Arkitektura Teknike

### 1. Software Stack

- **Framework**: **Arduino / PlatformIO**
- **Web Server**: **ESPAsyncWebServer** (Asinkron, lejon menaxhimin e shumë lidhjeve pa bllokuar procesorin).
- **Communication**: **AsyncWebSocket** (Komunikim i shpejtë dhe në dy drejtime midis serverit dhe klientit).
- **Graphics**: **U8g2lib** (Librari me performancë të lartë për ekranet **OLED**).
- **Sensors**: **DHT sensor library** dhe **Adafruit Unified Sensor**.
- **Data Handling**: **ArduinoJson** (Përdoret për paketimin e të dhënave të sistemit në formatin **JSON**).

### 2. Logjika e Kodit dhe Flow-i

Programi punon në mënyrë **non-blocking** për të garantuar që **Web Server** të jetë gjithmonë i disponueshëm ndërsa monitoron sensorët.

#### Inicializimi (`setup`)

- **System Config**: Çaktivizon **Brownout Detection** për të siguruar stabilitet gjatë ndezjes së **WiFi**.
- **Hardware Init**: Konfiguron pinat, inicializon ekranin **OLED** dhe sensorin **DHT**.
- **Network**: Lidhet me rrjetin **WiFi** lokal dhe sinkronizon orën e brendshme me serverët **NTP** duke përdorur stringun e zonës kohore `CET-1CEST`.
- **Server**: Ngarkon ndërfaqen **HTML/CSS/JS** dhe konfiguron **WebSocket handlers**.

#### Loop-i Kryesor (`loop`)

- **Sensor Reading**: Lexon temperaturën dhe lagështinë nga **DHT11** çdo 2 sekonda.
- **State Updates**: Kontrollon sensorin **IR** për praninë e kupës dhe menaxhon **button debouncing** për butonin fizik.
- **Alarm Logic**: Krahason orën aktuale me orën e caktuar të alarmit. Nëse ato përputhen dhe kupa është në vend, aktivizohet gjendja "brewing" (**LED ON**).
- **Brewing Management**: Procesi zgjat 30 sekonda (**BREW_DURATION_MS**) dhe pastaj fiket automatikisht.
- **UI Refresh**: Përditëson ekranin **OLED** çdo 500ms dhe dërgon gjendjen e sistemit si **JSON** te të gjithë klientët e lidhur në **WebSocket** çdo sekondë.

## Protokolli i Komunikimit (WebSocket JSON)

Sistemi përdor mesazhe **JSON** për të komunikuar me **Dashboard**:

**Nga ESP32 te Browser (Broadcast):**

```json
{
  "time": "07:30:05",
  "alarm": "07:30",
  "alarmOn": true,
  "cancelled": false,
  "temp": 24.5,
  "mug": true,
  "brewing": false
}
```

**Nga Browser te ESP32 (Command):**

```json
{
  "alarmHour": 7,
  "alarmMinute": 0,
  "alarmOn": true,
  "cancel": false
}
```

## Veçoritë e Web Dashboard

Ndërfaqja është ndërtuar me teknologjitë më të fundit të web-it:

- **Responsive UI**: Funksionon në mënyrë perfekte në telefon dhe kompjuter.
- **Real-time Sync**: Përdor **WebSockets** për të reflektuar ndryshimet e makinës menjëherë, pa pasur nevojë për **refresh**.
- **Activity Log**: Ruhet një histori e veprimeve (p.sh., "Alarm set", "Mug detected").
- **Dark/Light Mode**: Përduruesi mund të zgjedhë temën e preferuar vizuale.

## Struktura e Projektit

- `src/main.cpp`: Kodi kryesor i aplikacionit.
- `platformio.ini`: Konfigurimi i projektit dhe menaxhimi i librarive.
- `include/`: Folderi për skedarët **header** (nëse shtohen në të ardhmen).
- `lib/`: Folderi për librari lokale specifike.

## Instalimi dhe Përdorimi

1. **Konfigurimi**: Ndryshoni `WIFI_SSID` dhe `WIFI_PASSWORD` në skedarin `main.cpp`.
2. **Flash**: Përdorni **PlatformIO** për të ngarkuar kodin në **ESP32**.
3. **Aksesi**: Hapni adresën **IP** që shfaqet në **Serial Monitor** (115200 baud) në browser-in tuaj.
4. **Përdorimi**: Caktoni orarin e kafesë dhe sigurohuni që kupa të jetë në vend!
