#include <Wire.h>;
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include<ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
//#include "AppHtml.h";
#include <string>;
#include "RTClib.h";
#include <EEPROM.h>
#include <ArduinoJson.h>

#define DS1307_ADDRESS 0x68
#define EEPROM_SIZE 4096

const char *hotspotSsid = "DripDrop";
const char *hotspotPassword = "dripdrop";

const char *ssid = "";
const char *password = "";

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
RTC_Millis rtc;

const int VALVE_CHECK_INTERVAL = 5000;
const int SENSOR_CHECK_INTERVAL = 60000;

const int SCHEDULE_EEPROM_ADRESS = 100;

typedef struct
{
  int id;
  uint8_t pinId;
  int lastRunStart;
  int lastRunEnd;
  bool isManuallyStarted;
  
} valve;

valve valves[4] {
  {1, D4, 0,0, false},
  {2, D5, 0,0, false},
  {3, D6, 0,0, false},
  {4, D7, 0,0, false}
};

typedef struct
{
  int scheduleId;
  int valveId;
  uint8_t fromHour;
  uint8_t fromMinute;
  uint8_t toHour;
  uint8_t toMinute;
  byte days;
  
} valveSchedule;

valveSchedule schedules[32];

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
  Serial.begin(115200);
  Serial.println("Setting up");

  // Set up RTC
//  rtc.begin();

  // Set up valve pins
  for (int i = 0; i < sizeof(valves) / sizeof(valve); ++i)
  {
    pinMode(valves[i].pinId, OUTPUT);
    digitalWrite(valves[i].pinId, HIGH);
  }

  // Set up WiFi

  if(ssid){
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
      Serial.println("Couldn't connect to WiFi, restarting in 10 seconds");
      delay(10000);
      ESP.restart();
    }
  }else{
     WiFi.softAP(hotspotSsid, hotspotPassword);
  }  

  Serial.print("IP adress:\t");
  Serial.println(WiFi.localIP());

  // Set up mDNS
  if (MDNS.begin("dripdrop"))
  {
    Serial.println("mDNS responder started");
    MDNS.addService("http", "tcp", 80);
  }
  else
  {
    Serial.println("Error setting up MDNS responder!");
  }

  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(SCHEDULE_EEPROM_ADRESS, schedules);
  delay(1000);

  // Set up routes
  server.on("/", HTTP_GET, onRootRoute);

  server.on("/system/ip", HTTP_OPTIONS, onOptionRoute);
  server.on("/system/ip", HTTP_GET, onSystemIpRoute);
  server.on("/system/ping", HTTP_GET, onSystemPingRoute);

  server.on("/system/time", HTTP_OPTIONS, onOptionRoute);
  server.on("/system/time", HTTP_GET, onSystemTimeGetRoute);
  server.on("/system/time", HTTP_POST, onSystemTimeSetRoute);

  server.on("/valve/state/on", HTTP_OPTIONS, onOptionRoute);
  server.on("/valve/state/on", HTTP_POST, onValveStateChangeOnRoute);
  server.on("/valve/state/off", HTTP_OPTIONS, onOptionRoute);
  server.on("/valve/state/off", HTTP_POST, onValveStateChangeOffRoute);
  server.on("/valve/state", HTTP_OPTIONS, onOptionRoute);
  server.on("/valve/state", HTTP_GET, onValveStateRoute);
  server.on("/valves/off", HTTP_OPTIONS, onOptionRoute);
  server.on("/valves/off", HTTP_POST, onValvesAllOffRoute);
  server.on("/valves", HTTP_OPTIONS, onOptionRoute);
  server.on("/valves", HTTP_GET, onValveListRoute);

  server.on("/timer", HTTP_OPTIONS, onOptionRoute);
  server.on("/timer", HTTP_POST, onTimerPostRoute);
  server.on("/timer", HTTP_GET, onTimerGetRoute);
  server.on("/timer/abort", HTTP_OPTIONS, onOptionRoute);
  server.on("/timer/abort", HTTP_POST, onTimerAbortPostRoute);

  server.on("/schedule/list", HTTP_OPTIONS, onOptionRoute);
  server.on("/schedule/list", HTTP_GET, onScheduleGetRoute);
  server.on("/schedule/add", HTTP_OPTIONS, onOptionRoute);
  server.on("/schedule/add", HTTP_POST, onSchedulePostRoute);
  server.on("/schedule/update", HTTP_OPTIONS, onOptionRoute);
  server.on("/schedule/update", HTTP_POST, onScheduleUpdateRoute);
  server.on("/schedule/delete", HTTP_OPTIONS, onOptionRoute);
  server.on("/schedule/delete", HTTP_POST, onScheduleDeleteRoute);
  server.on("/schedule/deleteAll", HTTP_OPTIONS, onOptionRoute);
  server.on("/schedule/deleteAll", HTTP_POST, onScheduleDeleteAllRoute);

  server.onNotFound(onRouteNotFound);

  httpUpdater.setup(&server);
  server.enableCORS(true);
  server.begin();  
  
}

