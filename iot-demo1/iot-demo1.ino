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
//Mitrais Config
#include <MitraisConfig.h>

// If stuff isn't working right, watch the console:
#define DEBUG_PRINT 1

#define DHTPIN D2
#define LEDPIN D1
#define LIGHTSENSOR D0
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

    delete msg;

    Serial.print("LED is :");
    Serial.println(led);
    Serial.println("relay1 is :");
    Serial.println(relay1);
    Serial.println("relay2 is :");
    Serial.println(relay2);

    //right now sets {"led":1} in lambda func to turn on the led
    //under a certain threshold for the temp set in aws iot rule condition
    if (led == 1) {
      switchLed(true);
      delay(1000);
      switchLed(false);
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

   int rc = ipstack.connect(mitrais::config::aws::AWS_ENDPOINT, mitrais::config::aws::PORT);
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
    } else {
      //handle reconnection
      connect ();
    } 
}

//subscribes to a mqtt topic
void updateTopicSubscription () {
  unsigned long currentMillis = millis();
  if (currentMillis - prevSubscriptionMillis >= mitrais::config::SUBSCRIBE_TOPIC_INTERVAL) {
    int rc = client->subscribe(mitrais::config::aws::AWS_SUBSCRIBED_TOPIC, MQTT::QOS0, messageArrived);    
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
  if (currentMillis - prevLightMillis >= mitrais::config::DETECT_LIGHT_INTERVAL) {
    lastLightState = currentLightState;
    currentLightState = digitalRead(LIGHTSENSOR);
    if(lastLightState!=currentLightState){
      if (DEBUG_PRINT) {
        Serial.println(currentLightState == HIGH ? "LIGHT LEVEL IS OFF" : "LIGHT LEVEL IS ON"); 
      }
      //publish to aws
      String values = "{\"deviceId\": \"" + String(mitrais::config::DEVICE_ID) + "\",\"state\":{\"lightState\": " + String(currentLightState) + "}}";
      publishMessage(mitrais::config::aws::AWS_LIGHT_TOPIC, values);
    }
  }
}

//periodically reads the current thermal data and sends to the web server
void updateReadThermalData() {
  unsigned long currentMillis = millis();
  if (currentMillis - prevThermalMillis >= mitrais::config::PUBLISH_THERMAL_INTERVAL) {
    prevThermalMillis = currentMillis;
    
    String h = String(dht.readHumidity());    // Read temperature as Fahrenheit (isFahrenheit = true)
    String c = String(dht.readTemperature());
  
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
  
    String values = "{\"deviceId\": \"" + String(mitrais::config::DEVICE_ID) + "\",\"state\":{\"temp\": " + c + ",\"humidity\": " + h + "}}";
    publishMessage(mitrais::config::aws::AWS_THERMAL_TOPIC, values);
  }
}

void setup() {
    Serial.begin (9600);
    WiFiMulti.addAP(mitrais::config::WIFI_SSID, mitrais::config::WIFI_PASSWORD);

    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
        if (DEBUG_PRINT) {
          Serial.print (". ");
        }
    }
    if (DEBUG_PRINT) {
      Serial.println ("\nconnected to network " + String(mitrais::config::WIFI_SSID) + "\n");
    }

    //fill AWS parameters
    awsWSclient.setAWSRegion(mitrais::config::aws::AWS_REGION);
    awsWSclient.setAWSDomain(mitrais::config::aws::AWS_ENDPOINT);
    awsWSclient.setAWSKeyID(mitrais::config::aws::AWS_KEY);
    awsWSclient.setAWSSecretKey(mitrais::config::aws::AWS_SECRET);
    awsWSclient.setUseSSL(true);
    //setup dht sensor
    dht.begin();
    //setup for ledpin
    pinMode(LEDPIN,OUTPUT);
    //setup for lightsensor
    pinMode(LIGHTSENSOR,INPUT);
    //setup for relay
    pinMode(RELAY1,OUTPUT);
    pinMode(RELAY2,OUTPUT);
}

void loop() {  
  updateReadThermalData();
  updateTopicSubscription();
  updateReadLightChange();
}
