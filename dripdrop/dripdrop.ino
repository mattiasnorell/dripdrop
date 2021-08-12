#include <Wire.h>;
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "AppHtml.h";
#include <string>;
#include "RTClib.h"

#define DS1307_ADDRESS 0x68

const char *ssid = "";
const char *password = "";

ESP8266WebServer server(80);
RTC_Millis rtc;

const int SCHEDULE_INTERVAL = 5000;

typedef struct
{
  int id;
  uint8_t pinId;
} valve;

valve valves[4] {
  {1, D4},
  {2, D5},
  {3, D6},
  {4, D7}
};

typedef struct
{
  int valveId;
  uint8_t fromHour;
  uint8_t fromMinute;
  uint8_t toHour;
  uint8_t toMinute;
} valveSchedule;

valveSchedule schedules[4] {
  {1, 0, 0, 0, 0},
  {2, 0, 0, 0, 0},
  {3, 0, 0, 0, 0},
  {4, 0, 0, 0, 0}
};

typedef struct
{
  int valveId;
  int to;
} valveTimer;

valveTimer timers[4] {
  {1, -1},
  {2, -1},
  {3, -1},
  {4, -1}
};


void setup()
{
  // Set up logging
  Serial.begin(57600);
  Serial.println("Setting up");

  // Set up RTC
  //rtc.begin();

  // Set up valve pins
  for (int i = 0; i < sizeof(valves) / sizeof(valve); ++i)
  {
    pinMode(valves[i].pinId, OUTPUT);
    digitalWrite(valves[i].pinId, HIGH);
  }

  // Set up WiFi
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Checking WiFi");
    delay(1000);
  }

  Serial.print("IP adress:\t");
  Serial.println(WiFi.localIP());

  // Set up mDNS
  if (MDNS.begin("dripdrop"))
  {
    Serial.println("mDNS responder started");
  }
  else
  {
    Serial.println("Error setting up MDNS responder!");
  }

  // Set up routes
  server.on("/", HTTP_GET, onRootRoute);

  server.on("/system/ip", HTTP_GET, onSystemIpRoute);

  server.on("/system/time", HTTP_GET, onSystemTimeGetRoute);
  server.on("/system/time", HTTP_POST, onSystemTimeSetRoute);

  server.on("/valve/state/on", HTTP_POST, onValveStateChangeOnRoute);
  server.on("/valve/state/off", HTTP_POST, onValveStateChangeOffRoute);
  server.on("/valve/state", HTTP_GET, onValveStateRoute);

  server.on("/valves/off", HTTP_POST, onValvesAllOffRoute);

  server.on("/timer", HTTP_POST, onTimerPostRoute);
  server.on("/timer", HTTP_GET, onTimerGetRoute);
  server.on("/timer/abort", HTTP_POST, onTimerAbortPostRoute);

  server.on("/schedule", HTTP_GET, onScheduleGetRoute);
  server.on("/schedule", HTTP_POST, onSchedulePostRoute);

  server.onNotFound(onRouteNotFound);

  server.begin();
}

void loop()
{
  static unsigned long last_run = 0;
  server.handleClient();

  if (millis() - last_run > SCHEDULE_INTERVAL) {
    checkValveSchedule();
    checkValveTimers();

    last_run = millis();
  }
}

// Routing handlers
void onRootRoute()
{
  server.send(200, "text/html", APP_HTML);
}

void checkValveSchedule() {

  DateTime now = rtc.now();
  for (int i = 0; i < sizeof(schedules) / sizeof(valveSchedule); ++i)
  {
    if (schedules[i].fromHour == 0 && schedules[i].fromMinute == 0 && schedules[i].toHour == 0 && schedules[i].toMinute == 0) {
      continue;
    }

    uint8_t valvePin = getValvePinId(schedules[i].valveId);
    int unixtime = now.unixtime();
    DateTime from = DateTime(now.year(), now.month(), now.day(), schedules[i].fromHour, schedules[i].fromMinute, 0).unixtime();
    int fromUnixtime = from.unixtime();
    DateTime to = DateTime(now.year(), now.month(), now.day(), schedules[i].toHour, schedules[i].toMinute, 0).unixtime();
    int toUnixtime = to.unixtime();
    
    pinMode(valvePin, OUTPUT);
    if (fromUnixtime <= unixtime && toUnixtime >= unixtime) {
      Serial.println("on");
      digitalWrite(valvePin, LOW);
    }else{
      Serial.println("off");
      digitalWrite(valvePin, HIGH);
    }
  }
}

void checkValveTimers() {
  DateTime now = rtc.now();
  int unixtime = now.unixtime();
  for (int i = 0; i < sizeof(timers) / sizeof(valveTimer); ++i)
  {
    if (timers[i].to < 0) {
      continue;
    }

    uint8_t valvePin = getValvePinId(timers[i].valveId);
    pinMode(valvePin, OUTPUT);

    if (timers[i].to > unixtime) {
      digitalWrite(valvePin, LOW);
    } else {
      timers[i].to = -1;
      digitalWrite(valvePin, HIGH);
    }
  }
}

