/**
 * DripDrop - Scenario action execution
 *
 * ScenarioManager members that carry out a fired scenario's actions (relay,
 * driver, callUrl, display) plus the deferred callUrl HTTP queue. Split out of
 * scenarios.cpp; compiled as its own translation unit for the esp32dev build and
 * #included by the native test harness.
 */

#include "scenarios.h"
#include "relays.h"
#include "timers.h"
#include "modules.h"
#include "mqtt.h"
#include "display.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#if defined(ESP32)
#include <esp_task_wdt.h>
#endif

void ScenarioManager::executeActions(const JsonArray &actions, time_t now)
{
  for (JsonObject action : actions)
  {
    const char *type = action[SKey::TYPE] | SVal::RELAY; // default for backward compat

    if (strcmp(type, SVal::RELAY) == 0)
    {
      uint8_t relayId = action[SKey::RELAY_ID];
      const char *state = action[SKey::STATE];
      if (!state)
        continue; // Malformed action — skip (never deref a null state)

      int8_t index = Relays.findByRelayId(relayId);
      if (index < 0)
        continue;

      const Relay *relay = Relays.getRelay(index);
      if (!relay)
        continue;

      // Respect priority: Manual > user Timer > Scenario.
      // A scenario-owned relay reports source SCENARIO even while its auto-off
      // timer runs (the timer remembers SCENARIO), so it stays re-assignable by
      // this or another scenario; only manual / user timers block us here.
      if (relay->isManuallyControlled() || relay->isTimerControlled())
      {
        DEBUG_SCENARIO("Skipping relay %d — overridden by %s\n",
                       relayId,
                       relay->isManuallyControlled() ? "manual" : "timer");
        continue;
      }

      if (strcmp(state, SVal::ON) == 0)
      {
        int duration = action[SKey::DURATION];
        if (duration > MAX_SCENARIO_DURATION_SEC)
        {
          duration = MAX_SCENARIO_DURATION_SEC;
        }

        // Idempotent re-fire: a repeatInterval scenario re-runs every cycle. If
        // this scenario already holds the relay on with a live auto-off timer,
        // skip it — no GPIO write, no MQTT, no timer reset. After the timer
        // expires the relay is off, so a re-fire here re-arms it (the intended
        // duty-cycle when duration < repeatInterval).
        if (relay->isOn && relay->isScenarioControlled() &&
            Timers.isActive(relayId, now))
        {
          continue;
        }

        // The auto-off timer drives the relay but keeps SCENARIO as the logical
        // owner (see Timers.start source param), so we no longer double-write
        // the pin or mask the timer behind a TIMER source.
        Timers.start(relayId, duration, RelaySource::SCENARIO);
        Mqtt.publishRelayState(relayId);
        DEBUG_SCENARIO("Relay %d ON for %d sec\n", relayId, duration);
      }
      else
      {
        // state == "off". Timers.abort() already drops the relay if a timer was
        // running; the setState covers the no-timer case and is an idempotent
        // no-op otherwise. Publish unconditionally so the off state is always
        // reported (abort() does not publish relay state itself).
        Timers.abort(relayId);
        Relays.setState(index, false, RelaySource::NONE);
        Mqtt.publishRelayState(relayId);
        DEBUG_SCENARIO("Relay %d OFF\n", relayId);
      }
    }
    else if (strcmp(type, SVal::CALL_URL) == 0)
    {
      if (_callUrlCount >= CALL_URL_QUEUE_SIZE)
      {
        DEBUG_SCENARIO("callUrl queue full, dropping request\n");
        continue;
      }
      CallUrlRequest &req = _callUrlQueue[_callUrlCount++];
      const char *url = action[SKey::URL] | "";
      const char *method = action[SKey::METHOD] | SVal::GET;
      const char *headers = action[SKey::HEADERS] | "";
      const char *body = action[SKey::BODY] | "";
      req.url.reserve(CALL_URL_MAX_URL_LEN);
      req.url = url;
      req.url = req.url.substring(0, CALL_URL_MAX_URL_LEN);
      req.method = method;
      req.headers = String(headers).substring(0, CALL_URL_MAX_HEADERS_LEN);
      req.body = String(body).substring(0, CALL_URL_MAX_BODY_LEN);
    }
    else if (strcmp(type, SVal::DRIVER) == 0)
    {
      const char *uid = action[SKey::UID] | "";
      uint8_t cmd = action[SKey::CMD].as<int>();
      uint8_t addr = Modules.addrForUid(uid);
      if (addr == 0)
      {
        DEBUG_SCENARIO("driver action: uid=%s not registered, skipping\n", uid);
        continue;
      }
      if (!Modules.commandModule(addr, cmd))
      {
        DEBUG_SCENARIO("driver action: commandModule failed for uid=%s cmd=%d\n", uid, cmd);
      }
    }
    else if (strcmp(type, SVal::DISPLAY_ACT) == 0)
    {
      // Note: with repeatInterval set this re-applies every cycle. Give the
      // override a timeout >= repeatInterval so it refreshes seamlessly instead
      // of lapsing to the normal screen and snapping back (visible flicker).
      int timeout = action[SKey::TIMEOUT] | 0;
      if (timeout == 0)
      {
        DEBUG_SCENARIO("display action: timeout=0, skipping\n");
        continue;
      }
      String r0 = action[SKey::ROW0] | "";
      String r1 = action[SKey::ROW1] | "";
      String r2 = action[SKey::ROW2] | "";
      String r3 = action[SKey::ROW3] | "";
      Display.showOverride(r0, r1, r2, r3, (uint32_t)timeout);
      DEBUG_SCENARIO("display action: override for %d sec\n", timeout);
    }
    else
    {
      DEBUG_SCENARIO("Unknown action type '%s', skipping\n", type);
    }
  }
}

