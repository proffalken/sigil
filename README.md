# Sigil

A lightweight RS485 device networking library for Arduino / ESP32.

Sigil handles the boilerplate of building devices for a shared RS485 bus: registration, namespaced message routing, sensor data publishing, and command dispatch — all over newline-delimited JSON.

---

## Concepts

| Term | Meaning |
|------|---------|
| **End device** | An ESP32 (or similar) with sensors or actuators. Announces itself on boot, sends readings, receives commands. |
| **Relay** | A second microcontroller that bridges one end device to the RS485 bus and adds location metadata to every message. |
| **system_name** | A namespace string. Devices only respond to commands that match their own `system_name`. Equivalent to a base topic in MQTT. |

---

## Installation

### From the PlatformIO registry (once published)

```ini
lib_deps = proffalken/Sigil
```

### From GitHub (available now)

```ini
lib_deps =
    https://github.com/proffalken/sigil.git
    bblanchon/ArduinoJson@^7.0.0
```

---

## Quick start — End device

```cpp
#include <SigilDevice.h>

HardwareSerial relaySerial(1);
SigilDevice device("servo01", "actuator", "home_automation");

void handleSetAngle(JsonObjectConst params) {
    int channel = params["channel"];
    int angle   = params["angle"];
    int speed   = params["speed"];
    // move your servo here
}

void setup() {
    Serial.begin(115200);  // debug

    // Freeform attributes — appear in every outgoing message
    device.addAttribute("serial",   "SV-001");
    device.addAttribute("hw_rev",   "1.0");

    device.addCapability("set_angle", "Set servo angle", {"channel", "angle", "speed"});
    device.addCapability("get_angle", "Read current servo angle", {"channel"});

    device.onCommand("set_angle", handleSetAngle);

    // Starts serial, waits 500 ms for relay to boot, then sends register
    device.begin(relaySerial, 115200, /*rx=*/9, /*tx=*/10);
}

void loop() {
    device.update();  // checks for incoming commands

    // Send a reading whenever you have new data
    device.sendReading("position", myServo.read());
    delay(1000);
}
```

---

## Quick start — Relay

```cpp
#include <SigilRelay.h>

HardwareSerial rs485(1);
HardwareSerial deviceSerial(2);
SigilRelay relay("relay01");

void setup() {
    Serial.begin(115200);  // debug

    // Freeform attributes — merged into every message forwarded upstream
    relay.addAttribute("location", "office");
    relay.addAttribute("area",     "desk");

    relay.begin(
        rs485,        115200, /*rx=*/22, /*tx=*/21,
        deviceSerial, 115200, /*rx=*/16, /*tx=*/17
    );
}

void loop() {
    relay.update();
}
```

---

## API reference

### `SigilDevice`

```cpp
SigilDevice(const char* deviceId, const char* deviceType, const char* systemName)
```
- `deviceType`: `"sensor"`, `"actuator"`, or `"sensor_actuator"`
- `systemName`: namespace — device ignores commands from other namespaces

```cpp
void addAttribute(const char* key, const char* value)
```
Add a freeform attribute included in all outgoing messages under `"attributes"`. Call before `begin()`. Capped at `SIGIL_MAX_ATTRIBUTES` (default 16).

```cpp
void addCapability(const char* name, const char* description,
                   std::initializer_list<const char*> params = {})
```
Declare a capability. Call before `begin()`. Capped at `SIGIL_MAX_CAPABILITIES` (default 8).

```cpp
void onCommand(const char* name, void (*handler)(JsonObjectConst params))
```
Register a command handler. Capped at `SIGIL_MAX_HANDLERS` (default 8).
After a matching handler runs, the device automatically sends an `"ack"`
message upstream (`"status":"ok"`). A command with no matching handler
gets `"status":"unrecognized"` instead of vanishing silently. See
"Command ack/retry" below.

```cpp
void begin(HardwareSerial& serial, uint32_t baud, int rxPin, int txPin,
           uint32_t bootDelayMs = 500)
```
Initialise the serial link. `bootDelayMs` gives the relay time to start before the register message is sent.

```cpp
void update()
```
Call every `loop()` iteration. Sends `register` on the first call, then polls for incoming messages.

```cpp
template <typename T>
void sendReading(const char* name, T value)
```
Send a single sensor reading. `T` can be any type ArduinoJson can serialise (`int`, `float`, `bool`, etc.).

