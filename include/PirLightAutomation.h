#pragma once

#include <Arduino.h>

#include "RelayBank.h"

using AutomationStateHandler =
    void (*)(size_t channelIndex, bool state, const char* source,
             const char* commandId);

class PirLightAutomation {
 public:
  void begin(RelayBank& relayBank, AutomationStateHandler stateHandler);
  void loop();
  void handleExternalCommand(size_t channelIndex, const char* source);

 private:
  RelayBank* relayBank_ = nullptr;
  AutomationStateHandler stateHandler_ = nullptr;
  int targetChannelIndex_ = -1;
  uint32_t startedAtMs_ = 0;
  uint32_t lastPollMs_ = 0;
  uint32_t lastMotionMs_ = 0;
  bool autoOffArmed_ = false;
  bool suppressedUntilClear_ = false;
};
