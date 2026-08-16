#include <Arduino.h>
#include <RMaker.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <cstring>

#include "AppConfig.h"
#include "ChannelConfig.h"
#include "LocalMqtt.h"
#include "PirLightAutomation.h"
#include "RelayBank.h"
#include "SensorTelemetry.h"

#if __has_include("WifiSecrets.h")
#include "WifiSecrets.h"
#else
namespace wifi_config {
inline constexpr bool kUseFixedCredentials = false;
inline constexpr char kSsid[] = "";
inline constexpr char kPassword[] = "";
}  // namespace wifi_config
#endif

static_assert(!wifi_config::kUseFixedCredentials || wifi_config::kSsid[0] != '\0',
              "Set a Wi-Fi SSID when fixed credentials are enabled");

namespace {

RelayBank relayBank;
LocalMqtt localMqtt;
SensorTelemetry sensorTelemetry;
PirLightAutomation pirLightAutomation;
Device* rainMakerDevices[kChannelCount]{};
Device* rainMakerSensorDevice = nullptr;
char provisioningName[20]{};
constexpr uint8_t kResetButtonPin = 0;

void reportAllStatesToMqtt() {
  for (size_t index = 0; index < kChannelCount; ++index) {
    localMqtt.publishChannelState(index, relayBank.state(index), "boot");
  }
  localMqtt.publishSensorState(sensorTelemetry.snapshot());
}

void reportSensorStateToRainMaker(const SensorSnapshot& snapshot) {
  if (rainMakerSensorDevice == nullptr) {
    return;
  }

  char temperature[16] = "N/A";
  char humidity[16] = "N/A";
  const char* presence = "N/A";
  if (snapshot.dht11Available) {
    std::snprintf(temperature, sizeof(temperature), "%.1f C",
                  snapshot.temperatureC);
    std::snprintf(humidity, sizeof(humidity), "%.1f %%",
                  snapshot.humidityPercent);
  }
  if (snapshot.mmWaveAvailable) {
    presence = snapshot.presenceDetected ? "Detected" : "Clear";
  }

  rainMakerSensorDevice->updateAndReportParam("Temperature", temperature);
  rainMakerSensorDevice->updateAndReportParam("Humidity", humidity);
  rainMakerSensorDevice->updateAndReportParam("Presence", presence);
}

void reportStateToRainMaker(size_t channelIndex, bool state) {
  if (channelIndex < kChannelCount && rainMakerDevices[channelIndex] != nullptr) {
    rainMakerDevices[channelIndex]->updateAndReportParam(
        ESP_RMAKER_DEF_POWER_NAME, state);
  }
}

void applyChannelState(size_t channelIndex, bool state, const char* source,
                       const char* commandId) {
  if (channelIndex >= kChannelCount) {
    return;
  }

  pirLightAutomation.handleExternalCommand(channelIndex, source);
  if (!relayBank.setState(channelIndex, state)) {
    return;
  }

  const auto& channel = kChannelDefinitions[channelIndex];
  Serial.printf("Channel %s -> %s (source=%s, command=%s)\n", channel.id,
                state ? "ON" : "OFF", source, commandId);

  reportStateToRainMaker(channelIndex, state);
  localMqtt.publishChannelState(channelIndex, state, source, commandId);
}

void onMqttCommand(size_t channelIndex, bool state, const char* source,
                   const char* commandId) {
  applyChannelState(channelIndex, state, source, commandId);
}

void onMqttPirTest() { pirLightAutomation.triggerTestMotion(); }

void onRainMakerWrite(Device*, Param* param, const param_val_t value,
                      void* privateData, write_ctx_t*) {
  if (param == nullptr || privateData == nullptr ||
      std::strcmp(param->getParamName(), ESP_RMAKER_DEF_POWER_NAME) != 0) {
    return;
  }

  const size_t channelIndex =
      static_cast<size_t>(*static_cast<uint8_t*>(privateData));
  applyChannelState(channelIndex, value.val.b, "rainmaker", "");
}

void initializeRainMakerDevices(Node& node) {
  static uint8_t channelIndexes[kChannelCount]{};

  for (size_t index = 0; index < kChannelCount; ++index) {
    channelIndexes[index] = static_cast<uint8_t>(index);
    const auto& channel = kChannelDefinitions[index];
    const char* deviceType = channel.kind == ChannelKind::Light
                                 ? ESP_RMAKER_DEVICE_LIGHTBULB
                                 : ESP_RMAKER_DEVICE_FAN;

    auto* device =
        new Device(channel.displayName, deviceType, &channelIndexes[index]);
    if (device == nullptr || device->getDeviceHandle() == nullptr) {
      Serial.printf("Failed to create RainMaker device %s\n", channel.id);
      continue;
    }

    device->addNameParam();
    device->addPowerParam(false);
    device->assignPrimaryParam(
        device->getParamByName(ESP_RMAKER_DEF_POWER_NAME));
    device->addCb(onRainMakerWrite);
    node.addDevice(*device);
    rainMakerDevices[index] = device;
  }
}

void initializeRainMakerSensorDevice(Node& node) {
  auto* device =
      new Device("Room Sensors", "adoptive.device.room-sensors", nullptr);
  if (device == nullptr || device->getDeviceHandle() == nullptr) {
    Serial.println("Failed to create RainMaker sensor device");
    return;
  }

  device->addNameParam();
  device->addParam(Param("Temperature", "adoptive.param.temperature",
                         value("N/A"), PROP_FLAG_READ));
  device->addParam(Param("Humidity", "adoptive.param.humidity", value("N/A"),
                         PROP_FLAG_READ));
  device->addParam(Param("Presence", "adoptive.param.presence", value("N/A"),
                         PROP_FLAG_READ));
  node.addDevice(*device);
  rainMakerSensorDevice = device;
}

void onSystemEvent(arduino_event_t* event) {
  switch (event->event_id) {
    case ARDUINO_EVENT_PROV_START:
      Serial.printf("RainMaker SoftAP provisioning started: %s\n",
                    provisioningName);
      printQR(provisioningName, app_config::kRainMakerPop, "softap");
      break;
    case ARDUINO_EVENT_PROV_INIT:
      wifi_prov_mgr_disable_auto_stop(10000);
      break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      Serial.println("Wi-Fi provisioning succeeded");
      wifi_prov_mgr_stop_provisioning();
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("Wi-Fi disconnected");
      break;
    default:
      break;
  }
}

void initializeProvisioningName() {
  const uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac());
  std::snprintf(provisioningName, sizeof(provisioningName), "ADOPT_%06lX",
                static_cast<unsigned long>(suffix & 0xFFFFFF));
}

