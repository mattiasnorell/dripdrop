# DripDrop

An ESP8266-based automated irrigation controller with WiFi connectivity, REST API, scheduling, and NTP time synchronization.

## Features

- Control up to 4 irrigation valves
- Schedule-based automatic irrigation with second-level precision
- One-time timer support for manual watering sessions
- REST API for remote control and monitoring
- NTP time synchronization (no manual time setting required)
- OTA firmware updates via web interface
- mDNS discovery (access via `dripdrop.local`)
- EEPROM persistence with data validation
- Automatic WiFi reconnection
- Watchdog timer for reliability
- Optional API key authentication

## Hardware Requirements

- ESP8266 board (NodeMCU, Wemos D1 Mini, or similar)
- 4-channel relay module (active LOW recommended)
- 12V/24V solenoid valves (depending on your irrigation system)
- Power supply appropriate for your valves

### Default Pin Configuration

| Valve | GPIO Pin | NodeMCU Label |
|-------|----------|---------------|
| 1     | GPIO2    | D4            |
| 2     | GPIO14   | D5            |
| 3     | GPIO12   | D6            |
| 4     | GPIO13   | D7            |

## Installation

### Arduino IDE

1. Install the ESP8266 board support:
   - Open Arduino IDE preferences
   - Add to "Additional Board Manager URLs": `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Open Tools > Board > Board Manager, search for "ESP8266" and install

2. Install required libraries via Library Manager:
   - ArduinoJson (v7.x)
   - (ESP8266 core libraries are included with board support)

3. Open `dripdrop/dripdrop.ino`

4. Configure your settings:
   - Copy `config.h` to `config_local.h`
   - Edit `config_local.h` with your WiFi credentials and timezone

5. Select your board (e.g., "NodeMCU 1.0") and upload

### PlatformIO

```ini
[env:nodemcu]
platform = espressif8266
board = nodemcuv2
framework = arduino
lib_deps = 
    bblanchon/ArduinoJson@^7.0.0
monitor_speed = 115200
```

## Configuration

Edit `config.h` or create `config_local.h` (recommended, gitignored) with your settings:

```cpp
// WiFi credentials
#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"

// Timezone (seconds from UTC)
#define TIMEZONE_OFFSET_SEC 3600   // UTC+1
#define DST_OFFSET_SEC 3600        // Daylight saving offset

// Optional: Enable API authentication
#define API_AUTH_ENABLED true
#define API_KEY "your-secure-api-key"
```

### Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `WIFI_SSID` | `""` | WiFi network name |
| `WIFI_PASSWORD` | `""` | WiFi password |
| `AP_SSID` | `"DripDrop"` | Access point name (fallback mode) |
| `AP_PASSWORD` | `"dripdrop123"` | Access point password |
| `TIMEZONE_OFFSET_SEC` | `3600` | Timezone offset from UTC in seconds |
| `DST_OFFSET_SEC` | `3600` | Daylight saving time offset |
| `API_AUTH_ENABLED` | `false` | Enable API key authentication |
| `API_KEY` | `"change-me..."` | API key for authentication |
| `NUM_VALVES` | `4` | Number of valves |
| `MAX_SCHEDULES` | `32` | Maximum number of schedules |
| `DEBUG_ENABLED` | `1` | Enable serial debug output |

## API Reference

Base URL: `http://dripdrop.local` or `http://<device-ip>`

All endpoints return JSON. POST endpoints accept JSON body with `Content-Type: application/json`.

### System Endpoints

#### GET /system/status
Returns system diagnostics.

Response:
```json
{
  "firmware": "2.1.0",
  "uptime": 3600000,
  "uptimeFormatted": "0d 1h 0m",
  "freeHeap": 35000,
  "wifiConnected": true,
  "wifiRssi": -65,
  "apMode": false,
  "ntpSynced": true,
  "currentTime": 1707840000,
  "activeValves": 1,
  "activeSchedules": 5
}
```

#### GET /system/time
Returns current time information.

#### GET /system/ip
Returns device IP address as plain text.

#### GET /system/ping
Health check endpoint. Returns `{"message": "pong"}`.

#### POST /system/reboot
Reboots the device.

### Valve Endpoints

#### GET /valves
Returns list of all valves with their current state.

Response:
```json
[
  {
    "id": 1,
    "isOn": true,
    "source": 2,
    "lastRunStart": 1707840000,
    "lastRunEnd": 1707839000,
    "timerRemaining": 120
  }
]
```

