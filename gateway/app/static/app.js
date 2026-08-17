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
const humidityBand = document.querySelector("#humidity-band");
const humidityReadout = document.querySelector("#humidity-readout");
const humidityNeedle = document.querySelector("#humidity-needle");
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
const aiAnalyzeButton = document.querySelector("#ai-analyze-button");
const aiMode = document.querySelector("#ai-mode");
const aiResult = document.querySelector("#ai-result");
const aiResultTitle = document.querySelector("#ai-result-title");
const aiSummary = document.querySelector("#ai-summary");
const aiSuggestions = document.querySelector("#ai-suggestions");
const aiWarning = document.querySelector("#ai-warning");
const aiDisclaimer = document.querySelector("#ai-disclaimer");
const kpiDeviceValue = document.querySelector("#kpi-device-value");
const kpiDeviceHint = document.querySelector("#kpi-device-hint");
const kpiChannelsValue = document.querySelector("#kpi-channels-value");
const kpiChannelsHint = document.querySelector("#kpi-channels-hint");
const kpiTempValue = document.querySelector("#kpi-temp-value");
const kpiTempHint = document.querySelector("#kpi-temp-hint");
const kpiAckValue = document.querySelector("#kpi-ack-value");
const kpiAckHint = document.querySelector("#kpi-ack-hint");
const eventLog = document.querySelector("#event-log");
const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
const SPARK_LEN = 24;
const MAX_EVENTS = 20;
const sparks = { device: [], channels: [], temp: [], ack: [] };
const eventEntries = [];
let snapshot = null;
let snapshotRenderedAt = performance.now();
let needleAngle = 0;
let needleFrame = 0;

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

function formatClock(date = new Date()) {
  return date.toLocaleTimeString("en-GB", { hour12: false });
}

function channelName(channelId) {
  const card = cards.get(channelId);
  return card?.querySelector("h2")?.textContent ?? channelId;
}

function pushSpark(key, value) {
  if (!Number.isFinite(value)) return;
  sparks[key].push(value);
  if (sparks[key].length > SPARK_LEN) sparks[key].shift();
}

function sparkPath(values, width = 120, height = 32) {
  const series = values.length === 1 ? [values[0], values[0]] : values;
  if (series.length < 2) return "";
  const min = Math.min(...series);
  const max = Math.max(...series);
  const span = max - min || 1;
  return series
    .map((value, index) => {
      const x = (index / (series.length - 1)) * width;
      const y = height - 2 - ((value - min) / span) * (height - 4);
      return `${index === 0 ? "M" : "L"}${x.toFixed(1)} ${y.toFixed(1)}`;
    })
    .join(" ");
}

function renderSparks() {
  document.querySelector("#kpi-device-spark").setAttribute("d", sparkPath(sparks.device));
  document.querySelector("#kpi-channels-spark").setAttribute("d", sparkPath(sparks.channels));
  document.querySelector("#kpi-temp-spark").setAttribute("d", sparkPath(sparks.temp));
  document.querySelector("#kpi-ack-spark").setAttribute("d", sparkPath(sparks.ack));
}

function latestAckMs(data) {
  let best = null;
  let bestAge = Infinity;
  for (const channel of Object.values(data.channels ?? {})) {
    if (!Number.isFinite(channel.acknowledgement_ms)) continue;
    const age = Number.isFinite(channel.last_update_age_ms)
      ? channel.last_update_age_ms
      : Infinity;
    if (age < bestAge) {
      bestAge = age;
      best = channel.acknowledgement_ms;
    }
  }
  return best;
}

function renderEventLog(flashNewest) {
  if (eventEntries.length === 0) {
    eventLog.replaceChildren();
    const empty = document.createElement("li");
    empty.className = "empty";
    empty.textContent = "Waiting for live events";
    eventLog.append(empty);
    return;
  }

  eventLog.replaceChildren();
  eventEntries.forEach((entry, index) => {
    const item = document.createElement("li");
    if (flashNewest && index === 0 && !reduceMotion) item.className = "log-flash";
    const ts = document.createElement("span");
    ts.className = "ts";
    ts.textContent = entry.ts;
    const src = document.createElement("span");
    src.className = entry.tone === "act" ? "src act" : "src";
    src.textContent = entry.source;
    const event = document.createElement("span");
    event.className = "event";
    event.textContent = entry.event;
    item.append(ts, src, event);
    eventLog.append(item);
  });
}

