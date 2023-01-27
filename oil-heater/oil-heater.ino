#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ESPDateTime.h>
#include <ctime>
#include <iostream>
#include "wlan.h"

void ghData();

#define DATAPOINT_SIZE 8640

const char* ssid = WLAN;
const char* password = WLAN_PW;
const int pin = 26; // 14 geht nicht
int timer = 0;
bool lastState = false;
long int lastTime = 0;
time_t lastEventStart = 0;
time_t lastEventTime = 0;
int lastEventCounter = 0;
bool vibStateAlreadyWritten = false;
bool noVibStateAlreadyWritten = false;

WebServer server(80);

class DataPoint {
  public:
    time_t time;
    bool vibration;
};

// the array will need about 8 (time_t = long int = 64 bit.) + 1 (bool) = 9 bytes
// 9 bytes  * 2160 = 18 kb of RAM required
DataPoint *points = new DataPoint[DATAPOINT_SIZE];
int pointsPointer = 0;

// hardly copy & pasted from Stackoverflow: Get int from time_t by calculating diff between 1970 and curtime
long int getTime(time_t curtime) {

    std::tm epoch_strt;
    epoch_strt.tm_sec = 0;
    epoch_strt.tm_min = 0;
    epoch_strt.tm_hour = 0;
    epoch_strt.tm_mday = 1;
    epoch_strt.tm_mon = 0;
    epoch_strt.tm_year = 70;
    epoch_strt.tm_isdst = -1;

    std::time_t basetime = std::mktime(&epoch_strt);
    long long nsecs = std::difftime(curtime, basetime);

    return nsecs;
}

void handleRoot() {
  server.send(200, "text/plain", "hello from esp32!");
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup(void) {
  for (int j = 0; j < DATAPOINT_SIZE; j++) {
    points[j].time = 0;
  }

  // setup sensor pin for INPUT
  pinMode(pin, INPUT);
  Serial.begin(115200);

  // setup Wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // setup Datetime
  DateTime.setTimeZone("TZ_Europe_Berlin");
  DateTime.setServer("ptbtime1.ptb.de");
  DateTime.begin();

  if (!DateTime.isTimeValid()) {
    Serial.println("Failed to get time from server.");
  } else {
    Serial.println("timeserver found");
  }

  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }

  // setup http server
  server.on("/", handleRoot);
  server.on("/data", []() {
    ghData();   
  });
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("HTTP server started");
}

// make json out of data points array
void ghData() {
  char buff[30];
  String gd = "{\"data\":[";
  
  for (int j = 0; j < DATAPOINT_SIZE; j++) {
    if (points[j].time == 0)
      break;
    gd += "{\"t\":";
    ltoa(getTime(points[j].time), buff, 10);
    gd += buff;
    gd += ",\"v\":";
    if (points[j].vibration > 0) {
      gd += "1";
    } else {
      gd += "0";
    }
    gd += "},";
  }
  gd += "]}";
  server.send(200, "application/json", gd);
}

int stateChanges = 0;
long int lastEventStartTime = 0;

// main loop
void loop(void) {
  
  bool val = digitalRead(pin);
  timer++;
  
  if (val != lastState) {
    stateChanges++;
    lastState = val;
  }
  
  if (timer >= 1500) {
    timer = 0;
    // do this all 3 seconds
    if (stateChanges > 5) {
      stateChanges = 0;
      // found vibration
      if (!vibStateAlreadyWritten) {
        // write noVibEnd + vibStart
        vibStateAlreadyWritten = true;
        noVibStateAlreadyWritten = false;

        long int seconds = getTime(DateTime.now());
        points[pointsPointer].time = seconds - 1;
        points[pointsPointer++].vibration = false;

        points[pointsPointer].time = seconds;
        points[pointsPointer++].vibration = true;

        Serial.println("vib begin");
        Serial.println(seconds);
      }
    } else {
      // found no-vib
      if (!noVibStateAlreadyWritten) {
        // write vibEnd + noVibStart

        vibStateAlreadyWritten = false;
        noVibStateAlreadyWritten = true;

        long int seconds = getTime(DateTime.now());
        points[pointsPointer].time = seconds - 1;
        points[pointsPointer++].vibration = true;

        points[pointsPointer].time = seconds;
        points[pointsPointer++].vibration = false;

        Serial.println("no vib begin");
        Serial.println(seconds);
      }
    }
  }

  if (pointsPointer >= DATAPOINT_SIZE) {
    pointsPointer = 0;
  }

  server.handleClient();
  delay(2);//allow the cpu to switch to other tasks
}
