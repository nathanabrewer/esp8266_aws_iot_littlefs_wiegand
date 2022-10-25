
// ESP8266 Board community 3.0.2 -- Generis ESP8266 Board
// Be sure to add ESP8266LittleFS tool to your arduino env!
// upload data folder using LittleFS Tool

// ISR functions must be prefixed with IRAM_ATTR

#include <Arduino.h>
#include <ArduinoJson.h>        // version 6.19.4
#include <ESP8266WiFi.h>        // (arduino esp8266 3.0.2)
#include <ESPAsyncWebServer.h>  // version 1.2.3
#include <Updater.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>       //version 2.8
#include <time.h>
#include <LittleFS.h>           // (arduino esp8266 3.0.2)

#define U_PART U_FS

#define STATUS_REBOOTING 0
#define STATUS_NORMAL 1

#include "bs_wiegand.h"

BS_WIEGAND bsWiegand(0,14,12); //instance 0, D0 pin reference GPIO 14, D1 pin reference GPIO 12

WiFiClientSecure net;
PubSubClient client(net);
AsyncWebServer server(80);
size_t content_len;
time_t now;
time_t nowish = 1510592825;

int deviceStatus = STATUS_NORMAL;

StaticJsonDocument<512> config;

/////////////////////////////////////////////////////////////////////////////////////////////
// Start Button Example

unsigned long buttonPressTime;

struct Button {
  const uint8_t PIN;
  uint32_t numberKeyPresses;
  bool pressed;
};

Button button1 = {0, 0, false}; //initialize struct so button1.pin is 0, button1.numberKeyPresses = 0, and button1.pressed = false

IRAM_ATTR void isr() {
 static unsigned long last_interrupt_time = 0;
 unsigned long interrupt_time = millis();
 // If interrupts come faster than 200ms, assume it's a bounce and ignore
 if (interrupt_time - last_interrupt_time > 200) 
 {
  button1.numberKeyPresses++;
  button1.pressed = true;
 }
 last_interrupt_time = interrupt_time;
}

void setupButtonExample(){
  //initialize pins as INPUT with Internal Pullup Resistor
  pinMode(button1.PIN, INPUT_PULLUP);
  attachInterrupt(button1.PIN, isr, FALLING);
}

void setupButtonExampleLoopHandler(){
  buttonPressTime = millis();
  Serial.print("Button Pressed ");
  Serial.println(button1.numberKeyPresses);
  button1.pressed = false;
  StaticJsonDocument<200> doc;
  doc["time"] = millis();
  doc["data"] = button1.numberKeyPresses;
  doc["message"] = "Button Pressed";
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer); // print to client

  String pubTopic = config["PUB_TOPIC"];
  client.publish(pubTopic.c_str(), jsonBuffer);
  if(button1.numberKeyPresses > 10){
    Serial.print("We got 10 Button Presses... GOTO AP MODE");
    config["AP_MODE"] = true;
    saveConfig();
    deviceStatus = STATUS_REBOOTING; //request reboot!
    
  }
}

//this is called in the loop
void buttonHandler(){
  //out example button press does need an active IoT connection...
  if(button1.pressed && client.connected()){
    setupButtonExampleLoopHandler();
  }
  
  if(buttonPressTime > 0){
    if((millis() - buttonPressTime) > 5000){
      Serial.print("Clearing button Presses.");
      button1.numberKeyPresses = 0;  
      buttonPressTime = 0;
    }
  }  
}

// End Button1 Example
/////////////////////////////////////////////////////////////////////////////////////////////

void examplePublishMessage(){
      StaticJsonDocument<200> doc;
      doc["time"] = millis();
      char jsonBuffer[512];
      serializeJson(doc, jsonBuffer); // print to client
      String pubTopic = config["PUB_TOPIC"];
      client.publish(pubTopic.c_str(), jsonBuffer);
      Serial.print("Published to");
      Serial.println(pubTopic);   
}

void setup()
{
  Serial.begin(115200);
  Serial.println("");
  
  if(!LittleFS.begin()){
    Serial.println("LittleFS Failed.");
  }else{
    Serial.println("LittleFS Started.");
  }
  
  readConfig();
    
  String space_id = config["SPACE_ID"];
  String ssid = config["STA_SSID"];
  String psk = config["STA_PSK"];
  
  Serial.println("CONFIG----- ");

  Serial.print("SPACE_ID: ");
  Serial.println(space_id);  

  Serial.print("STA_SSID: ");
  Serial.println(ssid);  
  
  Serial.print("STA_PSK: ");
  Serial.println(psk);  


  Serial.print("WiFi.macAddress() : ");
  Serial.println(WiFi.macAddress());

  bsWiegand.setResultHandler(handleWeigandResults);
  bsWiegand.begin();
    
  setupButtonExample();

  setupWiFi();

}

