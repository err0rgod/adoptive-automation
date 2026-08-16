from __future__ import annotations

import json
import unittest
from types import SimpleNamespace
from unittest.mock import Mock

import paho.mqtt.client as mqtt

from gateway.app.config import CHANNELS, settings
from gateway.app.mqtt_service import MqttService


async def ignore_broadcast(_message):
    return None


class MqttServiceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.service = MqttService(settings, ignore_broadcast)

    def test_snapshot_contains_all_eight_channels(self) -> None:
        snapshot = self.service.snapshot()
        self.assertEqual(len(snapshot["channels"]), 8)
        self.assertEqual(set(snapshot["channels"]), {item.id for item in CHANNELS})

    def test_sensor_snapshot_defaults_to_unavailable(self) -> None:
        sensors = self.service.snapshot()["sensors"]
        self.assertIs(sensors["dht11"]["available"], False)
        self.assertIsNone(sensors["dht11"]["temperature_c"])
        self.assertIsNone(sensors["dht11"]["humidity_percent"])
        self.assertIs(sensors["mmwave"]["available"], False)
        self.assertIsNone(sensors["mmwave"]["presence"])

    def test_sensor_state_message_updates_snapshot(self) -> None:
        message = SimpleNamespace(
            topic=f"{settings.device_topic}/sensors/state",
            payload=json.dumps(
                {
                    "dht11": {
                        "available": True,
                        "temperature_c": 24.5,
                        "humidity_percent": 61.0,
                    },
                    "mmwave": {"available": True, "presence": True},
                    "uptime_ms": 4321,
                }
            ).encode(),
        )

        self.service._on_message(self.service.client, None, message)

        sensors = self.service.snapshot()["sensors"]
        self.assertEqual(sensors["dht11"]["temperature_c"], 24.5)
        self.assertEqual(sensors["dht11"]["humidity_percent"], 61.0)
        self.assertIs(sensors["mmwave"]["presence"], True)
        self.assertEqual(sensors["uptime_ms"], 4321)

    def test_malformed_sensor_state_is_ignored(self) -> None:
        before = self.service.snapshot()["sensors"]
        message = SimpleNamespace(
            topic=f"{settings.device_topic}/sensors/state",
            payload=json.dumps(
                {
                    "dht11": {
                        "available": True,
                        "temperature_c": "hot",
                        "humidity_percent": 50,
                    },
                    "mmwave": {"available": False, "presence": None},
                }
            ).encode(),
        )

        self.service._on_message(self.service.client, None, message)

        self.assertEqual(self.service.snapshot()["sensors"], before)

    def test_channel_state_message_updates_authoritative_snapshot(self) -> None:
        topic = f"{settings.device_topic}/channels/light-1/state"
        message = SimpleNamespace(
            topic=topic,
            payload=json.dumps(
                {
                    "state": True,
                    "source": "rainmaker",
                    "command_id": "command-1",
                    "uptime_ms": 1234,
                }
            ).encode(),
        )

        self.service._on_message(self.service.client, None, message)

        state = self.service.snapshot()["channels"]["light-1"]
        self.assertIs(state["state"], True)
        self.assertEqual(state["source"], "rainmaker")
        self.assertEqual(state["command_id"], "command-1")

    def test_dashboard_command_has_source_and_unique_id(self) -> None:
        self.service.connected = True
        publish_result = SimpleNamespace(rc=mqtt.MQTT_ERR_SUCCESS)
        self.service.client.publish = Mock(return_value=publish_result)

        command_id = self.service.set_channel("fan-1", True)

        topic, payload = self.service.client.publish.call_args.args[:2]
        body = json.loads(payload)
        self.assertEqual(topic, f"{settings.device_topic}/channels/fan-1/set")
        self.assertIs(body["state"], True)
        self.assertEqual(body["source"], "dashboard")
        self.assertEqual(body["command_id"], command_id)
        self.assertTrue(command_id)

    def test_pir_test_command_has_source_and_unique_id(self) -> None:
        self.service.connected = True
        publish_result = SimpleNamespace(rc=mqtt.MQTT_ERR_SUCCESS)
        self.service.client.publish = Mock(return_value=publish_result)

        command_id = self.service.test_pir_motion()

        topic, payload = self.service.client.publish.call_args.args[:2]
        body = json.loads(payload)
        self.assertEqual(topic, f"{settings.device_topic}/automation/pir/test")
        self.assertEqual(body["source"], "dashboard")
        self.assertEqual(body["command_id"], command_id)
        self.assertTrue(command_id)

    def test_unknown_channel_is_rejected(self) -> None:
        self.service.connected = True
        with self.assertRaises(KeyError):
            self.service.set_channel("not-a-channel", True)


if __name__ == "__main__":
    unittest.main()