Source values: 0=NONE, 1=SCHEDULE, 2=TIMER, 3=MANUAL

#### GET /valve/state?valveId=1
Returns state of a specific valve.

#### POST /valve/state/on
Turn a valve on manually.

Request:
```json
{"valveId": 1}
```

#### POST /valve/state/off
Turn a valve off.

Request:
```json
{"valveId": 1}
```

#### POST /valves/off
Turn all valves off and cancel all timers.

### Timer Endpoints

Timers provide one-time valve activation for a specified duration.

#### GET /timer
Returns all timer states.

#### POST /timer
Start a timer for a valve.

Request:
```json
{
  "valveId": 1,
  "duration": 300
}
```
Duration is in seconds (max 86400 = 24 hours).

#### POST /timer/abort
Cancel an active timer.

Request:
```json
{"valveId": 1}
```

### Schedule Endpoints

Schedules provide recurring valve activation based on time and day of week.

#### GET /schedule/list
Returns all schedules.

Response:
```json
[
  {
    "scheduleId": 0,
    "valveId": 1,
    "fromHour": 6,
    "fromMinute": 30,
    "duration": 600,
    "active": true,
    "days": [true, true, true, true, true, false, false]
  }
]
```

Days array: [Sunday, Monday, Tuesday, Wednesday, Thursday, Friday, Saturday]

#### POST /schedule/add
Add a new schedule.

Request:
```json
{
  "valveId": 1,
  "fromHour": 6,
  "fromMinute": 30,
  "duration": 600,
  "days": [false, true, true, true, true, true, false]
}
```

Duration is in seconds. Response includes the assigned `scheduleId`.

#### POST /schedule/update
Update an existing schedule.

Request:
```json
{
  "scheduleId": 0,
  "valveId": 1,
  "fromHour": 7,
  "fromMinute": 0,
  "duration": 900,
  "days": [true, true, true, true, true, true, true]
}
```

#### POST /schedule/delete
Delete a schedule.

Request:
```json
{"scheduleId": 0}
```

#### POST /schedule/deleteAll
Delete all schedules.

### Authentication

If `API_AUTH_ENABLED` is set to `true`, all requests must include the API key header:

```
X-API-Key: your-api-key
```

### OTA Updates

Navigate to `http://dripdrop.local/update` to upload new firmware via web browser.

## Project Structure

```
dripdrop/
├── dripdrop.ino      # Main sketch - setup, loop, HTTP handlers
├── config.h          # Configuration constants and defaults
├── types.h           # Type definitions, structs, enums
├── valves.h          # ValveController class declaration
├── valves.cpp        # ValveController implementation
├── scheduler.h       # SchedulerClass declaration
├── scheduler.cpp     # SchedulerClass implementation (EEPROM persistence)
├── timers.h          # TimerManager class declaration
├── timers.cpp        # TimerManager implementation
└── AppHtml.h         # Embedded web interface (optional)
```

## Control Priority

When multiple control sources affect the same valve, the following priority applies:

1. **Manual** (highest) - API-triggered on/off commands
2. **Timer** - One-time duration-based activation
3. **Schedule** (lowest) - Recurring time-based activation

A valve controlled manually will not be affected by schedules or timers until manually turned off.

## Troubleshooting

### Device not connecting to WiFi
- Verify credentials in `config_local.h`
- Device will create an access point "DripDrop" if WiFi connection fails
- Connect to AP and access `http://192.168.4.1`

### Schedules not running
- Check NTP sync status via `/system/status` (ntpSynced should be true)
- Verify timezone configuration matches your location
- Ensure schedule has correct days enabled

### EEPROM data corrupted
- The system automatically detects and reinitializes corrupted EEPROM data
- All schedules will be cleared if corruption is detected
- Check serial output for "EEPROM not initialized" or "checksum mismatch" messages

### Valves not switching
- Verify relay module is active-LOW (most common) or adjust `VALVE_ACTIVE_HIGH` in config
- Check GPIO pin assignments match your wiring
- Test with `/valve/state/on` API call and monitor serial output

## Serial Debug Output

Connect via serial monitor at 115200 baud to see debug output:

```
[VALVE] Valve 1: ON (source: 3)
[SCHED] Schedule 0 activated valve 1
[WIFI] Connection lost, reconnecting...
```

Debug output can be disabled for production by setting `DEBUG_ENABLED` to `0`.

## License

MIT License