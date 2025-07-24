#include <Arduino.h>
#include <Preferences.h>
#include <ezButton.h>
#include "TFT_eSPI.h" /* Please use the TFT library provided in the library. */
#include "img_logo.h"
#include "pin_config.h"
#include <Adafruit_ADS1X15.h>
#include <RunningAverage.h>
#include <esp_now.h>
#include <WiFi.h>
#include "RMSCurrentSampler.h"

#include <WiFiManager.h> 

#define FONT_LARGE &Dialog_plain_100 // Key label font 2

TFT_eSPI tft = TFT_eSPI();

TFT_eSprite tft_percent = TFT_eSprite(&tft); // Sprite object graph1
TFT_eSprite tft_percent_cell = TFT_eSprite(&tft); // Sprite object graph1
TFT_eSprite tft_menu = TFT_eSprite(&tft);
WiFiManager wifiManager;

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

#define SENSOR_THRESHOLD_MILLIVOLT_MIN 5
#define SENSOR_THRESHOLD_MILLIVOLT_MAX 20
#define SOLENOID_CLOSE_DELAY 300 // milliseconds
#define MAX_PRESSURE_BAR 240 //240
#define MAX_PRESSURE_BAR_200 250
#define MAX_PRESSURE_BAR_300 330

#define MENU_ITEM_CLOSE 0
#define MENU_ITEM_CLEAR_CALIBRATION 1
#define MENU_ITEM_AUTOSTOP_MODE 2
#define MENU_ITEM_WIFI_CONFIG 3

bool isMenuMode = false;

struct ButtonStates {
  bool stopPressed;
  bool startPressed;
  bool calibratePressed;
};

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

