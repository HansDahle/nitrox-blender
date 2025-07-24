#include <Arduino.h>
#include "TFT_eSPI.h" /* Please use the TFT library provided in the library. */
#include "img_logo.h"
#include "pin_config.h"
#include <RunningAverage.h>
#include <Adafruit_ADS1X15.h>


#define FONT_LARGE &Dialog_plain_100 // Key label font 2

TFT_eSPI tft = TFT_eSPI();

TFT_eSprite tft_percent = TFT_eSprite(&tft); // Sprite object graph1
TFT_eSprite tft_percent_cell = TFT_eSprite(&tft); // Sprite object graph1
TFT_eSprite tft_menu = TFT_eSprite(&tft);


#define CELL_WIDTH 120
#define CELL_SPACING 5
#define CELL_PADDING_LEFT 40
#define HEADER_ROW_Y 92

#define O2_I2Caddress 0x48

#define RA_SIZE 4
#define SOLENOID_O2_LIMIT 35

#define SENSOR_THRESHOLD_MILLIVOLT_MIN 7
#define SENSOR_THRESHOLD_MILLIVOLT_MAX 20
#define SOLENOID_CLOSE_DELAY 300 // milliseconds

#define MENU_ITEM_CLOSE 0
#define MENU_ITEM_CLEAR_CALIBRATION 1
#define MENU_ITEM_DISABLE_CELL_1 2
#define MENU_ITEM_DISABLE_CELL_2 3

RunningAverage reading = RunningAverage(RA_SIZE);
Adafruit_ADS1115 ads1115;  // Construct an ads1115

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
  tft.setTextColor(TFT_BLACK);
  tft.setFreeFont(&FreeSerif18pt7b);
}


long lastScreenUpdate = 0;
long lastRead = 0;

float voltPerBar = (300/5);

void loop()
{

  if (millis() - lastRead > 1000) {
    
    int data = analogRead(PIN_POTENTIOMETER);
    reading.add(data);
      // Serial.print("reading: ");
      // Serial.println(data);

      lastRead = millis();
  }

  if ((millis() - lastScreenUpdate) > 2000)
  {

    // int offset = tft_percent.drawFloat(o2, 1, 0, 0);
    // tft_percent.drawString("%", offset, 0);    
    float average = reading.getAverage();
    int pressure = map((int)average, 0, 4095, 0, 300);  

    lastScreenUpdate = millis();



      // Serial.println("Error sending the data");
      Serial.print("average: ");
      Serial.print(average);
      Serial.print(" ");
      Serial.println(pressure);

      int offset = tft.drawNumber(pressure, 0, 0, 4);
      tft.drawString(" BAR", offset, 0);
      
  }
}


