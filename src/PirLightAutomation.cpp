#include "PirLightAutomation.h"

#include <cstring>

#if __has_include("AutomationConfig.h")
#include "AutomationConfig.h"
#else
namespace automation_config {
inline constexpr bool kPirLightEnabled = false;
inline constexpr bool kPirDashboardTestEnabled = true;
inline constexpr uint8_t kPirPin = 32;
inline constexpr uint8_t kPirActiveLevel = HIGH;
inline constexpr char kPirTargetChannelId[] = "light-1";
inline constexpr uint32_t kPirOnDurationMs = 2UL * 60UL * 1000UL;
inline constexpr uint32_t kPirWarmupMs = 60UL * 1000UL;
inline constexpr uint32_t kPirPollIntervalMs = 50;
}  // namespace automation_config
#endif

static_assert(automation_config::kPirOnDurationMs > 0,
              "PIR ON duration must be greater than zero");
static_assert(automation_config::kPirPollIntervalMs > 0,
              "PIR polling interval must be greater than zero");

void PirLightAutomation::begin(RelayBank& relayBank,
                               AutomationStateHandler stateHandler) {
  relayBank_ = &relayBank;
  stateHandler_ = stateHandler;
  startedAtMs_ = millis();

  if constexpr (!automation_config::kPirLightEnabled &&
                !automation_config::kPirDashboardTestEnabled) {
    Serial.println("PIR light automation and dashboard test disabled");
    return;
  }

  targetChannelIndex_ =
      relayBank.findChannel(automation_config::kPirTargetChannelId);
  if (targetChannelIndex_ < 0) {
    Serial.printf("PIR target channel not found: %s\n",
                  automation_config::kPirTargetChannelId);
    return;
  }

  if constexpr (automation_config::kPirLightEnabled) {
    pinMode(automation_config::kPirPin, INPUT_PULLDOWN);
  }
  Serial.printf("PIR target ready: %s for %lu ms (sensor=%s, test=%s)\n",
                automation_config::kPirTargetChannelId,
                static_cast<unsigned long>(
                    automation_config::kPirOnDurationMs),
                automation_config::kPirLightEnabled ? "enabled" : "disabled",
                automation_config::kPirDashboardTestEnabled ? "enabled"
                                                            : "disabled");
}

void PirLightAutomation::loop() {
  if (relayBank_ == nullptr || stateHandler_ == nullptr ||
      targetChannelIndex_ < 0) {
    return;
  }

  const uint32_t now = millis();
  if constexpr (automation_config::kPirLightEnabled) {
    if (now - lastPollMs_ >= automation_config::kPirPollIntervalMs) {
      lastPollMs_ = now;

      const bool motion = digitalRead(automation_config::kPirPin) ==
                          automation_config::kPirActiveLevel;
      if (now - startedAtMs_ >= automation_config::kPirWarmupMs) {
        if (!motion) {
          suppressedUntilClear_ = false;
        } else if (!suppressedUntilClear_) {
          handleMotion(now);
        }
      }
    }
  }

  // Dashboard test events use the same ownership and timer state even when no
  // physical PIR is configured, so auto-off must always be serviced.
  if (autoOffArmed_ &&
      now - lastMotionMs_ >= automation_config::kPirOnDurationMs) {
    autoOffArmed_ = false;
    stateHandler_(static_cast<size_t>(targetChannelIndex_), false, "pir", "");
  }
}

void PirLightAutomation::triggerTestMotion() {
  if constexpr (!automation_config::kPirDashboardTestEnabled) {
    Serial.println("Ignored dashboard PIR test because it is disabled");
    return;
  }

  if (relayBank_ == nullptr || stateHandler_ == nullptr ||
      targetChannelIndex_ < 0) {
    Serial.println("Ignored dashboard PIR test because target is unavailable");
    return;
  }

  Serial.println("Dashboard requested PIR test motion");
  suppressedUntilClear_ = false;
  handleMotion(millis());
}

void PirLightAutomation::handleMotion(uint32_t now) {
  if (autoOffArmed_) {
    lastMotionMs_ = now;
  } else if (!relayBank_->state(static_cast<size_t>(targetChannelIndex_))) {
    stateHandler_(static_cast<size_t>(targetChannelIndex_), true, "pir", "");
    autoOffArmed_ = true;
    lastMotionMs_ = now;
  }
}

void PirLightAutomation::handleExternalCommand(size_t channelIndex,
                                               const char* source) {
  if constexpr (!automation_config::kPirLightEnabled &&
                !automation_config::kPirDashboardTestEnabled) {
    return;
  }

  if (!autoOffArmed_ || targetChannelIndex_ < 0 ||
      channelIndex != static_cast<size_t>(targetChannelIndex_) ||
      (source != nullptr && std::strcmp(source, "pir") == 0)) {
    return;
  }

  // A user or another control path takes ownership immediately. If the PIR is
  // still HIGH, wait for it to clear before accepting a new motion event.
  autoOffArmed_ = false;
  suppressedUntilClear_ = true;
}
