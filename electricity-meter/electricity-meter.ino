
#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <iostream>
#include <PubSubClient.h>
#include "wlan.h"

#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// https://community.home-assistant.io/t/energy-measurement-with-mqtt-sensor/424558/19

const char* ssid = WLAN;
const char* password = WLAN_PW;

const char* mqtt_server = MQTT;

WiFiClient espClient;
PubSubClient client(espClient);
WiFiServer TelnetServer(23);
WiFiClient Telnet;

int rounds = 0;
bool alreadyCount = false;
long lastPingMillis = 0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(35, INPUT_PULLDOWN);

 // setup Wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    Serial.println("restart due to failing Wifi");
    ESP.restart();
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, 1883);

  ArduinoOTA.setHostname("electricity-meter");
  ArduinoOTA.setPassword("admin");
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  TelnetServer.begin();
  TelnetServer.setNoDelay(true); 
}

void handleTelnet() {
  if (TelnetServer.hasClient()) {
    if (!Telnet || !Telnet.connected()) {
      if (Telnet) Telnet.stop();
      Telnet = TelnetServer.available();
    } else {
      TelnetServer.available().stop();
    }
  }
}

void sendPing() {
  if (lastPingMillis + 20000 < millis()) {
    lastPingMillis = millis();
    char pingStr[20];
    dtostrf(lastPingMillis, 1, 2, pingStr);
    client.publish("home/energy/ping", pingStr);
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("electricity-meter")) {
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
int sum = 0;
int amount = 0;
long lastMillis = 0;

void loop() {
  ArduinoOTA.handle();
  
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  handleTelnet(); 

  sendPing();

  unsigned int v = (unsigned int)analogRead(35);
  
  sum += v;
  amount++;

  if (millis() > lastMillis + 500) {
    
    Serial.printf("%d\r\n", sum/amount);
    //Telnet.printf("%d\r\n", sum/amount);

    if (sum/amount > 24 && !alreadyCount) {
        Telnet.printf("avg: %d, millis: %d\r\n", sum/amount, (int)millis());
          rounds++;
          alreadyCount = true;
          Serial.printf("%u\r\n", v);
          Serial.printf("New round: %d\r\n", rounds);
          //Telnet.printf("New round: %d\r\n", rounds);
          
          // send by mqtt
          char tempString[8];
          dtostrf(rounds, 1, 2, tempString);
          client.publish("home/energy/rounds", tempString);
          client.publish("home/energy/round", "1");
      } else if (sum/amount <= 24) {
        alreadyCount = false;
      }
    
    lastMillis = millis();
    sum = 0;
    amount = 0;
    Serial.println("reset");
  }
  
  delay(1);
  return;
}