struct PressureReading {
  float voltage;
  int adc;
  int bar;
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
  bool autoStopActive;
  bool autoStopTriggered;
  bool compressorRunning;
  float compressorCurrent;
  int autoStopMaxPressure;
  bool wifiConnected;
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

void drawSolenoidValue();
void drawCellInfo(int index);
void drawPressureInfo();
void drawMainOxygenValue();
void drawMenu();
void drawInitalScreen();
void handleSensor();
void handleButtons();
void handleStartStopButton();
void handleSystemState();
// void handlePotentiometer();
void handleSolenoid();
void handleAutoStop();
void calibrate();
void restoreCalibration();
void persistCalibration();
float readOxygenCellVoltage();
void menuLongClick();
void menuShortClick();

Preferences preferences;
ezButton calibrateButton(PIN_BTN_CALIBRATE); 
ezButton startButton(PIN_BTN_START);
ezButton stopButton(PIN_BTN_STOP);
Adafruit_ADS1115 ads1115;  // Construct an ads1115
Adafruit_ADS1115 ads1115_2;


float FACTOR = 30.0F;      // example value
RMSCurrentSampler currentSampler(&ads1115_2, FACTOR);

float gain = 0.0625F;

const char* cellHeader[2] = { "CELL 1", "BAR" };
MenuOption menuOptions[] = {
  { "Close", MENU_ITEM_CLOSE },
  { "Clear Calibration", MENU_ITEM_CLEAR_CALIBRATION },
  { "AutoStop 200/300 BAR", MENU_ITEM_AUTOSTOP_MODE },
  { "WiFi Config", MENU_ITEM_WIFI_CONFIG }
};
Menu menuState;

SystemStatus systemState;
SensorReading sensorValue[2];
PressureReading pressureValue;
CellCalibration cellCalibration[2];
SolenoidStatus solenoid;
ButtonStates buttonStates;
RunningAverage currentReading = RunningAverage(10);
RunningAverage pressureReading = RunningAverage(4);
RunningAverage cellReadings[2] = { RunningAverage(RA_SIZE), RunningAverage(RA_SIZE) };


void setup()
{
  preferences.begin("calibration", false);

  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  pinMode(PIN_RELAY_SOLENOID, OUTPUT);
  pinMode(PIN_RELAY_START, OUTPUT);
  pinMode(PIN_RELAY_STOP, OUTPUT);
  
  pinMode(PIN_PRESSURE_SIGNAL, INPUT);

  pinMode(PIN_SWITCH_PRESSURE_MODE, INPUT_PULLUP);

  calibrateButton.setDebounceTime(50);
  startButton.setDebounceTime(50);
  stopButton.setDebounceTime(50);

  Serial.begin(115200);
  Serial.println("Hello T-Display-S3");

  // Setup amp
  Wire.begin(PIN_IIC_SDA, PIN_IIC_SCL);
  // ads1115.setGain(GAIN_TWO);
  ads1115.setGain(GAIN_TWOTHIRDS);
  ads1115.setDataRate(RATE_ADS1115_64SPS);
  ads1115.begin();


  ads1115_2.begin(0x49);
  // End

  // Setup screen
  tft.begin();

  tft.setRotation(1);
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

  // LOAD CALIBRATION
  restoreCalibration(); 

  /* Communication */
   WiFi.mode(WIFI_STA);
   WiFi.setHostname("esp32-nitrox-blender");

  // Try to connect without blocking
  // WiFi.begin();
  // if (WiFi.SSID() != "" && WiFi.psk() != "") {
  //   WiFi.begin();
  //   Serial.println("Attempting to connect to saved WiFi...");
  // }

  wifiManager.setConfigPortalBlocking(false);
  wifiManager.autoConnect("Nitrox Blender");
  // systemState.wifiConnected = wifiManager.autoConnect("Nitrox Blender");

  systemState.autoStopMaxPressure = MAX_PRESSURE_BAR_200;
}


long lastScreenUpdate = 0;

void loop()
{
  handleSensor();

  if (!isMenuMode) {
    handleButtons();
  }
  handleStartStopButton();
  
  handleSystemState();

  handleSolenoid();
  handleAutoStop();

  // Update Screen

  if ((millis() - lastScreenUpdate) > 500)
  {

    if (menuState.isMenuMode) {
      drawMenu();
    } else {
      drawMainOxygenValue();

      tft.setTextColor(TFT_GREEN, TFT_BLACK);    

      drawCellInfo(0);
      drawPressureInfo();
      drawSolenoidValue();
    }

    lastScreenUpdate = millis();

  }
}



// Record when we last did a reading to manage when we should read the next time.
long lastPressureReadMillis = 0;
long lastReadMillis = 0;

/*
 * Read a value every 50 ms
*/
float voltage;
void handleSensor() {

  // Process the current detector
  if (currentSampler.update()) {
    currentReading.add(currentSampler.getRMS());
    systemState.compressorCurrent = currentSampler.getRMS();

    // Left in for debug
  //   Serial.print("RMS Current: ");
  //   Serial.print(currentSampler.getRMS(), 3);
  //   Serial.print(" voltage: ");
  //   Serial.println(currentSampler.getSensorVoltageRMS(), 4);
  }


  if (millis() - lastPressureReadMillis > 500) {
    // Set gain to read 0-6V.
    ads1115.setGain(GAIN_TWOTHIRDS);
    delay(10);

    pressureValue.adc = ads1115.readADC_SingleEnded(3);
    pressureValue.voltage = ads1115.computeVolts(pressureValue.adc);
    pressureValue.bar = pressureValue.voltage * 60;
    
    lastPressureReadMillis = millis();

  }

  if (millis() - lastReadMillis > 50) {

    // Set gain to read mv.
    ads1115.setGain(GAIN_TWO);
    delay(10);

    readOxygenCellVoltage();

    float o2Percent = 0;
    int validReadingsCount = 0;

    for (int cellIndex = 0; cellIndex < 2; cellIndex++) {
      bool isCalibrated = cellCalibration[cellIndex].isValid();
      bool dataInconsistency = (cellReadings[cellIndex].getMax() - cellReadings[cellIndex].getMin()) > 6; // Check if there is large difference in readings
      float maxVal = cellReadings[cellIndex].getMaxInBuffer();
      float minVal = cellReadings[cellIndex].getMinInBuffer();
      // Serial.printf("Cell #%d: min [%f] max [%f] diff [%f] dev [%f] err [%f]", cellIndex, maxVal, minVal, (maxVal - minVal), cellReadings[cellIndex].getStandardDeviation(), cellReadings[cellIndex].getStandardError());
      // Serial.println();

      sensorValue[cellIndex].avgMv = abs(cellReadings[cellIndex].getAverage());
      sensorValue[cellIndex].o2Percent = (sensorValue[cellIndex].avgMv / cellCalibration[cellIndex].value) * 20.9;
      if (sensorValue[cellIndex].o2Percent > 99.9) {
        sensorValue[cellIndex].o2Percent = 99.9;
      }

      bool isReadingError = !isCalibrated || !sensorValue[cellIndex].isValid();

      sensorValue[cellIndex].sensorWarning = isReadingError;

      if (!isReadingError && !sensorValue[cellIndex].isDisabledByMenu) {
        o2Percent += sensorValue[cellIndex].o2Percent;
        validReadingsCount++;
      }

    }

    // Calculate combined percentage
    systemState.isReadingError = validReadingsCount == 0;
    if (validReadingsCount > 0) {
      systemState.o2 = o2Percent / validReadingsCount;
    } else {
      systemState.o2 = -1;
    }

    lastReadMillis = millis();    
  }  
}

long pressStarted = -1;
bool hasTriggeredClear = false;

void handleStartStopButton() {
  startButton.loop();
  stopButton.loop();

  if (startButton.isPressed()) {
    Serial.println("START");
    Serial.println(startButton.getState());
    digitalWrite(PIN_RELAY_START, HIGH);
    buttonStates.startPressed = true;
  } 
  if (startButton.isReleased()) {
    digitalWrite(PIN_RELAY_START, LOW);
    buttonStates.startPressed = false;
  } 
  if (stopButton.isPressed()) {
    Serial.println("STOP");
    digitalWrite(PIN_RELAY_STOP, HIGH);
    buttonStates.stopPressed = true;
  }
  if (stopButton.isReleased()) {
    digitalWrite(PIN_RELAY_STOP, LOW);
    buttonStates.stopPressed = false;
  }

  // Handle switch

  if (digitalRead(PIN_SWITCH_PRESSURE_MODE) == LOW) {
    systemState.autoStopMaxPressure = MAX_PRESSURE_BAR_300;
  } else {
    systemState.autoStopMaxPressure = MAX_PRESSURE_BAR_200;
  }
}

void handleButtons() {
  calibrateButton.loop();

  long pressLength = 0;
  if (pressStarted > 0) {
    pressLength = millis() - pressStarted;
  }

  bool isLongPress = pressLength > 2000;
  
  int buttonState = calibrateButton.getState();

  if (calibrateButton.isPressed() && pressStarted < 0) {
    buttonStates.calibratePressed = true;

    Serial.println("Press started");

    pressStarted = millis();
  }


  if (isLongPress && !hasTriggeredClear) {
    Serial.println("Long button press");

    if (menuState.isMenuMode) {
      menuLongClick();
    } else {
      menuState.isMenuMode = true;
    }

    // preferences.clear();
    // tft.drawString("Calibration cleared", 0, 0, 4);
    // delay(3000);

    hasTriggeredClear = true;
  }

  if(calibrateButton.isReleased()) {
      buttonStates.calibratePressed = false;

      if (!isLongPress) {

        if (menuState.isMenuMode) {
          menuShortClick();
        } else {
          Serial.println("CALIBRATING");
          calibrate();
        }
      }

    // Reset state
    pressStarted = -1;
    hasTriggeredClear = false;
  }
}

bool solenoidIsClosed = true;
void handleSolenoid() {

    // The previous state is closed solenoid and is now open
    if (solenoidIsClosed && solenoid.isOpen) {
      digitalWrite(PIN_RELAY_SOLENOID, HIGH);
      solenoidIsClosed = false;
    }

    if (!solenoidIsClosed && !solenoid.isOpen) {
      digitalWrite(PIN_RELAY_SOLENOID, LOW);
      solenoidIsClosed = true;
    }
}

void handleSystemState() {
  // IF there is a load on the contactor, we should assume that the compressor is running
  
  systemState.compressorRunning = systemState.compressorCurrent > 1;


  // Calculate solenoid state
  if (systemState.compressorRunning && systemState.o2 < 40) {
    solenoid.isOpen = true;
  } else {
    solenoid.isOpen = false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    systemState.wifiConnected = false;
  } else {
    systemState.wifiConnected = true;
  }
}

long lastAutoStopHandled;
long autoStopTrigger = -1;
void handleAutoStop() {
  
  if (millis() - lastAutoStopHandled < 100) {
    return;
  }

  if (pressureValue.bar > systemState.autoStopMaxPressure && autoStopTrigger < 0) {
    systemState.autoStopTriggered = true;
    autoStopTrigger = millis();
  } 
  
  if (pressureValue.bar <= systemState.autoStopMaxPressure && autoStopTrigger > 0) {
    // Reset trigger
    systemState.autoStopTriggered = false;
    autoStopTrigger = -1;
  }

  // If pressure is over threshold for more than 5 secs, trigger stop button
  if (systemState.autoStopTriggered && !systemState.autoStopActive && (millis() - autoStopTrigger) > 5000) {
    digitalWrite(PIN_RELAY_STOP, HIGH);
    systemState.autoStopActive = true;
  }
  if (systemState.autoStopActive && !systemState.autoStopTriggered) {
    systemState.autoStopActive = false;
    digitalWrite(PIN_RELAY_STOP, LOW);
  }
  
  lastAutoStopHandled = millis();
}
void calibrate() {
  Serial.println("Calibrating...");

  // renderCalibratingStarted();
  tft.fillRect(0, 0, 340, 90, TFT_GREEN);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("CALIBRATING", 80, 37, 4);

  delay(3000);

  for (int i = 0; i < RA_SIZE; i++) {
    handleSensor();

    drawCellInfo(0);
    drawPressureInfo();
    
    delay(100);
  }

  for (int i = 0; i < 2; i++) {
    float calibrationVoltage = sensorValue[i].avgMv;

    cellCalibration[i].calibratedAtMs = millis();
    cellCalibration[i].value = calibrationVoltage;    
  }

  tft.fillRect(0, 0, 340, 90, TFT_GREEN);
  tft.drawString("DONE", 125, 35, 4);
  
  delay(2000);

  persistCalibration();
}

void persistCalibration() {
  preferences.putFloat("cell1", cellCalibration[0].value);
}
void restoreCalibration() {
  cellCalibration[0].value = preferences.getFloat("cell1", 0);
}

float readOxygenCellVoltage() {
  float value = ads1115.readADC_Differential_0_1() * gain;
  cellReadings[0].addValue(value);
  return value;
}

void drawInitalScreen() {
  tft.fillScreen(TFT_BLACK);

  // Draw headers
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK);

