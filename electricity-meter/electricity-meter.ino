
#include <WiFi.h>
#include <WiFiClient.h>
#include <iostream>
#include <PubSubClient.h>
#define WLAN_PW "";
#define WLAN "";

// https://community.home-assistant.io/t/energy-measurement-with-mqtt-sensor/424558/19

const char* ssid = WLAN;
const char* password = WLAN_PW;

const char* mqtt_server = "";

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

int rounds = 0;
int timeInHighState = 0;
int timeInLowState = 0;
int pastMs = 0;
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
  //client.setCallback(callback);
}


/*void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  // Feel free to add more if statements to control more GPIOs with MQTT

  // If a message is received on the topic esp32/output, you check if the message is either "on" or "off". 
  // Changes the output state according to the message
  if (String(topic) == "esp32/output") {
    Serial.print("Changing output to ");
    if(messageTemp == "on"){
      Serial.println("on");
      digitalWrite(ledPin, HIGH);
    }
    else if(messageTemp == "off"){
      Serial.println("off");
      digitalWrite(ledPin, LOW);
    }
  }
}*/

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      // Subscribe
      client.subscribe("esp32/output");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  pastMs++;
  unsigned int v = (unsigned int)analogRead(35);
  
  if (v > 58 && !alreadyCount) {
    timeInHighState++;
    if (timeInHighState < 700) {
      delay(1);
      return;
    }    

    // if pastMs is higher, there was much switching between states, maybe we had an invalid state so ignore this round
    //if (pastMs < 770) {
      rounds++;
      alreadyCount = true;
      Serial.printf("%u\r\n", v);
      Serial.printf("New round: %d\r\n", rounds);
      
      // send by mqtt
      char tempString[8];
      dtostrf(rounds, 1, 2, tempString);
      client.publish("home/energy/rounds", tempString);
      client.publish("home/energy/round", "1");

    //} else {
    //  Serial.println("too much time was consumed for state switching. Ignore round");
    //}

    timeInLowState = 0;
    timeInHighState = 0;
    pastMs = 0;
  } else if (v <= 58 && timeInLowState++ > 3000) {
    alreadyCount = false;
    timeInHighState = 0;
    timeInLowState = 0;
    pastMs = 0;
  }
  
  delay(1);
  return;
}
