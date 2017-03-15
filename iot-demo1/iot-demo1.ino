#include <Arduino.h>
#include <Stream.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
//DHT
#include <DHT.h>
//AWS
#include "sha256.h"
#include "Utils.h"
#include "AWSClient2.h"
//WEBSockets
//#include <Hash.h>
#include <WebSocketsClient.h>
//MQTT PAHO
#include <SPI.h>
#include <IPStack.h>
#include <Countdown.h>
#include <MQTTClient.h>
//AWS MQTT Websocket
#include "Client.h"
#include "AWSWebSocketClient.h"
#include "CircularByteBuffer.h"
//ArduinoJson
#include <ArduinoJson.h>

// If stuff isn't working right, watch the console:
#define DEBUG_PRINT 1

#define DHTPIN D2
#define LEDPIN D7
#define STATUSLEDPIN D0
#define LIGHTSENSOR D1
#define RELAY1 D5
#define RELAY2 D6
#define DHTTYPE DHT22 //Set for 11, 21, or 22

//variable for lightsensor
int lastLightState;
int currentLightState=LOW;

//MQTT config
const int maxMQTTpackageSize = 512;
const int maxMQTTMessageHandlers = 1;
// ---------- /Config ----------//
char* AWS_ENDPOINT = "**";
const char* AWS_KEY = "**";
const char* AWS_SECRET = "**";
const char* AWS_REGION = "**";
const char* AWS_THERMAL_TOPIC = "**";
const char* AWS_LIGHT_TOPIC = "**";
const char* AWS_SUBSCRIBED_TOPIC = "**";                             
const int PORT = 443;
const char* DEVICE_ID = "**";
const char* WIFI_SSID = "**";
const char* WIFI_PASSWORD = "**";
unsigned long PUBLISH_THERMAL_INTERVAL = 20000; //equals to 10 secs
unsigned long DETECT_LIGHT_INTERVAL = 5000; //equals to 5 secs
unsigned long SUBSCRIBE_TOPIC_INTERVAL = 5000;

DHT dht(DHTPIN, DHTTYPE, 26);
ESP8266WiFiMulti WiFiMulti;

AWSWebSocketClient awsWSclient(1000);

IPStack ipstack(awsWSclient);
MQTT::Client<IPStack, Countdown, maxMQTTpackageSize, maxMQTTMessageHandlers> *client = NULL;

//# of connections
long connection = 0;

//generate random mqtt clientID
char* generateClientID () {
  char* cID = new char[23]();
  for (int i=0; i<22; i+=1)
    cID[i]=(char)random(1, 256);
  return cID;
}

//count messages arrived
int arrivedcount = 0;

unsigned long prevThermalMillis = 0;
unsigned long prevLightMillis = 0;
unsigned long prevSubscriptionMillis = 0;

//temp variable for the prev temperature after the threshold is exceeded
float prevTemp = 0.f;

//function for turning on or off relay
void switchRelay(int relayPin, bool isOn) {
  digitalWrite(relayPin, isOn ? HIGH : LOW);
}

//function for turning on or off led
void switchLed(bool isOn) {
  digitalWrite(LEDPIN, isOn ? HIGH : LOW);
}

