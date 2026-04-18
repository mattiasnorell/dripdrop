# DripDrop API Documentation

Base URL: `http://dripdrop.local` (or `http://{device-ip}`)

## Authentication

When authentication is enabled, all endpoints (except `GET /` and `OPTIONS` preflight requests) require an API key header:

```
X-API-Key: your-api-key
```

Omitting the header returns `401`. An incorrect key returns `403`.

## Common Response Formats

**Success:**
```json
{ "message": "ok" }
```

**Error:**
```json
{ "error": "Description of the error" }
```

All endpoints return `Content-Type: application/json` unless noted otherwise.

---

## System

### `GET /system/status`

Returns overall system status.

**Response:**
```json
{
  "firmware": "3.1.0",
  "uptime": 3723000,
  "uptimeFormatted": "0d 1h 2m",
  "freeHeap": 34120,
  "wifiConnected": true,
  "wifiRssi": -62,
  "apMode": false,
  "ntpSynced": true,
  "currentTime": 1711100400,
  "activeValves": 1,
  "activeSchedules": 3
}
```

| Field | Type | Description |
|-------|------|-------------|
| `firmware` | string | Firmware version |
| `uptime` | number | Milliseconds since last boot |
| `uptimeFormatted` | string | Human-readable uptime |
| `freeHeap` | number | Free heap memory in bytes |
| `wifiConnected` | boolean | Whether connected to a WiFi network |
| `wifiRssi` | number | WiFi signal strength in dBm |
| `apMode` | boolean | Whether running as an Access Point (fallback mode) |
| `ntpSynced` | boolean | Whether time has been synchronized via NTP |
| `currentTime` | number | Current Unix timestamp |
| `activeValves` | number | Number of valves currently on |
| `activeSchedules` | number | Number of configured (non-empty) schedules |

---

### `GET /system/ip`

Returns the device IP address as plain text.

**Response:** `text/plain`
```
192.168.1.42
```

---

### `GET /system/ping`

Health check endpoint.

**Response:**
```json
{ "message": "pong" }
```

---

### `GET /system/time`

Returns current device time.

**Response:**
```json
{
  "unixTime": 1711100400,
  "synced": true,
  "formatted": "2024-03-22 14:00:00",
  "dayOfWeek": 5
}
```

| Field | Type | Description |
|-------|------|-------------|
| `unixTime` | number | Current Unix timestamp |
| `synced` | boolean | Whether NTP is synchronized |
| `formatted` | string | Local time as `YYYY-MM-DD HH:MM:SS` — only present when `synced` is `true` |
| `dayOfWeek` | number | 0=Sunday, 1=Monday, …, 6=Saturday — only present when `synced` is `true` |

---

### `POST /system/reboot`

Reboots the device. The device will be unreachable for a few seconds after this call.

**Request body:** none

**Response:**
```json
{ "message": "Rebooting..." }
```

---

## Valves

Valve IDs are **1-based** integers (`1`–`4`).

### `GET /valves`

Returns state of all valves.

**Response:**
```json
[
  {
    "id": 1,
    "isOn": true,
    "source": 2,
    "lastRunStart": 1711100000,
    "lastRunEnd": 0,
    "timerRemaining": 182
  },
  {
    "id": 2,
    "isOn": false,
    "source": 0,
    "lastRunStart": 1711090000,
    "lastRunEnd": 1711090600
  }
]
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | number | Valve ID (1–4) |
| `isOn` | boolean | Current valve state |
| `source` | number | What is controlling the valve: `0`=none, `1`=schedule, `2`=timer, `3`=manual |
| `lastRunStart` | number | Unix timestamp of last activation (`0` if never run) |
| `lastRunEnd` | number | Unix timestamp of last deactivation (`0` if never stopped or currently running) |
| `timerRemaining` | number | Seconds remaining on active timer — only present when a timer is active |

---

### `GET /valves/{id}/state`

Returns state of a single valve.

**Path parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | number | Valve ID (1–4) |

**Response:**
```json
{
  "valveId": 1,
  "isOn": true,
  "source": 3,
  "timerRemaining": 45
}
```

**Errors:** `404` valve not found.

---

### `POST /valves/{id}/on`

Turns a valve on manually. Clears any schedule control; a running timer is not cancelled.

**Path parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | number | Valve ID (1–4) |

**Request body:** none

**Response:** `{ "message": "ok" }`

**Errors:** `404` valve not found.

---

### `POST /valves/{id}/off`

Turns a valve off and cancels any active timer for that valve.

**Path parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | number | Valve ID (1–4) |

**Request body:** none

**Response:** `{ "message": "ok" }`

**Errors:** `404` valve not found.

---

### `POST /valves/off`

Turns all valves off and cancels all active timers.

**Request body:** none

**Response:** `{ "message": "ok" }`

---

## Timers

A timer turns a valve on for a fixed duration and then turns it off automatically. Only one timer per valve. Starting a new timer on a valve that already has one replaces it.

### `GET /timers`

Returns timer state for all valves.

**Response:**
```json
[
  {
    "valveId": 1,
    "endTime": 1711100582,
    "active": true,
    "remaining": 182
  },
  {
    "valveId": 2,
    "endTime": -1,
    "active": false
  }
]
```

| Field | Type | Description |
|-------|------|-------------|
| `valveId` | number | Valve ID (1–4) |
| `endTime` | number | Unix timestamp when timer expires; `-1` if inactive |
| `active` | boolean | Whether the timer is currently running |
| `remaining` | number | Seconds until expiry — only present when `active` is `true` |

---

### `POST /valves/{id}/timer`

Starts a timer on a valve. The valve turns on immediately and off when the timer expires.

**Path parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | number | Valve ID (1–4) |

**Request body:**
```json
{ "duration": 300 }
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `duration` | number | Yes | Duration in seconds (1–86400) |

