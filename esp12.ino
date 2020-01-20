#include <stdlib.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Servo.h>
int servoPin = 2;
int ledPin = 0;
int minValue = 530; // 544
int maxValue = 2400; // 2400 

Servo servo;

const char* ssid        = "";
const char* password    = "";

const char* mqttServer   = "";
const int   mqttPort     = ;
const char* mqttUser     = "";
const char* mqttPassword = "";

WiFiClient espClient;
PubSubClient client(espClient);

long lastMsg = 0;
char msg[50];
char topic[50] = "popup-iot/12/1/11/2";
int value = 0;

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(ledPin, HIGH);
    delay(50);
    Serial.print(".");
    digitalWrite(ledPin, LOW);
    delay(50);
  }
  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  char buf[32];
  memset(buf, 0, 32);
  memcpy(buf, payload, length);
  int rad = atoi((char*)buf);
  servo.write(rad);
  Serial.print("rad: ");
  Serial.println(rad);
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP12-MYSTAMP-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  digitalWrite(ledPin, HIGH);
}

void setup() {
  pinMode (BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  pinMode (ledPin, OUTPUT);
  pinMode (servoPin, OUTPUT);

  Serial.begin(115200);
  setup_wifi();

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect("ESP12E", mqttUser, mqttPassword )) {
      Serial.println("connected");  
      client.subscribe(topic);
      digitalWrite(ledPin, HIGH);
    } else {
      Serial.print("failed with state ");
      Serial.print(client.state());
      digitalWrite(ledPin, LOW);
      delay(2000); 
    }
  }
  
  servo.attach(servoPin, minValue, maxValue);
  servo.write(0);
  delay(1000);
  servo.write(30);
  delay(1000);
  servo.write(60);
  delay(1000);
  servo.write(90);
  delay(1000);
  servo.write(120);
  delay(1000);
  servo.write(150);
  delay(1000);
  servo.write(180);
  delay(1000);
  servo.write(0);

//  digitalWrite(ledPin, LOW);
}

void loop() {
  if(WiFi.status() != WL_CONNECTED) {
    digitalWrite(ledPin, LOW);
    setup_wifi();
  }
  if (!client.connected()) {
    digitalWrite(ledPin, LOW);
    reconnect();
  }
  client.loop();
}