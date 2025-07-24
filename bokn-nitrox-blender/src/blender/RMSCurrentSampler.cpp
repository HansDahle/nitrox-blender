#include "RMSCurrentSampler.h"

RMSCurrentSampler::RMSCurrentSampler(Adafruit_ADS1115* ads, float factor, unsigned long sampleWindow)
  : ads(ads), factor(factor), sampleWindow(sampleWindow) {
  begin();
}

void RMSCurrentSampler::begin() {
  startTime = millis();
  sum = 0;
  counter = 0;
  voltageSensorSum = 0;
}

bool RMSCurrentSampler::update() {
  if (millis() - startTime < sampleWindow) {
    float voltage = ads->computeVolts(ads->readADC_Differential_2_3());
    float current = voltage * factor;
    sum += sq(current);
    voltageSensorSum += sq(voltage);
    counter++;
    
    return false;
  } else {
    if (counter > 0) {
      latestRMS = sqrt(sum / counter);
      sensorVoltageRMS = sqrt(voltageSensorSum / counter);
    } else {
      latestRMS = 0;
      sensorVoltageRMS = 0;
    }
    begin();
    return true;
  }
}

float RMSCurrentSampler::getRMS() const {
  return latestRMS;
}

float RMSCurrentSampler::getSensorVoltageRMS() const {
  return sensorVoltageRMS;
}