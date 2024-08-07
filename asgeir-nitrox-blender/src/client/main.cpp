#include <Arduino.h>
#include <Preferences.h>
#include <ezButton.h>
#include "TFT_eSPI.h" /* Please use the TFT library provided in the library. */
#include "img_logo.h"
#include "pin_config.h"
#include <esp_now.h>
#include <WiFi.h>

#define FONT_LARGE &Dialog_plain_100 // Key label font 2

TFT_eSPI tft = TFT_eSPI();

TFT_eSprite tft_percent = TFT_eSprite(&tft); // Sprite object graph1
TFT_eSprite tft_percent_cell = TFT_eSprite(&tft); // Sprite object graph1
TFT_eSprite tft_menu = TFT_eSprite(&tft);

esp_now_peer_info_t Client;
#define CHANNEL 1
#define PRINTSCANRESULTS 0
#define DELETEBEFOREPAIR 0


#define CELL_WIDTH 120
#define CELL_SPACING 5
#define CELL_PADDING_LEFT 40
#define HEADER_ROW_Y 92

#define O2_I2Caddress 0x48

#define RA_SIZE 40
#define SOLENOID_O2_LIMIT 35

#define SENSOR_THRESHOLD_MILLIVOLT_MIN 7
#define SENSOR_THRESHOLD_MILLIVOLT_MAX 20
#define SOLENOID_CLOSE_DELAY 300 // milliseconds

#define MENU_ITEM_CLOSE 0
#define MENU_ITEM_CLEAR_CALIBRATION 1
#define MENU_ITEM_DISABLE_CELL_1 2
#define MENU_ITEM_DISABLE_CELL_2 3

bool isMenuMode = false;

struct SolenoidStatus {
  int maxO2Percent;
  bool isOpen;
  long solenoidClosedAt;
};

struct CellCalibration {
  float value;
  long calibratedAtMs;
  
  /// @brief If the cell is calibrated at an "reasonable" voltage.. E.g. if calibration was performed when cell was misbehaving.
  /// @return true if calibration value is within acceptable range.
  bool isValid() {
    return value > SENSOR_THRESHOLD_MILLIVOLT_MIN && value < SENSOR_THRESHOLD_MILLIVOLT_MAX;
  };
};

struct SensorReading {
  float avgMv;
  float o2Percent;
  bool sensorWarning;
  bool isDisabledByMenu;

  /// @brief If the cell reading at an "reasonable" voltage.. E.g. if calibration was performed when cell was misbehaving.
  /// @return true if calibration value is within acceptable range.
  bool isValid() {
    return avgMv > SENSOR_THRESHOLD_MILLIVOLT_MIN && avgMv < SENSOR_THRESHOLD_MILLIVOLT_MAX;
  };
};

struct SystemStatus {
  float o2;
  bool isReadingError;
};

struct O2Reading {
  int percent;
  bool cellError;
};

struct Menu {
  int selectedOption;
  bool isMenuMode;

};
struct MenuOption {
  String label;
  int actionId;
};

struct EspNowMessage {
  SystemStatus systemState;
  SensorReading readingCell1;
  SensorReading readingCell2;
  CellCalibration cellCalibrationCell1;
  CellCalibration cellCalibrationCell2;
  SolenoidStatus solenoid;
};

void drawSolenoidValue();
void drawCellInfo(int index);
void drawMainOxygenValue();
void drawMenu();
void drawInitalScreen();

float gain = 0.0625F;

const char* cellHeader[2] = { "CELL 1", "CELL 2" };
MenuOption menuOptions[] = {
  { "Close", MENU_ITEM_CLOSE },
  { "Clear Calibration", MENU_ITEM_CLEAR_CALIBRATION },
  { "Disable Cell 1", MENU_ITEM_DISABLE_CELL_1 },
  { "Disable Cell 2", MENU_ITEM_DISABLE_CELL_2 }
};
Menu menuState;

SystemStatus systemState;
SensorReading sensorValue[2];
CellCalibration cellCalibration[2];
SolenoidStatus solenoid;
EspNowMessage espMessageData;

// callback when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&espMessageData, incomingData, sizeof(espMessageData));

  cellCalibration[0] = espMessageData.cellCalibrationCell1;
  cellCalibration[1] = espMessageData.cellCalibrationCell2;
  sensorValue[0] = espMessageData.readingCell1;
  sensorValue[1] = espMessageData.readingCell2;
  solenoid = espMessageData.solenoid;
  systemState = espMessageData.systemState;
}

void setup()
{
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

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

  drawInitalScreen();

  tft_percent.setColorDepth(8);
  tft_percent.createSprite(340, 90);
  tft_percent.setFreeFont(FONT_LARGE);

  tft_percent_cell.setColorDepth(8);
  tft_percent_cell.createSprite(CELL_WIDTH - 50, 30);
  tft_percent_cell.setFreeFont(&FreeSerif18pt7b);

  tft_menu.setColorDepth(8);
  tft_menu.createSprite(300, 150);

  /* Communication */
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
  }
  esp_now_register_recv_cb(OnDataRecv);
}


long lastScreenUpdate = 0;

