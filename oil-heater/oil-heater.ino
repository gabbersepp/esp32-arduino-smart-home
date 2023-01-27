#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ESPDateTime.h>
#include <ctime>
#include <iostream>
#include "wlan.h"
#include <Wire.h>
#include <hd44780.h>                       // main hd44780 header
#include <hd44780ioClass/hd44780_I2Cexp.h> // i2c expander i/o class header

void ghData();

#define DATAPOINT_SIZE 8640

// LCD geometry
const int LCD_COLS = 16;
const int LCD_ROWS = 2;
hd44780_I2Cexp lcd;

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
int lastDay = 0;

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

void setupLcd(void) {
  int status;

	// initialize LCD with number of columns and rows: 
	// hd44780 returns a status from begin() that can be used
	// to determine if initalization failed.
	// the actual status codes are defined in <hd44780.h>
	// See the values RV_XXXX
	//
	// looking at the return status from begin() is optional
	// it is being done here to provide feedback should there be an issue
	//
	// note:
	//	begin() will automatically turn on the backlight
	//
	status = lcd.begin(LCD_COLS, LCD_ROWS);

	if(status) // non zero status means it was unsuccesful
	{
		// hd44780 has a fatalError() routine that blinks an led if possible
		// begin() failed so blink error code using the onboard LED if possible
    Serial.println("error during lcd init");
		hd44780::fatalError(status); // does not return
	}

	// initalization was successful, the backlight should be on now
  Serial.println("lcd init success");
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

  if (MDNS.begin("oilheater")) {
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
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain", "");

  char buff[30];
  
  for (int j = 0; j < DATAPOINT_SIZE;) {
    String gd = "";
    
    for (int z = 0; z < 2; z++) {
      if (points[j].time == 0)
        break;
      ltoa(getTime(points[j].time), buff, 10);
      gd += buff;
      gd += '\t';
      if (points[j].vibration > 0) {
        gd += "1";
      } else {
        gd += "0";
      }
      gd += '\r';
      gd += '\n';
      
      j++;
    }

    server.sendContent(gd);
  }
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

        int currentDay = DateTime.getParts().getYearDay();
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
