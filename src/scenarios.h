/**
 * DripDrop - Scenario Manager
 *
 * If-this-then-that automation rules evaluated on a 5-second interval.
 * Scenarios are persisted to LittleFS as a single JSON file.
 */

#ifndef DRIPDROP_SCENARIOS_H
#define DRIPDROP_SCENARIOS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "types.h"

// LittleFS file path for scenario storage
constexpr const char *SCENARIOS_FILE = "/scenarios.json";

// Debounce save delay (ms) — batch rapid API changes into one write
constexpr unsigned long SCENARIO_SAVE_DEBOUNCE_MS = 2000;

// Queued callUrl request — populated during executeActions(), drained in loop()
struct CallUrlRequest
{
  String url;
  String method;  // "GET" | "POST"
  String headers; // optional raw "Key: Value\n..." lines
  String body;    // optional, POST only
};

class ScenarioManager
{
public:
  /**
   * Initialize LittleFS and load scenarios - call once in setup()
   */
  void begin();

  /**
   * Evaluate all scenarios against current conditions.
   * Call periodically from loop().
   * @param currentTime Current Unix timestamp
   */
  void check(time_t currentTime);

  /**
   * Add a new scenario from JSON input (without id/lastRun).
   * @param input ScenarioInput JSON object
   * @param outId Receives the assigned ID string
   * @return Error message, or nullptr on success
   */
  const char *add(const JsonObject &input, String &outId);

  /**
   * Update an existing scenario by ID.
   * @param id Scenario ID string
   * @param input ScenarioInput JSON object
   * @return Error message, or nullptr on success
   */
  const char *update(const char *id, const JsonObject &input);

  /**
   * Delete a scenario by ID.
   * @return true if found and deleted
   */
  bool remove(const char *id);

  /**
   * Serialize all scenarios directly to output.
   */
  void serialize(String &output) const;

  /**
   * Get count of stored scenarios.
   */
  uint8_t count() const { return _count; }

  /**
   * Flush pending save if debounce delay has elapsed.
   * Call from loop().
   */
  void maybeSave(unsigned long now);

  /**
   * Execute any queued callUrl requests.
   * Call from loop() after check() returns.
   * HTTPS is supported with cert verification skipped (setInsecure).
   */
  void drainCallUrlQueue();

private:
  // In-memory scenario storage as a single JSON document
  mutable JsonDocument _doc;
  uint8_t _count = 0;
  bool _dirty = false;
  unsigned long _dirtyTime = 0;

  // Queue of callUrl requests accumulated during executeActions()
  CallUrlRequest _callUrlQueue[CALL_URL_QUEUE_SIZE];
  uint8_t _callUrlCount = 0;

  // Per-scenario runtime state, keyed by ID (not persisted)
  struct RuntimeState
  {
    uint16_t id;    // Scenario ID (0 = unused slot)
    bool fired;     // Edge detection: true = already fired this window
    time_t lastRun; // Unix timestamp of last fire, 0 = never
  };
  RuntimeState _state[MAX_SCENARIOS];

  /**
   * Load scenarios from LittleFS.
   */
  void load();

  /**
   * Save scenarios to LittleFS immediately.
   */
  void save();

  /**
   * Find the next available ID (max existing + 1).
   */
  uint16_t nextId() const;

  /**
   * Validate a ScenarioInput JSON object.
   * @return Error message, or nullptr if valid
   */
  const char *validate(const JsonObject &input) const;

  /**
   * Evaluate all conditions for a scenario.
   * @return true if all conditions pass
   */
  bool evaluateConditions(const JsonArray &conditions, const struct tm *timeInfo, time_t now) const;

  /**
   * Execute all actions for a scenario.
   */
  void executeActions(const JsonArray &actions, time_t now);

  /**
   * Find a scenario's index in the array by ID.
   * @return Index, or -1 if not found
   */
  int findIndex(const char *id) const;

  /**
   * Get or create runtime state slot for a scenario ID.
   */
  RuntimeState *getState(uint16_t id);

  /**
   * Clear runtime state for a scenario ID.
   */
  void clearState(uint16_t id);
};

extern ScenarioManager Scenarios;

#endif // DRIPDROP_SCENARIOS_H
