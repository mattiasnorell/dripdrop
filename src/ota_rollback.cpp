/**
 * DripDrop - Application-Managed OTA Rollback Implementation
 */

#include "ota_rollback.h"
#include "config.h"
#include "api_utils.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_ota_ops.h>

// NVS namespace + key for the pending-verify state. A single counter is the whole state
// machine: 0/absent means nothing pending, N>0 means an update is pending with N-1 boot
// attempts consumed. (No separate "pending" flag — it would be derivable from the
// counter, and two keys can end up half-written.)
static const char *OTA_NVS_NS = "ota";
static const char *KEY_ATTEMPTS = "attempts";

void otaArmPendingVerify()
{
  Preferences p;
  if (!p.begin(OTA_NVS_NS, false))
    return;
  p.putUChar(KEY_ATTEMPTS, 1);
  p.end();
  DEBUG_PRINTLN(F("[OTA] pending-verify armed"));
}

void otaBootCheck()
{
  Preferences p;
  if (!p.begin(OTA_NVS_NS, false))
    return;

  uint8_t attempts = p.getUChar(KEY_ATTEMPTS, 0);
  if (attempts == 0)
  {
    p.end();
    return; // nothing pending — normal boot
  }

  if (attempts > OTA_ROLLBACK_MAX_BOOTS)
  {
    // The new image has failed to validate too many times. Revert to the other OTA
    // slot, which still holds the previously-running (known-good) firmware — the
    // updater always flashes the INACTIVE partition, so the fallback is intact.
    const esp_partition_t *prev = esp_ota_get_next_update_partition(NULL);
    if (!prev || esp_ota_set_boot_partition(prev) != ESP_OK)
    {
      // Leave the counter armed so the rollback is retried on the next boot rather
      // than continuing into the bad image with the guard disarmed.
      p.end();
      DEBUG_PRINTLN(F("[OTA] rollback failed — will retry next boot"));
      return;
    }
    // Boot partition now points at the known-good slot — safe to clear the guard.
    p.remove(KEY_ATTEMPTS);
    p.end();

    // Restore the webapp that matches the firmware being rolled back to (the updater
    // parked it in WEBAPP_OLD_DIR). LittleFS is not yet mounted this early in setup(),
    // so mount it here; the restart below gives the normal boot a clean slate.
    if (LittleFS.begin() && LittleFS.exists(WEBAPP_OLD_DIR))
    {
      removeTree(WEBAPP_DIR);
      LittleFS.rename(WEBAPP_OLD_DIR, WEBAPP_DIR);
    }

    DEBUG_PRINTLN(F("[OTA] update failed to validate — rolling back"));
    delay(100);
    ESP.restart(); // does not return
    return;
  }

  p.putUChar(KEY_ATTEMPTS, attempts + 1);
  p.end();
  DEBUG_PRINTF("[OTA] pending verify (boot %u/%u)\n", attempts, OTA_ROLLBACK_MAX_BOOTS);
}

void otaMarkUpdateValid()
{
  Preferences p;
  if (!p.begin(OTA_NVS_NS, false))
    return;

  if (p.getUChar(KEY_ATTEMPTS, 0) > 0)
  {
    p.remove(KEY_ATTEMPTS);
    DEBUG_PRINTLN(F("[OTA] update validated — commit"));

    // The rollback fallback is no longer needed — drop the parked old webapp.
    removeTree(WEBAPP_OLD_DIR);

    // If the ESP-IDF bootloader-level rollback IS enabled (image sits in PENDING_VERIFY),
    // also cancel it there. Guarded so we never emit a spurious error on stock Arduino,
    // where the image is not in that state.
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY)
    {
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }
  p.end();
}
