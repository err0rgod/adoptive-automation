from __future__ import annotations

import unittest
from dataclasses import replace

from gateway.app.ai_advisor import AiAdvisor
from gateway.app.config import settings


def sample_snapshot() -> dict:
    return {
        "device_online": True,
        "heartbeat": {"wifi_rssi": -48, "uptime_ms": 123000},
        "sensors": {
            "dht11": {
                "available": False,
                "temperature_c": None,
                "humidity_percent": None,
            },
            "mmwave": {"available": False, "presence": None},
        },
        "channels": {
            "light-1": {
                "state": True,
                "source": "dashboard",
                "command_id": "must-not-leave-gateway",
            },
            "fan-1": {"state": False, "source": "rainmaker"},
        },
        "diagnostics": {
            "mqtt_host": "private-broker-address",
            "mqtt_port": 1884,
        },
    }


class AiAdvisorTests(unittest.IsolatedAsyncioTestCase):
    async def test_demo_mode_works_without_api_key_and_is_cached(self) -> None:
        advisor = AiAdvisor(replace(settings, deepseek_api_key=""))

        first = await advisor.analyze(sample_snapshot())
        second = await advisor.analyze(sample_snapshot())

        self.assertEqual(first["mode"], "demo")
        self.assertIs(first["configured"], False)
        self.assertGreaterEqual(len(first["suggestions"]), 1)
        self.assertIs(first["cached"], False)
        self.assertIs(second["cached"], True)

    async def test_snapshot_sent_to_ai_excludes_private_gateway_data(self) -> None:
        safe_snapshot = AiAdvisor._snapshot_for_ai(sample_snapshot())

        self.assertNotIn("diagnostics", safe_snapshot)
        self.assertNotIn("command_id", safe_snapshot["channels"]["light-1"])
        self.assertEqual(safe_snapshot["channels"]["light-1"]["state"], True)

    async def test_deepseek_result_is_bounded_and_validated(self) -> None:
        advisor = AiAdvisor(replace(settings, deepseek_api_key="test-only"))

        result = advisor._validated_result(
            {
                "title": "Current snapshot",
                "summary": "No learned history is available.",
                "suggestions": [
                    {
                        "title": "Observe first",
                        "reason": "Collect verified events before automation.",
                        "confidence_percent": 105,
                    }
                ],
            }
        )

        self.assertEqual(result["mode"], "deepseek")
        self.assertEqual(result["suggestions"][0]["confidence_percent"], 100)
        self.assertEqual(result["disclaimer"], "Advisory only. No relay command was sent.")


if __name__ == "__main__":
    unittest.main()
