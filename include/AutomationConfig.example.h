#pragma once

#include <Arduino.h>

// Copy this file to AutomationConfig.h, then edit the copy. The local file is
// ignored by Git so each controller can have its own automation settings.
namespace automation_config {

inline constexpr bool kPirLightEnabled = false;
inline constexpr uint8_t kPirPin = 32;
inline constexpr uint8_t kPirActiveLevel = HIGH;
inline constexpr char kPirTargetChannelId[] = "light-1";
inline constexpr uint32_t kPirOnDurationMs = 2UL * 60UL * 1000UL;
inline constexpr uint32_t kPirWarmupMs = 60UL * 1000UL;
inline constexpr uint32_t kPirPollIntervalMs = 50;

}  // namespace automation_config
