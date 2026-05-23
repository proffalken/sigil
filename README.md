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
lib_deps = mattmacwall/Sigil
```

### From GitHub (available now)

```ini
lib_deps =
    https://github.com/mattmacwall/sigil.git
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
SigilRelay relay("relay01", "office");

void setup() {
    Serial.begin(115200);  // debug
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
void addCapability(const char* name, const char* description,
                   std::initializer_list<const char*> params = {})
```
Declare a capability. Call before `begin()`. Capped at `SIGIL_MAX_CAPABILITIES` (default 8).

```cpp
void onCommand(const char* name, void (*handler)(JsonObjectConst params))
```
Register a command handler. Capped at `SIGIL_MAX_HANDLERS` (default 8).

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

### `SigilRelay`

```cpp
SigilRelay(const char* relayId, const char* location)
```

```cpp
void begin(HardwareSerial& rs485Serial,  uint32_t rs485Baud,  int rs485Rx,  int rs485Tx,
           HardwareSerial& deviceSerial, uint32_t deviceBaud, int deviceRx, int deviceTx)
```

```cpp
void update()
```
Call every `loop()` iteration.

### Compile-time limits

Override these **before** including any Sigil header:

```cpp
#define SIGIL_MAX_CAPABILITIES  8   // max capabilities per device
#define SIGIL_MAX_PARAMS        8   // max params per capability
#define SIGIL_MAX_HANDLERS      8   // max command handlers per device
#include <SigilDevice.h>
```

---

## Message schema

See the [WHH project documentation](https://github.com/mattmacwall/WHH) for the full JSON message schema that Sigil implements.

---

## Publishing to the PlatformIO registry

1. Create a public GitHub repo and push this library to it
2. Sign up for a free account at [registry.platformio.org](https://registry.platformio.org)
3. Install the PlatformIO CLI: `pip install platformio`
4. From the repo root, run:
   ```bash
   pio pkg publish
   ```
5. The library will be available as `lib_deps = mattmacwall/Sigil`

Update `library.json` with the correct GitHub URL before publishing.

---

## License

MIT — see [LICENSE](LICENSE)
