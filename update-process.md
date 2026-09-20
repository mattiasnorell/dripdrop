# DripDrop — Self-Update Process (OTA Pull)

This document describes how the device's **pull-based self-update** works, the exact
format of the update **manifest**, and how to **host** the firmware and web GUI so a
device can update itself. It covers both **self-hosting** (start here) and moving to an
**online service** later.

> This is the *pull* update flow (`POST /api/v1/system/update`), where the device
> reaches out and downloads an update on its own. It is separate from the *push* OTA
> flow used during development (`make ota-firmware` / `make ota-webapp`, which POST
> files **to** the device). See `README.md` for the push flow.

---

## 1. How the update flow works

The logic lives in `src/handlers_update.cpp` and is triggered by:

```
POST /api/v1/system/update
```

(Include the `X-API-Key` header if `API_AUTH_ENABLED` is set on the device.)

When called, the device performs these steps in order:

1. **Fetch the manifest** — `GET <UPDATE_BASE_URL>/manifest.json`.
2. **Compare versions** — parses `firmware.version` and compares it numerically against
   the running `FIRMWARE_VERSION` (compiled into the firmware, currently `4.5.1` in
   `src/config.h`). The update proceeds **only if the manifest version is strictly
   newer**. Otherwise the device responds `{"status":"up-to-date", ...}` and stops.
3. **Stage the webapp** — downloads each file listed in the manifest's `webapp` array,
   one by one, from `<UPDATE_BASE_URL>/webapp/<path>` into a **staging directory**
   (`/webapp.new`). The live `/webapp` and the root-level config files
   (`/settings.json`, `/scenarios.json`, `/modules.json`, `/relay_names.json`) are
   **left untouched** at this point.
4. **Download + flash the firmware** — downloads the firmware binary from
   `<UPDATE_BASE_URL>/<firmware.path>` into the **inactive** OTA partition. If the
   manifest provides `firmware.md5`, the flash is **integrity-verified** and aborts on
   mismatch (the boot partition is never switched to a corrupt image).
5. **Commit the webapp** — only now, after the firmware is verified and staged, is the
   staged webapp swapped over the live `/webapp` (an atomic LittleFS rename).
6. **Arm rollback + reboot** — records a "pending verify" marker, responds
   `{"message":"updating"}`, and restarts into the new firmware. On the next boot the new
   firmware must validate itself, or the device **automatically rolls back** to the
   previous firmware (see §9).

### Behavior details (from the implementation)

- **Redirects are followed** (`HTTPC_STRICT_FOLLOW_REDIRECTS`) — this is why GitHub
  release asset URLs, which `302` to a CDN, work.
- **HTTPS is accepted without certificate verification** (`setInsecure()`) — standard
  for embedded devices. Both `http://` and `https://` base URLs are supported.
- **Per-request timeout** is `UPDATE_HTTP_TIMEOUT_MS` = 15 s (`src/config.h`).
- The **task watchdog is disabled** during the download/flash phase (it is a long
  blocking operation) and re-armed if an early step fails.
- Firmware is flashed **last**, so a failure while downloading the webapp aborts the
  update before the firmware is touched.

### On-screen feedback (LCD)

Because `loop()` (and therefore the normal `Display.update()` refresh) is paused during
the blocking update, the handler writes progress to the LCD **synchronously** via
`Display.showMessage()`. The panel shows, in order:

| Stage                         | Row 0                | Row 1                   | Row 2         |
|-------------------------------|----------------------|-------------------------|---------------|
| Update begins                 | `Update in progress` | `Please wait...`        |               |
| Downloading webapp            | `Update in progress` | `Download UI`           | `Step 1/3`    |
| Downloading + flashing FW     | `Update in progress` | `Download firmware`     | `Step 2/3`    |
| Verifying / arming rollback   | `Update in progress` | `Verifying firmware`    | `Step 3/3`    |
| Success (just before reboot)  | `Update in progress` | `Update successful`     | `Rebooting...`|
| Webapp download failed        | `Update failed`      | `Can't update UI`       |               |
| Firmware download/flash failed| `Update failed`      | `Can't update firmware` |               |