void loop()
{
  static unsigned long last_valve_run_check = 0;
  static unsigned long last_sensor_run_check = 0;

  
  server.handleClient();
 
  if (millis() - last_sensor_run_check > SENSOR_CHECK_INTERVAL)
  {
    // Not yet implemented
    last_sensor_run_check = millis();
  }

  if (millis() - last_valve_run_check > VALVE_CHECK_INTERVAL)
  {
    last_valve_run_check = millis();
    checkValveSchedule();
    checkValveTimers();    
  }
}

void setCors() {
    if (server.hasHeader("Access-Control-Allow-Headers") == false) {
    Serial.println("Dont have Access-Control-Allow-Headers");
    server.sendHeader("Access-Control-Allow-Headers", "*");
  } else {
    Serial.println("Has Access-Control-Allow-Headers");
  }
  if (server.hasHeader("Access-Control-Allow-Methods") == false) {
    Serial.println("Dont have Access-Control-Allow-Methods");
    server.sendHeader("Access-Control-Allow-Methods", "PUT,POST,GET,OPTIONS");
  } else {
    Serial.println("Has Access-Control-Allow-Methods");
  }
  if (server.hasHeader("Access-Control-Allow-Max-Age") == false) {
    Serial.println("Dont have Access-Control-Allow-Max-Age");
    server.sendHeader("Access-Control-Allow-Max-Age", "600");
  } else {
    Serial.println("Has Access-Control-Allow-Max-Age");
  }
};

// Routing handlers
void onRootRoute()
{
  server.send(200, "text/html", "Drip Drop!");
}

void onOptionRoute() {
  server.sendHeader("access-control-allow-credentials", "false");
  setCors();
  server.send(204);
}

void checkValveSchedule()
{
  DateTime now = rtc.now();
  for (int i = 0; i < sizeof(schedules) / sizeof(valveSchedule); ++i)
  {
     if (schedules[i].valveId < 0) {
      continue;
    }
    
    if (valves[schedules[i].valveId].isManuallyStarted == true) {
      Serial.println("Valve is manually started, skipping schedule");
      continue;
    }
   
    if (schedules[i].fromHour == 0 && schedules[i].fromMinute == 0 && schedules[i].toHour == 0 && schedules[i].toMinute == 0) {
      Serial.println("Schedule empty");
      continue;
    }
    
    if(bitRead(schedules[i].days, 7 - now.dayOfTheWeek())){
      Serial.println("Schedule not active today");
      continue;
    }

    uint8_t valvePin = getValvePinId(schedules[i].valveId);    
    DateTime from = DateTime(now.year(), now.month(), now.day(), schedules[i].fromHour, schedules[i].fromMinute, 0).unixtime();
    int fromUnixtime = from.unixtime();
    DateTime to = DateTime(now.year(), now.month(), now.day(), schedules[i].toHour, schedules[i].toMinute, 0).unixtime();
    int toUnixtime = to.unixtime();
    int unixtime = now.unixtime();
    
    pinMode(valvePin, OUTPUT);
    if (fromUnixtime <= unixtime && toUnixtime >= unixtime) {
      Serial.println("Schedule, on");
      valves[schedules[i].valveId - 1].lastRunStart = fromUnixtime;
      digitalWrite(valvePin, LOW);
    } else {
      Serial.println("Schedule, off");
      valves[schedules[i].valveId - 1].lastRunEnd = toUnixtime;
      digitalWrite(valvePin, HIGH);
    }
  }
}

