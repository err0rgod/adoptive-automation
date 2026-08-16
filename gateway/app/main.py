from __future__ import annotations

import asyncio
import logging
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel

from .config import CHANNELS, CHANNEL_BY_ID, settings
from .mqtt_service import MqttService

logging.basicConfig(level=logging.INFO)
BASE_DIR = Path(__file__).resolve().parent
templates = Jinja2Templates(directory=BASE_DIR / "templates")


class SocketHub:
    def __init__(self) -> None:
        self.clients: set[WebSocket] = set()

    async def connect(self, socket: WebSocket) -> None:
        await socket.accept()
        self.clients.add(socket)

    def disconnect(self, socket: WebSocket) -> None:
        self.clients.discard(socket)

    async def broadcast(self, message: dict[str, Any]) -> None:
        failed: list[WebSocket] = []
        for socket in tuple(self.clients):
            try:
                await socket.send_json(message)
            except (RuntimeError, WebSocketDisconnect):
                failed.append(socket)
        for socket in failed:
            self.disconnect(socket)


hub = SocketHub()
mqtt_service = MqttService(settings, hub.broadcast)


@asynccontextmanager
async def lifespan(_app: FastAPI):
    mqtt_service.start(asyncio.get_running_loop())
    try:
        yield
    finally:
        mqtt_service.stop()


app = FastAPI(title="Adoptive Automation", version="0.1.0", lifespan=lifespan)
app.mount("/static", StaticFiles(directory=BASE_DIR / "static"), name="static")


class ChannelCommand(BaseModel):
    state: bool


@app.get("/", response_class=HTMLResponse)
async def dashboard(request: Request):
    return templates.TemplateResponse(
        request=request,
        name="index.html",
        context={"channels": CHANNELS, "device_id": settings.device_id},
    )


@app.get("/api/state")
async def state():
    return mqtt_service.snapshot()


@app.post("/api/channels/{channel_id}", status_code=202)
async def set_channel(channel_id: str, command: ChannelCommand):
    if channel_id not in CHANNEL_BY_ID:
        raise HTTPException(status_code=404, detail="Unknown channel")
    try:
        command_id = mqtt_service.set_channel(channel_id, command.state)
    except ConnectionError as error:
        raise HTTPException(status_code=503, detail=str(error)) from error
    return {"accepted": True, "command_id": command_id}


@app.post("/api/automation/pir/test", status_code=202)
async def test_pir_motion():
    try:
        command_id = mqtt_service.test_pir_motion()
    except ConnectionError as error:
        raise HTTPException(status_code=503, detail=str(error)) from error
    return {"accepted": True, "command_id": command_id}


@app.websocket("/ws")
async def websocket_endpoint(socket: WebSocket):
    await hub.connect(socket)
    await socket.send_json({"type": "snapshot", "data": mqtt_service.snapshot()})
    try:
        while True:
            await socket.receive_text()
    except WebSocketDisconnect:
        hub.disconnect(socket)
