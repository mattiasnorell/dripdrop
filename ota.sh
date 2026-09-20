#!/usr/bin/env bash
set -euo pipefail

# Load .env if present (same as Makefile's -include .env / export)
if [ -f .env ]; then
  set -a
  # shellcheck source=/dev/null
  source .env
  set +a
fi

DEVICE_IPS="${DEVICE_IPS:-192.168.0.86}"
BUILD_DIR="build"

# Build
#mkdir -p "$BUILD_DIR"
#docker build --build-arg "WEBAPP_REPO=${WEBAPP_REPO}" -t dripdrop-build .
#docker run --rm -v "$(pwd)/build:/output" dripdrop-build sh -c "
#  mkdir -p /output/webapp &&
#  cp .pio/build/esp32dev/firmware.bin /output/firmware.bin &&
#  cp .pio/build/esp32dev/littlefs.bin /output/littlefs.bin &&
#  cp -r data/webapp/. /output/webapp/ &&
#  echo 'Build complete. Artifacts in ./build/'
#"

# OTA webapp: clear /webapp dir then upload individual files
#for ip in $DEVICE_IPS; do
# # echo "Clearing /webapp on $ip..."
#  curl -s -X DELETE "http://$ip/api/v1/fs/dir?path=/webapp"
#  echo "Uploading webapp files to $ip..."
#  find "$BUILD_DIR/webapp" -type f | while read -r file; do
#    relpath="/webapp/${file#$BUILD_DIR/webapp/}"
#    echo "  $relpath"
#    curl -s -X POST "http://$ip/api/v1/fs/upload?path=$relpath" -F "file=@$file"
#  done
#  echo "Done $ip"
#done

# OTA firmware: push firmware binary over WiFi (device reboots after this)
for ip in $DEVICE_IPS; do
  echo "Flashing firmware to $ip..."
  curl -X POST "http://$ip/api/v1/ota/upload" \
    -F "firmware=@$BUILD_DIR/firmware.bin" \
    --progress-bar | cat
done