void startWifi() {
  if constexpr (wifi_config::kUseFixedCredentials) {
    // Credentials are deliberately not printed. They come from the local,
    // Git-ignored WifiSecrets.h and are reapplied on every boot.
    Serial.println("Connecting with fixed Wi-Fi credentials");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(wifi_config::kSsid, wifi_config::kPassword);
    return;
  }

  WiFiProv.beginProvision(WIFI_PROV_SCHEME_SOFTAP,
                          WIFI_PROV_SCHEME_HANDLER_NONE,
                          WIFI_PROV_SECURITY_1, app_config::kRainMakerPop,
                          provisioningName);
}

void serviceResetButton() {
  static bool wasPressed = false;
  static uint32_t pressedAtMs = 0;
  const bool pressed = digitalRead(kResetButtonPin) == LOW;

  if (pressed && !wasPressed) {
    pressedAtMs = millis();
  } else if (!pressed && wasPressed) {
    const uint32_t heldMs = millis() - pressedAtMs;
    if (heldMs >= 10000) {
      Serial.println("RainMaker factory reset requested");
      RMakerFactoryReset(2);
    } else if (heldMs >= 3000) {
      Serial.println("Wi-Fi reset requested");
      RMakerWiFiReset(2);
    }
  }
  wasPressed = pressed;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nStarting %s\n", app_config::kProjectName);

  relayBank.begin();
  pirLightAutomation.begin(relayBank, applyChannelState);
  sensorTelemetry.begin();
  pinMode(kResetButtonPin, INPUT_PULLUP);
  localMqtt.begin(onMqttCommand, onMqttPirTest);
  initializeProvisioningName();

  Node node = RMaker.initNode(app_config::kNodeName);
  initializeRainMakerDevices(node);
  initializeRainMakerSensorDevice(node);
  // OTA, schedules, timezone, scenes, and remote system services are omitted
  // from MVP 1 to retain flash headroom on the 4 MB WROOM-32U module.
  RMaker.start();

  WiFi.onEvent(onSystemEvent);
  startWifi();
}

void loop() {
  static bool mqttWasConnected = false;
  localMqtt.loop();

  const bool mqttIsConnected = localMqtt.connected();
  if (mqttIsConnected && !mqttWasConnected) {
    reportAllStatesToMqtt();
  }
  mqttWasConnected = mqttIsConnected;

  if (sensorTelemetry.poll()) {
    const auto& snapshot = sensorTelemetry.snapshot();
    reportSensorStateToRainMaker(snapshot);
    localMqtt.publishSensorState(snapshot);
  }

  serviceResetButton();
  pirLightAutomation.loop();

  delay(5);
}
