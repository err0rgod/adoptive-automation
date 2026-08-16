#include "LocalMqtt.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <cstring>

#include "AppConfig.h"

LocalMqtt* LocalMqtt::instance_ = nullptr;

namespace {

String deviceTopic(const char* suffix) {
  String topic(app_config::kMqttBaseTopic);
  topic += "/devices/";
  topic += app_config::kDeviceId;
  topic += suffix;
  return topic;
}

String channelTopic(size_t channelIndex, const char* suffix) {
  String topic = deviceTopic("/channels/");
  topic += kChannelDefinitions[channelIndex].id;
  topic += suffix;
  return topic;
}

}  // namespace

void LocalMqtt::begin(MqttCommandHandler commandHandler) {
  instance_ = this;
  commandHandler_ = commandHandler;
  client_.setCallback(staticMessageCallback);
  client_.setBufferSize(768);
}

void LocalMqtt::loop() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  connectIfNeeded();
  if (!client_.connected()) {
    return;
  }

  client_.loop();
  const uint32_t now = millis();
  if (now - lastHeartbeatMs_ >= app_config::kMqttHeartbeatIntervalMs) {
    publishHeartbeat();
    lastHeartbeatMs_ = now;
  }
}

bool LocalMqtt::connected() { return client_.connected(); }

void LocalMqtt::connectIfNeeded() {
  if (client_.connected()) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastConnectAttemptMs_ < app_config::kMqttReconnectIntervalMs) {
    return;
  }
  lastConnectAttemptMs_ = now;

  if (!brokerDiscovered_) {
    brokerDiscovered_ = discoverBroker();
  }

  String clientId = String("adoptive-") + app_config::kDeviceId + "-" +
                    String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
  const String availabilityTopic = deviceTopic("/availability");

  Serial.printf("Connecting to local MQTT as %s...\n", clientId.c_str());
  const bool connected = client_.connect(
      clientId.c_str(), availabilityTopic.c_str(), 1, true, "offline");
  if (!connected) {
    Serial.printf("Local MQTT connection failed, state=%d\n", client_.state());
    return;
  }

  Serial.println("Local MQTT connected");
  publishAvailability("online");
  subscribeToCommands();
  publishHeartbeat();
}

bool LocalMqtt::discoverBroker() {
  if (!mdnsStarted_) {
    mdnsStarted_ = MDNS.begin(app_config::kDeviceId);
    if (!mdnsStarted_) {
      Serial.println("mDNS responder could not start; using MQTT fallback");
    }
  }

  if (mdnsStarted_) {
    const int count = MDNS.queryService("mqtt", "tcp");
    if (count > 0) {
      brokerAddress_ = MDNS.IP(0);
      brokerPort_ = MDNS.port(0);
      client_.setServer(brokerAddress_, brokerPort_);
      Serial.printf("Discovered MQTT broker at %s:%u\n",
                    brokerAddress_.toString().c_str(), brokerPort_);
      return true;
    }
  }

  client_.setServer(app_config::kMqttFallbackHost,
                    app_config::kMqttFallbackPort);
  Serial.printf("Using MQTT fallback %s:%u\n", app_config::kMqttFallbackHost,
                app_config::kMqttFallbackPort);
  // Keep checking mDNS on later reconnect attempts. This lets the gateway be
  // started after the ESP32 without requiring a controller reboot.
  return false;
}

void LocalMqtt::subscribeToCommands() {
  for (size_t index = 0; index < kChannelCount; ++index) {
    const String topic = channelTopic(index, "/set");
    client_.subscribe(topic.c_str(), 1);
  }
}

void LocalMqtt::publishChannelState(size_t channelIndex, bool state,
                                    const char* source,
                                    const char* commandId) {
  if (!client_.connected() || channelIndex >= kChannelCount) {
    return;
  }

  JsonDocument document;
  document["state"] = state;
  document["source"] = source != nullptr ? source : "unknown";
  document["command_id"] = commandId != nullptr ? commandId : "";
  document["uptime_ms"] = millis();

  char payload[256];
  serializeJson(document, payload, sizeof(payload));
  const String topic = channelTopic(channelIndex, "/state");
  client_.publish(topic.c_str(), payload, true);
}

void LocalMqtt::publishSensorState(const SensorSnapshot& snapshot) {
  if (!client_.connected()) {
    return;
  }

  JsonDocument document;
  JsonObject dht11 = document["dht11"].to<JsonObject>();
  dht11["available"] = snapshot.dht11Available;
  if (snapshot.dht11Available) {
    dht11["temperature_c"] = snapshot.temperatureC;
    dht11["humidity_percent"] = snapshot.humidityPercent;
  } else {
    dht11["temperature_c"] = nullptr;
    dht11["humidity_percent"] = nullptr;
  }

  JsonObject mmWave = document["mmwave"].to<JsonObject>();
  mmWave["available"] = snapshot.mmWaveAvailable;
  if (snapshot.mmWaveAvailable) {
    mmWave["presence"] = snapshot.presenceDetected;
  } else {
    mmWave["presence"] = nullptr;
  }
  document["uptime_ms"] = millis();

  char payload[384];
  serializeJson(document, payload, sizeof(payload));
  const String topic = deviceTopic("/sensors/state");
  client_.publish(topic.c_str(), payload, true);
}

void LocalMqtt::publishAvailability(const char* status) {
  const String topic = deviceTopic("/availability");
  client_.publish(topic.c_str(), status, true);
}

void LocalMqtt::publishHeartbeat() {
  JsonDocument document;
  document["uptime_ms"] = millis();
  document["wifi_rssi"] = WiFi.RSSI();
  document["free_heap"] = ESP.getFreeHeap();
  document["firmware"] = "0.1.0";

  char payload[256];
  serializeJson(document, payload, sizeof(payload));
  const String topic = deviceTopic("/heartbeat");
  client_.publish(topic.c_str(), payload, false);
}

void LocalMqtt::staticMessageCallback(char* topic, uint8_t* payload,
                                      unsigned int length) {
  if (instance_ != nullptr) {
    instance_->onMessage(topic, payload, length);
  }
}

void LocalMqtt::onMessage(char* topic, uint8_t* payload,
                          unsigned int length) {
  if (commandHandler_ == nullptr || length == 0 || length >= 512) {
    return;
  }

  char body[512];
  std::memcpy(body, payload, length);
  body[length] = '\0';

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, body);
  if (error || !document["state"].is<bool>()) {
    Serial.printf("Rejected invalid MQTT command on %s\n", topic);
    return;
  }

  const String topicString(topic);
  for (size_t index = 0; index < kChannelCount; ++index) {
    if (topicString == channelTopic(index, "/set")) {
      const bool state = document["state"].as<bool>();
      const char* source = document["source"] | "dashboard";
      const char* commandId = document["command_id"] | "";
      commandHandler_(index, state, source, commandId);
      return;
    }
  }
}
