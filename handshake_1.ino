#include "secrets.h"

// WIFI
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
// JSON read
#include <ArduinoJson.h>


// RX TX setting
#include <HardwareSerial.h>
const int BAUD = 115200;
const int TXInterval = 200;
const int RXTimeout = 600;
const char* msgLine = "MSG:";
int lastMsgTime = 0;
bool msgActivate = false;

const int RXPin = 5;
const int TXPin = 6;

HardwareSerial Uart(2);

String RXLine;
int lastTX = 0;
int counter = 0;



// Common Debounce time
const uint32_t debounceMs = 220;


// ------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------




// WIFI Connection
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  static bool started = false;
  static unsigned long lastAttempt = 0;

  if (millis() - lastAttempt < 5000) return;
  lastAttempt = millis();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (!started) {
    Serial.println("Scanning WiFi...");
    int n = WiFi.scanNetworks();

    if (n <= 0) {
      Serial.println("No WiFi networks found");
      return;
    }

    for (int i = 0; i < n; i++) {
      String found = WiFi.SSID(i);

      for (int j = 0; j < WIFI_COUNT; j++) {
        if (found == WIFI_SSIDS[j]) {
          Serial.print("Connecting to ");
          Serial.println(WIFI_SSIDS[j]);

          WiFi.begin(WIFI_SSIDS[j], WIFI_PASSWORDS[j]);
          started = true;
          return;
        }
      }
    }
    Serial.println("No known WiFi detected");
  }
}

// RX TX 
// Action after receive the msg
void onMsgLine(String line){
  Serial.print("Receive, ");
  Serial.println(line);
  // Action
  rgbLedWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0);
  delay(1000);
  rgbLedWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS, 0);
  delay(1000);
  rgbLedWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);
  delay(1000);
  // digitalWrite(RGB_BUILTIN, LOW);
  // delay(1000);
}

// Action after lost the msg
void onMsgLost() {
  Serial.println("Connection lost");
  rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
  digitalWrite(RGB_BUILTIN, LOW);
  delay(1000);

}

void TXTask(){
  int now  = millis();
  if(now - lastTX < TXInterval){
    return;
  }
  lastTX = now;
  Uart.print(msgLine);
  Uart.print("REPLACE HERE WITH CLIENT ID");
  Uart.print('\n');
  // Serial.println("Sending MSG");
}

void RXTask(){
  while (Uart.available()>0){
    char c = (char)Uart.read();
    if(c == '\r'){
      continue;
    }

    if(c == '\n'){
      if(RXLine.startsWith(msgLine)){
        lastMsgTime = millis();
        if(!msgActivate){
          msgActivate = true;
        }        
        onMsgLine(RXLine);
      }
      RXLine = "";
      continue;
    }
  }
}

void msgLostTask(){
  if(!msgActivate){
    return;
  }
  int now = millis();
  if(now - lastMsgTime > RXTimeout){
    msgActivate = false;
    onMsgLost();
  }
}


void setup() {
  Serial.begin(115200);
  delay(200);

  Uart.begin(BAUD, SERIAL_8N1, RXPin, TXPin);


  Serial.println("SETUP READY");

  connectWiFi();
  digitalWrite(RGB_BUILTIN, HIGH);
  delay(200);


}

void loop() {
  // WIFI reconnect attempt
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 5000) {
      lastTry = millis();
      connectWiFi();
    }
  }

  // Looping RX TX task
  TXTask();
  RXTask();
  msgLostTask();

}