  tft.fillRect(0, 92, CELL_WIDTH, 15, TFT_GREEN);
  tft.drawString("CELL 1", CELL_PADDING_LEFT, HEADER_ROW_Y, 2);

  tft.fillRect(CELL_WIDTH + CELL_SPACING, HEADER_ROW_Y, CELL_WIDTH, 15, TFT_GREEN);
  // tft.drawString("CELL 2", CELL_PADDING_LEFT + CELL_WIDTH + CELL_SPACING, HEADER_ROW_Y, 2);
  tft.drawString("BARR", CELL_PADDING_LEFT + CELL_WIDTH + CELL_SPACING, HEADER_ROW_Y, 2);

  // tft.fillRect(250, HEADER_ROW_Y, 90, 15, TFT_RED);
  // tft.drawString("SOLENOID", 256, HEADER_ROW_Y, 2);
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

bool hasShownButtonState = false;
void drawSolenoidValue() {  

  if (buttonStates.startPressed) {
    tft.fillRect(250, HEADER_ROW_Y, 90, 15, TFT_GREEN);
    tft.setTextColor(TFT_BLACK);
    tft.drawString("START", 256, HEADER_ROW_Y, 2);
    hasShownButtonState = true;
  } else if (buttonStates.stopPressed) {
    tft.fillRect(250, HEADER_ROW_Y, 90, 15, TFT_GREEN);
    tft.setTextColor(TFT_BLACK);
    tft.drawString("STOP", 256, HEADER_ROW_Y, 2);
    hasShownButtonState = true;
  } else if (buttonStates.calibratePressed) {
    tft.fillRect(250, HEADER_ROW_Y, 90, 15, TFT_GREEN);
    tft.setTextColor(TFT_BLACK);
    tft.drawString("CAL", 256, HEADER_ROW_Y, 2);
    hasShownButtonState = true;
  } else {
    if (hasShownButtonState) {
      tft.fillRect(250, HEADER_ROW_Y, 90, 15, TFT_BLACK);
      hasShownButtonState = false;
    }
  }

  // Print compressor indicator
  if (systemState.compressorRunning) {
    tft.fillRect(250, HEADER_ROW_Y, 20, 15, TFT_GREEN);
  } else {
    tft.fillRect(250, HEADER_ROW_Y, 20, 15, TFT_RED);
  }
    tft.setTextColor(TFT_BLACK);
    tft.drawString("C", 257, HEADER_ROW_Y, 2);

  if (solenoid.isOpen) {
    tft.fillRect(273, HEADER_ROW_Y, 20, 15, TFT_GREEN);
  } else {
    tft.fillRect(273, HEADER_ROW_Y, 20, 15, TFT_RED);
  }
  tft.setTextColor(TFT_BLACK);
  tft.drawString("S", 280, HEADER_ROW_Y, 2);

  /* WiFi status */
  if (systemState.wifiConnected) {
    tft.fillRect(296, HEADER_ROW_Y, 20, 15, TFT_GREEN);
  } else {
    tft.fillRect(296, HEADER_ROW_Y, 20, 15, TFT_RED);
  }
  tft.setTextColor(TFT_BLACK);
  tft.drawString("W", 303, HEADER_ROW_Y, 2);


  if (systemState.autoStopMaxPressure == MAX_PRESSURE_BAR_200) {
    tft.fillRect(250, HEADER_ROW_Y + 20, 90, 15, TFT_GREEN);
  } else {
    tft.fillRect(250, HEADER_ROW_Y + 20, 90, 15, TFT_ORANGE);
  }
  tft.setTextColor(TFT_BLACK);
  tft.drawString("AUTOSTOP", 256, HEADER_ROW_Y + 20, 2);
  
  if (systemState.autoStopActive) {
    tft.fillRect(250, HEADER_ROW_Y + 40, 90, 15, TFT_YELLOW);
  } else {
    tft.fillRect(250, HEADER_ROW_Y + 40, 90, 15, TFT_DARKGREEN);
  }
  tft.setTextColor(TFT_BLACK);
  tft.drawString("PSR_STOP", 256, HEADER_ROW_Y + 40, 2);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  int xpos = 250;
  xpos += tft.drawFloat(currentReading.getMaxInBuffer(), 3, xpos, 150, 1);
  xpos += tft.drawString(" Amp", xpos, 150, 1);

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

void drawPressureInfo() {
  int index = 1;
  int offsetX = index * (CELL_WIDTH + CELL_SPACING);

  // Heading
  if (systemState.autoStopMaxPressure > MAX_PRESSURE_BAR_200) {
    tft.fillRect(offsetX, HEADER_ROW_Y, CELL_WIDTH, 15, TFT_ORANGE);
    tft.setTextColor(TFT_BLACK);
    tft.drawString("BAR (300)", offsetX + CELL_PADDING_LEFT + 10 - 20, HEADER_ROW_Y, 2);
  } else {
    tft.fillRect(offsetX, HEADER_ROW_Y, CELL_WIDTH, 15, TFT_GREEN);
    tft.setTextColor(TFT_BLACK);
    tft.drawString("BAR", offsetX + CELL_PADDING_LEFT + 10, HEADER_ROW_Y, 2);
  }

  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  
  int xpos = offsetX;
  xpos += tft.drawString("Ref: ", offsetX, 150, 1);
  xpos += tft.drawFloat(pressureValue.voltage, 3, xpos, 150, 1);
  xpos += tft.drawString(" V", xpos, 150, 1);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);

  // 60 bar pr volt. 300 bar, 0-5V.
  tft_percent_cell.fillSprite(TFT_BLACK);
  tft_percent_cell.setTextColor(TFT_GREEN);
  tft_percent_cell.drawNumber(pressureValue.bar, 0, 0);
  tft_percent_cell.pushSprite(offsetX + 50, 112);
}


void drawMenu() {
  // 
  tft_menu.fillSprite(TFT_BLACK);
  tft_menu.drawRect(0, 0, 300, 150, TFT_YELLOW);
  tft_menu.drawRect(1, 1, 298, 148, TFT_YELLOW);

  for (int i = 0; i < 4; i++) {
    int itemX = 6;
    int itemY = 6 + (i * 30);

    if (menuState.selectedOption == i) {
      tft_menu.fillRect(itemX, itemY, 288, 30, TFT_YELLOW);
      tft_menu.setTextColor(TFT_BLACK);
    } else {
      tft_menu.setTextColor(TFT_YELLOW);
    }
    tft_menu.drawString(menuOptions[i].label, itemX + 3, itemY + 4, 4);
  }
  
  tft_menu.pushSprite(10, 10);
}

void menuLongClick() {

  if (menuState.selectedOption == MENU_ITEM_CLOSE) {
  }

  if (menuState.selectedOption == MENU_ITEM_CLEAR_CALIBRATION) {
    preferences.clear();
  }

  if (menuState.selectedOption == MENU_ITEM_AUTOSTOP_MODE) {
    if (systemState.autoStopMaxPressure == MAX_PRESSURE_BAR_200) {
      systemState.autoStopMaxPressure = MAX_PRESSURE_BAR_300;
    } else {
      systemState.autoStopMaxPressure = MAX_PRESSURE_BAR_200;
    }
  }

  if (menuState.selectedOption == MENU_ITEM_WIFI_CONFIG) {
    // Need to stop the portal if it is running, due to autoconnect in top. 
    // If it is running, it is not handling connections and just returns.
    if (wifiManager.getConfigPortalActive()) {
      wifiManager.stopConfigPortal();
    }

    wifiManager.setConfigPortalBlocking(true);
    wifiManager.startConfigPortal("Nitrox Blender");
    
  }
  
  menuState.isMenuMode = false;
  drawInitalScreen();
}

void menuShortClick() {
  menuState.selectedOption += 1;
  if (menuState.selectedOption > 3) {
    menuState.selectedOption = 0;
  }
}