function pushEvent(source, event, tone = "sense") {
  eventEntries.unshift({ ts: formatClock(), source, event, tone });
  if (eventEntries.length > MAX_EVENTS) eventEntries.pop();
  renderEventLog(true);
}

function collectEvents(previous, data) {
  if (!previous) {
    pushEvent("GATEWAY", "Dashboard snapshot received");
    return;
  }

  if (previous.broker_connected !== data.broker_connected) {
    pushEvent("MQTT", data.broker_connected ? "Broker online" : "Broker offline");
  }
  if (previous.device_online !== data.device_online) {
    pushEvent(
      "ESP32",
      data.device_online ? "Device online" : "Device offline",
      data.device_online ? "sense" : "act",
    );
  }

  for (const channelId of cards.keys()) {
    const before = previous.channels?.[channelId];
    const after = data.channels?.[channelId];
    if (!after) continue;
    if (before?.state !== after.state || before?.source !== after.source) {
      const stateLabel =
        after.state === true ? "ON" : after.state === false ? "OFF" : "unknown";
      pushEvent(after.source || "unknown", `${channelName(channelId)} ${stateLabel}`, "act");
    }
  }

  const prevDht = previous.sensors?.dht11;
  const nextDht = data.sensors?.dht11;
  if (prevDht?.available !== nextDht?.available) {
    pushEvent("DHT11", nextDht?.available ? "Available" : "Unavailable");
  } else if (nextDht?.available) {
    const tempDelta = Math.abs(
      Number(nextDht.temperature_c) - Number(prevDht?.temperature_c),
    );
    const humidityDelta = Math.abs(
      Number(nextDht.humidity_percent) - Number(prevDht?.humidity_percent),
    );
    if (tempDelta >= 1 || humidityDelta >= 5) {
      pushEvent(
        "DHT11",
        `${Number(nextDht.temperature_c).toFixed(1)} C / ${Number(nextDht.humidity_percent).toFixed(1)} %`,
      );
    }
  }

  const prevWave = previous.sensors?.mmwave;
  const nextWave = data.sensors?.mmwave;
  if (prevWave?.available !== nextWave?.available) {
    pushEvent("MMWAVE", nextWave?.available ? "Available" : "Unavailable");
  } else if (nextWave?.available && prevWave?.presence !== nextWave?.presence) {
    pushEvent("MMWAVE", nextWave.presence ? "Presence detected" : "Room clear");
  }
}

function setNeedle(angle) {
  const apply = (next) => {
    needleAngle = next;
    humidityNeedle.setAttribute("transform", `translate(100 110) rotate(${next})`);
  };

  if (needleFrame) cancelAnimationFrame(needleFrame);
  if (reduceMotion || Math.abs(angle - needleAngle) < 0.4) {
    apply(angle);
    return;
  }

  const start = performance.now();
  const from = needleAngle;
  const tick = (now) => {
    const t = Math.min(1, (now - start) / 700);
    const eased = 1 - (1 - t) ** 3;
    apply(from + (angle - from) * eased);
    if (t < 1) needleFrame = requestAnimationFrame(tick);
    else needleFrame = 0;
  };
  needleFrame = requestAnimationFrame(tick);
}

function renderHumidity(available, humidity) {
  if (!available || !Number.isFinite(humidity)) {
    humidityValue.textContent = "N/A";
    humidityBand.textContent = "UNAVAILABLE";
    humidityReadout.className = "gauge-readout na";
    setNeedle(0);
    return;
  }

  const band = humidity <= 40 ? "DRY" : humidity <= 70 ? "COMFORT" : "HUMID";
  const tone = humidity <= 40 ? "sense" : humidity <= 70 ? "mid" : "high";
  humidityValue.textContent = `${humidity.toFixed(0)}%`;
  humidityBand.textContent = band;
  humidityReadout.className = `gauge-readout ${tone}`;
  setNeedle((Math.min(100, Math.max(0, humidity)) / 100) * 180);
}

