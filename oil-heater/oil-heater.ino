#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPDateTime.h>
#include <ctime>
#include <iostream>
#include "wlan.h"
#include <Wire.h>
#include <hd44780.h>                       // main hd44780 header
#include <hd44780ioClass/hd44780_I2Cexp.h> // i2c expander i/o class header
#include "SPI.h"
#include <PubSubClient.h>

// LCD geometry
const int LCD_COLS = 16;
const int LCD_ROWS = 2;
hd44780_I2Cexp lcd;
WiFiClient espClient;
PubSubClient client(espClient);

const char* ssid = WLAN;
const char* password = WLAN_PW;
const char* mqtt_server = MQTT;

const int vibrationPin = 26;
const int soundPin = 14;
const int lcdButtonPin = 27;

int lastDay = 0;
int overallRuntime = 0;
int dailyRuntime = 0;

int backlightCounter = 0;

int timer = 0;
long lastHeatingStartTime = 0;

bool lastVibState = false;
bool lastSoundState = false;

long lastPingMillis = 0;

void sendPing() {
  if (lastPingMillis + 20000 < millis()) {
    lastPingMillis = millis();
    char pingStr[20];
    dtostrf(lastPingMillis, 1, 2, pingStr);
    client.publish("home/oil/ping", pingStr);
  }
}

bool isVibStateChangeDetected() {
  // we read the states ten times. If there is only one occurence of a "true", this sensor is assumed to deliver true
  bool vib;

  for (int i = 0; i < 10; i++) {
    if (i == 0 || vib == lastVibState)
      vib = digitalRead(vibrationPin);

    if (vib != lastVibState) {
      break;
    }
  }

  bool result = vib != lastVibState;
  lastVibState = vib;

  return result;
}

bool isSoundStateChangeDetected() {
  // we read the states ten times. If there is only one occurence of a "true", this sensor is assumed to deliver true
  bool sound;

  for (int i = 0; i < 10; i++) {
    if (i == 0 || sound == lastSoundState)
      sound = digitalRead(soundPin);

    if (sound != lastSoundState) {
      break;
    }
  }

  bool result = sound != lastSoundState;
  lastSoundState = sound;

  return result;
}

void setupLcd(void) {
  int status;

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
  lcd.noBacklight();
}

void setupDate() {
  DateTime.setTimeZone("TZ_Europe_Berlin");
  DateTime.setServer("ptbtime1.ptb.de");
  DateTime.begin();

  if (!DateTime.isTimeValid()) {
    Serial.println("Failed to get time from server.");
  } else {
    Serial.println("timeserver found");
  }
}

void setupWifi() {
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
}

void setup(void) {
  Serial.begin(115200);

  pinMode(vibrationPin, INPUT);
  pinMode(lcdButtonPin, INPUT_PULLDOWN);
  pinMode(soundPin, INPUT);

  setupWifi();
  setupDate();
  setupLcd();

  client.setServer(mqtt_server, 1883);
}

void reconnectMqtt() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("oil-heater")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void lcdBacklightCheck() {
  backlightCounter++;
  
  if (backlightCounter == 5000) {
    lcd.noBacklight();
  }   
  
  bool lcdBacklight = digitalRead(27);
  
  if (lcdBacklight == 1) {
    lcd.backlight();
    backlightCounter = 0;
  }
}

void setLcd() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("D: %.0fm %.2fl", (float)dailyRuntime / 60, (dailyRuntime*1.9*1.197)/(60*60));
  lcd.setCursor(0, 1);
  lcd.printf("O: %.1fh %.1fl", (float)overallRuntime / (60*60), (overallRuntime*1.9*1.197)/(60*60));
}

int countVibStateChanges = 0;
int countSoundStateChanges = 0;

// main loop
void loop(void) {  
  if (!client.connected()) {
    reconnectMqtt();
  }

  client.loop();
  lcdBacklightCheck();
  sendPing();
  
  bool soundStateChange = isSoundStateChangeDetected();
  bool vibStateChange = isVibStateChangeDetected();
  timer++;

  if (soundStateChange) {
    countSoundStateChanges++;
  }

  if (vibStateChange) {
    countVibStateChanges++;
  }

  if (timer > 10000 && countSoundStateChanges >= 10 && countVibStateChanges >= 10 && lastHeatingStartTime == 0) {
    Serial.println("found possible beginning of period");
    lastHeatingStartTime = millis() - 20000; //subtract 10 seconds to set startpoint when state changes began
    countSoundStateChanges = 0;  
    countVibStateChanges = 0;
    timer = 0; // reset timer so we know if not enough changes happened anymore
  } else if (timer > 10000 && countSoundStateChanges >= 10 && countVibStateChanges >= 10 && lastHeatingStartTime != 0) {
    timer = 0;
    countSoundStateChanges = 0;  
    countVibStateChanges = 0;
    Serial.println("reset timer during running period");
  } else if (timer > 10000 && (countSoundStateChanges + countVibStateChanges) > 0 && (countSoundStateChanges + countVibStateChanges) < 20 && lastHeatingStartTime == 0)  {
    Serial.printf("found no open period. Changes sound: %d, Changes vib: %d\r\n", countSoundStateChanges, countVibStateChanges);
    
    timer = 0;
    countSoundStateChanges = 0;  
    countVibStateChanges = 0;
  } else if (lastHeatingStartTime > 0 && timer > 10000 && (countSoundStateChanges < 10 || countVibStateChanges < 10)) {
    // not enough state change since beginning of period may indicate idle period
    Serial.println("Found end of period");

    int runtime = (millis() - lastHeatingStartTime - timer * 2) / 1000; // subtract ms of timer

    if (runtime > 30) {
      // runtime less than 30 seconds would be unusual

      DateTimeParts parts = DateTime.getParts();
      int currentDay = parts.getYearDay();
      
      if (currentDay != lastDay) {
        lastDay = currentDay;
        dailyRuntime = 0;
      }

      dailyRuntime += runtime;
      overallRuntime += runtime;
      setLcd();

      char runtimeStr[20];
      char literStr[20];
      dtostrf(overallRuntime, 1, 2, runtimeStr);
      client.publish("home/oil/heating_duration_s", runtimeStr);
      dtostrf((overallRuntime*1.9*1.197)/(60*60), 1, 2, literStr);
      client.publish("home/oil/heating_liter", literStr);
      Serial.println(runtimeStr);
      Serial.println(literStr);
    } else {
      Serial.println("found unusual period");
    }

    timer = 0;
    countSoundStateChanges = 0;  
    countVibStateChanges = 0;
    lastHeatingStartTime = 0;    
  }

  delay(2);
}

