#include <Arduino.h>
#include "TFT_eSPI.h" /* Please use the TFT library provided in the library. */
#include "img_logo.h"
#include "pin_config.h"
#include <RunningAverage.h>
// #include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"

#include <ArduinoJson.h>

//needed for library
#include <WiFiManager.h> 

#define FONT_LARGE &Dialog_plain_100 // Key label font 2

TFT_eSPI tft = TFT_eSPI();
WiFiManager wifiManager;

void setup()
{

  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  pinMode(PIN_POTENTIOMETER, INPUT);


  Serial.begin(115200);
  Serial.println("Hello T-Display-S3");

  // Setup screen
  tft.begin();

  tft.setRotation(3);
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, 320, 170, (uint16_t *)img_logo);

  delay(2000);

  ledcSetup(0, 2000, 8);
  ledcAttachPin(PIN_LCD_BL, 0);
  ledcWrite(0, 255);


    // tft_percent.setTextColor(TFT_RED);
    // tft_percent.drawString("ERR-1", 0, 0);    

  // tft.setTextColor(TFT_RED);
  // tft.drawString("ERR-1", 0, 0)
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);


  // Setup WIFI
  wifiManager.startConfigPortal("Nitrox Blender");
  wifiManager.autoConnect("Nitrox Blender");

  // WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD);
  // Serial.println("Connecting");
  // while(WiFi.status() != WL_CONNECTED) {
  //   delay(500);
  //   Serial.print(".");
  // }

  tft.drawString("Connected...", 0, 0, 3);


}


long lastScreenUpdate = 0;
long lastRead = 0;

char* endpoint = "http://192.168.1.6:5000/telemetry";
unsigned long timerDelay = 5000;
unsigned long lastTime = 0;

HTTPClient http;
JsonDocument doc;

void sendJsonData();

void loop()
{

  if ((millis() - lastTime) > timerDelay) {
    //Check WiFi connection status
    if(WiFi.status()== WL_CONNECTED)  {
      Serial.println("WiFi Connected");
      sendJsonData();
    }
    else {
      Serial.println("WiFi Disconnected");
    }
    lastTime = millis();
  }
}

void sendJsonData() {
  
    // Your Domain name with URL path or IP address with path
    http.begin(endpoint);
    
    http.addHeader("Content-Type", "application/json");

    doc["sensor"] = "compressorBokn";
    doc["time"] = millis();

    doc["action"] = "start";

    JsonObject sensorData = doc["sensors"].to<JsonObject>(); 
    sensorData["temp-1"] = 20;
    sensorData["temp-2"] = 40;
    sensorData["pressure"] = 250;
    sensorData["o2"] = 32;

    // Add an array
    JsonArray data = doc["data"].to<JsonArray>();
    data.add(48.756080);
    data.add(2.302038);

    
    // Generate the minified JSON and send it to the Serial port
    String payload;
    serializeJson(doc, payload);
    // The above line prints:
    // {"sensor":"gps","time":1351824120,"data":[48.756080,2.302038]}

    // Start a new line
    Serial.println();

    // Generate the prettified JSON and send it to the Serial port
    serializeJsonPretty(doc, Serial);

    int httpResponseCode = http.POST(payload);
    
    // If you need Node-RED/server authentication, insert user and password below
    //http.setAuthorization("REPLACE_WITH_SERVER_USERNAME", "REPLACE_WITH_SERVER_PASSWORD");
    
    // Free resources
    http.end();
}


