/**
 * DripDrop - Scenario JSON key & value constants
 *
 * Single source of truth for every JSON key string and every type/operator/state/
 * method string literal used by the scenario code. Centralizing them turns a
 * mistyped key (e.g. the old `IsActive` vs `isActive` bug) into a compile error
 * instead of a silent runtime failure.
 *
 * IMPORTANT: these are declared as `constexpr char[]` arrays, not
 * `constexpr const char*`. ArduinoJson stores string keys given as immutable
 * char arrays / const char* by pointer (zero-copy); the array form matches how a
 * bare string literal is treated, so swapping literals for these constants does
 * not change `_doc` memory behavior. Keep them as arrays.
 *
 * The on-disk /scenarios.json field spellings are frozen — do not rename the
 * string values here without a migration.
 */

#ifndef DRIPDROP_SCENARIO_KEYS_H
#define DRIPDROP_SCENARIO_KEYS_H

// JSON object field keys
namespace SKey {
// Top-level scenario fields
constexpr char ID[]              = "id";
constexpr char NAME[]            = "name";
constexpr char CONDITIONS[]      = "conditions";
constexpr char ACTIONS[]         = "actions";
constexpr char REPEAT_INTERVAL[] = "repeatInterval";
constexpr char IS_ACTIVE[]       = "isActive";
constexpr char LAST_RUN[]        = "lastRun";

// Shared discriminator key (conditions and actions)
constexpr char TYPE[]            = "type";

// Condition fields
constexpr char HOUR[]            = "hour";
constexpr char MINUTE[]          = "minute";
constexpr char START_HOUR[]      = "startHour";
constexpr char START_MINUTE[]    = "startMinute";
constexpr char END_HOUR[]        = "endHour";
constexpr char END_MINUTE[]      = "endMinute";
constexpr char DAYS[]            = "days";
constexpr char SENSOR_ID[]       = "sensorId";
constexpr char OPERATOR[]        = "operator";
constexpr char VALUE[]           = "value";

// Action fields
constexpr char RELAY_ID[]        = "relayId";
constexpr char STATE[]           = "state";
constexpr char DURATION[]        = "duration";
constexpr char UID[]             = "uid";
constexpr char CMD[]             = "cmd";
constexpr char URL[]             = "url";
constexpr char METHOD[]          = "method";
constexpr char HEADERS[]         = "headers";
constexpr char BODY[]            = "body";
constexpr char TIMEOUT[]         = "timeout";
constexpr char ROW0[]            = "row0";
constexpr char ROW1[]            = "row1";
constexpr char ROW2[]            = "row2";
constexpr char ROW3[]            = "row3";
}  // namespace SKey

// Discriminator string values compared against the "type"/"operator"/"state"/
// "method" fields.
namespace SVal {
// Condition types
constexpr char TIME[]         = "time";
constexpr char TIME_RANGE[]   = "timeRange";
constexpr char DAY_OF_WEEK[]  = "dayOfWeek";
constexpr char SENSOR_VALUE[] = "sensorValue";

// Action types
constexpr char RELAY[]        = "relay";
constexpr char DRIVER[]       = "driver";
constexpr char CALL_URL[]     = "callUrl";
// NB: not `DISPLAY` — the ESP32 Arduino core defines `#define DISPLAY 0x1`,
// which would macro-expand the identifier and break the firmware build.
constexpr char DISPLAY_ACT[]  = "display";

// sensorValue operators
constexpr char OP_GT[]        = "gt";
constexpr char OP_LT[]        = "lt";
constexpr char OP_EQ[]        = "eq";

// relay action states
constexpr char ON[]           = "on";
constexpr char OFF[]          = "off";

// callUrl HTTP methods
constexpr char GET[]          = "GET";
constexpr char POST[]         = "POST";
}  // namespace SVal

#endif  // DRIPDROP_SCENARIO_KEYS_H
