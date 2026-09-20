-include .env
export

DEVICE_IPS ?= 192.168.0.86
BUILD_DIR   = build
PIO        ?= $(shell command -v pio 2>/dev/null || echo ~/.platformio/penv/bin/pio)

.PHONY: build ota-firmware ota-fs ota-webapp ota flash flash-all clean

build:
	@mkdir -p $(BUILD_DIR)
	docker compose run --rm --build build

# OTA: push firmware binary over WiFi
ota-firmware: 
	@for ip in $(DEVICE_IPS); do \
	  echo "Flashing firmware to $$ip..."; \
	  curl -X POST "http://$$ip/api/v1/ota/upload" \
	    -F "firmware=@$(BUILD_DIR)/firmware.bin" \
	    --progress-bar | cat; \
	done

# OTA: replace LittleFS image over WiFi (replaces all webapp files, config is preserved by partition)
ota-fs: build
	@for ip in $(DEVICE_IPS); do \
	  echo "Uploading filesystem to $$ip..."; \
	  curl -X POST "http://$$ip/api/v1/ota/upload-fs" \
	    -F "fs=@$(BUILD_DIR)/littlefs.bin" \
	    --progress-bar | cat; \
	done

# OTA: clear /webapp dir then upload individual files (preserves config files on the device)
ota-webapp: build
	@for ip in $(DEVICE_IPS); do \
	  echo "Clearing /webapp on $$ip..."; \
	  curl -s -X DELETE "http://$$ip/api/v1/fs/dir?path=/webapp"; \
	  echo "Uploading webapp files to $$ip..."; \
	  find $(BUILD_DIR)/webapp -type f | while read file; do \
	    relpath="/webapp/$${file#$(BUILD_DIR)/webapp/}"; \
	    echo "  $$relpath"; \
	    curl -s -X POST "http://$$ip/api/v1/fs/upload?path=$$relpath" \
	      -F "file=@$$file"; \
	  done; \
	  echo "Done $$ip"; \
	done

# Full OTA: update dashboard then firmware (firmware reboots device)
ota: ota-webapp ota-firmware

# First-time USB flash: firmware only
flash: 
	$(PIO) run -e esp32dev -t upload

# First-time USB flash: firmware + LittleFS (use only on a fresh device with no config)
flash-all: build
	$(PIO) run -e esp32dev -t upload
	$(PIO) run -e esp32dev -t uploadfs

# Report firmware flash/RAM usage (and per-symbol breakdown via -v)
size:
	$(PIO) run -e esp32dev -t size

clean:
	rm -rf $(BUILD_DIR)
