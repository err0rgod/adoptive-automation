const cards = new Map(
  [...document.querySelectorAll("[data-channel]")].map((card) => [card.dataset.channel, card]),
);
const brokerBadge = document.querySelector("#broker-status");
const deviceBadge = document.querySelector("#device-status");
const message = document.querySelector("#message");
const dht11Card = document.querySelector("#dht11-card");
const dht11Status = document.querySelector("#dht11-status");
const temperatureValue = document.querySelector("#temperature-value");
const humidityValue = document.querySelector("#humidity-value");
const mmWaveCard = document.querySelector("#mmwave-card");
const mmWaveStatus = document.querySelector("#mmwave-status");
const presenceValue = document.querySelector("#presence-value");
const pirTestButton = document.querySelector("#pir-test-button");
const ipWarning = document.querySelector("#ip-warning");
const heartbeatHealth = document.querySelector("#heartbeat-health");
const heartbeatAge = document.querySelector("#heartbeat-age");
const wifiRssi = document.querySelector("#wifi-rssi");
const firmwareVersion = document.querySelector("#firmware-version");
const deviceUptime = document.querySelector("#device-uptime");
const freeHeap = document.querySelector("#free-heap");
const mqttSessions = document.querySelector("#mqtt-sessions");
const brokerEndpoint = document.querySelector("#broker-endpoint");
const pendingCommands = document.querySelector("#pending-commands");
let snapshot = null;
let snapshotRenderedAt = performance.now();

function setBadge(element, online, onlineText, offlineText) {
  element.textContent = online ? onlineText : offlineText;
  element.classList.toggle("online", online);
  element.classList.toggle("offline", !online);
}