void checkValveTimers()
{
  Serial.println("Check timers");
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
      valves[timers[i].valveId - 1].lastRunStart = unixtime;
      Serial.println("Timer, on");
      digitalWrite(valvePin, LOW);
    } else {
      timers[i].to = -1;
      valves[timers[i].valveId - 1].lastRunEnd = unixtime;
      Serial.println("Timer, off");
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
void onValveListRoute() {
  String output = "[";

  int arrLen = sizeof(valves) / sizeof(valve);
  for (int i = 0; i < arrLen; ++i)
  {
    output = output + "{";
    output = output + "\"id\":" + String(valves[i].id) + ",";
    output = output + "\"lastRunStart\":" + String(valves[i].lastRunStart) + ",";
    output = output + "\"lastRunEnd\":" + String(valves[i].lastRunEnd);
    output = output + "}";

    if (i < arrLen - 1) {
      output = output + ",";
    }
  }

  output = output + "]";

  server.send(200, "text/plain", output);
}

void onValveStateRoute()
{
  int valveId = server.arg("valveId").toInt();
  uint8_t valvePin = getValvePinId(valveId);
  pinMode(valvePin, OUTPUT);
  bool val = digitalRead(valvePin) == LOW;

  server.send(200, "application/json", "{\"message\": \""+ String(val)+"\"}");
}

void onValvesAllOffRoute()
{
  for (int i = 0; i < sizeof(valves) / sizeof(valve); ++i)
  {
    pinMode(valves[i].pinId, OUTPUT);
    digitalWrite(valves[i].pinId, LOW);
  }

  server.send(200, "application/json", "{\"message\": \"ok\"}");
}

void onValveStateChangeOnRoute()
{
  setCors();
  DateTime now = rtc.now();
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, server.arg("plain"));
  int valveId = doc["valveId"];
  uint8_t valvePin = getValvePinId(valveId);
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, LOW);
  valves[valveId].isManuallyStarted = true;
  valves[valveId - 1].lastRunStart = now.unixtime();
  
  server.send(200, "application/json", "{\"message\": \"ok\"}");
}

void onValveStateChangeOffRoute()
{
  setCors();
  DateTime now = rtc.now();
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, server.arg("plain"));
  int valveId = doc["valveId"];
  uint8_t valvePin = getValvePinId(valveId);
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, HIGH);
  valves[valveId].isManuallyStarted = false;  
  valves[valveId - 1].lastRunEnd = now.unixtime();
  server.send(200, "application/json", "{\"message\": \"ok\"}");
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
  setCors();

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, server.arg("plain"));
  uint16_t year = doc["year"];
  uint8_t month = doc["month"];
  uint8_t day = doc["day"];
  uint8_t hour = doc["hour"];
  uint8_t minute = doc["minute"];
  uint8_t second = doc["second"];

  rtc.adjust(DateTime(year, month, day, hour, minute, second));
  
  delay(100);

  DateTime now = rtc.now();

  server.send(200, "text/plain", String(now.unixtime()));
}

void onSystemTimeGetRoute()
{
  setCors();
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
  setCors();
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, server.arg("plain"));
  uint8_t valveId = doc["valveId"];
  int duration = doc["duration"];

  DateTime now = rtc.now();
  int unixtime = now.unixtime();
  int to = unixtime + duration;

  timers[valveId - 1].to = to;

 
  server.send(200, "application/json", "{\"message\": \"ok\"}");
}

void onTimerAbortPostRoute()
{
  setCors();
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, server.arg("plain"));
  uint8_t valveId = doc["valveId"];

  timers[valveId - 1].to = -1;

  server.send(200, "application/json", "{\"message\": \"ok\"}");
}

/*
  Schedule
*/

void onScheduleGetRoute()
{
  setCors();
  String output = "[";
  int arrLen = sizeof(schedules) / sizeof(valveSchedule);
  for (int i = 0; i < arrLen; ++i)
  {
    output = output + "{";
    output = output + "\"scheduleId\":" + String(i) + ",";
    output = output + "\"valveId\":" + String(schedules[i].valveId) + ",";
    output = output + "\"fromHour\":" + String(schedules[i].fromHour) + ",";
    output = output + "\"fromMinute\":" + String(schedules[i].fromMinute) + ",";
    output = output + "\"toHour\":" + String(schedules[i].toHour) + ",";
    output = output + "\"toMinute\":" + String(schedules[i].toMinute) + ",";
    output = output + "\"days\":[";
    output = output + String(bitRead(schedules[i].days,7) == 1 ? "true" : "false") + ",";
    output = output + String(bitRead(schedules[i].days,6) == 1 ? "true" : "false") + ",";
    output = output + String(bitRead(schedules[i].days,5) == 1 ? "true" : "false") + ",";
    output = output + String(bitRead(schedules[i].days,4) == 1 ? "true" : "false") + ",";
    output = output + String(bitRead(schedules[i].days,3) == 1 ? "true" : "false") + ",";
    output = output + String(bitRead(schedules[i].days,2) == 1 ? "true" : "false") + ",";
    output = output + String(bitRead(schedules[i].days,1) == 1 ? "true" : "false");
    output = output + "]}";

    if (i < arrLen - 1)
    {
      output = output + ",";
    }
  }

  output = output + "]";

  server.send(200, "application/json", output);
}

