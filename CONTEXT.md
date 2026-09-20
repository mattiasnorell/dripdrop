# DripDrop — Project Context

> Shared context for AI assistants and new contributors. Captures **purpose, architecture,
> and structure**. For end-user docs (full API reference, build/flash steps, configuration
> tables, troubleshooting), see [`README.md`](README.md). For driver-module design notes,
> see the agent memory file `project_driver_modules.md`.

## 1. Purpose

DripDrop is an **ESP32-based automated irrigation controller**. It switches relays (driving
solenoid valves) based on:

- **Scenarios** — if-this-then-that automation (time of day, day of week, sensor thresholds)
- **Timers** — one-shot timed activations ("water zone 1 for 5 minutes")
- **Manual** control via REST API / MQTT / web dashboard

It runs standalone on the device. A React dashboard (built from a separate repo,
`dripdrop-control`) is served from the device's LittleFS at `/webapp`. The device is reached
at `http://dripdrop.local` (mDNS) or its IP.

## 2. Platform & Build

- **Target:** ESP32 (`esp32dev`), Arduino framework, PlatformIO. Filesystem = LittleFS.
- **Firmware is size-constrained.** `platformio.ini` builds with `-Os`, `--gc-sections`, and
  debug stripped. Code routinely trades convenience for flash/RAM savings (e.g. `buildTimestamp()`
  hand-parses dates with `atoi` to avoid pulling in `sscanf`). Keep this in mind before adding
  heavy dependencies or stdlib calls.
- **Two build envs:** `esp32dev` (hardware) and `native` (Unity unit tests with hardware stubs
  in `test/stubs/`, run via `pio test -e native`).
