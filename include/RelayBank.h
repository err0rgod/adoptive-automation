#pragma once

#include <Arduino.h>

#include "ChannelConfig.h"

class RelayBank {
 public:
  void begin();
  bool setState(size_t channelIndex, bool state);
  bool state(size_t channelIndex) const;
  int findChannel(const char* channelId) const;

 private:
  bool states_[kChannelCount]{};
};

