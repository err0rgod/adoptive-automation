# Adoptive Automation engineering map

## Current milestone

MVP 1 provides eight synchronized relay controls through local MQTT/dashboard
and ESP RainMaker/Google Home. Optional DHT11 and digital mmWave inputs are
read-only telemetry. A compile-time optional PIR may control one configured
light with a user-override-safe timer. Do not add wall inputs, persistence,
learning, routines, or external AI until relay control is physically verified.

## Components

- `platformio.ini`: ESP32 build environments. Default is active-low.
- `include/ChannelConfig.h`: single firmware source of truth for channel IDs,
  RainMaker names/types, GPIO pins, and active level.
- `src/RelayBank.cpp`: safe GPIO output and in-memory state.
- `src/LocalMqtt.cpp`: broker discovery, commands, retained state, availability,
  and heartbeat.
- `src/main.cpp`: unified state transition, RainMaker devices/provisioning, and
  reset button.
- `include/SensorConfig.h` and `src/SensorTelemetry.cpp`: optional DHT11 and
  digital mmWave pin configuration, availability, and read-only sampling.
- `include/AutomationConfig.example.h` and `src/PirLightAutomation.cpp`:
  optional PIR target/timing configuration and user-override-safe ownership.
- `gateway/app/`: FastAPI UI, MQTT client, WebSocket state fan-out, and mDNS
  broker advertisement.
- `gateway/app/config.py`: dashboard copy of channel IDs/names/types. Keep it in
  sync with `include/ChannelConfig.h` until a generator is introduced.
- `partitions_mvp.csv`: single application slot plus RainMaker `fctry` at
  `0x3D0000`. OTA is deliberately unavailable in MVP 1.
- Legacy `.ino` and AceButton files are reference material and are not compiled.

## Invariants

1. ESP32 physical state is authoritative; UIs send desired state and wait for
   the ESP32 acknowledgement.
2. All state changes pass through `applyChannelState()` and carry a source.
3. Every boot starts all eight outputs OFF; never restore relay state in MVP 1.
4. Set the inactive GPIO level before `pinMode(OUTPUT)` to prevent relay pulses.
5. Never treat RainMaker, dashboard, or future automation as separate state
   owners.
6. PIR auto-off is armed only when PIR turned an OFF light ON. Any external
   command to that channel cancels PIR ownership.
7. Keep standard RainMaker light/fan device types for Google Home discovery.
8. Never commit Wi-Fi, RainMaker account, or future MQTT credentials.
9. Do not expose the anonymous development broker or unauthenticated dashboard
   outside a trusted LAN.
10. The WROOM-32U has 4 MB flash. Recheck image size after every firmware feature.
11. A full-chip erase destroys RainMaker claiming data in `fctry`.

## Verification commands

```powershell
$env:PYTHONIOENCODING='utf-8'
pio run -e esp32dev-relay-active-low
pio run -e esp32dev-relay-active-high
.\.venv\Scripts\python.exe -m compileall -q gateway
.\.venv\Scripts\python.exe -c "from gateway.app.main import app, mqtt_service; print(app.title, len(mqtt_service.states))"
```

Hardware validation must test boot behavior first with low-voltage loads and
confirm that dashboard, RainMaker app, and Google Home all converge after every
command and reconnect.

## Planned increments

1. MVP 2: persist existing sensor telemetry and events, without automation.
2. MVP 3: entry-light learner in shadow mode.
3. MVP 4: user-approved deterministic automation and correction tracking.
4. MVP 5: short routines, optional external AI rule advisor, and additional
   context sensors.
