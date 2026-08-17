#include "SensorTelemetry.h"

#include <cmath>

namespace {

float oneDecimal(float value) { return std::round(value * 10.0F) / 10.0F; }

bool differs(float left, float right) {
  return std::fabs(left - right) >= 0.1F;
}

}  // namespace

void SensorTelemetry::begin() {
  if constexpr (sensor_config::kDht11Enabled) {
    dht11_.begin();
    // Permit an immediate first sample so RainMaker can advertise only valid
    // climate values while constructing its node configuration.
    lastDht11ReadMs_ = millis() - sensor_config::kDht11IntervalMs;
  }

  if constexpr (sensor_config::kMmWaveEnabled) {
    pinMode(sensor_config::kMmWavePin, INPUT_PULLDOWN);
    snapshot_.mmWaveAvailable = true;
    snapshot_.presenceDetected =
        digitalRead(sensor_config::kMmWavePin) ==
        sensor_config::kMmWaveActiveLevel;
    lastMmWaveReadMs_ = millis() - sensor_config::kMmWaveIntervalMs;
  }
}

bool SensorTelemetry::poll() {
  const uint32_t now = millis();
  const bool changed = pollDht11(now) | pollMmWave(now);
  if (changed) {
    snapshot_.updatedAtMs = now;
  }
  return changed;
}

const SensorSnapshot& SensorTelemetry::snapshot() const { return snapshot_; }

bool SensorTelemetry::pollDht11(uint32_t now) {
  if constexpr (!sensor_config::kDht11Enabled) {
    return false;
  }

  if (now - lastDht11ReadMs_ < sensor_config::kDht11IntervalMs) {
    return false;
  }
  lastDht11ReadMs_ = now;

  const float humidity = dht11_.readHumidity();
  const float temperature = dht11_.readTemperature();
  if (std::isnan(humidity) || std::isnan(temperature)) {
    if (consecutiveDhtFailures_ < UINT8_MAX) {
      ++consecutiveDhtFailures_;
    }
    if (snapshot_.dht11Available &&
        consecutiveDhtFailures_ >= sensor_config::kDhtFailureLimit) {
      snapshot_.dht11Available = false;
      return true;
    }
    return false;
  }

  consecutiveDhtFailures_ = 0;
  const float roundedHumidity = oneDecimal(humidity);
  const float roundedTemperature = oneDecimal(temperature);
  const bool changed = !snapshot_.dht11Available ||
                       differs(snapshot_.humidityPercent, roundedHumidity) ||
                       differs(snapshot_.temperatureC, roundedTemperature);
  snapshot_.dht11Available = true;
  snapshot_.humidityPercent = roundedHumidity;
  snapshot_.temperatureC = roundedTemperature;
  return changed;
}

bool SensorTelemetry::pollMmWave(uint32_t now) {
  if constexpr (!sensor_config::kMmWaveEnabled) {
    return false;
  }

  if (now - lastMmWaveReadMs_ < sensor_config::kMmWaveIntervalMs) {
    return false;
  }
  lastMmWaveReadMs_ = now;

  const bool presence = digitalRead(sensor_config::kMmWavePin) ==
                        sensor_config::kMmWaveActiveLevel;
  const bool changed = !snapshot_.mmWaveAvailable ||
                       presence != snapshot_.presenceDetected;
  snapshot_.mmWaveAvailable = true;
  snapshot_.presenceDetected = presence;
  return changed;
}
