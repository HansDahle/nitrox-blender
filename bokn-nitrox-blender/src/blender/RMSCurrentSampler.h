#ifndef RMS_CURRENT_SAMPLER_H
#define RMS_CURRENT_SAMPLER_H

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

class RMSCurrentSampler {
public:
  RMSCurrentSampler(Adafruit_ADS1115* ads, float factor, unsigned long sampleWindow = 1000);

  void begin();
  bool update();
  float getRMS() const;
  float getSensorVoltageRMS() const;

private:
  Adafruit_ADS1115* ads;
  float factor;
  unsigned long sampleWindow;

  unsigned long startTime;
  float sum;
  int counter;
  float latestRMS;
  float voltageSensorSum;
  float sensorVoltageRMS;
};

#endif