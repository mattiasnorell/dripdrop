/**
 * DripDrop - Application-Managed OTA Rollback
 *
 * Provides crash-safe rollback for the self-update flow WITHOUT requiring the ESP-IDF
 * bootloader option CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE (which the stock Arduino
 * precompiled bootloader does not enable).
 *
 * Mechanism: after a self-update the newly flashed image boots in a "pending verify"
 * state tracked in NVS. Every boot increments an attempt counter; a healthy boot reaches
 * otaMarkUpdateValid() and clears it. If the new image crash-loops and never validates,
 * otaBootCheck() reverts the boot partition to the previous (known-good) slot, restores
 * the matching webapp parked in WEBAPP_OLD_DIR, and reboots.
 *
 * Limitation: a crash occurring before otaBootCheck() runs (e.g. in a global constructor)
 * is not counted, so keep otaBootCheck() first in setup().
 */

#ifndef DRIPDROP_OTA_ROLLBACK_H
#define DRIPDROP_OTA_ROLLBACK_H

/**
 * Call FIRST in setup(), before any other initialization. If a pending update has failed
 * to validate across OTA_ROLLBACK_MAX_BOOTS boots, this reverts the boot partition to the
 * previous slot and reboots (does not return in that case). Otherwise it records another
 * boot attempt and returns.
 */
void otaBootCheck();

/**
 * Call once the device is fully up — after server.begin(), so a validated image is
 * guaranteed to be reachable for the next update. Commits a pending update (no longer
 * subject to rollback) and deletes the parked old webapp. No-op when nothing is pending.
 */
void otaMarkUpdateValid();

/**
 * Call right before rebooting into a freshly flashed image (end of the update handler).
 * Arms the pending-verify state so the new image must validate itself or be rolled back.
 */
void otaArmPendingVerify();

#endif // DRIPDROP_OTA_ROLLBACK_H