function formatDuration(milliseconds) {
  if (!Number.isFinite(milliseconds)) return "N/A";
  const totalSeconds = Math.max(0, Math.floor(milliseconds / 1000));
  const days = Math.floor(totalSeconds / 86400);
  const hours = Math.floor((totalSeconds % 86400) / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  if (days > 0) return `${days}d ${hours}h`;
  if (hours > 0) return `${hours}h ${minutes}m`;
  if (minutes > 0) return `${minutes}m ${seconds}s`;
  return `${seconds}s`;
}

function renderHeartbeatAge() {
  const baseAge = snapshot?.diagnostics?.heartbeat_age_ms;
  if (!Number.isFinite(baseAge)) {
    heartbeatAge.textContent = "N/A";
    heartbeatHealth.textContent = "Waiting";
    heartbeatHealth.classList.remove("healthy", "stale");
    return;
  }

  const currentAge = baseAge + (performance.now() - snapshotRenderedAt);
  const healthy = currentAge < 45000;
  heartbeatAge.textContent = `${formatDuration(currentAge)} ago`;
  heartbeatHealth.textContent = healthy ? "Healthy" : "Stale";
  heartbeatHealth.classList.toggle("healthy", healthy);
  heartbeatHealth.classList.toggle("stale", !healthy);
}

function renderDiagnostics(data) {
  const diagnostics = data.diagnostics ?? {};
  const heartbeat = data.heartbeat ?? {};
  snapshotRenderedAt = performance.now();
  renderHeartbeatAge();

  wifiRssi.textContent = Number.isFinite(heartbeat.wifi_rssi)
    ? `${heartbeat.wifi_rssi} dBm`
    : "N/A";
  firmwareVersion.textContent = heartbeat.firmware ?? "N/A";
  deviceUptime.textContent = formatDuration(heartbeat.uptime_ms);
  freeHeap.textContent = Number.isFinite(heartbeat.free_heap)
    ? `${Math.round(heartbeat.free_heap / 1024)} KB`
    : "N/A";
  mqttSessions.textContent = diagnostics.broker_connect_count ?? 0;
  brokerEndpoint.textContent = diagnostics.mqtt_host
    ? `${diagnostics.mqtt_host}:${diagnostics.mqtt_port}`
    : "N/A";
  pendingCommands.textContent = diagnostics.pending_command_count ?? 0;

  ipWarning.hidden = !diagnostics.ip_warning;
  ipWarning.textContent = diagnostics.ip_warning ?? "";
}

function render(data) {
  snapshot = data;
  renderDiagnostics(data);
  setBadge(brokerBadge, data.broker_connected, "Broker online", "Broker offline");
  setBadge(deviceBadge, data.device_online, "Device online", "Device offline");
  const pirTargetReady = data.channels?.["light-1"]?.state === false;
  pirTestButton.disabled =
    !data.broker_connected || !data.device_online || !pirTargetReady;

  const dht11 = data.sensors?.dht11;
  const dht11Available = dht11?.available === true;
  dht11Card.classList.toggle("available", dht11Available);
  dht11Status.textContent = dht11Available ? "Available" : "N/A";
  temperatureValue.textContent = dht11Available
    ? `${Number(dht11.temperature_c).toFixed(1)} C`
    : "N/A";
  humidityValue.textContent = dht11Available
    ? `${Number(dht11.humidity_percent).toFixed(1)} %`
    : "N/A";

  const mmWave = data.sensors?.mmwave;
  const mmWaveAvailable = mmWave?.available === true;
  const presence = mmWaveAvailable && mmWave.presence === true;
  mmWaveCard.classList.toggle("available", mmWaveAvailable);
  mmWaveCard.classList.toggle("presence", presence);
  mmWaveStatus.textContent = mmWaveAvailable ? "Available" : "N/A";
  presenceValue.textContent = mmWaveAvailable
    ? (presence ? "Presence detected" : "Room clear")
    : "N/A";

  for (const [channelId, card] of cards) {
    const channel = data.channels[channelId];
    const button = card.querySelector("button");
    const stateLabel = card.querySelector(".power-state");
    const sourceLabel = card.querySelector(".source");
    const diagnosticLabel = card.querySelector(".channel-diagnostic");
    const known = channel?.state !== null && channel?.state !== undefined;
    const on = known && channel.state;

    card.classList.toggle("on", on);
    card.classList.remove("pending");
    button.disabled = !data.broker_connected || !data.device_online || !known;
    button.setAttribute("aria-pressed", String(on));
    stateLabel.textContent = known ? (on ? "ON" : "OFF") : "Unknown";
    sourceLabel.textContent = known ? `Last changed by ${channel.source}` : "Waiting for device state";
    if (!known) {
      diagnosticLabel.textContent = "No acknowledgement yet";
    } else if (Number.isFinite(channel.acknowledgement_ms)) {
      diagnosticLabel.textContent = `Dashboard acknowledged in ${channel.acknowledgement_ms} ms`;
    } else {
      diagnosticLabel.textContent = `State update ${formatDuration(channel.last_update_age_ms)} ago`;
    }
  }
}

async function sendCommand(channelId) {
  const card = cards.get(channelId);
  const current = snapshot?.channels[channelId]?.state;
  if (current === null || current === undefined) return;

  card.classList.add("pending");
  card.querySelector("button").disabled = true;
  message.textContent = `Sending ${channelId} command...`;
  try {
    const response = await fetch(`/api/channels/${channelId}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ state: !current }),
    });
    if (!response.ok) {
      const error = await response.json();
      throw new Error(error.detail || "Command failed");
    }
    message.textContent = "Command accepted; waiting for device acknowledgement";
  } catch (error) {
    card.classList.remove("pending");
    message.textContent = error.message;
    render(snapshot);
  }
}

async function testPirMotion() {
  pirTestButton.disabled = true;
  message.textContent = "Sending PIR test motion...";
  try {
    const response = await fetch("/api/automation/pir/test", { method: "POST" });
    if (!response.ok) {
      const error = await response.json();
      throw new Error(error.detail || "PIR test failed");
    }
    message.textContent = "PIR test sent; waiting for ESP32 acknowledgement";
  } catch (error) {
    message.textContent = error.message;
  } finally {
    pirTestButton.disabled = !snapshot?.broker_connected || !snapshot?.device_online;
  }
}

for (const [channelId, card] of cards) {
  card.querySelector("button").addEventListener("click", () => sendCommand(channelId));
}

pirTestButton.addEventListener("click", testPirMotion);

function connectWebSocket() {
  const protocol = location.protocol === "https:" ? "wss" : "ws";
  const socket = new WebSocket(`${protocol}://${location.host}/ws`);
  socket.addEventListener("open", () => { message.textContent = "Dashboard connected"; });
  socket.addEventListener("message", (event) => {
    const payload = JSON.parse(event.data);
    if (payload.type === "snapshot") render(payload.data);
  });
  socket.addEventListener("close", () => {
    message.textContent = "Dashboard connection lost; retrying...";
    setTimeout(connectWebSocket, 1500);
  });
}

connectWebSocket();
setInterval(renderHeartbeatAge, 1000);
