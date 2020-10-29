#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <ESP8266FtpServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>

//#define SERIAL_ON 
#define TAP_OPEN_CONTROL 5
//#define TAP_CLOSE_CONTROL 4
//------------- NTP ---------------
#define GMTOFFSET_SEC 7200 //GMT+2
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", GMTOFFSET_SEC);
//--------------------------------

const char* ssid = "2.4"; //Enter SSID
const char* password = "0542070459"; //Enter Password 


uint32_t timer_count = 0;    
ESP8266WebServer HTTP(80);
FtpServer ftpSrv;


void setup() {
pinMode(TAP_OPEN_CONTROL, OUTPUT);
digitalWrite(TAP_OPEN_CONTROL, LOW);

#ifdef SERIAL_ON
  Serial.begin(115200);
  Serial.println("");
#endif
  WiFi.begin(ssid, password);
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
  //----------------------------------//
  SPIFFS.begin();
  #ifdef SERIAL_ON
  Serial.println("Started SPIFFS");
  #endif
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
}

void onTimerISR(){
    if (++timer_count >= 4*60*8) {//every 8h
      timeClient.update();
      timer_count=0;
    }
}

void loop() {
  HTTP.handleClient(); 
  ftpSrv.handleFTP();
}

String relay_status() {
  return String(digitalRead(TAP_OPEN_CONTROL));
}

String relay_switch() { 
  Serial.println("Relay_switch");
  bool new_status = !(digitalRead(TAP_OPEN_CONTROL));
  digitalWrite(TAP_OPEN_CONTROL, new_status);
  Serial.println(String(digitalRead(new_status)));
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