On success the "Update successful" screen is held briefly (~1.5 s) before the device
restarts, after which the normal boot screen returns. On failure the `loop()` resumes and
the normal status screen repaints within about a second.

### Safety properties

The flow is designed so a failure never leaves the device in a broken state:

- **Webapp is staged, not wiped.** New files download into `/webapp.new`; the live
  `/webapp` is only replaced (atomic rename) after **every** file has been fetched *and*
  the firmware has been verified. A failed/partial webapp download discards the staging
  dir and leaves the working dashboard untouched.
- **Firmware is verified before it can boot.** With `firmware.md5` in the manifest, a
  truncated or corrupt binary fails verification and the boot partition is never
  switched — the running firmware stays active.
- **Firmware flashes to the inactive partition.** The device runs from one of two app
  slots; the update is written to the *other* one, so an interrupted firmware download
  cannot corrupt the running firmware.
- **Automatic rollback on a bad boot.** Even a firmware that flashes cleanly but fails to
  run (crash/boot-loop) is reverted to the previous slot automatically (see §9).

The REST API and device automation keep running throughout; only the served web GUI is
briefly affected during a swap. You can still ship **firmware-only** updates (empty or
omitted `webapp` array).

---

## 2. Manifest format (`manifest.json`)

The manifest is a single JSON file served at `<UPDATE_BASE_URL>/manifest.json`.

### Schema

| Field                | Type            | Required | Default          | Meaning |
|----------------------|-----------------|----------|------------------|---------|
| `firmware`           | object          | yes      | —                | Firmware descriptor |
| `firmware.version`   | string          | **yes**  | —                | Dotted version `MAJOR.MINOR.PATCH`. Must be non-empty or the update is rejected (`502`). |
| `firmware.path`      | string          | no       | `"firmware.bin"` | Path to the firmware binary, **relative to `UPDATE_BASE_URL`**. |
| `firmware.md5`       | string          | no       | *(none)*         | Lowercase 32-char hex MD5 of the firmware binary. **Strongly recommended** — when present the flash is verified and aborts on mismatch. Omitted ⇒ no integrity check. |
| `webapp`             | array of string | no       | `[]`             | Web GUI files to download, each **relative to `<UPDATE_BASE_URL>/webapp/`**. Omit or leave empty for a firmware-only update. |

Any extra fields you add (e.g. `notes`, `releaseDate`) are ignored by the device and are
safe to include for humans/tooling.

### Version comparison rules

- Versions are parsed as three integers via `%d.%d.%d` — **only major, minor, and
  patch** are considered. A `4.5.10` beats `4.5.9`; a pre-release suffix like
  `4.6.0-rc1` is read as `4.6.0`.
- The device updates **only when the manifest version is strictly greater** than the
  running firmware version. Equal or lower ⇒ no update.
- To ship a firmware-only or webapp-only change, you **still must bump the version** —
  the version gate controls the entire update, webapp included. (There is no separate
  webapp version.)

### `webapp` path semantics

Each string in `webapp` is a path **relative to the `webapp/` directory** on the host.
For a manifest entry `"fonts/dm-sans-normal-latin.woff2"`, the device:

- downloads `GET <UPDATE_BASE_URL>/webapp/fonts/dm-sans-normal-latin.woff2`
- writes it to `/webapp/fonts/dm-sans-normal-latin.woff2` on LittleFS

A leading slash is tolerated (`"/index.html"` and `"index.html"` behave the same).
Parent directories are created automatically on the device. **List every file** the web
GUI needs — the device only downloads what's in the array (it does not crawl the host).

### Full example