void onSchedulePostRoute()
{
  setCors();

  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, server.arg("plain"));

  

  uint8_t scheduleId = 0;
  uint8_t valveId = doc["valveId"];
  uint8_t fromHour = doc["fromHour"];
  uint8_t fromMinute = doc["fromMinute"];
  uint8_t toHour = doc["toHour"];
  uint8_t toMinute = doc["toMinute"];
  
  int arrLen = sizeof(schedules) / sizeof(valveSchedule);
  for (int i = 0; i < arrLen; ++i)
  {
    if (schedules[i].valveId == -1) {
      scheduleId = i;
      break;
    }
  }

  commitScheduleItem(scheduleId, valveId, fromHour, fromMinute,toHour,toMinute, doc["days"]);
   
  server.send(200, "application/json", "{\"message\": \"ok\"}");
}

void onScheduleUpdateRoute(){
  setCors();

  DynamicJsonDocument doc(512);
  Serial.println(server.arg("plain"));
  deserializeJson(doc, server.arg("plain"));

  uint8_t scheduleId = doc["scheduleId"];
  uint8_t valveId = doc["valveId"];
  uint8_t fromHour = doc["fromHour"];
  uint8_t fromMinute = doc["fromMinute"];
  uint8_t toHour = doc["toHour"];
  uint8_t toMinute = doc["toMinute"];

  commitScheduleItem(scheduleId, valveId, fromHour, fromMinute,toHour,toMinute, doc["days"]);
  
  server.send(200, "application/json", "{\"message\": \"ok\"}");
}

void commitScheduleItem(int scheduleId, int valveId, int fromHour, int fromMinute, int toHour, int toMinute, JsonArray days){
  schedules[scheduleId].valveId = valveId;
  schedules[scheduleId].fromHour = fromHour;
  schedules[scheduleId].fromMinute = fromMinute;
  schedules[scheduleId].toHour = toHour;
  schedules[scheduleId].toMinute = toMinute;

  bitWrite(schedules[scheduleId].days, 7, days[0]);
  bitWrite(schedules[scheduleId].days, 6, days[1]);
  bitWrite(schedules[scheduleId].days, 5, days[2]);
  bitWrite(schedules[scheduleId].days, 4, days[3]);
  bitWrite(schedules[scheduleId].days, 3, days[4]);
  bitWrite(schedules[scheduleId].days, 2, days[5]);
  bitWrite(schedules[scheduleId].days, 1, days[6]);
            
  Serial.println("Saving new schedule item");
  Serial.println(valveId);
  Serial.println(fromHour);
  Serial.println(fromMinute);
  Serial.println(toHour);
  Serial.println(toMinute);

  Serial.println("Days");
  for(int i = 0; i < 7; i++){
    Serial.println(String(i) + "  " + String(days[i]));
  }
Serial.println("/ Days");

  EEPROM.put(SCHEDULE_EEPROM_ADRESS, schedules);
  delay(200);
  EEPROM.commit();
  delay(200);
  
  Serial.println("Saved new schedule item");
}

void onScheduleDeleteRoute() {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, server.arg("plain"));

  setCors();
  uint8_t scheduleId = doc["scheduleId"];
  schedules[scheduleId].valveId = -1;
  schedules[scheduleId].fromHour = 0;
  schedules[scheduleId].fromMinute = 0;
  schedules[scheduleId].toHour = 0;
  schedules[scheduleId].toMinute = 0;

  EEPROM.put(SCHEDULE_EEPROM_ADRESS, schedules);
  delay(200);
  EEPROM.commit();
  delay(200);

  server.send(200, "application/json", "{\"message\": \"ok\"}");
}

void onScheduleDeleteAllRoute(){
 int arrLen = sizeof(schedules) / sizeof(valveSchedule);
  for (int i = 0; i < arrLen; ++i)
  {
      schedules[i].valveId = -1;
  }

  EEPROM.put(SCHEDULE_EEPROM_ADRESS, schedules);
  delay(200);
  EEPROM.commit();
  delay(200);
  
  server.send(200, "application/json", "{\"message\": \"ok\"}");
}

void onSystemPingRoute()
{
  server.send(200, "text/plain", "ok");
}

void onRouteNotFound()
{
  server.send(404, "text/plain", "404 :(");
}
