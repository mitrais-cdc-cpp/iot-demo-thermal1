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

//  --------- Config ---------- //
//AWS IOT config, change these:
char wifi_ssid[]       = "xx";
char wifi_password[]   = "xx";
char aws_endpoint[]    = "xx";
char aws_key[]         = "xx";
char aws_secret[]      = "xx";
char aws_region[]      = "xx";
const char* aws_topic  = "xx";
int port = 443;

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
int currentLightState=0;

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

//Detect light level change
void detectLightChange () {
  lastLightState = currentLightState;
  currentLightState= digitalRead(LIGHTSENSOR);
  if(lastLightState!=currentLightState)
  {
    Serial.println("changed");
    if(currentLightState==0)
    {
      Serial.println("LIGHT LEVEL IS HIGH");
    }
    else Serial.println("LIGHT LEVEL IS LOW");
  }
}

//function for turning on or off relay
void turnOnRelay1 () {
  digitalWrite(RELAY1,LOW);
}
void turnOnRelay2 () {
  digitalWrite(RELAY2,LOW);
}
void turnOffRelay1 () {
  digitalWrite(RELAY1,HIGH);
}
void turnOffRelay2 () {
  digitalWrite(RELAY2,HIGH);
}

//function for turning on or off led
void turnOnLed () {
  digitalWrite(LEDPIN,HIGH);
}
void turnOffLed () {
  digitalWrite(LEDPIN,LOW);
}

//callback to handle mqtt messages
void messageArrived(MQTT::MessageData& md)
{
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
      turnOnLed();
      delay(1000);
      turnOffLed();
    }

    if (relay1 == 1) {
      turnOnRelay1();
      delay(2000);
      turnOffRelay1();
    }

    if (relay2 == 1) {
      turnOnRelay2();
      delay(2000);
      turnOffRelay2();
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

   int rc = ipstack.connect(aws_endpoint, port);
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

//subscribe to a mqtt topic
void subscribe () {
   //subscrip to a topic
    int rc = client->subscribe(aws_topic, MQTT::QOS0, messageArrived);    
    if (rc != 0) {            
      Serial.print("rc from MQTT subscribe is ");
      Serial.println(rc);
      return;
    }
    if (DEBUG_PRINT) {
      Serial.println("MQTT subscribed");
    }
}

void setup() {
    Serial.begin (115200);
    WiFiMulti.addAP(wifi_ssid, wifi_password);

    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
        if (DEBUG_PRINT) {
          Serial.print (". ");
        }
    }
    if (DEBUG_PRINT) {
      Serial.println ("\nconnected to network " + String(wifi_ssid) + "\n");
    }

    //fill AWS parameters
    awsWSclient.setAWSRegion(aws_region);
    awsWSclient.setAWSDomain(aws_endpoint);
    awsWSclient.setAWSKeyID(aws_key);
    awsWSclient.setAWSSecretKey(aws_secret);
    awsWSclient.setUseSSL(true);

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
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  delay(10000);

  detectLightChange();

  String h = String(dht.readHumidity());    // Read temperature as Fahrenheit (isFahrenheit = true)
  String c = String(dht.readTemperature());

  if (isnan(dht.readHumidity()) || isnan(dht.readTemperature())) {
    Serial.println("Failed to read from DHT sensor!");
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

  String values = "{\"state\":{\"reported\":{\"temp\": " + c + ",\"humidity\": " + h + "}}}";
  // http://stackoverflow.com/questions/31614364/arduino-joining-string-and-char
  const char *publish_message = values.c_str();

  //keep the mqtt up and running
  if (awsWSclient.connected ()) {
      client->yield();

      subscribe ();

      //publish
      MQTT::Message message;
      char buf[1000];
      strcpy(buf, publish_message);
      message.qos = MQTT::QOS0;
      message.retained = false;
      message.dup = false;
      message.payload = (void*)buf;
      message.payloadlen = strlen(buf)+1;
      int rc = client->publish(aws_topic, message);
  } else {
    //handle reconnection
    connect ();
}
}
