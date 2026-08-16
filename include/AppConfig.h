#pragma once

#include <Arduino.h>

namespace app_config {

inline constexpr char kProjectName[] = "Adoptive Automation";
inline constexpr char kNodeName[] = "Adoptive Automation Room";
inline constexpr char kDeviceId[] = "room-controller-01";

// The gateway advertises its broker through mDNS. This address is only the
// fallback when mDNS discovery is unavailable. Change it to the gateway PC's
// LAN address before hardware testing.
inline constexpr char kMqttFallbackHost[] = "10.144.6.247";
inline constexpr uint16_t kMqttFallbackPort = 1884;
inline constexpr char kMqttBaseTopic[] = "adoptive/v1";

inline constexpr uint32_t kMqttReconnectIntervalMs = 5000;
inline constexpr uint32_t kMqttHeartbeatIntervalMs = 30000;

// Prototype-only RainMaker Proof of Possession. Change before enrolling a
// device that is not a bench prototype.
inline constexpr char kRainMakerPop[] = "adopt123";

}  // namespace app_config
