# Adoptive Automation

MVP 1 is an eight-channel ESP32 room controller with two synchronized control
paths:

- ESP RainMaker exposes six lights and two fans to its phone app and Google Home.
- A local FastAPI dashboard controls the same channels through Mosquitto MQTT.

The ESP32 is authoritative for physical output state. Every accepted command is
applied once, reported to RainMaker, and acknowledged to the local dashboard.
All channels boot OFF. PIR sensing, wall switches, learning, and AI are not part
of this milestone.

## Project map

- `src/` and `include/`: PlatformIO firmware.
- `gateway/`: FastAPI dashboard, MQTT bridge, and development broker config.
- `partitions_mvp.csv`: 4 MB flash layout with one large app and RainMaker's
  credential partition at `0x3D0000`.
- `adoptive_automation.ino` and the AceButton folders: legacy reference only;
  PlatformIO does not compile them.
- `AGENTS.md`: engineering map and invariants for future work.

## What you need to do first

1. Send or inspect the relay-module model, supply voltage, and input polarity.
   Do not connect a module to ESP32 GPIO until its inputs are confirmed as
   3.3 V compatible.
2. Confirm the proposed pin list in `include/ChannelConfig.h` against the labels
   on your 30-pin carrier board.
3. Install Mosquitto on the gateway PC and allow inbound TCP ports `1884`
   (MQTT) and `8000` (dashboard) on the trusted/private LAN.
4. Connect the ESP32 by USB. It was not connected while this project was built,
   so upload and physical polarity tests remain for you and Codex to run together.

This is a low-voltage prototype configuration. The broker and dashboard have no
login and must not be exposed to the internet.

Install the verified Windows broker package from a terminal where you can
approve its installer prompt:

```powershell
winget install --id EclipseFoundation.Mosquitto --exact
```

## Firmware

PlatformIO environments select relay polarity without changing source code:

```powershell
# Common relay modules: LOW energizes the channel
pio run -e esp32dev-relay-active-low

# Alternative modules: HIGH energizes the channel
pio run -e esp32dev-relay-active-high
```

After confirming polarity, upload and monitor:

```powershell
pio run -e esp32dev-relay-active-low -t upload
pio device monitor -b 115200
```

The proposed output pins are GPIO 13, 14, 16, 17, 18, 19, 23, and 25. Change
only `include/ChannelConfig.h` when wiring changes. The firmware writes the
inactive output level before enabling each GPIO, preventing an active-low boot
pulse.

The local MQTT broker is discovered via `_mqtt._tcp` mDNS. If discovery fails,
set the gateway PC's LAN IP in `include/AppConfig.h` and rebuild.

### Code-only Wi-Fi

RainMaker provisioning remains the safe default. For a fixed development
network, copy `include/WifiSecrets.example.h` to `include/WifiSecrets.h`, then
edit only the copied file:

```cpp
inline constexpr bool kUseFixedCredentials = true;
inline constexpr char kSsid[] = "your-network";
inline constexpr char kPassword[] = "your-password";
```

`WifiSecrets.h` is ignored by Git and must never be committed. Build and upload
the firmware after changing it. The ESP32 then connects directly without the
RainMaker Wi-Fi provisioning flow. Flashing still restarts the ESP32, and the
local dashboard will show it offline briefly while Wi-Fi and MQTT reconnect.

When fixed credentials are enabled, a BOOT-button Wi-Fi reset cannot select a
different network because the compiled configuration is reapplied at startup.
Disable `kUseFixedCredentials`, rebuild, and upload to restore SoftAP
provisioning.

### RainMaker enrollment

The firmware uses a one-time Wi-Fi SoftAP provisioning flow and then reuses the
credentials stored in NVS. The provisioning hotspot is named `ADOPT_xxxxxx` and
the prototype Proof of Possession is `adopt123`; change it in
`include/AppConfig.h` before using a device outside the bench.

RainMaker enrollment has two separate trust steps:

1. Host claiming writes the device identity and certificates into the `fctry`
   flash partition. The ESP32 uses those certificates to authenticate to the
   RainMaker cloud; there is no RainMaker API key in this project's source.
2. The QR code starts a local SoftAP provisioning session. It identifies the
   provisioning service, transport, protocol version, and PoP. It does not
   contain the RainMaker account password, a cloud API key, or the target Wi-Fi
   password. The signed-in RainMaker app supplies Wi-Fi credentials during the
   provisioning session and associates the device with the user's account.

Enabling code-only Wi-Fi skips only the second step's Wi-Fi setup flow.
`RMaker.start()` still runs, and the claimed ESP32 continues to authenticate to
RainMaker with the certificates already stored in `fctry`.

For public RainMaker on classic ESP32:

1. Upload the firmware.
2. Host-claim the board using the RainMaker CLI and this project's exact
   credential partition address:

   ```powershell
   esp-rainmaker-cli login
   esp-rainmaker-cli claim COM5 --addr 0x3D0000
   ```

   Replace `COM5` with the port shown by `pio device list`.
3. Open the ESP RainMaker app, add a device, select Wi-Fi/SoftAP provisioning,
   and use the name and PoP printed in the serial monitor.
4. Confirm that Light 1–6 and Fan 1–2 operate and report state.
5. In Google Home, select **Works with Google**, choose **ESP RainMaker**, and
   link the same RainMaker account.

Do not run a full-chip erase after claiming unless you intend to claim again;
it removes the `fctry` credentials. A normal PlatformIO upload preserves them.
Hold the ESP32 BOOT button for 3–10 seconds to reset Wi-Fi provisioning, or for
at least 10 seconds to request a full RainMaker factory reset.

## Local gateway

Create the environment and install dependencies:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r gateway\requirements.txt
Copy-Item gateway\.env.example gateway\.env
```

Start Mosquitto in terminal 1:

```powershell
mosquitto -c gateway\mosquitto\mosquitto.conf -v
```

Start the dashboard in terminal 2:

```powershell
.\.venv\Scripts\python.exe gateway\run.py
```

Open `http://localhost:8000` on the PC or
`http://<gateway-PC-LAN-IP>:8000` on a phone connected to the same network.
Cards remain disabled until the broker is connected, the ESP32 reports online,
and its retained channel states have arrived.

## MQTT contract

Topics are rooted at `adoptive/v1/devices/room-controller-01`:

- `channels/<channel-id>/set`: dashboard command with `state`, `source`, and a
  unique `command_id`.
- `channels/<channel-id>/state`: retained ESP32 acknowledgement with actual
  state and source.
- `availability`: retained `online`/`offline`; MQTT Last Will handles failures.
- `heartbeat`: 30-second uptime, Wi-Fi RSSI, heap, and firmware telemetry.

This source metadata is intentionally present in MVP 1 so later learning can
distinguish human dashboard/RainMaker actions from automated actions.
