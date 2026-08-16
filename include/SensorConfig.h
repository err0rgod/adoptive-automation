#pragma once

#include <Arduino.h>

namespace sensor_config {

// DHT11 probing is safe when the sensor is absent: failed reads are reported
// as unavailable. Wire DATA to GPIO 27 with a 4.7-10 kOhm pull-up to 3.3 V.
inline constexpr bool kDht11Enabled = true;
inline constexpr uint8_t kDht11Pin = 27;
inline constexpr uint32_t kDht11IntervalMs = 2500;
inline constexpr uint8_t kDhtFailureLimit = 3;

// This integration expects a 3.3 V-safe digital presence output, not UART.
// A digital output cannot identify whether its module is physically connected,
// so enable this only after wiring and confirming the module's output voltage.
inline constexpr bool kMmWaveEnabled = false;
inline constexpr uint8_t kMmWavePin = 33;
inline constexpr uint8_t kMmWaveActiveLevel = HIGH;
inline constexpr uint32_t kMmWaveIntervalMs = 100;

}  // namespace sensor_config