//callback to handle mqtt messages
void messageArrived(MQTT::MessageData& md) {
  MQTT::Message &message = md.message;

  if (DEBUG_PRINT) {
    Serial.print("Message ");
    Serial.print(++arrivedcount);
    Serial.print(" arrived: qos ");
    Serial.print(message.qos);
    Serial.print(", retained ");
    Serial.print(message.retained);
    Serial.print(", dup ");
    Serial.print(message.dup);
    Serial.print(", packetid ");
    Serial.println(message.id);
    Serial.print("Payload ");
    char* msg = new char[message.payloadlen+1]();
    memcpy (msg,message.payload,message.payloadlen);
    Serial.println(msg);

    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(msg);

    int led = root["led"];    
    int relay1 = root["relay1"];
    int relay2 = root["relay2"];
    float temperature = root["temp"];

    delete msg;

    Serial.print("relay1 is : ");
    Serial.print(relay1);
    Serial.print(" relay2 is : ");
    Serial.print(relay2);

    //right now sets {"led":1,"temp":.f} in lambda func to turn on the led
    //under a certain threshold for the temp set in aws iot rule condition
    if (led == 1 && temperature > 0.f) {
      if (DEBUG_PRINT) {
        Serial.print(" LED is : ");
        Serial.print(led);
        Serial.print(" Temp is : ");
        Serial.println(temperature);
      }
      prevTemp = temperature;
      switchLed(true);
    }

    if (relay1 == 1) {
      switchRelay(RELAY1, true);
      delay(2000);
      switchRelay(RELAY1, false);
    }

    if (relay2 == 1) {
      switchRelay(RELAY2, true);
      delay(2000);
      switchRelay(RELAY2, false);
    }
  }
}

//connects to websocket layer and mqtt layer
bool connect () {
    if (client == NULL) {
      digitalWrite(STATUSLEDPIN,HIGH);
      client = new MQTT::Client<IPStack, Countdown, maxMQTTpackageSize, maxMQTTMessageHandlers>(ipstack);
    } else {

      if (client->isConnected ()) {
        client->disconnect ();
      }
      delete client;
      client = new MQTT::Client<IPStack, Countdown, maxMQTTpackageSize, maxMQTTMessageHandlers>(ipstack);
    }

    //delay is not necessary... it just help us to get a "trustful" heap space value
    delay (1000);
    if (DEBUG_PRINT) {
      Serial.print (millis ());
      Serial.print (" - conn: ");
      Serial.print (++connection);
      Serial.print (" - (");
      Serial.print (ESP.getFreeHeap ());
      Serial.println (")");
    }

   int rc = ipstack.connect(AWS_ENDPOINT, PORT);
    if (rc != 1)
    {
      if (DEBUG_PRINT) {
        Serial.println("error connection to the websocket server");
      }
      return false;
    } else {
      if (DEBUG_PRINT) {
        Serial.println("websocket layer connected");
      }
    }

    if (DEBUG_PRINT) {
      Serial.println("MQTT connecting");
    }

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    char* clientID = generateClientID ();
    data.clientID.cstring = clientID;
    rc = client->connect(data);
    delete[] clientID;
    if (rc != 0)
    {
      if (DEBUG_PRINT) {
        Serial.print("error connection to MQTT server");
        Serial.println(rc);
        return false;
      }
    }
    if (DEBUG_PRINT) {
      Serial.println("MQTT connected");
    }
    digitalWrite(STATUSLEDPIN,LOW);
    return true;
}

void publishMessage(const char* topic, String& values) {
  const char *publish_message = values.c_str();
   //keep the mqtt up and running
   if (awsWSclient.connected ()) {
        client->yield();
        //publish
        MQTT::Message message;
        char buf[1000];
        strcpy(buf, publish_message);
        message.qos = MQTT::QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void*)buf;
        message.payloadlen = strlen(buf)+1;
        int rc = client->publish(topic, message);
        digitalWrite(STATUSLEDPIN,HIGH);
        delay (500);
        digitalWrite(STATUSLEDPIN,LOW);
    } else {
      digitalWrite(STATUSLEDPIN,HIGH);
      //handle reconnection
      connect ();
    } 
}

//subscribes to a mqtt topic
void updateTopicSubscription () {
  unsigned long currentMillis = millis();
  if (currentMillis - prevSubscriptionMillis >= SUBSCRIBE_TOPIC_INTERVAL) {
    int rc = client->subscribe(AWS_SUBSCRIBED_TOPIC, MQTT::QOS0, messageArrived);    
    if (rc != 0) {       
      if (DEBUG_PRINT){
        Serial.print("rc from MQTT subscribe is ");
        Serial.println(rc);      
      }      
      return;
    }    
  }  
}

