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

typedef struct
{
  int id;
  uint8_t valveId;
} valve;

const valve valves[] {
  {1, D1},
  {2, D2},
  {3, D3},
  {4, D4}
};

void setup()
{
  for (uint8_t i = 0; i < sizeof(valves) / sizeof(valve); ++i)
  {
    pinMode(valves[i].valveId, OUTPUT);
    digitalWrite(valves[i].valveId, LOW);
  }

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
  }

  Serial.print("IP adress:\t");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("dripdrop"))
  {
    Serial.println("mDNS responder started");
  }
  else
  {
    Serial.println("Error setting up MDNS responder!");
  }

  server.on("/", HTTP_GET, onRootRoute);
  server.on("/system/ip", HTTP_GET, onSystemIpRoute);
  server.on("/system/time", HTTP_GET, onSystemTimeGetRoute);
  server.on("/system/time", HTTP_POST, onSystemTimeSetRoute);
  server.on("/valve/state/on", HTTP_POST, onValveStateChangeOnRoute);
  server.on("/valve/state/off", HTTP_POST, onValveStateChangeOffRoute);
  server.on("/valve/state", HTTP_GET, onValveStateRoute);
  server.onNotFound(onRouteNotFound);

  server.begin();
}

void loop()
{
  server.handleClient();
}

// Routing handlers
void onRootRoute()
{
  server.send(200, "text/html", APP_HTML);
}

void onValveStateRoute()
{
  String valveId = server.arg("valveId");
  uint8_t valvePin = valves[valveId.toInt()].valveId;
  pinMode(valvePin, INPUT);
  int val = digitalRead(valvePin);

  server.send(200, "text/plain", val + "0");
}

void onValveStateChangeOnRoute()
{
  String valveId = server.arg("valveId");
  uint8_t valvePin = valves[valveId.toInt()].valveId;
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, LOW);

  server.send(200, "text/plain", "on");
}

void onValveStateChangeOffRoute()
{
  String valveId = server.arg("valveId");
  uint8_t valvePin = valves[valveId.toInt()].valveId;
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, HIGH);

  server.send(200, "text/plain", "off");
}

void onSystemIpRoute()
{
  server.send(200, "text/plain", WiFi.localIP().toString());
}

void onSystemTimeSetRoute()
{
  uint8_t year = server.arg("year").toInt();
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

void onRouteNotFound()
{
  server.send(404, "text/plain", "404 :(");
}
