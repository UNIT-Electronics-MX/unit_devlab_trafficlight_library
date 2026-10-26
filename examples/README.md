# DevLab_TrafficLights examples

| Example | Purpose |
|---|---|
| `traffic/trafficLightsDemo` | Cycle red, yellow, green and off, reading the mask back after each change. |
| `traffic/serialControl` | Control each light from the Serial monitor (`red`, `on g`, `off y`, `status`...). |
| `traffic/nonBlockingSequence` | Run a green/yellow/red/red+yellow sequence with `millis()` instead of `delay()`. |
| `traffic/multiModule` | Drive three modules with different addresses on one bus. |
| `traffic/multiTrafficController` | Coordinate 2 to 8 modules (single, pairs, green wave, blink, all red); also assigns their addresses. |
| `i2c/scanBus` | Scan the bus and list DDP devices, flagging the traffic lights module. |
| `i2c/changeAddress` | Scan, change (`change 36 37`) or restore (`factory`) the address of a module. |
| `i2c/busDiagnostics` | Raw Wire-only check that shows SDA/SCL levels and every address result at 100 and 400 kHz; use it when modules are not found. |
| `i2c/readAddress` | Read the address a module reports and whether it is a user or factory address. |

All examples verify Device ID `0x0116` before sending any command (default
address `0x36`; use `i2c/changeAddress` to give each module its own). Lights are set with command `0xA8 | mask` (the module answers DDP
status `0x00`) and read back with command `0x80`.

Wiring (`Wire`):

| Master | SDA | SCL |
|---|---|---|
| ESP32 | GPIO6 | GPIO7 |
| Pulsar RP2350A | GPIO24 | GPIO25 |
| Other RP2040 / RP2350 | GPIO12 | GPIO13 |

Connect the master's GND to the module's GND and use Serial at 115200 baud.
The module firmware targets a 100 kHz bus; the master provides the pull-ups.

To change an address, open Serial and enter `scan`, then e.g. `change 36 37`.
Connect one module at a time while they share the same address. Address
changes need module firmware 1.2 or later.

## Several coordinated traffic lights

`traffic/multiTrafficController` uses the address plan `0x40 + index` (light #0
is `0x40`, #1 is `0x41`...). Assigned modules never use the factory address
`0x36`, so you can add one new module at a time without unplugging the others:

1. Connect the first module, enter `assign` (it becomes #0 at `0x40`).
2. Connect the next module, enter `assign` (#1 at `0x41`), and so on. `assign 3`
   picks the index yourself; `move 1 3` renumbers a light.
3. `id <n>` flashes light n so you can match each index to its position.
4. `count 4`, `mode single`, `start`.

Modes: `single` (one green at a time, the others red), `pairs` (opposite lights
share green, for 4, 6 or 8 lights), `wave` (green wave, each light starts
`offset` ms after the previous one), `blink` (flashing yellow) and `red`.
`time <green> <yellow> <clear>` sets the milliseconds, where `clear` is the
all-red pause between phases. `factory <n>` returns a light to `0x36`.
