#pragma once

#include <Arduino.h>
#include <DHT.h>

#include "SensorConfig.h"

struct SensorSnapshot {
  bool dht11Available = false;
  float temperatureC = 0.0F;
  float humidityPercent = 0.0F;
  bool mmWaveAvailable = false;
  bool presenceDetected = false;
  uint32_t updatedAtMs = 0;
};

class SensorTelemetry {
 public:
  void begin();
  bool poll();
  const SensorSnapshot& snapshot() const;

 private:
  bool pollDht11(uint32_t now);
  bool pollMmWave(uint32_t now);

  DHT dht11_{sensor_config::kDht11Pin, DHT11};
  SensorSnapshot snapshot_{};
  uint32_t lastDht11ReadMs_ = 0;
  uint32_t lastMmWaveReadMs_ = 0;
  uint8_t consecutiveDhtFailures_ = 0;
};
