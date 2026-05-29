-include .env
export

DEVICE_IPS ?= 192.168.0.86
BUILD_DIR   = build
PIO        ?= $(shell command -v pio 2>/dev/null || echo ~/.platformio/penv/bin/pio)

.PHONY: build ota-firmware ota-fs ota flash flash-all clean

build:
	@mkdir -p $(BUILD_DIR)
	docker compose run --rm build

# OTA: push firmware binary over WiFi
ota-firmware: build
	@for ip in $(DEVICE_IPS); do \
	  echo "Flashing firmware to $$ip..."; \
	  curl -X POST "http://$$ip/ota/upload" \
	    -F "firmware=@$(BUILD_DIR)/firmware.bin" \
	    --progress-bar | cat; \
	done

# OTA: replace LittleFS image over WiFi (replaces all webapp files, config is preserved by partition)
ota-fs: build
	@for ip in $(DEVICE_IPS); do \
	  echo "Uploading filesystem to $$ip..."; \
	  curl -X POST "http://$$ip/ota/upload-fs" \
	    -F "fs=@$(BUILD_DIR)/littlefs.bin" \
	    --progress-bar | cat; \
	done

# Full OTA: update dashboard then firmware (firmware reboots device)
ota: ota-fs ota-firmware

# First-time USB flash: firmware only
flash: build
	$(PIO) run -e esp32dev -t upload

# First-time USB flash: firmware + LittleFS (use only on a fresh device with no config)
flash-all: build
	$(PIO) run -e esp32dev -t upload
	$(PIO) run -e esp32dev -t uploadfs

clean:
	rm -rf $(BUILD_DIR)
