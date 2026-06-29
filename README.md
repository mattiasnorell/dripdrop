# DripDrop

An ESP32-based automated irrigation controller with WiFi connectivity, REST API, scenario-based automation, and NTP time synchronization.

## Features

- Control up to 4 relays (e.g. driving irrigation valves)
- Scenario-based if-this-then-that automation (time, day-of-week, sensor conditions)
- One-time timer support for manual watering sessions
- REST API for remote control and monitoring
- I²C sensor module support (up to 8 modules; temperature, humidity, etc.)
- MQTT pub/sub for telemetry, remote control, and event logging
- NTP time synchronization with DST support
- OTA firmware updates via web interface
- mDNS discovery (access via `dripdrop.local`)
- LittleFS persistence for scenarios, modules, and settings
- Automatic WiFi reconnection with AP fallback
- Watchdog timer for reliability
- Optional API key authentication

## Hardware Requirements

- ESP32 board (ESP32 DevKit or compatible)
- 4-channel relay module (active LOW recommended)
- 12V/24V solenoid valves (depending on your irrigation system)
- Power supply appropriate for your valves
- (Optional) Arduino Nano-based I²C sensor modules on the I²C bus (SDA/SCL)

### Default Pin Configuration

| Relay | GPIO Pin |
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
│   ├── dripdrop.cpp        # Main entry point — setup, loop, route registration
│   ├── config.h            # Configuration constants and defaults
│   ├── types.h             # Type definitions, structs, enums
│   ├── relays.cpp/h        # RelayController — GPIO control, state tracking
│   ├── timers.cpp/h        # TimerManager — one-shot timed relay activation
│   ├── scenarios.cpp/h     # ScenarioManager — condition/action automation
│   ├── modules.cpp/h       # ModuleManager — I²C sensor module discovery and readings
│   ├── mqtt.cpp/h          # MQTT — telemetry publishing and remote control
│   ├── handlers_system.cpp # /system/* endpoints
│   ├── handlers_static.cpp # Static file serving and SPA fallback
│   ├── handlers_fs.cpp     # /fs/* endpoints — file upload, directory ops, inspection
│   ├── handlers_ota.cpp    # /ota/* endpoints — firmware and filesystem OTA
│   └── AppHtml.h           # Embedded provisioning UI (served when no webapp present)
├── data/                   # LittleFS data directory (webapp files placed here by Docker build)
├── test/
│   ├── stubs/              # Arduino/hardware stubs for native testing
│   ├── test_timers/        # TimerManager unit tests
│   └── test_scenarios/     # ScenarioManager unit tests
├── Dockerfile              # Two-stage build: React webapp + ESP32 firmware
├── docker-compose.yml      # Build service — outputs firmware.bin, littlefs.bin, webapp/
├── Makefile                # Build and OTA deployment targets
└── platformio.ini
```

### LittleFS layout

| Path | Contents | Written by |
|------|----------|------------|
| `/webapp/` | React dashboard (index.html, assets/) | `make ota-webapp` |
| `/settings.json` | WiFi, MQTT, device name | `/system/*` API |
| `/scenarios.json` | Automation scenarios | `/scenarios/*` API |
| `/modules.json` | Registered I²C sensor modules | `/modules/*` API |
| `/relay_names.json` | Custom relay display names | `/relays/*` API |

Config files at the root are never touched by a webapp update.

## Installation

### Prerequisites

- [Docker](https://docs.docker.com/get-docker/) — used for the standard build
- `make` — orchestrates build and OTA deployment
- A `.env` file in the project root (copy from `.env.example`):

```env
WEBAPP_REPO=https://github.com/mattiasnorell/dripdrop-control.git
DEVICE_IPS=dripdrop.local
```

`DEVICE_IPS` can be a space-separated list of IPs or hostnames for multi-device OTA.

### Makefile targets

| Target | Description |
|--------|-------------|
| `make build` | Build firmware + LittleFS image + webapp files via Docker |
| `make ota` | Full OTA: upload webapp files then firmware (device reboots) |
| `make ota-webapp` | Upload webapp files only — clears `/webapp` on device first, config files untouched |
| `make ota-firmware` | Upload firmware binary only |
| `make ota-fs` | Replace entire LittleFS image (wipes all files including config — use with care) |
| `make flash` | First-time USB flash: firmware only |
| `make flash-all` | First-time USB flash: firmware + LittleFS (fresh device only) |
| `make clean` | Remove `build/` directory |

`make ota-webapp` is the recommended update path for day-to-day webapp deployments. It preserves all device configuration.

### Build & Upload (without Docker)

Install the PlatformIO CLI:
```
pip install platformio
```

Or install the [PlatformIO VS Code extension](https://platformio.org/install/ide?install=vscode) — the project's `.vscode/` config is already set up for it.

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
| `NUM_RELAYS` | `4` | Number of relays |
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
| POST | `/system/reboot` | Reboot device |

### Relays

A relay is the controlled output that switches a load — typically an irrigation valve. The
firmware drives relays; a frontend may still label them "Valves" for users while calling these
`/relays` endpoints.

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/relays` | List all relays with state and timer info |
| GET | `/relays/{id}/state` | State of a specific relay |
| POST | `/relays/{id}` | Update relay settings — body: `{"customName": "Front Garden"}` |
| POST | `/relays/{id}/on` | Turn relay on manually |
| POST | `/relays/{id}/off` | Turn relay off, cancel its timer |
| POST | `/relays/off` | Turn all relays off, cancel all timers |

Relay list response:
```json
[
  {
    "id": 1,
    "customName": "Front Garden",
    "isOn": true,
    "source": 3,
    "lastRunStart": 1707840000,
    "lastRunEnd": 0,
    "timerRemaining": 240
  }
]
```
Source values: `0`=NONE, `1`=SCENARIO, `2`=TIMER, `3`=MANUAL

Pass `null` or an empty string for `customName` to clear it.

### Timers

One-time timed relay activation.

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/timers` | List all active timers |
| POST | `/relays/{id}/timer` | Start a timer — body: `{"duration": 300}` |
| DELETE | `/relays/{id}/timer` | Cancel a timer |

Duration is in seconds (max 86400 = 24 hours).

### Scenarios

Scenarios fire relay actions when all conditions are met (AND logic). By default they use edge detection — a scenario fires once when conditions become true, and resets when they become false. Set the optional `repeatInterval` field to re-fire while conditions stay true (see below).

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
{"relayId": 1, "state": "on", "duration": 600}
```
`duration` (seconds) is required when `state` is `"on"`. Not needed for `"off"`.

**Repeating scenarios (optional):**

By default a scenario fires once when its conditions become true. Add a top-level
`repeatInterval` (seconds) to make it re-fire repeatedly while all conditions stay true:

```json
{"repeatInterval": 600}
```

- Omitted or `0` → fire once on the rising edge (default behavior).
- `> 0` → re-fire every `repeatInterval` seconds as long as conditions remain true. All
  actions run again on each re-fire. The effective cadence is rounded up to the ~5 s evaluation
  interval. When conditions go false the scenario re-arms and fires immediately on the next
  rising edge.

For relay actions, if `repeatInterval` ≤ the action `duration` the relay stays on continuously
while conditions hold; if it's greater, the relay pulses (on for `duration`, off, on again next
interval). Use this for e.g. *keep watering while the soil sensor reads dry within a time window*.

**Full example:**
```json
{
  "name": "Morning watering on weekdays",
  "conditions": [
    {"type": "time", "hour": 6, "minute": 30},
    {"type": "dayOfWeek", "days": [false, true, true, true, true, true, false]}
  ],
  "actions": [
    {"relayId": 1, "state": "on", "duration": 600},
    {"relayId": 2, "state": "on", "duration": 300}
  ]
}
```

### Modules

Sensor modules are Arduino Nano-based I²C devices. They must be physically connected to the I²C bus, discovered via scan, then registered before they can be used in scenarios.

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/modules` | List all registered modules |
| POST | `/modules/scan` | Scan the I²C bus for modules |
| POST | `/modules/{uid}/register` | Register a discovered module by UID |
| POST | `/modules/{uid}` | Update module settings — body: `{"customName": "Soil Sensor"}` |
| DELETE | `/modules/{uid}` | Remove a registered module |
| GET | `/modules/{uid}/reading` | Get current sensor reading |

Registered modules response:
```json
[
  {
    "uid": "TEMP001ABC12",
    "type": "TMP",
    "version": 1,
    "unit": "C",
    "addr": 8,
    "customName": "Soil Sensor"
  }
]
```

Scan result (`POST /modules/scan`):
```json
[
  {
    "addr": 8,
    "uid": "TEMP001ABC12",
    "type": "TMP",
    "version": 1,
    "unit": "C",
    "registered": true
  }
]
```

Reading response (`GET /modules/{uid}/reading`):
```json
{"value": 22.5}
```

### MQTT

MQTT is optional. When enabled, the device publishes relay state, timers, and events to a broker and subscribes to control topics.

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/system/mqtt` | Get MQTT configuration and connection status |
| POST | `/system/mqtt` | Update MQTT settings |

MQTT settings body:
```json
{
  "enabled": true,
  "server": "192.168.1.100",
  "port": 1883,
  "user": "mqttuser",
  "password": "mqttpassword"
}
```

**Published topics** (prefix: `dripdrop/{deviceName}`):

| Topic | Description |
|-------|-------------|
| `.../relay/{id}/state` | Relay on/off state, source, last run |
| `.../timer/{id}/state` | Timer start/remaining/expire events |
| `.../sensor/{uid}/state` | Sensor reading `{"value": 22.5}` |
| `.../system/state` | System health snapshot |
| `.../event` | Structured log events (INFO/WARNING) |
| `.../status` | `online` / `offline` (LWT) |

**Subscribed topics:**

| Topic | Payload | Description |
|-------|---------|-------------|
| `.../relay/+/set` | `ON` / `OFF` | Remote relay control |
| `.../timer/+/set` | `{"duration": 300}` | Start a timer |

### Filesystem

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/fs/info` | Filesystem capacity — total, used, free bytes |
| GET | `/fs/list?path=<path>` | Recursive file listing for a path (default `/`) |
| DELETE | `/fs/dir?path=<path>` | Recursively delete a directory |
| POST | `/fs/upload?path=<path>` | Upload a single file to LittleFS |

`/fs/info` response:
```json
{"total": 1441792, "used": 312400, "free": 1129392}
```

`/fs/list` response:
```json
{
  "path": "/webapp",
  "files": [
    {"path": "/webapp/index.html", "size": 4321},
    {"path": "/webapp/assets/main.abc.js", "size": 98765}
  ]
}
```

### Authentication

If `API_AUTH_ENABLED` is `true`, all requests must include:
```
X-API-Key: your-api-key
```

### OTA Updates

Use `make ota` for a full over-the-air update (webapp + firmware), or the individual targets for partial updates. For scripted/manual curl:

```bash
# Firmware only
curl -X POST http://dripdrop.local/ota/upload -F "firmware=@build/firmware.bin"

# Full LittleFS image (overwrites everything including config — use with care)
curl -X POST http://dripdrop.local/ota/upload-fs -F "fs=@build/littlefs.bin"
```

## Control Priority

When multiple sources affect the same relay:

1. **Manual** (highest) — API on/off commands
2. **Timer** — one-shot timed activation
3. **Scenario** (lowest) — condition-based automation

A manually controlled relay will not be overridden by scenarios or timers until turned off manually.

## Troubleshooting

**Device not connecting to WiFi**
- Verify credentials in `src/config_local.h`
- On failure, the device starts an AP named "DripDrop" — connect and access `http://192.168.4.1`

**Scenarios not running**
- Check NTP sync via `GET /system/status` (`ntpSynced` must be `true`)
- Verify timezone configuration matches your location
- Scenarios use edge detection by default — if conditions were already true at boot, they won't fire until conditions reset and become true again (set `repeatInterval` to re-fire while conditions stay true)

**Relays not switching**
- Verify relay module is active-LOW (default) or set `RELAY_ACTIVE_HIGH true` in config
- Check GPIO pin assignments match your wiring
- Test with `POST /relays/1/on` and monitor serial output

**Serial debug output**
Connect at 115200 baud. Disable for production with `DEBUG_ENABLED 0` in config.

## License

MIT License
