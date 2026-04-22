# DripDrop

An ESP32-based automated irrigation controller with WiFi connectivity, REST API, scenario-based automation, and NTP time synchronization.

## Features

- Control up to 4 irrigation valves
- Scenario-based if-this-then-that automation (time, day-of-week, sensor conditions)
- One-time timer support for manual watering sessions
- REST API for remote control and monitoring
- NTP time synchronization with DST support
- OTA firmware updates via web interface
- mDNS discovery (access via `dripdrop.local`)
- LittleFS persistence for scenarios and settings
- Automatic WiFi reconnection with AP fallback
- Watchdog timer for reliability
- Optional API key authentication
- Event logging to external HTTP endpoint

## Hardware Requirements

- ESP32 board (ESP32 DevKit or compatible)
- 4-channel relay module (active LOW recommended)
- 12V/24V solenoid valves (depending on your irrigation system)
- Power supply appropriate for your valves

### Default Pin Configuration

| Valve | GPIO Pin |
|-------|----------|
| 1     | GPIO 25  |
| 2     | GPIO 26  |
| 3     | GPIO 27  |
| 4     | GPIO 32  |

Pins can be changed in `src/config.h`. Avoid strapping pins (0, 2, 5, 12, 15) and input-only pins (34–39).

## Project Structure

```
dripdrop/
├── src/
│   ├── dripdrop.cpp    # Main entry point — setup, loop, HTTP handlers
│   ├── config.h        # Configuration constants and defaults
│   ├── types.h         # Type definitions, structs, enums
│   ├── valves.cpp/h    # ValveController — GPIO control, state tracking
│   ├── timers.cpp/h    # TimerManager — one-shot timed valve activation
│   ├── scenarios.cpp/h # ScenarioManager — condition/action automation
│   ├── sensors.cpp/h   # SensorManager — I2C sensor readings
│   ├── logger.cpp/h    # Event logger — HTTP log shipping
│   └── AppHtml.h       # Embedded web UI
├── test/
│   ├── stubs/          # Arduino/hardware stubs for native testing
│   ├── test_timers/    # TimerManager unit tests
│   └── test_scenarios/ # ScenarioManager unit tests
└── platformio.ini
```

## Installation

### Prerequisites

Install the PlatformIO CLI:
```
pip install platformio
```