```json
{
  "firmware": {
    "version": "4.6.0",
    "path": "firmware.bin",
    "md5": "9e107d9d372bb6826bd81d3542a419d6"
  },
  "webapp": [
    "index.html",
    "favicon.ico",
    "favicon.svg",
    "robots.txt",
    "placeholder.svg",
    "index-DAkKNxvg.js",
    "index-BJUFPTmi.css",
    "vendor-CTUe5Mva.js",
    "react-vendor-AhWEiyyn.js",
    "radix-Gk-ANn4R.js",
    "query-C9iqGUnz.js",
    "Dashboard-DExMnKzt.js",
    "Relays-CsR-Od8H.js",
    "Scenarios-Qzws35RW.js",
    "Sensors-DalsHzxT.js",
    "Modules-Cg2Yv_JQ.js",
    "SettingsPage-COQJfpdJ.js",
    "fonts/dm-sans-normal-latin.woff2",
    "fonts/dm-sans-italic-latin.woff2",
    "fonts/dm-sans-normal-latin-ext.woff2",
    "fonts/dm-sans-italic-latin-ext.woff2"
  ]
}
```

> The webapp is a hashed/fingerprinted Vite build, so **filenames change every build**.
> Generate the `webapp` array from the actual build output rather than hand-maintaining
> it — see the script in §5.

---

## 3. Required hosting layout

Whatever host you use, these three things must be reachable relative to a single base
URL (`UPDATE_BASE_URL`, no trailing slash):

```
<UPDATE_BASE_URL>/
├── manifest.json                     # the manifest (§2)
├── firmware.bin                      # or whatever firmware.path points to
└── webapp/
    ├── index.html
    ├── index-*.js
    ├── index-*.css
    ├── ...
    └── fonts/
        └── *.woff2
```

Requirements for the host:

- Serves plain **static files** over HTTP or HTTPS.
- Supports **nested paths** under `webapp/` (i.e. real directories, not a flat
  filename namespace). This matters when picking an online service — see §6.
- If using HTTPS, any certificate is accepted (the device does not verify certs), so a
  self-signed cert on a LAN host is fine.

---

## 4. Configuring the device (`UPDATE_BASE_URL`)

The base URL is a compile-time constant in `src/config.h`:

```c
#ifndef UPDATE_BASE_URL
  #define UPDATE_BASE_URL "https://github.com/mattiasnorell/dripdrop/releases/latest/download"
#endif
```

**Do not edit `config.h` for your own value** — override it in the gitignored
`src/config_local.h` (this is the project convention for local overrides):

```c
// src/config_local.h
#define UPDATE_BASE_URL "http://192.168.0.10:8080/dripdrop"   // self-hosted, no trailing slash
```

Rebuild and flash the firmware after changing this. The device always requests
`<UPDATE_BASE_URL>/manifest.json`, so point the base at the directory that contains
`manifest.json`.

---

## 5. Building the artifacts and generating the manifest

### 5.1 Build

`make build` (Docker-based) produces everything under `build/`:

```
build/
├── firmware.bin        # the firmware binary to host
├── littlefs.bin        # full filesystem image (not used by the pull-update flow)
└── webapp/             # the built web GUI — this becomes <host>/webapp/
    ├── index.html
    ├── *.js / *.css
    └── fonts/*.woff2
```

For the pull-update you need **`build/firmware.bin`** and the contents of
**`build/webapp/`**. `littlefs.bin` is only for the push OTA / first flash.

### 5.2 Assemble the hosting directory + generate the manifest

Because the webapp filenames are content-hashed, generate the `webapp` array from the
build output. This script assembles a `release/` folder ready to host and writes a
matching `manifest.json`:

```bash
#!/usr/bin/env bash
set -euo pipefail

VERSION="$1"                 # e.g. ./make-release.sh 4.6.0
BUILD_DIR="build"
OUT="release"

rm -rf "$OUT"
mkdir -p "$OUT/webapp"
cp "$BUILD_DIR/firmware.bin" "$OUT/firmware.bin"
cp -r "$BUILD_DIR/webapp/." "$OUT/webapp/"

# Drop macOS cruft that can sneak into the build dir
find "$OUT/webapp" -name '.DS_Store' -delete

# Compute the firmware MD5 (macOS: `md5 -q`; Linux: `md5sum`)
if command -v md5 >/dev/null; then
  MD5=$(md5 -q "$OUT/firmware.bin")
else
  MD5=$(md5sum "$OUT/firmware.bin" | awk '{print $1}')
fi

# Build the webapp file list (paths relative to release/webapp)
FILES=$(cd "$OUT/webapp" && find . -type f | sed 's|^\./||' | sort)

# Emit manifest.json with a JSON array of those files
{
  echo '{'
  echo "  \"firmware\": { \"version\": \"$VERSION\", \"path\": \"firmware.bin\", \"md5\": \"$MD5\" },"
  echo '  "webapp": ['
  printf '%s\n' "$FILES" | awk 'NR>1{printf ",\n"} {printf "    \"%s\"", $0} END{print ""}'
  echo '  ]'
  echo '}'
} > "$OUT/manifest.json"

echo "Wrote $OUT/ (firmware.bin, webapp/, manifest.json) for version $VERSION"
```

**Keep `firmware.version` in the manifest in sync with `FIRMWARE_VERSION` in
`config.h`** for the build you're shipping, and make sure it is **higher** than what
devices are currently running — otherwise they will report `up-to-date` and skip.

---

## 6. Hosting options

### 6.1 Self-hosting (start here)

Any static file server works. Serve the `release/` directory produced in §5.

**Option A — Python (quickest, testing/LAN):**

```bash
cd release
python3 -m http.server 8080
# Base URL: http://<your-host-ip>:8080
# → set UPDATE_BASE_URL to  http://<your-host-ip>:8080
```

**Option B — nginx (durable self-host):**

```nginx
server {
    listen 80;
    server_name updates.local;      # or an IP

    root /srv/dripdrop/release;      # contains manifest.json, firmware.bin, webapp/
    autoindex off;

    location / {
        try_files $uri =404;
    }
}
# Base URL: http://updates.local
```

**Option C — Caddy (automatic HTTPS if you have a domain):**

```
updates.example.com {
    root * /srv/dripdrop/release
    file_server
}
# Base URL: https://updates.example.com
```

Notes for self-hosting:

- Use the host's **LAN IP or an mDNS/DNS name** the device can resolve. `UPDATE_BASE_URL`
  must have **no trailing slash**.
- HTTP is fine on a trusted LAN. HTTPS also works (certs are not verified by the device),
  so self-signed is acceptable.
- Verify from a workstation before triggering a device update:
  ```bash
  curl http://<host>:8080/manifest.json
  curl -I http://<host>:8080/firmware.bin
  curl -I http://<host>:8080/webapp/index.html
  ```

### 6.2 Moving to an online service (later)

The only hard requirement is static hosting that supports **nested `webapp/` paths**.
Good fits:

- **Object storage (S3 / Cloudflare R2 / GCS) behind a CDN** — upload `release/` keeping
  the directory structure; point `UPDATE_BASE_URL` at the bucket/CDN base. Set the
  correct content types and make the objects public (or use signed-URL-free public read).
- **GitHub Pages** — commit `release/` to a `gh-pages` branch or `docs/` folder. Pages
  serves directories, so `webapp/index.html` resolves correctly. Base URL is
  `https://<user>.github.io/<repo>` (plus any subpath you place the release under).
- **Static hosts (Netlify, Cloudflare Pages, Vercel static)** — deploy the `release/`
  directory as-is.

**About the default GitHub Releases URL:** the shipped default points at
`…/releases/latest/download`. GitHub **release assets share a flat filename namespace and
cannot contain `/`**, so `…/latest/download/webapp/index.html` will **404**. That means
GitHub Releases is suitable for **firmware-only** updates (host `manifest.json` and
`firmware.bin` as release assets, with an empty `webapp` array), but **not** for the
per-file webapp scheme. For combined firmware + webapp updates, use one of the
directory-capable hosts above (GitHub Pages, object storage, or a static host).

When you migrate, the device side is a one-line change: update `UPDATE_BASE_URL` in
`config_local.h`, rebuild, and flash. Nothing about the manifest or the download logic
changes.

---

## 7. Triggering and verifying an update

