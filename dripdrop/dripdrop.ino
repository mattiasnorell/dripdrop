#include <Wire.h>;
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "RTClib.h"
#include "AppHtml.h";
#include <string>;

const char *ssid = "";
const char *password = "";

ESP8266WebServer server(80);
RTC_DS1307 rtc;

int SCHEDULE_INTERVAL = 10000;
int currentScheduleTime;

typedef struct
{
  int id;
  uint8_t pinId;
} valve;

valve valves[4] {
  {1, D1},
  {2, D2},
  {3, D3},
  {4, D4}
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
  uint64_t from;
  uint64_t to;
} valveTimer;

valveTimer timers[4] {
  {1, 0, 0},
  {2, 0, 0},
  {3, 0, 0},
  {4, 0, 0}
};


void setup()
{
  // Set up logging
  Serial.begin(9600);
  Serial.println("Setting up");

  // Set up RTC
  Wire.begin(D2, D3); // set I2C pins [SDA = D7, SCL = D8], default clock is 100kHz
  rtc.begin();

if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running!");
    // following line sets the RTC to the date & time this sketch was compiled
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    rtc.adjust(DateTime(2016, 11, 19, 19, 45, 0));   // <----------------------SET TIME AND DATE: YYYY,MM,DD,HH,MM,SS
  }
  
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
  /*
    server.on("/timer", HTTP_GET, onTimerGetRoute);
    server.on("/timer", HTTP_POST, onTimerPostRoute);

    server.on("/schedule", HTTP_GET, onScheduleGetRoute);
    server.on("/schedule", HTTP_POST, onSchedulePostRoute);
  */

  server.onNotFound(onRouteNotFound);

  server.begin();
}

void loop()
{
  server.handleClient();

  currentScheduleTime = currentScheduleTime + 1;

  if (currentScheduleTime > SCHEDULE_INTERVAL) {
    currentScheduleTime = 0;
  }
}

// Routing handlers
void onRootRoute()
{
  server.send(200, "text/html", APP_HTML);
}

/*void checkValveSchedule(uint8_t unixtime) {
  DateTime now = rtc.now();

  for (int i = 0; i < sizeof(schedules) / sizeof(valveSchedule); ++i)
  {

    DateTime from = DateTime(now.year(), now.month(), now.day(), schedules[i].fromHour, schedules[i].fromMinute, 0);
    DateTime to = DateTime(now.year(), now.month(), now.day(), schedules[i].toHour, schedules[i].toMinute, 0);

    pinMode(schedules[i].valveId, OUTPUT);

    if (from.unixtime() > unixtime && to.unixtime() < unixtime) {
      digitalWrite(timers[i].valveId, HIGH);
    } else {
      digitalWrite(timers[i].valveId, LOW);
    }
  }
  }

  void checkValveTimers(uint8_t unixtime) {
  for (int i = 0; i < sizeof(timers) / sizeof(valveTimer); ++i)
  {
    pinMode(timers[i].valveId, OUTPUT);
    if (timers[i].from > unixtime && timers[i].to < unixtime) {
      digitalWrite(timers[i].valveId, HIGH);
    } else {
      digitalWrite(timers[i].valveId, LOW);
    }
  }
  }*/

/*
  Valve settings
*/
void onValveStateRoute()
{
  int valveId = server.arg("valveId").toInt();
  uint8_t valvePin = valves[valveId - 1].pinId;
  //pinMode(valvePin, INPUT);
  int val = digitalRead(valvePin);

  server.send(200, "text/plain", String(val));
}

void onValveStateChangeOnRoute()
{
  Serial.print("Open valve");
  Serial.println(server.arg("valveId"));
  int valveId = server.arg("valveId").toInt();
  uint8_t valvePin = valves[valveId - 1].pinId;
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, LOW);

  server.send(200, "text/plain", "on");
}

void onValveStateChangeOffRoute()
{
  Serial.print("Close valve");
  Serial.println(server.arg("valveId"));
  int valveId = server.arg("valveId").toInt();
  uint8_t valvePin = valves[valveId - 1].pinId;
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
  byte year = server.arg("year").toInt();
  byte month = server.arg("month").toInt();
  byte day = server.arg("day").toInt();
  byte hour = server.arg("hour").toInt();
  byte minute = server.arg("minute").toInt();
  byte second = server.arg("second").toInt();

  rtc.adjust(DateTime(year, month, day, hour, minute, 0));

  delay(100);
  
  DateTime now = rtc.now();

  Serial.println(String(now.unixtime()));
  server.send(200, "text/plain", String(now.unixtime()));
}

void onSystemTimeGetRoute()
{
  DateTime now = rtc.now();
  server.send(200, "text/plain", String(now.unixtime()));
}

/*
  Timers

  void onTimerGetRoute()
  {
  server.send(501, "text/plain", "Not implemented :(");
  }

  void onTimerPostRoute()
  {
  uint8_t valveId = server.arg("valveId").toInt();
  uint8_t duration = server.arg("duration").toInt();
  DateTime now = rtc.now();
  uint8_t unixtime = now.unixtime();

  timers[valveId].from = unixtime;
  timers[valveId].to = unixtime + (duration * 100);
  }
*/
/*
  Schedule

  void onScheduleGetRoute()
  {
  server.send(501, "text/plain", "Not implemented :(");
  }

  void onSchedulePostRoute()
  {
  uint8_t valveId = server.arg("valveId").toInt();

  schedules[valveId].fromHour = server.arg("fromHour").toInt();
  schedules[valveId].fromMinute = server.arg("fromMinute").toInt();
  schedules[valveId].toHour = server.arg("toHour").toInt();
  schedules[valveId].toMinute = server.arg("toMinutemHour").toInt();
  }
*/

void onRouteNotFound()
{
  server.send(404, "text/plain", "404 :(");
}