**Response:** `{ "message": "ok" }`

**Errors:** `400` missing/invalid fields or duration out of range, `404` valve not found, `500` internal failure.

---

### `DELETE /valves/{id}/timer`

Cancels an active timer and turns the valve off.

**Path parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | number | Valve ID (1–4) |

**Request body:** none

**Response:** `{ "message": "ok" }`

**Errors:** `404` valve not found.

---

## Schedules

Schedules run a valve automatically at a fixed time on selected days of the week. Up to **32 schedules** can be stored. Schedule IDs are **0-based** slot indices (`0`–`31`).

### Days Array Format

The `days` field is an array of 7 booleans, one per day of the week starting from **Sunday**:

```
Index: [0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat]
```

Example — weekdays only:
```json
[false, true, true, true, true, true, false]
```

---

### `GET /schedules`

Returns all active schedule slots.

**Response:**
```json
[
  {
    "scheduleId": 0,
    "valveId": 1,
    "fromHour": 6,
    "fromMinute": 30,
    "duration": 900,
    "days": [false, true, true, true, true, true, false]
  }
]
```

| Field | Type | Description |
|-------|------|-------------|
| `scheduleId` | number | Slot index (0–31) |
| `valveId` | number | Valve ID (1–4) |
| `fromHour` | number | Start hour (0–23) |
| `fromMinute` | number | Start minute (0–59) |
| `duration` | number | Run duration in seconds (1–14400) |
| `days` | array | 7 booleans, index 0=Sunday through 6=Saturday |

---

### `POST /schedules`

Adds a new schedule to the first available slot.

**Request body:**
```json
{
  "valveId": 2,
  "fromHour": 7,
  "fromMinute": 0,
  "duration": 600,
  "days": [false, true, true, true, true, true, false]
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `valveId` | number | Yes | — | Valve ID (1–4) |
| `fromHour` | number | No | `0` | Start hour (0–23) |
| `fromMinute` | number | No | `0` | Start minute (0–59) |
| `duration` | number | No | `300` | Duration in seconds (1–14400) |
| `days` | array | No | none (never fires) | 7 booleans, Sun–Sat |

**Response:**
```json
{
  "message": "ok",
  "scheduleId": 4
}
```

`scheduleId` is the slot index that was assigned.

**Errors:** `400` invalid parameters or no free slots available.

---

### `PUT /schedules/{id}`

Updates an existing schedule. Only include fields you want to change; omitted fields keep their current values. The `days` array, if provided, must be the full 7-element array.

**Path parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | number | Schedule slot index (0–31) |

**Request body:**
```json
{
  "fromHour": 8,
  "duration": 1200,
  "days": [true, false, false, false, false, false, true]
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `valveId` | number | No | Valve ID (1–4) |
| `fromHour` | number | No | Start hour (0–23) |
| `fromMinute` | number | No | Start minute (0–59) |
| `duration` | number | No | Duration in seconds (1–14400) |
| `days` | array | No | Full 7-boolean array, Sun–Sat |

**Response:** `{ "message": "ok" }`

**Errors:** `400` invalid parameters, `404` schedule slot not found or empty, `500` internal failure.

---

### `DELETE /schedules/{id}`

Deletes a schedule by slot index.

**Path parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | number | Schedule slot index (0–31) |

**Request body:** none

**Response:** `{ "message": "ok" }`

**Errors:** `400` invalid scheduleId.

---

### `DELETE /schedules`

Deletes all schedules.

**Request body:** none

**Response:** `{ "message": "ok" }`

---

## Control Priority

When multiple control sources are active, the following priority applies (highest first):

1. **Manual** — set via `POST /valves/{id}/on`. Overrides everything. Cleared by turning the valve off manually or via `POST /valves/off`.
2. **Timer** — set via `POST /valves/{id}/timer`. Overrides schedule control. Expires automatically.
3. **Schedule** — automatic time-based control. Only activates if the valve is not manually or timer controlled.

---

## Notes

- All timestamps are **Unix time** (seconds since 1970-01-01 UTC).
- Schedule and timer checks run every **5 seconds**, so activation may be up to 5 seconds late.
- Schedules that span midnight are supported (e.g. start 23:00, duration 2 hours ends at 01:00 the next day).
- The device is discoverable on the local network as `dripdrop.local` via mDNS.
- OTA firmware updates are available at `http://dripdrop.local/update`.
- Requires Arduino ESP32 core v2.x+ for path parameter support (`UriBraces`).
