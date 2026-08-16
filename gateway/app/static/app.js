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
let snapshot = null;

function setBadge(element, online, onlineText, offlineText) {
  element.textContent = online ? onlineText : offlineText;
  element.classList.toggle("online", online);
  element.classList.toggle("offline", !online);
}

function render(data) {
  snapshot = data;
  setBadge(brokerBadge, data.broker_connected, "Broker online", "Broker offline");
  setBadge(deviceBadge, data.device_online, "Device online", "Device offline");

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
    const known = channel?.state !== null && channel?.state !== undefined;
    const on = known && channel.state;

    card.classList.toggle("on", on);
    card.classList.remove("pending");
    button.disabled = !data.broker_connected || !data.device_online || !known;
    button.setAttribute("aria-pressed", String(on));
    stateLabel.textContent = known ? (on ? "ON" : "OFF") : "Unknown";
    sourceLabel.textContent = known ? `Last changed by ${channel.source}` : "Waiting for device state";
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

for (const [channelId, card] of cards) {
  card.querySelector("button").addEventListener("click", () => sendCommand(channelId));
}

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
