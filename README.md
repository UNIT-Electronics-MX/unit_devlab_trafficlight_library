# DevLab_TrafficLights

Arduino library for the UE0183 I2C traffic lights module using the DevLab
Device Protocol (DDP) over I2C.

This library wraps the shared
[`DevLabDDP`](https://github.com/UNIT-Electronics-MX/unit_devlab_ddp_library)
master and the
[`DevLab_Interface`](https://github.com/UNIT-Electronics-MX/unit_devlab_interface_library)
`DevLab_I2C_Orchestrator` bus class into a single `DevLab_TrafficLights` object.
Firmware: `unit_firmware_i2c_traffic_lights_module`.

Compatible with ESP32, RP2040/RP2350, STM32 and AVR.

# Features

- Device identification (Device ID `0x0116`) before any command
- `setMask()` sets red, yellow and green at once and checks the DDP status
- `setLight()`, `red()`, `yellow()`, `green()`, `allOn()`, `allOff()` helpers
- `readMask()` / `readLight()` read the real output state from the module
- Changeable I2C address (`0x08` to `0x77`, factory `0x36`) kept in the module's Flash, with factory restore, so several modules can share one bus
- 100 kHz bus
- Bus recovery (`beginRecovered`) for a slave left mid-transaction

# Quick Start Example

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <DevLab_TrafficLights.h>

DevLab_TrafficLights lights(Wire, DevLab_TrafficLights::DEFAULT_ADDRESS, 100000);

void setup() {
  Serial.begin(115200);
  // ESP32: 6/7. RP2040/RP2350: 24/25 (Pulsar) or 12/13.
  if (!lights.beginRecovered(6, 7)) {
    Serial.println("Traffic lights module not found");
  }
}

void loop() {
  lights.red();    delay(1000);
  lights.yellow(); delay(1000);
  lights.green();  delay(1000);
  lights.allOff(); delay(1000);
}
```

# API

| Method | Description |
|---|---|
| `DevLab_TrafficLights(wire, address, clock)` | Create the object (default `Wire`, `0x36`, 100 kHz). |
| `begin()` / `begin(sda, scl, clock)` | Start the bus and verify the device. |
| `beginRecovered(sda, scl, timeoutUs, restart)` | Same, clearing a stuck bus first. |
| `setMask(mask)` | Set all lights; `RED`, `YELLOW`, `GREEN`, `ALL` combine with `\|`. |
| `setLight(lights, on)` | Turn lights on or off, keeping the others (reads the module first). |
| `red()` / `yellow()` / `green()` | Show only that light. |
| `allOn()` / `allOff()` | All lights on / off. |
| `readMask(mask)` / `readLight(light, on)` | Read the state from the module. |
| `setAddress(newAddress)` | Store a new I2C address in the module (it restarts) and retarget this object. |
| `resetFactoryAddress()` | Restore the factory address `0x36` and retarget this object. |
| `readAddress(address)` / `readAddressSource(fromFlash)` | Address the module reports, and whether it was set by the user. |
| `mask()` | Last mask confirmed by the module. |
| `isConnected()` / `busReady()` | Result of the last `begin`. |
| `deviceInfo()` / `printInfo(out)` | Identity reported by the module. |
| `protocol()` / `bus()` | Access the underlying DDP master and I2C bus. |

All calls return `false` on an I2C error or if `begin` has not verified the
device. Logical bits: green = 0 (PA2), yellow = 1 (PA1), red = 2 (PA0); `1`
means lit. A set uses command `0xA8 | mask` and returns DDP status `0x00`; the
mask is read with command `0x80`. The module keeps its last light state until reset
(an address change restarts it, so the lights go off) and has no autonomous timing, so the controller chooses the sequence.

# Using several modules

Every module leaves the factory at `0x36`, so give each one its own address
before putting them on the same bus:

1. Connect **one** module and run `examples/i2c/changeAddress`.
2. Enter `change 36 37`. The address is stored in Flash and survives power loss.
3. Repeat with the next module (`change 36 38`, ...).
4. To return a module to `0x36`, enter `factory` (or `factory 37` if several
   modules are connected).

Then create one object per module, e.g.
`DevLab_TrafficLights second(Wire, 0x37)` (see `examples/traffic/multiModule`). For a coordinated set with serial
assignment and ready-made sequences, use `examples/traffic/multiTrafficController`,
which numbers the lights `0x40 + index` so new modules can be added without
unplugging the others.
Address changes need module firmware 1.2 or later; older firmware answers on
`0x36` only. Firmware 1.2 also fixes SCL being pulled low by the module, which
made two or more modules fail on one bus: reflash every module. The modules
have no I2C pull-ups of their own, so the master or its Qwiic port must supply
them (the ESP32 internal ones are enough for a short bus of a few modules).

# License

MIT, see [LICENSE](LICENSE).
