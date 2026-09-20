#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"
OUT="release"

# Version comes from the firmware's own source of truth so the manifest can never
# advertise a version the binary doesn't report (which would re-update in a loop).
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION=$(sed -n 's/#define FIRMWARE_VERSION "\(.*\)"/\1/p' "$SCRIPT_DIR/src/config.h")
if [ -z "$VERSION" ]; then
  echo "Could not read FIRMWARE_VERSION from src/config.h" >&2
  exit 1
fi

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

# Total webapp payload size, so the device can fail fast when LittleFS is too full to
# stage the download alongside the live webapp.
WEBAPP_BYTES=$(find "$OUT/webapp" -type f -exec cat {} + | wc -c | tr -d '[:space:]')

# Emit manifest.json with a JSON array of those files
{
  echo '{'
  echo "  \"firmware\": { \"version\": \"$VERSION\", \"path\": \"firmware.bin\", \"md5\": \"$MD5\" },"
  echo "  \"webappBytes\": $WEBAPP_BYTES,"
  echo '  "webapp": ['
  printf '%s\n' "$FILES" | awk 'NR>1{printf ",\n"} {printf "    \"%s\"", $0} END{print ""}'
  echo '  ]'
  echo '}'
} > "$OUT/manifest.json"

echo "Wrote $OUT/ (firmware.bin, webapp/, manifest.json) for version $VERSION"
