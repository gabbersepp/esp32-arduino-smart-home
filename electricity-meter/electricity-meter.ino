
#include <WiFi.h>
#include <WiFiClient.h>
#include <iostream>
#include <PubSubClient.h>
#include "wlan.h"

// https://community.home-assistant.io/t/energy-measurement-with-mqtt-sensor/424558/19

const char* ssid = WLAN;
const char* password = WLAN_PW;

const char* mqtt_server = MQTT;

WiFiClient espClient;
PubSubClient client(espClient);

int rounds = 0;
bool alreadyCount = false;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(35, INPUT_PULLDOWN);

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

  client.setServer(mqtt_server, 1883);
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
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned int v = (unsigned int)analogRead(35);
  
  sum += v;
  amount++;

  if (millis() > lastMillis + 1000) {
    
    Serial.printf("%d\r\n", sum/amount);

    if (sum/amount > 60 && !alreadyCount) {
          rounds++;
          alreadyCount = true;
          Serial.printf("%u\r\n", v);
          Serial.printf("New round: %d\r\n", rounds);
          
          // send by mqtt
          char tempString[8];
          dtostrf(rounds, 1, 2, tempString);
          client.publish("home/energy/rounds", tempString);
          client.publish("home/energy/round", "1");
      } else if (sum/amount <= 60) {
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
