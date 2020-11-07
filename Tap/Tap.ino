#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <ESP8266FtpServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

#define SERIAL_ON 
#define TAP_OPEN_CONTROL 5
#define TAP_CLOSE_CONTROL 4

void  update_conf();
bool sendFile(String path);
String relay_status();
String relay_switch();
void onTimerISR();

//------------- NTP ---------------
#define GMTOFFSET_SEC 7200 //GMT+2
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", GMTOFFSET_SEC);
//------------ config --------------------
struct Config {
  int o_h;
  int o_m;
  int c_h;
  int c_m;
};
Config conf;
bool upload_flag = false;
bool auto_open = false;
//------------------------------------
const char* ssid = "SSID";
const char* password = "PSW";
const char* conf_file ="/conf.json";

uint32_t timer_count = 0;    
ESP8266WebServer HTTP(80);
FtpServer ftpSrv;

void setup() {
  pinMode(TAP_OPEN_CONTROL, OUTPUT);
  digitalWrite(TAP_OPEN_CONTROL, LOW);
  pinMode(TAP_CLOSE_CONTROL, OUTPUT);
  digitalWrite(TAP_OPEN_CONTROL, HIGH);
#ifdef SERIAL_ON
  Serial.begin(115200);
  Serial.println("");
#endif
//---- parse conf json ----
  if(!SPIFFS.begin()){
    Serial.println("SPIFFS Initialization...failed");
  }else{
    #ifdef SERIAL_ON
    Serial.println("Started SPIFFS");
    #endif
  }
  update_conf();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid,password);
  while (WiFi.status()!= WL_CONNECTED) {
     delay(500);
     #ifdef SERIAL_ON
     Serial.print("*");
     #endif
  }
  
  #ifdef SERIAL_ON
  Serial.println("");
  Serial.println("WiFi connection Successful");
  Serial.print("The IP Address of ESP8266 Module is: ");
  Serial.println(WiFi.localIP());
  #endif
// --- OTA ----
   ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  //--------------------------------------------------//
  //update time
  timeClient.begin();
  while(!timeClient.update())
  {
    Serial.print(".");
    timeClient.forceUpdate();
    delay(500);
  }
  //----------------------------------------------------//
  Serial.println("Started NTP client");
  ftpSrv.begin("relay","relay");
  Serial.println("Started FTP derver");
  //----------------------------------//
  HTTP.begin();
  HTTP.on("/", [](){
     if (false == sendFile("/index.html")){
        HTTP.send(404, "text/plain", "Page Not Found");
     }
  });
  
  HTTP.on("/relay_status", [](){
      HTTP.send(200, "text/plain", relay_status());
  });
  
  HTTP.on("/relay_switch", [](){
      HTTP.send(200, "text/plain", relay_switch());
  });

  HTTP.on("/restart",[](){
    HTTP.send(200,"text/plain", "...Restarting...");
    delay(1000);
    ESP.restart();
  });

  HTTP.on("/upload",[](){
    HTTP.send(200,"text/plain", "....Going into wireless upload mode....");
    upload_flag = true;
  });

  HTTP.on("/done",[](){
    HTTP.send(200,"text/plain", "....Exiting upload mode....");
    upload_flag = false;
  });

  HTTP.on("/time",[](){
    HTTP.send(200,"text/plain", timeClient.getFormattedTime());
  });

  HTTP.on("/conf",[](){
    char buffer[100];
    update_conf();
    sprintf(buffer, "....opens at %d:%d ....\n....close at %d:%d ....",
                                  conf.o_h,conf.o_m,conf.c_h,conf.c_m);
    HTTP.send(200,"text/plain", buffer);
  });
  
  HTTP.onNotFound([](){    
    if(false == sendFile(HTTP.uri()))
      HTTP.send(404, "text/plain", "Not Found");
  });
  Serial.println("Started HTTP derver");
  //--------------------------------------------------//
  //set timer isr
  timer1_attachInterrupt(onTimerISR);
  timer1_enable(TIM_DIV256, TIM_EDGE, TIM_LOOP);
  timer1_write(4687500); //15000000 us = 15s.
}

bool shouldOpen(){
  return ((conf.o_h <= timeClient.getHours() and timeClient.getHours() <= conf.c_h) and 
          (conf.o_m <= timeClient.getMinutes() and  timeClient.getMinutes() <= conf.c_m));
}
void onTimerISR(){
    if (++timer_count >= 4*60*8) {//every 8h
      timeClient.update();
      timer_count=0;
    }
    
    if(not auto_open and shouldOpen()){
        digitalWrite(TAP_OPEN_CONTROL, HIGH);
        digitalWrite(TAP_CLOSE_CONTROL, LOW);
        auto_open = true;
    }
    else{
      if(auto_open and not shouldOpen()){
        digitalWrite(TAP_OPEN_CONTROL, LOW);
        digitalWrite(TAP_CLOSE_CONTROL, HIGH);
        auto_open = false;
      }
   }  
}

void loop() {
  if(upload_flag==true){
      ArduinoOTA.handle();
      HTTP.handleClient();
  }
  HTTP.handleClient(); 
  ftpSrv.handleFTP();
}

String relay_status() {
  return String(digitalRead(TAP_OPEN_CONTROL));
}

String relay_switch() { 
  Serial.println("Relay_switch");

  bool new_status = !(digitalRead(TAP_CLOSE_CONTROL));
  digitalWrite(TAP_CLOSE_CONTROL, new_status);
  #ifdef SERIAL_ON
  Serial.println(String(digitalRead(new_status)));
  #endif
  
  new_status = !(digitalRead(TAP_OPEN_CONTROL));
  digitalWrite(TAP_OPEN_CONTROL, new_status);
  #ifdef SERIAL_ON
  Serial.println(String(digitalRead(new_status)));
  #endif

  return String(new_status);
}

bool sendFile(String path){ 
  if(false == SPIFFS.exists(path)){ 
    return false;
  }
  File file = SPIFFS.open(path, "r");            
  size_t sent = HTTP.streamFile(file, getContentType(path));
  file.close();                                   
  return true;                                   
}

String getContentType(String filename){
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
//  else if (filename.endsWith(".js")) return "application/javascript";
//  else if (filename.endsWith(".png")) return "image/png";
//  else if (filename.endsWith(".jpg")) return "image/jpeg";
//  else if (filename.endsWith(".gif")) return "image/gif";
//  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

void update_conf(){
  File f = SPIFFS.open(conf_file, "r");
  if (!f) {
    Serial.println("conf file failed to open");
  }else{
      Serial.println("Reading Data from File:");
      StaticJsonDocument<512> doc;
    // Deserialize the JSON document
    if (deserializeJson(doc, f)){
      #ifdef SERIAL_ON
      Serial.println(F("Failed to read file, using default configuration"));
      #endif
    }
    conf.o_h = doc["open"]["hour"]; 
    conf.o_m = doc["open"]["min"]; 
    conf.c_h = doc["close"]["hour"]; 
    conf.c_m = doc["close"]["min"]; 
    conf.c_m = doc["close"]["min"]; 

    #ifdef SERIAL_ON
    Serial.println(conf.o_h);
    Serial.println(conf.o_m);
    Serial.println(conf.c_h);
    Serial.println(conf.c_m);
    #endif
    f.close();
  }
}