uint8_t getValvePinId(int valveId) {
  for (int i = 0; i < sizeof(valves) / sizeof(valve); ++i)
  {
    if (valves[i].id == valveId) {
      return valves[i].pinId;
    }
  }
}

/*
  Valve settings
*/
void onValveStateRoute()
{
  int valveId = server.arg("valveId").toInt();
  uint8_t valvePin = getValvePinId(valveId);
  //pinMode(valvePin, INPUT);
  int val = digitalRead(valvePin);

  server.send(200, "text/plain", String(val));
}

void onValvesAllOffRoute()
{
  for (int i = 0; i < sizeof(valves) / sizeof(valve); ++i)
  {
    pinMode(valves[i].pinId, OUTPUT);
    digitalWrite(valves[i].pinId, LOW);
  }

  server.send(200, "text/plain", "on");
}

void onValveStateChangeOnRoute()
{
  int valveId = server.arg("valveId").toInt();
  uint8_t valvePin = getValvePinId(valveId);
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, LOW);

  server.send(200, "text/plain", "on");
}

void onValveStateChangeOffRoute()
{
  int valveId = server.arg("valveId").toInt();
  uint8_t valvePin = getValvePinId(valveId);
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, HIGH);

  server.send(200, "text/plain", "off");
}

/*
  System - Network settings
*/
void onSystemIpRoute()
{
  server.send(200, "text/plain", WiFi.localIP().toString());
}

/*
  System - Time settings
*/

void onSystemTimeSetRoute()
{
  uint16_t year = server.arg("year").toInt();
  uint8_t month = server.arg("month").toInt();
  uint8_t day = server.arg("day").toInt();
  uint8_t hour = server.arg("hour").toInt();
  uint8_t minute = server.arg("minute").toInt();
  uint8_t second = server.arg("second").toInt();

  rtc.adjust(DateTime(year, month, day, hour, minute, second));

  DateTime now = rtc.now();
  server.send(200, "text/plain", String(now.unixtime()));
}

void onSystemTimeGetRoute()
{
  DateTime now = rtc.now();
  server.send(200, "text/plain", String(now.unixtime()));
}

/*
  Timers
*/
void onTimerGetRoute()
{
  String output = "[";
  int arrLen = sizeof(timers) / sizeof(valveTimer);
  for (int i = 0; i < arrLen; ++i)
  {
    output = output + "{";
    output = output + "\"valveId\":" + String(timers[i].valveId) + ",";
    output = output + "\"to\":" + String(timers[i].to);
    output = output + "}";

    if (i < arrLen - 1) {
      output = output + ",";
    }
  }

  output = output + "]";

  server.send(200, "application/json", output);
}

void onTimerPostRoute()
{
  uint8_t valveId = server.arg("valveId").toInt();
  int duration = server.arg("duration").toInt();

  DateTime now = rtc.now();
  int unixtime = now.unixtime();
  int to = unixtime + duration;

  timers[valveId - 1].to = to;

  server.send(200, "text/plain", "ok");
}

void onTimerAbortPostRoute()
{
  uint8_t valveId = server.arg("valveId").toInt();

  timers[valveId - 1].to = -1;

  server.send(200, "text/plain", "ok");
}

/*
  Schedule
*/

void onScheduleGetRoute()
{
  String output = "[";
  int arrLen = sizeof(schedules) / sizeof(valveSchedule);
  for (int i = 0; i < arrLen; ++i)
  {
    output = output + "{";
    output = output + "\"valveId\":" + String(schedules[i].valveId) + ",";
    output = output + "\"fromHour\":" + String(schedules[i].fromHour) + ",";
    output = output + "\"fromMinute\":" + String(schedules[i].fromMinute) + ",";
    output = output + "\"toHour\":" + String(schedules[i].toHour) + ",";
    output = output + "\"toMinute\":" + String(schedules[i].toMinute);
    output = output + "}";

    if (i < arrLen - 1) {
      output = output + ",";
    }
  }

  output = output + "]";

  server.send(200, "application/json", output);
}

void onSchedulePostRoute()
{
  uint8_t valveId = server.arg("valveId").toInt();
  uint8_t fromHour = server.arg("fromHour").toInt();
  uint8_t fromMinute = server.arg("fromMinute").toInt();
  uint8_t toHour = server.arg("toHour").toInt();
  uint8_t toMinute = server.arg("toMinute").toInt();

  schedules[valveId - 1].fromHour = fromHour;
  schedules[valveId - 1].fromMinute = fromMinute;
  schedules[valveId - 1].toHour = toHour;
  schedules[valveId - 1].toMinute = toMinute;

  server.send(200, "text/plain", "ok");
}

void onRouteNotFound()
{
  server.send(404, "text/plain", "404 :(");
}
