#include "RelayBank.h"

#include <cstring>

namespace {

uint8_t outputLevel(const ChannelDefinition& channel, bool state) {
  return state ? channel.activeLevel : !channel.activeLevel;
}

}  // namespace

void RelayBank::begin() {
  for (size_t index = 0; index < kChannelCount; ++index) {
    const auto& channel = kChannelDefinitions[index];
    states_[index] = false;

    // Set the safe inactive level before enabling the output driver. This
    // avoids the active-low startup pulse in the legacy sketch.
    digitalWrite(channel.pin, outputLevel(channel, false));
    pinMode(channel.pin, OUTPUT);
    digitalWrite(channel.pin, outputLevel(channel, false));
  }
}

bool RelayBank::setState(size_t channelIndex, bool state) {
  if (channelIndex >= kChannelCount) {
    return false;
  }

  const auto& channel = kChannelDefinitions[channelIndex];
  digitalWrite(channel.pin, outputLevel(channel, state));
  states_[channelIndex] = state;
  return true;
}

bool RelayBank::state(size_t channelIndex) const {
  return channelIndex < kChannelCount ? states_[channelIndex] : false;
}

int RelayBank::findChannel(const char* channelId) const {
  if (channelId == nullptr) {
    return -1;
  }
  for (size_t index = 0; index < kChannelCount; ++index) {
    if (std::strcmp(kChannelDefinitions[index].id, channelId) == 0) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