Trigger from any HTTP client on the network:

```bash
# Without API auth:
curl -X POST http://<device-ip>/api/v1/system/update

# With API auth enabled on the device:
curl -X POST http://<device-ip>/api/v1/system/update -H "X-API-Key: <key>"
```

Possible responses:

Success/status responses use a `message`/`status` body; errors use `{"error":"…"}`.

| Response                                             | Meaning |
|------------------------------------------------------|---------|
| `200 {"status":"up-to-date","current":"…","available":"…"}` | Manifest version is not newer; nothing done. |
| `200 {"message":"updating"}` then the device reboots | Success — new firmware is being flashed; device restarts. |
| `502 {"error":"Failed to fetch manifest"}`           | Manifest URL unreachable / non-200. Check `UPDATE_BASE_URL` and the host. |
| `502 {"error":"Invalid manifest"}`                   | `manifest.json` isn't valid JSON. |
| `502 {"error":"Manifest missing firmware.version"}`  | `firmware.version` absent or empty. |
| `502 {"error":"Webapp download failed"}`             | A file in the `webapp` array couldn't be fetched. Staging is discarded and the **live webapp is untouched**; safe to retry. |
| `502 {"error":"Firmware update failed"}`             | Firmware download/flash error (includes an **MD5 mismatch**). Live webapp and running firmware are untouched; safe to retry. |

After reboot, confirm the new version:

```bash
curl http://<device-ip>/api/v1/system/status      # includes "firmware": "<version>"
```

---

## 8. Release checklist

1. Bump `FIRMWARE_VERSION` in `src/config.h` (and commit).
2. `make build`.
3. Run the assemble/manifest script (§5.2) with the **same** version:
   `./make-release.sh <version>`.
4. Confirm `release/manifest.json` `firmware.version` matches and is **higher** than
   what devices run.
5. Publish `release/` to the host (§6). Keep the `webapp/` directory structure intact.
6. Smoke-test the URLs with `curl` (manifest, firmware, one webapp file).
7. Trigger `POST /api/v1/system/update` on a test device; confirm reboot and new version.
8. Roll out to the rest.

---

## 9. Firmware rollback (crash-safety)

A firmware can pass the MD5 check and flash perfectly, yet still fail to *run* — e.g. it
boots into a crash-loop or can't mount the filesystem. DripDrop guards against this with
an **application-managed rollback** (`src/ota_rollback.cpp`), so it works on the stock
Arduino bootloader without needing a custom ESP-IDF bootloader build.

How it works:

1. The updater writes new firmware to the **inactive** app partition; the previous
   (running) firmware stays intact in the other slot.
2. Before rebooting, the device sets a **pending-verify** marker in NVS.
3. On boot, `otaBootCheck()` runs first and counts the attempt. Once the new firmware
   comes up cleanly (LittleFS mounted, managers started), `otaMarkUpdateValid()` clears
   the marker — the update is **committed**.
4. If the new firmware keeps crashing before it can validate, after
   `OTA_ROLLBACK_MAX_BOOTS` (default **3**) attempts `otaBootCheck()` switches the boot
   partition back to the previous slot and reboots into the **known-good** firmware.

Implications for you:

- **No manifest changes** are needed for rollback — it's entirely device-side.
- After a rollback, `GET /api/v1/system/status` reports the **old** `firmware` version.
  If a device unexpectedly shows the previous version after an update, the new build was
  crash-looping and was auto-reverted — investigate that build before re-publishing.
- The webapp swap is committed just before reboot, so a rolled-back device runs the *old*
  firmware with the *new* webapp. This is normally fine (the API is backward-compatible);
  if you make a breaking API change, ship the firmware first in a separate release.
- Tunable via `OTA_ROLLBACK_MAX_BOOTS` in `src/config.h`.
- Limitation: a crash that happens *before* `otaBootCheck()` runs (e.g. in a global
  constructor) isn't counted. In practice such a fault would also have prevented the old
  firmware from booting, but keep `otaBootCheck()` first in `setup()`.
