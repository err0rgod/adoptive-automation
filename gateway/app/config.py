from __future__ import annotations

import os
from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class Channel:
    id: str
    name: str
    kind: str


CHANNELS = (
    Channel("light-1", "Light 1", "light"),
    Channel("light-2", "Light 2", "light"),
    Channel("light-3", "Light 3", "light"),
    Channel("light-4", "Light 4", "light"),
    Channel("light-5", "Light 5", "light"),
    Channel("light-6", "Light 6", "light"),
    Channel("fan-1", "Fan 1", "fan"),
    Channel("fan-2", "Fan 2", "fan"),
)
CHANNEL_BY_ID = {channel.id: channel for channel in CHANNELS}


@dataclass(frozen=True, slots=True)
class Settings:
    mqtt_host: str = os.getenv("ADOPTIVE_MQTT_HOST", "10.141.139.247")
    mqtt_port: int = int(os.getenv("ADOPTIVE_MQTT_PORT", "1884"))
    device_id: str = os.getenv("ADOPTIVE_DEVICE_ID", "room-controller-01")
    web_host: str = os.getenv("ADOPTIVE_WEB_HOST", "0.0.0.0")
    web_port: int = int(os.getenv("ADOPTIVE_WEB_PORT", "8000"))
    topic_root: str = "adoptive/v1"

    @property
    def device_topic(self) -> str:
        return f"{self.topic_root}/devices/{self.device_id}"


settings = Settings()
