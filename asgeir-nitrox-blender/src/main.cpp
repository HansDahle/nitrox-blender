#include <Arduino.h>
#include <Preferences.h>
#include <ezButton.h>
#include "TFT_eSPI.h" /* Please use the TFT library provided in the library. */
#include "img_logo.h"
#include "pin_config.h"
#include <Adafruit_ADS1X15.h>
#include <RunningAverage.h>

#define FONT_LARGE &Dialog_plain_100 // Key label font 2

TFT_eSPI tft = TFT_eSPI();

TFT_eSprite tft_percent = TFT_eSprite(&tft); // Sprite object graph1
TFT_eSprite tft_percent_cell = TFT_eSprite(&tft); // Sprite object graph1

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

void drawSolenoidValue();
void drawCellInfo(int index);
void drawMainOxygenValue();
void handleSensor();
void handleButtons();
void handlePotentiometer();
void handleSolenoid();
void calibrate();
void restoreCalibration();
void persistCalibration();
float readOxygenCellVoltage();

Preferences preferences;
RunningAverage RA(RA_SIZE);
RunningAverage RA2(RA_SIZE);
ezButton calibrateButton(PIN_CALIBRATE_BUTTON); 
Adafruit_ADS1115 ads1115;  // Construct an ads1115

float gain = 0.0625F;

const char* cellHeader[2] = { "CELL 1", "CELL 2" };

SystemStatus systemState;
SensorReading sensorValue[2];
CellCalibration cellCalibration[2];
SolenoidStatus solenoid;
RunningAverage cellReadings[2] = { RunningAverage(RA_SIZE), RunningAverage(RA_SIZE) };


void setup()
{
  preferences.begin("calibration", false);

  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  pinMode(PIN_SOLENOID_SIGNAL, OUTPUT);
  pinMode(PIN_POTENTIOMETER, INPUT);

  calibrateButton.setDebounceTime(50);

  Serial.begin(115200);
  Serial.println("Hello T-Display-S3");

  // Setup amp
  Wire.begin(PIN_IIC_SDA, PIN_IIC_SCL);
  ads1115.setGain(GAIN_TWO);
  ads1115.setDataRate(RATE_ADS1115_64SPS);
  ads1115.begin();
  // End

  // Setup screen
  tft.begin();

  tft.setRotation(3);
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, 320, 170, (uint16_t *)img_logo);

  delay(2000);

  ledcSetup(0, 2000, 8);
  ledcAttachPin(PIN_LCD_BL, 0);
  ledcWrite(0, 255);

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


  tft_percent.setColorDepth(8);
  tft_percent.createSprite(340, 90);
  tft_percent.setFreeFont(FONT_LARGE);

  tft_percent_cell.setColorDepth(8);
  tft_percent_cell.createSprite(CELL_WIDTH - 50, 30);
  tft_percent_cell.setFreeFont(&FreeSerif18pt7b);

  // LOAD CALIBRATION
  restoreCalibration(); 
}



long lastScreenUpdate = 0;

void loop()
{
  handleSensor();
  handlePotentiometer();
  handleButtons();
  handleSolenoid();

  // Update Screen

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

/*
 * Read a value every 50 ms
*/
void handleSensor() {

  if (millis() - lastReadMillis > 50) {

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

      if (!isReadingError) {
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

void handleButtons() {
  calibrateButton.loop();
  long pressLength = 0;
  if (pressStarted > 0) {
    pressLength = millis() - pressStarted;
  }

  bool isLongPress = pressLength > 4000;
  
  int buttonState = calibrateButton.getState();

  if (calibrateButton.isPressed() && pressStarted < 0) {
      Serial.println("Press started");

      pressStarted = millis();
  }


  if (isLongPress && !hasTriggeredClear) {
    Serial.println("Long button press");

    preferences.clear();
    tft.drawString("Calibration cleared", 0, 0, 4);
    delay(3000);

    hasTriggeredClear = true;
  }

  if(calibrateButton.isReleased()) {
    if (!isLongPress) {
      Serial.println("CALIBRATING");
      calibrate();
    }

    // Reset state
    pressStarted = -1;
    hasTriggeredClear = false;
  }
}
void handlePotentiometer() {
  int data = analogRead(PIN_POTENTIOMETER);
  
  solenoid.maxO2Percent = map(data, 0, 4095, 0, 40);  
}

void handleSolenoid() {
  if (!systemState.isReadingError && systemState.o2 < solenoid.maxO2Percent) {

    solenoid.solenoidClosedAt = millis();

    if (!solenoid.isOpen) {
      digitalWrite(PIN_SOLENOID_SIGNAL, HIGH);
      solenoid.isOpen = true;
    }
  } else {
    if (solenoid.isOpen) {
      if ((millis() - solenoid.solenoidClosedAt) > SOLENOID_CLOSE_DELAY) {
        digitalWrite(PIN_SOLENOID_SIGNAL, LOW);
        solenoid.isOpen = false;
      }
    }
  }
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
    drawCellInfo(1);
    
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
  preferences.putFloat("cell2", cellCalibration[1].value);
}
void restoreCalibration() {
  cellCalibration[0].value = preferences.getFloat("cell1", 0);
  cellCalibration[1].value = preferences.getFloat("cell2", 0);
}

float readOxygenCellVoltage() {
  float value = ads1115.readADC_Differential_0_1() * gain;
  float value2 = ads1115.readADC_Differential_2_3() * gain;

  cellReadings[0].addValue(value);
  cellReadings[1].addValue(value2);

  return value;
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
    bool isDisabled = sensorValue[index].isValid() == false;

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
