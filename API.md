# DripDrop API Documentation

**Firmware:** v4.0.0
**Base URL:** `http://dripdrop.local` (mDNS) or `http://<ip-address>`
**Port:** 80

---

## Authentication

Authentication is optional and disabled by default. When enabled, every request must include the API key header:

```
X-API-Key: <your-api-key>
```

| Status | Meaning |
|--------|---------|
| `401`  | API key header missing |
| `403`  | API key invalid |

---

## System

### `GET /`

Returns a basic HTML status page.

---

### `GET /system/ping`

Health check.

**Response `200`**
```json
{ "message": "pong" }
```

---

### `GET /system/ip`

Returns the device IP address as plain text.

**Response `200`**
```
192.168.1.42
```

---

### `GET /system/status`

Returns full system status.

**Response `200`**
```json
{
  "firmware": "4.0.0",
  "uptime": 123456,
  "uptimeFormatted": "0d 0h 2m",
  "freeHeap": 210000,
  "wifiConnected": true,
  "wifiRssi": -65,
  "apMode": false,
  "ntpSynced": true,
  "currentTime": 1700000000,
  "activeValves": 1,
  "activeScenarios": 3
}
```

| Field | Type | Description |
|-------|------|-------------|
| `uptime` | integer | Milliseconds since boot |
| `freeHeap` | integer | Free heap memory in bytes |
| `wifiRssi` | integer | Signal strength in dBm |
| `apMode` | boolean | `true` if running as fallback Access Point |
| `ntpSynced` | boolean | `true` if time has been synchronized |
| `currentTime` | integer | Current Unix timestamp |
| `activeValves` | integer | Number of currently open valves |
| `activeScenarios` | integer | Number of configured scenarios |

---

### `GET /system/time`

Returns the current device time.

**Response `200`**
```json
{
  "unixTime": 1700000000,
  "synced": true,
  "formatted": "2023-11-14 22:13:20",
  "dayOfWeek": 2
}
```

`formatted` and `dayOfWeek` are only present when `synced` is `true`.
`dayOfWeek`: `0` = Sunday … `6` = Saturday.

---

### `POST /system/time`

Manually set the device clock (useful when NTP is unavailable).

**Request body**
```json
{ "unixTime": 1700000000 }
```

**Response `200`**
```json
{ "message": "ok" }
```

**Errors**

| Status | Error |
|--------|-------|
| `400`  | `unixTime (integer) is required` |
| `400`  | `unixTime value is invalid` |

---

### `POST /system/reboot`

Reboots the device.

**Response `200`**
```json
{ "message": "Rebooting..." }
```

---

### `POST /update`

OTA firmware update via HTTP multipart upload. Handled by `HTTPUpdateServer`.
Upload a compiled `.bin` file using a tool such as `curl`:

```bash
curl -X POST http://dripdrop.local/update \
  -H "X-API-Key: <key>" \
  -F "firmware=@firmware.bin"
```

---

## Valves

Valve IDs are `1`–`4`. The `source` field indicates what is currently controlling the valve:

| Value | Source |
|-------|--------|
| `0` | Off / none |
| `1` | Scenario automation |
| `2` | One-time timer |
| `3` | Manual API call |

---

### `GET /valves`

Returns the state of all valves.

**Response `200`**
```json
[
  {
    "id": 1,
    "isOn": true,
    "source": 3,
    "lastRunStart": 1700000000,
    "lastRunEnd": 0,
    "timerRemaining": 540
  },
  {
    "id": 2,
    "isOn": false,
    "source": 0,
    "lastRunStart": 1699990000,
    "lastRunEnd": 1699990600
  }
]
```

`timerRemaining` (seconds) is only present when an active timer is running on that valve.

---

### `GET /valves/{id}/state`

Returns the state of a single valve.

**Response `200`**
```json
{
  "valveId": 1,
  "isOn": true,
  "source": 2,
  "timerRemaining": 120
}
```

**Errors**

| Status | Error |
|--------|-------|
| `404`  | `Valve not found` |

---

### `POST /valves/{id}/on`

Turns a valve on manually (no automatic shutoff — use `/timer` for that).

**Response `200`**
```json
{ "message": "ok" }
```

**Errors**

| Status | Error |
|--------|-------|
| `404`  | `Valve not found` |

---

### `POST /valves/{id}/off`

Turns a valve off and cancels any active timer for it.

**Response `200`**
```json
{ "message": "ok" }
```

**Errors**

| Status | Error |
|--------|-------|
| `404`  | `Valve not found` |

---

### `POST /valves/off`

Turns off **all** valves and cancels all timers.

**Response `200`**
```json
{ "message": "ok" }
```

---

## Timers

One-time timers automatically turn off a valve after a set duration.
**Requires NTP sync** (or a manual clock set) to function correctly.

---

### `GET /timers`

Returns all timer slots.

**Response `200`**
```json
[
  {
    "valveId": 1,
    "endTime": 1700000600,
    "active": true,
    "remaining": 540
  }
]
```

`remaining` (seconds) is only present when `active` is `true`.

---

### `POST /valves/{id}/timer`

Starts a one-time timer that turns the valve on and automatically shuts it off.

**Request body**
```json
{ "duration": 600 }
```

`duration` is in **seconds**. Maximum: `86400` (24 hours).

**Response `200`**
```json
{ "message": "ok" }
```

**Errors**