//periodically reads the light level change and publishes to the topic upon changed
void updateReadLightChange () {
  unsigned long currentMillis = millis();
  if (currentMillis - prevLightMillis >= DETECT_LIGHT_INTERVAL) {
    lastLightState = currentLightState;
    currentLightState = digitalRead(LIGHTSENSOR);
    if(lastLightState!=currentLightState){
      if (DEBUG_PRINT) {
        Serial.println(currentLightState == HIGH ? "LIGHT LEVEL IS OFF" : "LIGHT LEVEL IS ON"); 
      }
      //publish to aws
      String values = "{\"deviceId\": \"" + String(DEVICE_ID) + "\",\"state\":{\"lightState\": " + String(currentLightState) + "}}";
      publishMessage(AWS_LIGHT_TOPIC, values);
    }
  }
}

//periodically reads the current thermal data and sends to the web server
void updateReadThermalData() {
  unsigned long currentMillis = millis();
  if (currentMillis - prevThermalMillis >= PUBLISH_THERMAL_INTERVAL) {
    prevThermalMillis = currentMillis;

    float curTemp = dht.readTemperature();
    String h = String(dht.readHumidity());    // Read temperature as Fahrenheit (isFahrenheit = true)
    String c = String(curTemp);

    //turn off the led once the temp is back to normal again
    if (curTemp < prevTemp) {
      prevTemp = 0.f;
      switchLed(false);
    }
  
    if (isnan(dht.readHumidity()) || isnan(dht.readTemperature())) {
      if (DEBUG_PRINT) {
        Serial.println("Failed to read from DHT sensor!");
      }      
      return;
    } else {
      if (DEBUG_PRINT) {
        Serial.print("Humidity: ");
        Serial.print(h);
        Serial.print(" %\t");
        Serial.print("Temperature: ");
        Serial.print(c);
        Serial.print(" *C\t\n");
      }
    };
  
    String values = "{\"deviceId\": \"" + String(DEVICE_ID) + "\",\"state\":{\"temp\": " + c + ",\"humidity\": " + h + "}}";
    publishMessage(AWS_THERMAL_TOPIC, values);
  }
}

void setup() {
    Serial.begin (9600);
    Serial.print ("\nConnecting to network " + String(WIFI_SSID) + "\n");
    WiFiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
        if (DEBUG_PRINT) {
          Serial.print (".");
        }
    }
    Serial.println(".");
    if (DEBUG_PRINT) {
      Serial.println ("\nconnected to network " + String(WIFI_SSID) + "\n");
    }

    //fill AWS parameters
    awsWSclient.setAWSRegion(AWS_REGION);
    awsWSclient.setAWSDomain(AWS_ENDPOINT);
    awsWSclient.setAWSKeyID(AWS_KEY);
    awsWSclient.setAWSSecretKey(AWS_SECRET);
    awsWSclient.setUseSSL(true);    
    pinMode(STATUSLEDPIN,OUTPUT);
    digitalWrite(STATUSLEDPIN,HIGH);
    connect ();
    //setup dht sensor
    dht.begin();
    //setup for ledpin
    pinMode(LEDPIN,OUTPUT);
    //setup for lightsensor
    pinMode(LIGHTSENSOR,INPUT);
    //setup for relay
    pinMode(RELAY1,OUTPUT);
    pinMode(RELAY2,OUTPUT);
    digitalWrite(RELAY1,HIGH);
    digitalWrite(RELAY2,HIGH);

    //check light lavel state     
    currentLightState = digitalRead(LIGHTSENSOR);
    Serial.println(currentLightState == HIGH ? "LIGHT LEVEL IS OFF" : "LIGHT LEVEL IS ON"); 
}

void loop() {  
  updateReadThermalData();
  updateTopicSubscription();
  updateReadLightChange();
}