- **Build & deploy** go through the `Makefile` (Docker-based build → `make ota*` pushes firmware
  / filesystem / webapp over WiFi via the device's own `/ota` and `/fs` HTTP endpoints).
  `make ota-webapp` is the day-to-day path; it preserves device config.

## 3. Runtime Architecture

Single-threaded Arduino `setup()` / `loop()` model in `src/dripdrop.cpp`.

**`setup()`** mounts LittleFS, calls `begin()` on each manager singleton, loads settings, then
brings up WiFi (STA with AP fallback), mDNS, NTP, a task watchdog, and registers HTTP routes.

**`loop()`** is a cooperative scheduler — no RTOS tasks, no blocking. Each subsystem is polled
on its own interval:
- `server.handleClient()` every loop (HTTP)
- WiFi reconnect check (`WIFI_RECONNECT_INTERVAL_MS`)
- `Scenarios.check()` + `Timers.check()` (`SCENARIO_CHECK_INTERVAL_MS`, ~5s)
- `Scenarios.maybeSave()` (debounced flush) and `drainCallUrlQueue()`
- `Mqtt.loop()`
- `Display.update()` (`DISPLAY_UPDATE_INTERVAL_MS`)
- `esp_task_wdt_reset()` to feed the watchdog

**Implication for changes:** never block in a handler or `check()` — it stalls everything and
can trip the watchdog. Long/networked work (e.g. scenario `callUrl`) is **queued** and drained
in `loop()`, not done inline. Follow that pattern.

### Manager singletons

Each domain is a class with a single global instance (`extern` in its header), initialized via
`begin()` in `setup()`. State lives in fixed-size arrays sized by `config.h` constants
(`MAX_SCENARIOS`, `MAX_MODULES`, `NUM_RELAYS`) — no dynamic growth.

| Singleton   | Files                  | Responsibility |
|-------------|------------------------|----------------|
| `Relays`    | `relays.cpp/h`         | GPIO output, on/off, source tracking, custom names |
| `Timers`    | `timers.cpp/h`         | One-shot timed relay activation |
| `Scenarios` | `scenarios.cpp/h`      | Condition/action automation, edge-detected firing (optional per-scenario `repeatInterval` re-fires while conditions hold; per-scenario `IsActive` enable flag gates evaluation) |
| `Modules`   | `modules.cpp/h`        | I²C sensor/driver module discovery & I/O |
| `Mqtt`      | `mqtt.cpp/h`           | Telemetry publish + remote control subscribe |
| `Display`   | `display.cpp/h`        | I²C LCD status display |

### Control priority

When multiple sources target the same relay, priority is **Manual > Timer > Scenario**
(`RelaySource` enum in `types.h`). A manually controlled relay is not overridden by automation
until manually turned off. Each relay records *why* it's on via `source`.

## 4. HTTP API Layer

- All endpoints are under the versioned prefix **`/api/v1`** (the `API` macro in `routes.cpp`).
- **`routes.cpp`** owns the routing table only — it maps URLs to handler functions and contains
  the forward declarations. Path params use `UriBraces` (e.g. `/relays/{}/on`). **Register
  longer/more-specific patterns before shorter ones** (e.g. `/scenarios/{}/run` before
  `/scenarios/{}`) — the router matches in order.
- **Handler bodies** live in `handlers_*.cpp`, grouped by domain:
  `handlers_system / relays / timers / scenarios / modules / config / fs / ota / static`.
  (`handlers_update.cpp` is newer/uncommitted — system self-update flow.)
- **`api_utils.cpp/h`** holds shared helpers (JSON responses, the `WebServer` extern, API-key
  auth via the `X-API-Key` header when `API_AUTH_ENABLED`).
- `AppHtml.h` is an embedded provisioning page served when no `/webapp` is present on flash.
- `handlers_static.cpp` serves the webapp from LittleFS with SPA fallback (`onNotFound`).

When adding an endpoint: declare it in `routes.cpp`, register it in `setupRoutes()` (mind
pattern order), and implement the handler in the matching `handlers_*.cpp`.

## 5. Persistence (LittleFS)

State is persisted as JSON files at the flash root; the webapp lives under `/webapp`. Config
files are never touched by a webapp update.

| Path               | Contents                         | Owner |
|--------------------|----------------------------------|-------|
| `/settings.json`   | device name, WiFi, MQTT          | `dripdrop.cpp` load/saveSettings |
| `/scenarios.json`  | automation scenarios             | `Scenarios` |
| `/modules.json`    | registered I²C modules           | `Modules` |
| `/relay_names.json`| custom relay names               | `Relays` |
| `/webapp/`         | React dashboard                  | `make ota-webapp` |

Writes are **debounced where frequent** (e.g. `Scenarios.maybeSave` batches rapid API edits
into one flash write) to protect flash and avoid blocking.

## 6. I²C Module Protocol

The ESP32 is I²C **master**; Arduino Nano modules are slaves at addresses `0x08–0x77`. Modules
are discovered by scan, then must be **registered** (persisted) before use. Defined in
`modules.h`:

- **Three-command protocol:** `0x01` GET_DESCRIPTOR (all), `0x02` GET_READING (sensors),
  `0x03` DO_ACTION (drivers, fire-and-forget).
- **Roles:** `ROLE_SENSOR` (0x00) and `ROLE_DRIVER` (0x01). Sensors report readings; drivers
  receive action bytes.
- Wire structs (`Descriptor`, `SensorResponse`) are `#pragma pack(1)` for byte-exact `memcpy`
  from the I²C buffer. Every `Descriptor` starts with magic `DE AD BE EF`.
- Per-address probing is isolated in `scanAddress()` specifically so a TCA9548A I²C mux can be
  spliced in later without touching other code.
- Scenario sensor reads are **cached per `check()` cycle** (by address, including failures) so
  multiple conditions referencing one sensor cause only one I²C transaction per cycle.

> The driver-module support (role enum, `CMD_DO_ACTION`, Descriptor rework) is an in-progress
> design — see agent memory `project_driver_modules.md` for the decisions behind it.

## 7. Key Conventions / Gotchas

- **Size first.** Avoid heavy stdlib/template usage on the hot path; prefer the existing
  lean patterns. Check `make size` impact for non-trivial additions.
- **Never block** in handlers or `check()`; queue and drain in `loop()`.
- **Edge-detected scenarios:** a scenario fires once when its conditions become true and resets
  when they go false. If conditions are already true at boot, it won't fire until they reset.
  A scenario with `IsActive: false` is skipped entirely by `check()` (absent flag = active for
  backward compat); it re-arms so it fires cleanly on the next rising edge once re-enabled.
- **Time:** NTP-synced, with a compile-time fallback clock so scenarios run even before sync.
  Scenario time conditions depend on correct timezone config.
- **Fixed-capacity arrays** sized from `config.h` — respect `MAX_*` limits; there is no dynamic
  resizing.
- **Local config:** override defaults in a gitignored `src/config_local.h`, not by editing
  `config.h` directly.
- **Tests** run on the `native` env with stubs — `Timers` and `Scenarios` have suites. Prefer
  keeping logic testable off-hardware.

## 8. Source Map (quick reference)

```
src/
├── dripdrop.cpp        # Entry: setup(), loop(), WiFi/mDNS/NTP/watchdog, settings load/save
├── config.h            # All tunables, pins, MAX_* limits, feature flags, debug macros
├── types.h             # Relay, Timer, SystemStatus structs; RelaySource enum
├── routes.cpp / .h     # HTTP routing table (no logic)
├── api_utils.cpp / .h  # Shared HTTP/JSON/auth helpers, WebServer extern
├── handlers_*.cpp      # Endpoint implementations, grouped by domain
├── relays.cpp / .h     # RelayController
├── timers.cpp / .h     # TimerManager
├── scenarios.cpp / .h  # ScenarioManager (conditions, actions, callUrl queue)
├── modules.cpp / .h    # ModuleManager (I²C protocol)
├── mqtt.cpp / .h       # MQTT telemetry + control
├── display.cpp / .h    # LCD status display
└── AppHtml.h           # Embedded provisioning UI
test/                   # native Unity tests + hardware stubs
Makefile / Dockerfile / docker-compose.yml  # build + OTA deploy
platformio.ini         # esp32dev + native envs
```
