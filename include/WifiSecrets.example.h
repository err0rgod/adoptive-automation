#pragma once

// Copy this file to WifiSecrets.h, then edit the copy. WifiSecrets.h is
// intentionally ignored by Git so that LAN credentials are never committed.
namespace wifi_config {

inline constexpr bool kUseFixedCredentials = false;
inline constexpr char kSsid[] = "";
inline constexpr char kPassword[] = "";

}  // namespace wifi_config