function renderKpis(data) {
  const online = data.device_online === true;
  kpiDeviceValue.textContent = online ? "ONLINE" : "OFFLINE";
  kpiDeviceHint.textContent = data.broker_connected ? "broker linked" : "broker down";
  kpiDeviceValue.style.color = online ? "var(--sense)" : "var(--alert)";

  const states = Object.values(data.channels ?? {});
  const onCount = states.filter((channel) => channel.state === true).length;
  const knownOff = states.filter((channel) => channel.state === false).length;
  const known = states.filter((channel) => channel.state === true || channel.state === false).length;
  kpiChannelsValue.textContent = `${onCount} / 8`;
  kpiChannelsHint.textContent = known ? `${knownOff} off` : "waiting for state";

  const dht11 = data.sensors?.dht11;
  const tempAvailable = dht11?.available === true && Number.isFinite(Number(dht11.temperature_c));
  kpiTempValue.textContent = tempAvailable
    ? `${Number(dht11.temperature_c).toFixed(1)} C`
    : "N/A";
  kpiTempHint.textContent = tempAvailable ? "session spark" : "DHT11 unavailable";

  const ack = latestAckMs(data);
  kpiAckValue.textContent = Number.isFinite(ack) ? `${Math.round(ack)} ms` : "N/A";
  kpiAckHint.textContent = Number.isFinite(ack) ? "last dashboard ack" : "no ack yet";

  pushSpark("device", online ? 1 : 0);
  pushSpark("channels", onCount);
  if (tempAvailable) pushSpark("temp", Number(dht11.temperature_c));
  if (Number.isFinite(ack)) pushSpark("ack", ack);
  renderSparks();
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
  collectEvents(snapshot, data);
  snapshot = data;
  aiAnalyzeButton.disabled = false;
  renderDiagnostics(data);
  renderKpis(data);
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
  renderHumidity(
    dht11Available,
    dht11Available ? Number(dht11.humidity_percent) : null,
  );

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
    pushEvent("PIR", "Test motion sent", "act");
  } catch (error) {
    message.textContent = error.message;
  } finally {
    pirTestButton.disabled = !snapshot?.broker_connected || !snapshot?.device_online;
  }
}

function renderAiAdvice(advice) {
  aiResult.hidden = false;
  aiMode.textContent = advice.mode === "deepseek"
    ? `DeepSeek · ${advice.model}`
    : "Local demo";
  aiMode.classList.toggle("healthy", advice.mode === "deepseek");
  aiResultTitle.textContent = advice.title;
  aiSummary.textContent = advice.summary;
  aiSuggestions.replaceChildren();

  for (const suggestion of advice.suggestions ?? []) {
    const item = document.createElement("li");
    const heading = document.createElement("strong");
    const reason = document.createElement("p");
    const confidence = document.createElement("span");
    heading.textContent = suggestion.title;
    reason.textContent = suggestion.reason;
    confidence.textContent = `${suggestion.confidence_percent}% confidence`;
    item.append(heading, reason, confidence);
    aiSuggestions.append(item);
  }

  aiWarning.hidden = !advice.warning;
  aiWarning.textContent = advice.warning ?? "";
  aiDisclaimer.textContent = advice.disclaimer;
}

async function generateAiAdvice() {
  aiAnalyzeButton.disabled = true;
  aiMode.textContent = "Analyzing...";
  message.textContent = "Generating a read-only AI insight...";
  try {
    const response = await fetch("/api/ai/advice", { method: "POST" });
    if (!response.ok) {
      const error = await response.json();
      throw new Error(error.detail || "AI insight failed");
    }
    const advice = await response.json();
    renderAiAdvice(advice);
    message.textContent = advice.cached ? "Showing cached AI insight" : "AI insight ready";
    pushEvent("AI", advice.cached ? "Cached insight shown" : "Insight generated");
  } catch (error) {
    aiMode.textContent = "Unavailable";
    message.textContent = error.message;
  } finally {
    aiAnalyzeButton.disabled = snapshot === null;
  }
}

for (const [channelId, card] of cards) {
  card.querySelector("button").addEventListener("click", () => sendCommand(channelId));
}

pirTestButton.addEventListener("click", testPirMotion);
aiAnalyzeButton.addEventListener("click", generateAiAdvice);

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