void loop()
{
  if ((millis() - lastScreenUpdate) > 500)
  {

    drawMainOxygenValue();

    tft.setTextColor(TFT_GREEN, TFT_BLACK);    

    drawCellInfo(0);
    drawCellInfo(1);
    drawSolenoidValue();

    lastScreenUpdate = millis();
  }
}



// Record when we last did a reading to manage when we should read the next time.
long lastReadMillis = 0;


long pressStarted = -1;
bool hasTriggeredClear = false;

void drawInitalScreen() {
  tft.fillScreen(TFT_BLACK);

  // Draw headers
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK);

  tft.fillRect(0, 92, CELL_WIDTH, 15, TFT_GREEN);
  tft.drawString("CELL 1", CELL_PADDING_LEFT, HEADER_ROW_Y, 2);

  tft.fillRect(CELL_WIDTH + CELL_SPACING, HEADER_ROW_Y, CELL_WIDTH, 15, TFT_GREEN);
  tft.drawString("CELL 2", CELL_PADDING_LEFT + CELL_WIDTH + CELL_SPACING, HEADER_ROW_Y, 2);

  tft.fillRect(250, HEADER_ROW_Y, 90, 15, TFT_RED);
  tft.drawString("SOLENOID", 256, HEADER_ROW_Y, 2);
}

void drawMainOxygenValue() {
  float o2 = systemState.o2;

  tft_percent.fillSprite(TFT_BLACK);

  if (o2 <0) {
    tft_percent.setTextColor(TFT_RED);
    tft_percent.drawString("ERR-1", 0, 0);    
  } else {
    if (o2 > 40) { 
      tft_percent.setTextColor(TFT_RED);
    } else if (o2 > 32) {
      tft_percent.setTextColor(TFT_ORANGE);
    } else {
      tft_percent.setTextColor(TFT_GREEN);
    }

    int offset = tft_percent.drawFloat(o2, 1, 0, 0);
    tft_percent.drawString("%", offset, 0);
  }

  tft_percent.pushSprite(0, 0);
}

// Used in drawing of the solenoid value.
int lastSolenoidMaxValue = 0;
bool lastSolenoidOpen = false;

void drawSolenoidValue() {  
  if (solenoid.isOpen != lastSolenoidOpen) {
    if (solenoid.isOpen) {
      tft.fillRect(250, HEADER_ROW_Y, 90, 15, TFT_GREEN);
    } else {
      tft.fillRect(250, HEADER_ROW_Y, 90, 15, TFT_RED);
    }
    tft.setTextColor(TFT_BLACK);
    tft.drawString("SOLENOID", 256, HEADER_ROW_Y, 2);

    lastSolenoidOpen = solenoid.isOpen;
  }


  if (lastSolenoidMaxValue != solenoid.maxO2Percent) {
    if (solenoid.maxO2Percent > 34) {
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    } else {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
    }

    tft.setCursor(250, 115, 7);
    tft.printf("%02d", solenoid.maxO2Percent);
  }

  lastSolenoidMaxValue = solenoid.maxO2Percent;
}

int cellWasDisabled[2] = { -1, -1 };

void drawCellInfo(int index) {
    float o2 = sensorValue[index].o2Percent;
    float mv = sensorValue[index].avgMv;
    float calibration = cellCalibration[index].value;
    bool calibrationIsValid = cellCalibration[index].isValid();
    bool isDisabled = (sensorValue[index].isValid() == false) || sensorValue[index].isDisabledByMenu;

    int offsetX = index * (CELL_WIDTH + CELL_SPACING);

    // Heading
    if (cellWasDisabled[index] != (int)isDisabled) {
      if (isDisabled) {
        tft.fillRect(offsetX, HEADER_ROW_Y, CELL_WIDTH, 15, TFT_RED);
      } else {
        tft.fillRect(offsetX, HEADER_ROW_Y, CELL_WIDTH, 15, TFT_GREEN);
      }
      tft.setTextColor(TFT_BLACK);

      tft.drawString(cellHeader[index], offsetX + CELL_PADDING_LEFT, HEADER_ROW_Y, 2);

      cellWasDisabled[index] = (int)isDisabled;
    }



    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    
    // Draw the current mv measurement
    int xpos = offsetX;

    if (mv < 7) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    xpos += tft.drawFloat(mv, 2, offsetX, 112, 2);
    tft.drawString("  ", xpos, 112, 2); // Clear out background when going from 2 to 1 digit.
    tft.drawString("mV", offsetX + 12, 127, 2);
    
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    

    if (!calibrationIsValid) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    xpos = offsetX;
    xpos += tft.drawString("Ref: ", offsetX, 150, 1);
    xpos += tft.drawFloat(calibration, 2, xpos, 150, 1);
    xpos += tft.drawString(" mV", xpos, 150, 1);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);

    // Draw the o2 percent
    if (isDisabled) {
      tft_percent_cell.fillSprite(TFT_BLACK);
      tft_percent_cell.setTextColor(TFT_RED);
      tft_percent_cell.drawString("DISABLED", 0, 0, 2);
      tft_percent_cell.pushSprite(offsetX + 50, 112);
    } else {
      tft_percent_cell.fillSprite(TFT_BLACK);
      tft_percent_cell.setTextColor(TFT_GREEN);
      tft_percent_cell.drawFloat(o2, 1, 0, 0);
      tft_percent_cell.pushSprite(offsetX + 50, 112);
    }
}