String readData(String filename){
   String r;
   File file = LittleFS.open(filename, "r");
   if(!file){
    Serial.println("No Saved Data!");
    return r;
   }   
   
  while(file.available()){
    r += file.readString();
  }
  
  file.close();
  return r;
}

void writeContent(String filename, String data){
  File file = LittleFS.open(filename, "w");
  file.print(data);
  delay(1);
  file.close();
}

void readConfig(){
  String json = readData("/settings.json");
  Serial.println("READ SETTINGS....");
  Serial.println(json);
  DeserializationError error = deserializeJson(config, json.c_str());
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
}

void saveConfig(){
  String output;
  serializeJson(config, output);
  writeContent("/settings.json", output);
}

/////

void loop()
{
  if(deviceStatus == STATUS_REBOOTING){
      ESP.restart();
  }
  
  //bsWiegand loop will see if we have any events to handle 
  bsWiegand.loop();

  //example button code
  buttonHandler();
  
  if(config["AP_MODE"]){
    apModeLoop();
  }else{
    iotModeLoop();
  }
  
}

void apModeLoop(){
  if((millis() % 5000) == 0){
      Serial.printf("We are in AP MODE with %d Stations Connected\n", WiFi.softAPgetStationNum());
  }   
}

void iotModeLoop(){
  if (client.connected()){

    // if the modulus of the current time is evenly divisible by 5000  
    //              ---  this is a easy to make something run every X number of seconds (5 seconds in this case)
    if((millis() % 5000) == 0){
        examplePublishMessage();
    }   
    client.loop();  

  }else{
    setupMqtt();
  }
}

void handleWeigandResults(WiegandResult result){
  
    StaticJsonDocument<200> doc;
    
    doc["time"] = millis();
    doc["code"] = String(result.code);
    doc["hex"] = String(result.code, HEX);    
    doc["type"] = result.type;

    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer); // print to client 
    
    if(client.connected()){    
      String pubTopic = config["PUB_TOPIC"];
      client.publish(pubTopic.c_str(), jsonBuffer);
    }else{
      Serial.println("IoT Not Connected, Local Output only...");
      Serial.println(jsonBuffer);
      //Write to LittleFS Log File?
    }
           
}