```cpp
template <typename T>
void sendEvent(const char* eventName, T value)

void sendEvent(const char* eventName)
```
Send a self-describing event as a single atomic message (`msg_type: "event"`) — a discrete "this happened, here's the one fact about it" notification, e.g. `sendEvent("cycle_complete", "0142310")`. Distinct from `sendReading`: a reading is one value in an ongoing sensor stream, an event is a one-off occurrence with (optionally) one associated value. The no-value overload sends a bare event, e.g. `sendEvent("cycle_started")`. Device attributes are included automatically, same as `sendReading`.

### `SigilRelay`

```cpp
SigilRelay(const char* relayId)
```

```cpp
void addAttribute(const char* key, const char* value)
```
Add a freeform attribute merged into every message forwarded upstream. Device attributes already in the message are preserved; relay values win on key collision. Call before `begin()`.

```cpp
void begin(HardwareSerial& rs485Serial,  uint32_t rs485Baud,  int rs485Rx,  int rs485Tx,
           HardwareSerial& deviceSerial, uint32_t deviceBaud, int deviceRx, int deviceTx)
```

```cpp
void update()
```
Call every `loop()` iteration. When multiple relays share one RS485 bus,
`update()` holds at most one outgoing message and only writes it once the
bus has been quiet for `SIGIL_BUS_QUIET_MS` and a per-relay jitter delay
has elapsed (see `SIGIL_BUS_JITTER_MAX_MS` below), to reduce the chance of
two relays transmitting at the same instant. This is collision
*avoidance*, not detection or elimination — RS485 half-duplex has no way
to tell a transmission collided after the fact, so occasional corrupted
frames are still possible under load.

### Command ack/retry

The relay↔device link is a dedicated point-to-point connection, so Sigil
guarantees command delivery on that hop (not the shared bus — see below):

- The relay passively learns its attached device's `device_id`/
  `system_name` from the `"register"` message the device sends upward. It
  doesn't otherwise inspect bus traffic (see "namespace-agnostic" above).
- A `"command"` message whose `device_id`/`system_name` match what's been
  learned is tracked: the relay expects a matching `"ack"` back within
  `SIGIL_ACK_TIMEOUT_MS`. If none arrives, it re-forwards the same command
  to the device up to `SIGIL_ACK_MAX_RETRIES` times.
- If still no ack after all retries, the relay sends a synthetic
  `{"msg_type":"ack",...,"status":"timeout"}` upstream, so whatever issued
  the command gets a definitive failure signal instead of silence.
- Only one command is tracked at a time — a new matching command
  supersedes tracking of an older, not-yet-acked one.
- Commands for *other* devices on the bus, or any command seen before the
  relay has learned its own device's identity, are forwarded exactly as
  before with no tracking.
- This only covers the relay↔device hop. The controller↔relay hop over
  the shared RS485 bus has no retry here — that's the responsibility of
  whatever system issues commands.

### Compile-time limits

Override these **before** including any Sigil header:

```cpp
#define SIGIL_MAX_ATTRIBUTES     16  // max attributes per device or relay
#define SIGIL_MAX_CAPABILITIES    8  // max capabilities per device
#define SIGIL_MAX_PARAMS          8  // max params per capability
#define SIGIL_MAX_HANDLERS        8  // max command handlers per device
#define SIGIL_BUS_QUIET_MS        5  // ms the RS485 bus must be idle before a relay will transmit
#define SIGIL_BUS_JITTER_MAX_MS  20  // max per-relay jitter delay (ms) before transmitting
#define SIGIL_ACK_TIMEOUT_MS   2000  // ms the relay waits for a command ack before retrying
#define SIGIL_ACK_MAX_RETRIES     3  // max times the relay re-forwards a command awaiting ack
#include <SigilDevice.h>
```

`SIGIL_ACK_TIMEOUT_MS` should exceed the slowest expected `loop()` period
of your end device — a device that only calls `update()` once per second
(as in the quick-start example above) won't notice a command any faster
than that, so a short timeout would trigger retries against a perfectly
healthy device.

---

## Releasing

Every PR runs `pio run` against [`examples/BasicDevice`](examples/BasicDevice) to
confirm the library still builds (`.github/workflows/pr-build.yml`).

Releasing is automatic: bump the `version` field in `library.json` as part of
your PR. When it merges to `main`, `.github/workflows/release.yml` tags the
commit `vX.Y.Z`, creates a GitHub Release, and publishes the new version to
the PlatformIO registry (`registry.platformio.org`) as `lib_deps =
proffalken/Sigil`. Merges that don't change `library.json`'s version are a
no-op for this workflow.

Publishing requires a `PLATFORMIO_AUTH_TOKEN` repo secret — generate one with
`pio account token` after signing in with `pio account login`, then add it
under Settings → Secrets and variables → Actions.

---

## License

MIT — see [LICENSE](LICENSE)