Or install the [PlatformIO VS Code extension](https://platformio.org/install/ide?install=vscode) — the project's `.vscode/` config is already set up for it.

### Build & Upload

Compile only:
```
pio run -e esp32dev
```

Compile and upload to connected ESP32:
```
pio run -e esp32dev -t upload
```

Upload the LittleFS filesystem (required on first flash, or after clearing):
```
pio run -e esp32dev -t uploadfs
```

Monitor serial output:
```
pio device monitor -e esp32dev
```

### Flashing without PlatformIO

To flash a pre-built binary using only `esptool.py`:

**Step 1 — Produce a merged binary** (if you have the source and PlatformIO installed):
```sh
# Build firmware and filesystem
pio run -e esp32dev
pio run -e esp32dev -t buildfs

# Merge into a single binary flashable at offset 0x0
python3 ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32 merge_bin -o dripdrop-merged.bin \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000   .pio/build/esp32dev/bootloader.bin \
  0x8000   .pio/build/esp32dev/partitions.bin \
  0xe000   ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000  .pio/build/esp32dev/firmware.bin \
  0x290000 .pio/build/esp32dev/littlefs.bin
```

**Step 2 — Flash the merged binary:**
```sh
esptool.py --chip esp32 -p /dev/ttyUSB0 -b 921600 write_flash 0x0 dripdrop-merged.bin
```
Replace `/dev/ttyUSB0` with your port (macOS: `/dev/cu.usbserial-*`).

The merged binary can also be flashed via the [ESP Web Flasher](https://espressif.github.io/esptool-js/) in a browser — no install required.

**Flash offsets** (for flashing individual files separately):

| File | Offset |
|------|--------|
| `bootloader.bin` | `0x1000` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |
| `littlefs.bin` | `0x290000` |

### Run Unit Tests (no hardware needed)

```
pio test -e native
```

Run a specific suite:
```
pio test -e native -f test_timers
pio test -e native -f test_scenarios
```

## Configuration

Edit `src/config.h` or create `src/config_local.h` (recommended — gitignored) to override defaults:

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

### Configuration Reference

| Option | Default | Description |
|--------|---------|-------------|
| `WIFI_SSID` | `"WajFaj"` | WiFi network name |
| `WIFI_PASSWORD` | `"..."` | WiFi password |
| `AP_SSID` | `"DripDrop"` | Access point name (fallback mode) |
| `AP_PASSWORD` | `"dripdrop123"` | Access point password |
| `TIMEZONE_OFFSET_SEC` | `3600` | Timezone offset from UTC in seconds |
| `DST_OFFSET_SEC` | `3600` | Daylight saving time offset |
| `API_AUTH_ENABLED` | `false` | Enable API key authentication |
| `API_KEY` | `"change-me..."` | API key for authentication |
| `NUM_VALVES` | `4` | Number of valves |
| `MAX_SCENARIOS` | `16` | Maximum number of scenarios |
| `DEBUG_ENABLED` | `1` | Enable serial debug output |

## API Reference

Base URL: `http://dripdrop.local` or `http://<device-ip>`

All endpoints return JSON. POST endpoints accept a JSON body with `Content-Type: application/json`.

### System

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/system/status` | Firmware version, heap, WiFi, NTP state |
| GET | `/system/ping` | Health check — returns `{"message":"pong"}` |
| GET | `/system/ip` | Device IP as plain text |
| GET | `/system/time` | Current time and NTP sync status |
| POST | `/system/time` | Set time manually `{"unixTime": 1234567890}` |
| GET | `/system/name` | Get device name |
| POST | `/system/name` | Set device name `{"name": "garden"}` |
| GET | `/system/logs` | Get event log URL and token |
| POST | `/system/logs` | Set event log URL and token |
| POST | `/system/reboot` | Reboot device |

### Valves

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/valves` | List all valves with state and timer info |
| GET | `/valves/{id}/state` | State of a specific valve |
| POST | `/valves/{id}/on` | Turn valve on manually |
| POST | `/valves/{id}/off` | Turn valve off, cancel its timer |
| POST | `/valves/off` | Turn all valves off, cancel all timers |

Valve list response:
```json
[
  {
    "id": 1,
    "isOn": true,
    "source": 3,
    "lastRunStart": 1707840000,
    "lastRunEnd": 0,
    "timerRemaining": 240
  }
]
```
Source values: `0`=NONE, `1`=SCENARIO, `2`=TIMER, `3`=MANUAL

### Timers

One-time timed valve activation.

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/timers` | List all active timers |
| POST | `/valves/{id}/timer` | Start a timer — body: `{"duration": 300}` |
| DELETE | `/valves/{id}/timer` | Cancel a timer |

Duration is in seconds (max 86400 = 24 hours).

### Scenarios

Scenarios fire valve actions when all conditions are met (AND logic). They use edge detection — a scenario fires once when conditions become true, and resets when they become false.

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/scenarios` | List all scenarios |
| POST | `/scenarios` | Add a scenario |
| POST | `/scenarios/{id}` | Update a scenario |
| DELETE | `/scenarios/{id}` | Delete a scenario |

**Condition types:**

`time` — fires at a specific time each day:
```json
{"type": "time", "hour": 6, "minute": 30}
```

`dayOfWeek` — fires only on selected days (AND with other conditions):
```json
{"type": "dayOfWeek", "days": [false, true, true, true, true, true, false]}
```
Days array: `[Sun, Mon, Tue, Wed, Thu, Fri, Sat]`

`sensorValue` — fires based on a sensor reading:
```json
{"type": "sensorValue", "sensorId": "temp1", "operator": "gt", "value": 25}
```
Operators: `gt`, `lt`, `eq`

**Action fields:**

```json
{"valveId": 1, "state": "on", "duration": 600}
```
`duration` (seconds) is required when `state` is `"on"`. Not needed for `"off"`.

**Full example:**
```json
{
  "name": "Morning watering on weekdays",
  "conditions": [
    {"type": "time", "hour": 6, "minute": 30},
    {"type": "dayOfWeek", "days": [false, true, true, true, true, true, false]}
  ],
  "actions": [
    {"valveId": 1, "state": "on", "duration": 600},
    {"valveId": 2, "state": "on", "duration": 300}
  ]
}
```

### Authentication

If `API_AUTH_ENABLED` is `true`, all requests must include:
```
X-API-Key: your-api-key
```

### OTA Updates

Navigate to `http://dripdrop.local/update` to upload new firmware via the web browser.

## Control Priority

When multiple sources affect the same valve:

1. **Manual** (highest) — API on/off commands
2. **Timer** — one-shot timed activation
3. **Scenario** (lowest) — condition-based automation

A manually controlled valve will not be overridden by scenarios or timers until turned off manually.

## Troubleshooting

**Device not connecting to WiFi**
- Verify credentials in `src/config_local.h`
- On failure, the device starts an AP named "DripDrop" — connect and access `http://192.168.4.1`

**Scenarios not running**
- Check NTP sync via `GET /system/status` (`ntpSynced` must be `true`)
- Verify timezone configuration matches your location
- Scenarios use edge detection — if conditions were already true at boot, they won't fire until conditions reset and become true again

**Valves not switching**
- Verify relay module is active-LOW (default) or set `VALVE_ACTIVE_HIGH true` in config
- Check GPIO pin assignments match your wiring
- Test with `POST /valves/1/on` and monitor serial output

**Serial debug output**
Connect at 115200 baud. Disable for production with `DEBUG_ENABLED 0` in config.

## License

MIT License
