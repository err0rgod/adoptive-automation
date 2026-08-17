from __future__ import annotations

import asyncio
import json
import logging
import re
import socket
import threading
import time
import uuid
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Any

import paho.mqtt.client as mqtt
from zeroconf import ServiceInfo, Zeroconf

from .config import CHANNELS, CHANNEL_BY_ID, Settings

logger = logging.getLogger(__name__)


@dataclass(slots=True)
class ChannelState:
    state: bool | None = None
    source: str = "unknown"
    command_id: str = ""
    uptime_ms: int | None = None
    received_at: float | None = None
    acknowledgement_ms: int | None = None


Broadcast = Callable[[dict[str, Any]], Awaitable[None]]


class MqttService:
    def __init__(self, settings: Settings, broadcast: Broadcast) -> None:
        self.settings = settings
        self.broadcast = broadcast
        self.loop: asyncio.AbstractEventLoop | None = None
        self.connected = False
        self.device_online = False
        self.started_at = time.monotonic()
        self.heartbeat_received_at: float | None = None
        self.broker_connect_count = 0
        self.pending_commands: dict[str, tuple[str, float]] = {}
        self._command_lock = threading.Lock()
        self.states = {channel.id: ChannelState() for channel in CHANNELS}
        self.sensors: dict[str, Any] = {
            "dht11": {
                "available": False,
                "temperature_c": None,
                "humidity_percent": None,
            },
            "mmwave": {"available": False, "presence": None},
            "uptime_ms": None,
        }
        self.heartbeat: dict[str, Any] = {}
        self.client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id="adoptive-dashboard",
        )
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message
        self.client.reconnect_delay_set(min_delay=1, max_delay=15)
        escaped = re.escape(self.settings.device_topic)
        self._state_pattern = re.compile(
            rf"^{escaped}/channels/(?P<channel>[^/]+)/state$"
        )
        self._zeroconf: Zeroconf | None = None
        self._service_info: ServiceInfo | None = None
        self._advertise_thread: threading.Thread | None = None
        self._stopping = threading.Event()

    def start(self, loop: asyncio.AbstractEventLoop) -> None:
        self.loop = loop
        self._stopping.clear()
        self._advertise_thread = threading.Thread(
            target=self._advertise_broker,
            name="mqtt-mdns-advertiser",
            daemon=True,
        )
        self._advertise_thread.start()
        try:
            self.client.connect_async(self.settings.mqtt_host, self.settings.mqtt_port)
            self.client.loop_start()
        except OSError:
            logger.exception("Unable to start MQTT client")

    def stop(self) -> None:
        self._stopping.set()
        try:
            self.client.disconnect()
            self.client.loop_stop()
        finally:
            if self._advertise_thread is not None:
                self._advertise_thread.join(timeout=2)
            if self._zeroconf is not None and self._service_info is not None:
                try:
                    self._zeroconf.unregister_service(self._service_info)
                except OSError:
                    logger.warning("Could not unregister MQTT mDNS service")
            if self._zeroconf is not None:
                self._zeroconf.close()

    def snapshot(self) -> dict[str, Any]:
        now = time.monotonic()
        with self._command_lock:
            expired_commands = [
                command_id
                for command_id, (_, sent_at) in self.pending_commands.items()
                if now - sent_at > 30
            ]
            for command_id in expired_commands:
                self.pending_commands.pop(command_id, None)
            pending_command_count = len(self.pending_commands)

        lan_address = self._local_lan_address()
        ip_warning = None
        if (
            not lan_address.startswith("127.")
            and lan_address != self.settings.mqtt_host
        ):
            ip_warning = (
                f"Laptop LAN IP is {lan_address}, but MQTT is configured for "
                f"{self.settings.mqtt_host}."
            )

        return {
            "broker_connected": self.connected,
            "device_online": self.device_online,
            "heartbeat": self.heartbeat,
            "sensors": self.sensors,
            "channels": {
                channel_id: {
                    "state": state.state,
                    "source": state.source,
                    "command_id": state.command_id,
                    "uptime_ms": state.uptime_ms,
                    "last_update_age_ms": round(
                        (now - state.received_at) * 1000
                    )
                    if state.received_at is not None
                    else None,
                    "acknowledgement_ms": state.acknowledgement_ms,
                }
                for channel_id, state in self.states.items()
            },
            "diagnostics": {
                "gateway_uptime_ms": round((now - self.started_at) * 1000),
                "mqtt_host": self.settings.mqtt_host,
                "mqtt_port": self.settings.mqtt_port,
                "lan_address": lan_address,
                "ip_warning": ip_warning,
                "broker_connect_count": self.broker_connect_count,
                "pending_command_count": pending_command_count,
                "heartbeat_age_ms": round(
                    (now - self.heartbeat_received_at) * 1000
                )
                if self.heartbeat_received_at is not None
                else None,
            },
        }

    def set_channel(self, channel_id: str, state: bool) -> str:
        if channel_id not in CHANNEL_BY_ID:
            raise KeyError(channel_id)
        if not self.connected:
            raise ConnectionError("The dashboard is not connected to MQTT")

        command_id = str(uuid.uuid4())
        topic = f"{self.settings.device_topic}/channels/{channel_id}/set"
        payload = json.dumps(
            {
                "state": state,
                "source": "dashboard",
                "command_id": command_id,
            },
            separators=(",", ":"),
        )
        with self._command_lock:
            self.pending_commands[command_id] = (channel_id, time.monotonic())
        try:
            result = self.client.publish(topic, payload, qos=1, retain=False)
        except Exception:
            with self._command_lock:
                self.pending_commands.pop(command_id, None)
            raise
        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            with self._command_lock:
                self.pending_commands.pop(command_id, None)
            raise ConnectionError(f"MQTT publish failed with code {result.rc}")
        return command_id

    def test_pir_motion(self) -> str:
        if not self.connected:
            raise ConnectionError("The dashboard is not connected to MQTT")

        command_id = str(uuid.uuid4())
        topic = f"{self.settings.device_topic}/automation/pir/test"
        payload = json.dumps(
            {"source": "dashboard", "command_id": command_id},
            separators=(",", ":"),
        )
        result = self.client.publish(topic, payload, qos=1, retain=False)
        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            raise ConnectionError(f"MQTT publish failed with code {result.rc}")
        return command_id

    def _on_connect(
        self,
        client: mqtt.Client,
        _userdata: Any,
        _flags: mqtt.ConnectFlags,
        reason_code: mqtt.ReasonCode,
        _properties: mqtt.Properties | None,
    ) -> None:
        self.connected = not reason_code.is_failure
        if not self.connected:
            logger.error("MQTT connection rejected: %s", reason_code)
            return

        self.broker_connect_count += 1
        logger.info("Connected to MQTT broker")
        client.subscribe(f"{self.settings.device_topic}/channels/+/state", qos=1)
        client.subscribe(f"{self.settings.device_topic}/availability", qos=1)
        client.subscribe(f"{self.settings.device_topic}/heartbeat", qos=0)
        client.subscribe(f"{self.settings.device_topic}/sensors/state", qos=1)
        self._send_to_websockets({"type": "snapshot", "data": self.snapshot()})

    def _on_disconnect(
        self,
        _client: mqtt.Client,
        _userdata: Any,
        _disconnect_flags: mqtt.DisconnectFlags,
        reason_code: mqtt.ReasonCode,
        _properties: mqtt.Properties | None,
    ) -> None:
        self.connected = False
        logger.warning("Disconnected from MQTT: %s", reason_code)
        self._send_to_websockets({"type": "snapshot", "data": self.snapshot()})

    def _on_message(
        self, _client: mqtt.Client, _userdata: Any, message: mqtt.MQTTMessage
    ) -> None:
        payload_text = message.payload.decode("utf-8", errors="replace")
        availability_topic = f"{self.settings.device_topic}/availability"
        heartbeat_topic = f"{self.settings.device_topic}/heartbeat"
        sensor_topic = f"{self.settings.device_topic}/sensors/state"

        if message.topic == availability_topic:
            self.device_online = payload_text == "online"
        elif message.topic == heartbeat_topic:
            try:
                self.heartbeat = json.loads(payload_text)
                self.heartbeat_received_at = time.monotonic()
            except json.JSONDecodeError:
                logger.warning("Ignored malformed heartbeat: %s", payload_text)
                return
        elif message.topic == sensor_topic:
            try:
                payload = json.loads(payload_text)
                dht11 = payload["dht11"]
                mmwave = payload["mmwave"]
                dht11_available = dht11["available"]
                mmwave_available = mmwave["available"]
                if not isinstance(dht11_available, bool) or not isinstance(
                    mmwave_available, bool
                ):
                    raise ValueError("availability must be boolean")

                temperature = dht11.get("temperature_c")
                humidity = dht11.get("humidity_percent")
                presence = mmwave.get("presence")
                if dht11_available and (
                    not self._is_number(temperature) or not self._is_number(humidity)
                ):
                    raise ValueError("available DHT11 values must be numbers")
                if mmwave_available and not isinstance(presence, bool):
                    raise ValueError("available mmWave presence must be boolean")

                self.sensors = {
                    "dht11": {
                        "available": dht11_available,
                        "temperature_c": float(temperature)
                        if dht11_available
                        else None,
                        "humidity_percent": float(humidity)
                        if dht11_available
                        else None,
                    },
                    "mmwave": {
                        "available": mmwave_available,
                        "presence": presence if mmwave_available else None,
                    },
                    "uptime_ms": payload.get("uptime_ms"),
                }
            except (json.JSONDecodeError, KeyError, ValueError, TypeError):
                logger.warning("Ignored malformed sensor state: %s", payload_text)
                return
        else:
            match = self._state_pattern.match(message.topic)
            if not match:
                return
            channel_id = match.group("channel")
            if channel_id not in self.states:
                return
            try:
                payload = json.loads(payload_text)
                state = payload["state"]
                if not isinstance(state, bool):
                    raise ValueError("state must be boolean")
                command_id = str(payload.get("command_id", ""))
                acknowledgement_ms = None
                with self._command_lock:
                    pending = self.pending_commands.pop(command_id, None)
                if pending is not None and pending[0] == channel_id:
                    acknowledgement_ms = round(
                        (time.monotonic() - pending[1]) * 1000
                    )
                self.states[channel_id] = ChannelState(
                    state=state,
                    source=str(payload.get("source", "unknown")),
                    command_id=command_id,
                    uptime_ms=payload.get("uptime_ms"),
                    received_at=time.monotonic(),
                    acknowledgement_ms=acknowledgement_ms,
                )
            except (json.JSONDecodeError, KeyError, ValueError, TypeError):
                logger.warning("Ignored malformed channel state: %s", payload_text)
                return

        self._send_to_websockets({"type": "snapshot", "data": self.snapshot()})

    @staticmethod
    def _is_number(value: Any) -> bool:
        return isinstance(value, (int, float)) and not isinstance(value, bool)

    def _send_to_websockets(self, message: dict[str, Any]) -> None:
        if self.loop is not None and not self.loop.is_closed():
            asyncio.run_coroutine_threadsafe(self.broadcast(message), self.loop)

    def _advertise_broker(self) -> None:
        address = self._local_lan_address()
        if address.startswith("127."):
            logger.warning("No LAN address found; MQTT mDNS advertisement skipped")
            return
        try:
            self._zeroconf = Zeroconf()
            self._service_info = ServiceInfo(
                "_mqtt._tcp.local.",
                "Adoptive Automation MQTT._mqtt._tcp.local.",
                addresses=[socket.inet_aton(address)],
                port=self.settings.mqtt_port,
                properties={"project": "adoptive-automation"},
                server="automation-gateway.local.",
            )
            self._zeroconf.register_service(self._service_info)
            if self._stopping.is_set():
                return
            logger.info("Advertised MQTT broker at %s:%s", address, self.settings.mqtt_port)
        except Exception:
            logger.exception("Could not advertise the MQTT broker over mDNS")

    @staticmethod
    def _local_lan_address() -> str:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.connect(("192.0.2.1", 80))
            return str(sock.getsockname()[0])
        except OSError:
            return "127.0.0.1"
        finally:
            sock.close()
