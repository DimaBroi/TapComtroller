#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <ESP8266FtpServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ArduinoJson.h>

#define GMTOFFSET_SEC 7200 //GMT+2
const char* ssid = "2.4"; //Enter SSID
const char* password = "0542070459"; //Enter Password
const byte relay = 2; 
uint32_t timer_count = 0;
    
ESP8266WebServer HTTP(80);
FtpServer ftpSrv;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", GMTOFFSET_SEC);

const size_t capacity_read = 2*JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) +40;
StaticJsonDocument<capacity_read> json;

char status[2][6] = {"Open","Close"};

void setup() {
  unsigned char json_text[512]={0};
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("");
  while (WiFi.status() != WL_CONNECTED) {
     delay(500);
     Serial.print("*");
  }
  Serial.println("");
  Serial.println("WiFi connection Successful");
  Serial.print("The IP Address of ESP8266 Module is: ");
  Serial.println(WiFi.localIP());
  //----------------------------------//
  SPIFFS.begin();
  Serial.println("Started SPIFFS");
  timeClient.begin();
  timeClient.update();
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
  
  HTTP.onNotFound([](){    
    if(false == sendFile(HTTP.uri()))
      HTTP.send(404, "text/plain", "Not Found");
  });
  Serial.println("Started HTTP derver");
  //--------------------------------------------------//
  //update time
  Serial.println(timeClient.getFormattedTime());
  //--------------------------------------------------//
  //set timer isr
  timer1_attachInterrupt(onTimerISR);
  timer1_enable(TIM_DIV256, TIM_EDGE, TIM_LOOP);
  timer1_write(4687500); //15000000 us = 15s.

  // Use arduinojson.org/assistant to compute the capacity.
  File mfile = SPIFFS.open("/times.json","r");
  if (!mfile) {
    Serial.println("file open failed");
    return;
  }else{
    int rc = mfile.read(json_text, 512);
    mfile.close();
    Serial.print(rc);
    Serial.println(" bytes read from times.json");
  }
  // Parse JSON object
  DeserializationError error = deserializeJson(json, json_text);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }
  byte o_h = json["open"]["hour"];
  byte o_m = json["open"]["min"];
  byte c_h = json["close"]["hour"];
  byte c_m = json["close"]["min"];
  char s[35];
  sprintf( s, "Opens at %02d:%02d", o_h, o_m);
  Serial.println(s);
  sprintf( s, "Closes at %02d:%02d", c_h, c_m);
  Serial.println(s);
  byte status_val = json["status"];
  sprintf(s,"The tap now is %s", status[status_val]);
  Serial.println(s);
}

void onTimerISR(){
    if (++timer_count >= 4*60*6) {//every 6h
      //Serial.println("----pre-----");
      //Serial.println(timeClient.getFormattedTime());
      timeClient.update();
      timer_count=0;
      //Serial.println("----post-----");
      //Serial.println(timeClient.getFormattedTime());
    }
}

void loop() {
  HTTP.handleClient(); 
  ftpSrv.handleFTP();
}

String relay_status() {
  return String(digitalRead(relay));
}

String relay_switch() { 
  char state = 1-digitalRead(relay);
  File mfile = SPIFFS.open("/times_2.json","w");
  if (!mfile) {
    Serial.println("file open failed");
    return String(digitalRead(relay));
  }

  const size_t capacity_write = 2*JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3);
  StaticJsonDocument<capacity_write> doc;


  JsonObject open = doc.createNestedObject("open");
  open["hour"] = 7;
  open["min"] = 30;
  
  JsonObject close = doc.createNestedObject("close");
  close["hour"] = 19;
  close["min"] = 0;
  doc["status"] = 1;

  serializeJson(doc, Serial);
  
  // Serialize JSON to file
  if (serializeJson(json, mfile) == 0) {
    Serial.println(F("Failed to write to file"));
    json["status"] = digitalRead(relay);
    return String(digitalRead(relay));
  }
  mfile.close();
  digitalWrite(relay, state);
  return String(state);
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

String getContentType(String filename){                                 // Функция, возвращающая необходимый заголовок типа содержимого в зависимости от расширения файла
  if (filename.endsWith(".html")) return "text/html";                   // Если файл заканчивается на ".html", то возвращаем заголовок "text/html" и завершаем выполнение функции
  else if (filename.endsWith(".css")) return "text/css";                // Если файл заканчивается на ".css", то возвращаем заголовок "text/css" и завершаем выполнение функции
  else if (filename.endsWith(".js")) return "application/javascript";   // Если файл заканчивается на ".js", то возвращаем заголовок "application/javascript" и завершаем выполнение функции
  else if (filename.endsWith(".png")) return "image/png";               // Если файл заканчивается на ".png", то возвращаем заголовок "image/png" и завершаем выполнение функции
  else if (filename.endsWith(".jpg")) return "image/jpeg";              // Если файл заканчивается на ".jpg", то возвращаем заголовок "image/jpg" и завершаем выполнение функции
  else if (filename.endsWith(".gif")) return "image/gif";               // Если файл заканчивается на ".gif", то возвращаем заголовок "image/gif" и завершаем выполнение функции
  else if (filename.endsWith(".ico")) return "image/x-icon";            // Если файл заканчивается на ".ico", то возвращаем заголовок "image/x-icon" и завершаем выполнение функции
  return "text/plain";                                                  // Если ни один из типов файла не совпал, то считаем что содержимое файла текстовое, отдаем соответствующий заголовок и завершаем выполнение функции
}
