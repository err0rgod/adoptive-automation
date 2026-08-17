from __future__ import annotations

import asyncio
import copy
import json
import logging
import time
import urllib.request
from typing import Any

from .config import Settings

logger = logging.getLogger(__name__)


class AiAdvisor:
    """Read-only advisor with no MQTT or relay-control capability."""

    def __init__(self, settings: Settings) -> None:
        self.settings = settings
        self._lock = asyncio.Lock()
        self._cached_at = 0.0
        self._cached_result: dict[str, Any] | None = None

    async def analyze(self, snapshot: dict[str, Any]) -> dict[str, Any]:
        async with self._lock:
            now = time.monotonic()
            if self._cached_result is not None and now - self._cached_at < 30:
                result = copy.deepcopy(self._cached_result)
                result["cached"] = True
                return result

            result: dict[str, Any]
            if self.settings.deepseek_api_key:
                try:
                    result = await asyncio.to_thread(
                        self._request_deepseek, self._snapshot_for_ai(snapshot)
                    )
                except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError):
                    logger.exception("DeepSeek advice request failed")
                    result = self._local_demo(snapshot)
                    result["warning"] = (
                        "DeepSeek was unavailable, so this is a local demo insight."
                    )
            else:
                result = self._local_demo(snapshot)

            result["cached"] = False
            self._cached_result = copy.deepcopy(result)
            self._cached_at = now
            return result

    def _request_deepseek(self, safe_snapshot: dict[str, Any]) -> dict[str, Any]:
        endpoint = f"{self.settings.deepseek_base_url.rstrip('/')}/chat/completions"
        system_prompt = (
            "You are a read-only home-automation advisor. You cannot operate devices. "
            "Analyze only the supplied live snapshot. Do not claim that behavior was "
            "learned because no history is provided. Return JSON with string fields "
            "title and summary, plus suggestions as an array of 1 to 3 objects. Each "
            "suggestion must contain title, reason, and confidence_percent from 0 to "
            "100. Prefer safe, deterministic recommendations."
        )
        request_body = {
            "model": self.settings.deepseek_model,
            "messages": [
                {"role": "system", "content": system_prompt},
                {
                    "role": "user",
                    "content": json.dumps(safe_snapshot, separators=(",", ":")),
                },
            ],
            "response_format": {"type": "json_object"},
            "thinking": {"type": "disabled"},
            "max_tokens": 500,
            "stream": False,
        }
        request = urllib.request.Request(
            endpoint,
            data=json.dumps(request_body).encode("utf-8"),
            headers={
                "Authorization": f"Bearer {self.settings.deepseek_api_key}",
                "Content-Type": "application/json",
                "User-Agent": "adoptive-automation/0.1.0",
            },
            method="POST",
        )
        with urllib.request.urlopen(request, timeout=20) as response:
            response_body = json.loads(response.read().decode("utf-8"))

        content = response_body["choices"][0]["message"]["content"]
        if not isinstance(content, str):
            raise ValueError("DeepSeek returned no message content")
        return self._validated_result(json.loads(content))

    def _validated_result(self, result: Any) -> dict[str, Any]:
        if not isinstance(result, dict):
            raise ValueError("AI result must be an object")

        title = self._clean_text(result.get("title"), 80)
        summary = self._clean_text(result.get("summary"), 400)
        raw_suggestions = result.get("suggestions")
        if not isinstance(raw_suggestions, list) or not raw_suggestions:
            raise ValueError("AI result must contain suggestions")

        suggestions = []
        for item in raw_suggestions[:3]:
            if not isinstance(item, dict):
                continue
            confidence = item.get("confidence_percent")
            if not isinstance(confidence, (int, float)) or isinstance(
                confidence, bool
            ):
                continue
            suggestions.append(
                {
                    "title": self._clean_text(item.get("title"), 100),
                    "reason": self._clean_text(item.get("reason"), 300),
                    "confidence_percent": max(0, min(100, round(confidence))),
                }
            )
        if not suggestions:
            raise ValueError("AI result contained no valid suggestions")

        return {
            "mode": "deepseek",
            "model": self.settings.deepseek_model,
            "configured": True,
            "title": title,
            "summary": summary,
            "suggestions": suggestions,
            "disclaimer": "Advisory only. No relay command was sent.",
        }

    @staticmethod
    def _clean_text(value: Any, limit: int) -> str:
        if not isinstance(value, str) or not value.strip():
            raise ValueError("AI result contains invalid text")
        return " ".join(value.split())[:limit]

    @staticmethod
    def _snapshot_for_ai(snapshot: dict[str, Any]) -> dict[str, Any]:
        heartbeat = snapshot.get("heartbeat", {})
        sensors = snapshot.get("sensors", {})
        channels = snapshot.get("channels", {})
        return {
            "device_online": snapshot.get("device_online") is True,
            "wifi_rssi": heartbeat.get("wifi_rssi"),
            "device_uptime_ms": heartbeat.get("uptime_ms"),
            "sensors": {
                "dht11": sensors.get("dht11", {}),
                "mmwave": sensors.get("mmwave", {}),
            },
            "channels": {
                channel_id: {
                    "state": channel.get("state"),
                    "source": channel.get("source", "unknown"),
                }
                for channel_id, channel in channels.items()
                if isinstance(channel, dict)
            },
        }

    def _local_demo(self, snapshot: dict[str, Any]) -> dict[str, Any]:
        channels = snapshot.get("channels", {})
        known_channels = [
            channel
            for channel in channels.values()
            if isinstance(channel, dict) and isinstance(channel.get("state"), bool)
        ]
        active_count = sum(channel["state"] for channel in known_channels)
        device_online = snapshot.get("device_online") is True
        sensor_data = snapshot.get("sensors", {})
        sensors_available = any(
            sensor_data.get(sensor, {}).get("available") is True
            for sensor in ("dht11", "mmwave")
        )

        suggestions = [
            {
                "title": "Collect behavior before automating",
                "reason": (
                    "The current snapshot has no event history. Record manual actions "
                    "and corrections before proposing a recurring rule."
                ),
                "confidence_percent": 98,
            }
        ]
        if not device_online:
            suggestions.append(
                {
                    "title": "Restore controller connectivity",
                    "reason": (
                        "The ESP32 is offline, so recommendations based on its current "
                        "relay state may be stale."
                    ),
                    "confidence_percent": 100,
                }
            )
        elif not sensors_available:
            suggestions.append(
                {
                    "title": "Add context before occupancy rules",
                    "reason": (
                        "DHT11 and mmWave currently report N/A. Keep occupancy-based "
                        "automation disabled until a sensor is physically verified."
                    ),
                    "confidence_percent": 96,
                }
            )
        else:
            suggestions.append(
                {
                    "title": "Observe sensor-to-action timing",
                    "reason": (
                        "Compare verified sensor events with manual switch actions in "
                        "shadow mode before enabling a deterministic rule."
                    ),
                    "confidence_percent": 90,
                }
            )

        return {
            "mode": "demo",
            "model": None,
            "configured": False,
            "title": "AI readiness preview",
            "summary": (
                f"The controller is {'online' if device_online else 'offline'} with "
                f"{active_count} of {len(known_channels)} known channels active. This "
                "demo uses only the current snapshot and does not claim learned behavior."
            ),
            "suggestions": suggestions,
            "disclaimer": "Advisory only. No relay command was sent.",
        }