// =============================================================================
// callUrl Execution
// =============================================================================

static void applyParsedHeaders(HTTPClient &http, const String &raw)
{
  int start = 0;
  int len = raw.length();
  while (start < len)
  {
    int nl = raw.indexOf('\n', start);
    String line = (nl < 0) ? raw.substring(start) : raw.substring(start, nl);
    start = (nl < 0) ? len : nl + 1;

    line.trim();
    if (line.length() == 0)
      continue;

    int colon = line.indexOf(':');
    if (colon <= 0)
      continue; // no colon or colon at position 0 — malformed

    String key = line.substring(0, colon);
    String val = line.substring(colon + 1);
    key.trim();
    val.trim();
    if (key.length() > 0)
    {
      http.addHeader(key, val);
    }
  }
}

// Issue the configured request on an already-begun HTTPClient and return the code.
static int performCallUrl(HTTPClient &http, const CallUrlRequest &req)
{
  http.setTimeout(CALL_URL_TIMEOUT_MS);
  applyParsedHeaders(http, req.headers);

  if (req.method == SVal::POST)
  {
    http.addHeader("Content-Length", String(req.body.length()));
    return http.POST(req.body);
  }
  return http.GET();
}

static void executeCallUrl(const CallUrlRequest &req)
{
  unsigned long t0 = millis();
  HTTPClient http;
  int code;

  if (req.url.startsWith("https://"))
  {
    // HTTPS: cert verification skipped — standard for embedded devices
    WiFiClientSecure *secureClient = new WiFiClientSecure;
    secureClient->setInsecure();
    http.begin(*secureClient, req.url);
    code = performCallUrl(http, req);
    http.end();
    delete secureClient;
  }
  else
  {
    http.begin(req.url);
    code = performCallUrl(http, req);
    http.end();
  }

  DEBUG_SCENARIO("callUrl %s → %d (%lums)\n", req.url.c_str(), code, millis() - t0);
}

void ScenarioManager::drainCallUrlQueue()
{
  for (uint8_t i = 0; i < _callUrlCount; i++)
  {
#if defined(ESP32)
    // Each request blocks up to CALL_URL_TIMEOUT_MS; pet the watchdog between
    // them so a full queue of slow hosts can't exceed WATCHDOG_TIMEOUT_MS before
    // loop() resets it.
    esp_task_wdt_reset();
#endif
    executeCallUrl(_callUrlQueue[i]);
  }
  _callUrlCount = 0;
}