void awsIotMessageCallback(char *topic, byte *payload, unsigned int length)
{
  
  StaticJsonDocument<512> json;
  
  Serial.print("Received on Topic: ");
  Serial.println(topic);
  Serial.print("Raw: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
      
  DeserializationError error = deserializeJson(json, payload);
  if (error) {
    Serial.println(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  //json["something"]  
  //do something ^^^
}


//////////////////////////
// Setup WiFi Etc...

void setupWiFi(){
  
  delay(3000);
  if(config['AP_MODE']){

    //AP_SSID
    String ssid = config['AP_SSID'];
    ssid += "_"+WiFi.macAddress();
    String psk = "proximity";
    
    Serial.println("Setting up AP Mode");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("PSK: ");
    Serial.println(psk);
      
    if(WiFi.softAP(ssid, psk)){
      Serial.println("Success.");
    }else{
      Serial.println("FAILED.");
      delay(10000);
    }
    
  }else{
    Serial.println("STA mode!");
    
    WiFi.mode(WIFI_STA);
    String ssid = config["STA_SSID"];
    String psk = config["STA_PSK"];
    WiFi.begin(ssid, psk);
    
    Serial.println(String("Attempting to connect to SSID: ") + ssid);
    while (WiFi.status() != WL_CONNECTED)
    {
      Serial.print(".");
      delay(1000);
    }
    Serial.println('\n');
    Serial.println("Connection established!");  
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP()); 


    if(!LittleFS.exists("/certificate.key")){
      Serial.println("certificate.key exists in LittleFS, YAY!!");
      setupMqtt();
    }else{
      Serial.println("certificate.key does not exist in LittleFS, we will not load MQTT");    
    }
  }
  
  setupHttpServerEndpoints();

}

void setupMqtt()
{
  setupNtp();

  BearSSL::X509List cert(readData("/certificate.ca").c_str());
  BearSSL::X509List client_crt(readData("/certificate.crt").c_str());
  BearSSL::PrivateKey key(readData("/certificate.key").c_str());
  
  String iotEndpoint = config["IOT_ENDPOINT"];
  uint16_t iotPort = config["IOT_PORT"];
  
  Serial.print("MQTT endpoint: ");
  Serial.println(iotEndpoint);

  Serial.print("MQTT iotPort: ");
  Serial.println(iotPort);
  
  
  net.setTrustAnchors(&cert);
  net.setClientRSACert(&client_crt, &key);
  client.setServer(iotEndpoint.c_str(), iotPort);
  client.setCallback(awsIotMessageCallback);
  
  Serial.println("Connecting to AWS IOT");
  String wifiMacString = WiFi.macAddress();
  String clientId = "PARASITE_"+wifiMacString;
  
  while (!client.connect( clientId.c_str() ))
  {
    Serial.print(".");
    delay(1000);
  }
 
  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }
  // Subscribe to a topic
  String subTopic = config["SUB_TOPIC"];
  client.subscribe(subTopic.c_str());

  Serial.println("AWS IoT Connected!");
}

void setupNtp(void)
{
  Serial.print("Setting time using NTP");
  configTime(3600, 0 * 3600, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < nowish)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("done!");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}
 
void setupHttpServerEndpoints(){

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/style.css", "text/css");
    });
    
    server.on("/index.js", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/style.css", "text/javascript");
    });    
    
    server.on("/wifi_settings", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/wifi_settings.html", "text/html");
    });
    
    server.on("/wifi_settings", HTTP_POST, [](AsyncWebServerRequest *request){      
        if (request->hasParam("ssid", true)) {
          
            String ssid = request->getParam("ssid", true)->value();
            String psk = request->getParam("psk", true)->value();
            String ap_mode = request->getParam("ap_mode", true)->value();
            String space_id = request->getParam("space_id", true)->value();
            
            //SAVE TO CONIFG
            if(ssid == ""){
              Serial.println("ssid is blank, not updating");
              request->send(200, "text/plain", "ssid is blank, not updating"); 
            }else{
              config["STA_SSID"] = ssid;
              config["STA_PSK"] = psk;
              config["SPACE_ID"] = space_id;
              config["AP_MODE"] = (ap_mode == "1");
              saveConfig();              
  
              deviceStatus = STATUS_REBOOTING;
              request->send(200, "text/plain", "OK. Saved");              
            }
            

        }else{
            request->send(200, "text/plain", "NOOP -- ssid not present on post body");
        }
       
    });

    //iotSettingsHtml
    server.on("/iot_settings", HTTP_GET, [](AsyncWebServerRequest *request){
       request->send(LittleFS, "/iot_settings.html", "text/html");
    });
    
    // ref: https://github.com/lbernstone/asyncUpdate/blob/master/AsyncUpdate.ino
    server.on("/iot_settings", HTTP_POST, [](AsyncWebServerRequest *request){
      
        if (request->hasParam("iot_endpoint", true)) {
          
            String iot_endpoint = request->getParam("iot_endpoint", true)->value();
            String iot_port = request->getParam("iot_port", true)->value();
            String iot_cert = request->getParam("iot_cert", true)->value();
            String iot_key = request->getParam("iot_key", true)->value();

            if(iot_endpoint == ""){
              request->send(200, "text/plain", "IoT Endpoint is blank, not updating");
              Serial.println("IoT Endpoint is blank, not updating");
            }else{
              writeContent("/certificate.key", iot_key);
              writeContent("/certificate.crt", iot_cert);

              //TODO: iot_port should be number uint16_t not string!
              //config["IOT_PORT"] = iot_port;
              config["IOT_ENDPOINT"] = iot_endpoint;
              saveConfig();                  
              
              deviceStatus = STATUS_REBOOTING;
              request->send(200, "text/plain", "OK. Saved");            
            }

        }else{
            request->send(200, "text/plain", "NOOP - Parameter iot_endpoint not in post body");
        }
       
    });


    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/update.html", "text/html");
    });


    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
              if (!index){
                Serial.println("Update");
                content_len = request->contentLength();
                // if filename includes spiffs, update the spiffs partition
                int cmd = (filename.indexOf("spiffs") > -1) ? U_PART : U_FLASH;
                Update.runAsync(true);
                if (!Update.begin(content_len, cmd)) {
                  Update.printError(Serial);
                }
              }
            
              if (Update.write(data, len) != len) {
                Update.printError(Serial);
              } else {
                Serial.printf("Progress: %d%%\n", (Update.progress()*100)/Update.size());
              }
            
              if (final) {
                AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Please wait while the device reboots");
                response->addHeader("Refresh", "20");  
                response->addHeader("Location", "/");
                request->send(response);
                if (!Update.end(true)){
                  Update.printError(Serial);
                } else {
                  Serial.println("Update complete");
                  Serial.flush();
                  
                  deviceStatus = STATUS_REBOOTING;
                  //ESP.restart();
                }
              }
    });
    
    server.begin(); 
}