| Status | Error |
|--------|-------|
| `404`  | `Valve not found` |
| `400`  | `Missing duration` |
| `400`  | `Duration must be 1-86400 seconds` |
| `500`  | `Failed to start timer` |

---

### `DELETE /valves/{id}/timer`

Cancels the active timer for a valve (does not turn the valve off).

**Response `200`**
```json
{ "message": "ok" }
```

**Errors**

| Status | Error |
|--------|-------|
| `404`  | `Valve not found` |

---

## Scenarios

Scenarios are if-this-then-that automation rules evaluated every 5 seconds.
They are persisted to flash storage and survive reboots.
Maximum 16 scenarios.

A scenario fires when **all** its conditions match simultaneously.
It will not re-fire until conditions first become false and then true again (edge-triggered).

**Priority:** Manual > Timer > Scenario — a valve that is manually on or timer-controlled cannot be overridden by a scenario.

---

### Scenario Object

```json
{
  "id": "1",
  "name": "Morning watering",
  "conditions": [...],
  "actions": [...],
  "lastRun": 1700000000
}
```

`lastRun` is a Unix timestamp of the last time the scenario fired, or `null` if it has never run.

---

### Condition Types

#### `time` — Match a specific time of day

```json
{
  "type": "time",
  "hour": 6,
  "minute": 30
}
```

| Field | Type | Range |
|-------|------|-------|
| `hour` | integer | 0–23 |
| `minute` | integer | 0–59 |

Evaluated against local time (configured timezone + DST).

---

#### `dayOfWeek` — Match specific days of the week

```json
{
  "type": "dayOfWeek",
  "days": [false, true, true, true, true, true, false]
}
```

`days` is an array of 7 booleans: index `0` = Sunday, index `6` = Saturday.

---

#### `sensorValue` — Compare a sensor reading

```json
{
  "type": "sensorValue",
  "sensorId": "soil1",
  "operator": "lt",
  "value": 400
}
```

| Field | Type | Description |
|-------|------|-------------|
| `sensorId` | string | Sensor identifier |
| `operator` | string | `gt` (greater than), `lt` (less than), `eq` (equal) |
| `value` | integer | Threshold to compare against |

If the sensor is unavailable, the condition evaluates to `false` (fail-safe).

---

### Action Object

```json
{
  "valveId": 1,
  "state": "on",
  "duration": 30
}
```

| Field | Type | Description |
|-------|------|-------------|
| `valveId` | integer | Valve to control (1–4) |
| `state` | string | `"on"` or `"off"` |
| `duration` | integer | **Minutes** to run — required when `state` is `"on"`. Maximum: 240 minutes (4 hours) |

---

### `GET /scenarios`

Returns all configured scenarios.

**Response `200`** — Array of [Scenario objects](#scenario-object).

---

### `POST /scenarios`

Creates a new scenario.

**Request body**
```json
{
  "name": "Morning watering",
  "conditions": [
    { "type": "time", "hour": 6, "minute": 30 },
    { "type": "dayOfWeek", "days": [false, true, true, true, true, true, false] }
  ],
  "actions": [
    { "valveId": 1, "state": "on", "duration": 20 }
  ]
}
```

**Response `200`**
```json
{
  "message": "ok",
  "id": "1"
}
```

**Errors**

| Status | Error |
|--------|-------|
| `400`  | `Maximum number of scenarios reached` |
| `400`  | Validation error (see below) |

---

### `POST /scenarios/{id}`

Updates an existing scenario. Replaces `name`, `conditions`, and `actions` entirely.

**Request body** — same format as [POST /scenarios](#post-scenarios).

**Response `200`**
```json
{ "message": "ok" }
```

**Errors**

| Status | Error |
|--------|-------|
| `400`  | `Scenario not found` |
| `400`  | Validation error |

---

### `DELETE /scenarios/{id}`

Deletes a scenario.

**Response `200`**
```json
{ "message": "ok" }
```

**Errors**

| Status | Error |
|--------|-------|
| `404`  | `Scenario not found` |

---

## Validation Errors

Scenario validation returns one of the following messages on `400`:

| Message |
|---------|
| `Missing or invalid 'name'` |
| `Missing 'conditions' array` |
| `At least one condition is required` |
| `Condition missing 'type'` |
| `Time condition requires 'hour' and 'minute'` |
| `Invalid time values` |
| `dayOfWeek condition requires 'days' array of 7 booleans` |
| `sensorValue condition requires 'sensorId'` |
| `sensorValue condition requires 'operator' (gt, lt, eq)` |
| `sensorValue condition requires 'value'` |
| `Unknown condition type` |
| `Missing 'actions' array` |
| `At least one action is required` |
| `Action missing 'valveId'` |
| `Invalid valveId in action` |
| `Action requires 'state' (on or off)` |
| `Action with state 'on' requires positive 'duration'` |

---

## Error Response Format

All errors follow this structure:

```json
{ "error": "Error message here" }
```

---

## Example: Schedule daily watering Mon–Fri at 07:00 for 15 minutes

```bash
curl -X POST http://dripdrop.local/scenarios \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your-key" \
  -d '{
    "name": "Weekday morning",
    "conditions": [
      { "type": "time", "hour": 7, "minute": 0 },
      { "type": "dayOfWeek", "days": [false, true, true, true, true, true, false] }
    ],
    "actions": [
      { "valveId": 1, "state": "on", "duration": 15 }
    ]
  }'
```
