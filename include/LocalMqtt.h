#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "ChannelConfig.h"
#include "SensorTelemetry.h"

using MqttCommandHandler =
    void (*)(size_t channelIndex, bool state, const char* source,
             const char* commandId);

class LocalMqtt {
 public:
  void begin(MqttCommandHandler commandHandler);
  void loop();
  void publishChannelState(size_t channelIndex, bool state,
                           const char* source, const char* commandId = "");
  void publishSensorState(const SensorSnapshot& snapshot);
  bool connected();

 private:
  void connectIfNeeded();
  bool discoverBroker();
  void onMessage(char* topic, uint8_t* payload, unsigned int length);
  void publishAvailability(const char* status);
  void publishHeartbeat();
  void subscribeToCommands();

  static void staticMessageCallback(char* topic, uint8_t* payload,
                                    unsigned int length);
  static LocalMqtt* instance_;

  WiFiClient wifiClient_;
  PubSubClient client_{wifiClient_};
  MqttCommandHandler commandHandler_ = nullptr;
  IPAddress brokerAddress_;
  uint16_t brokerPort_ = 1883;
  bool brokerDiscovered_ = false;
  bool mdnsStarted_ = false;
  uint32_t lastConnectAttemptMs_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
};
