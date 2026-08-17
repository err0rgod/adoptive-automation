#pragma once

#include <Arduino.h>

#ifndef RELAY_ACTIVE_LEVEL
#define RELAY_ACTIVE_LEVEL 0
#endif

enum class ChannelKind : uint8_t {
  Light,
  Socket,
  AirConditioner,
  Fan,
};

struct ChannelDefinition {
  const char* id;
  const char* displayName;
  ChannelKind kind;
  uint8_t pin;
  uint8_t activeLevel;
};

// This is the only firmware file that should be edited when the relay wiring
// changes. These pins avoid the classic ESP32 flash pins and common boot straps.
// Confirm them against the labels on the actual carrier board before wiring.
inline constexpr ChannelDefinition kChannelDefinitions[] = {
    {"light-1", "Light 1", ChannelKind::Light, 13, RELAY_ACTIVE_LEVEL},
    {"light-2", "Light 2", ChannelKind::Light, 14, RELAY_ACTIVE_LEVEL},
    {"light-3", "Light 3", ChannelKind::Light, 16, RELAY_ACTIVE_LEVEL},
    {"socket-1", "Socket 1", ChannelKind::Socket, 17, RELAY_ACTIVE_LEVEL},
    {"socket-2", "Socket 2", ChannelKind::Socket, 18, RELAY_ACTIVE_LEVEL},
    {"ac-1", "AC", ChannelKind::AirConditioner, 19, RELAY_ACTIVE_LEVEL},
    {"fan-1", "Fan 1", ChannelKind::Fan, 23, RELAY_ACTIVE_LEVEL},
    {"fan-2", "Fan 2", ChannelKind::Fan, 25, RELAY_ACTIVE_LEVEL},
};

inline constexpr size_t kChannelCount =
    sizeof(kChannelDefinitions) / sizeof(kChannelDefinitions[0]);
static_assert(kChannelCount == 8, "The MVP requires exactly eight channels");
