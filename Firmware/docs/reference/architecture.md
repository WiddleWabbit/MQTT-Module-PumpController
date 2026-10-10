# Architecture reference

[Home](../home.md) · Guide: [Architecture](../architecture.md)

## Purpose

`src/main.cpp` wires this daughterboard. It owns the pins and the
product settings, constructs the adapter and the link, and calls the
link once per pass.

## Classes

`setup` drives `SNS_PIN`, configures `MOD_PIN`, and holds the pump
enable low. Both LEDs light for one second at boot, then stay low.
Delete that pause once the LEDs are confirmed. `loop` reads `MOD_PIN`
and calls `ModuleLink::update`. The objects are `moduleSlave`
(`AvrTwi0Slave`) and `moduleLink` (`ModuleLink`).

## States

The sketch has no mode of its own. Listening, assignment, and replies
are the link's states, on the [module link reference](module-link.md).

## Sequences

1. Construction registers the master-write handler. The slave stays off.
2. `setup` drives `PMP_EN_PIN` low, lights `P_ON_PIN` and `P_WARN_PIN` for one second, then drives both LEDs low. That pause is temporary.
3. `setup` sets `SNS_PIN` as an output driven low and `MOD_PIN` as an input with pull-up.
4. Each `loop` reads `MOD_PIN` and calls the link. The call returns without waiting.

The host holds MOD low for `kModGateMaxMs` (5 ms). Because `loop` does
not block, the unconfigured address is enabled on the pass that sees
MOD low.

## Errors

Protocol status bytes come from the link. A handler that runs in the
TWI interrupt finishes inside `kClockStretchMaxMs` (40 ms). Both limits
are in `ModuleProtocol.h`.

## Configuration

Declared in `src/main.cpp`.

| Constant | Value | Port | Role |
| --- | --- | --- | --- |
| `SDA_PIN` | 8 | PA1 | Motherboard SDA, TWI0 alternate |
| `SCL_PIN` | 9 | PA2 | Motherboard SCL, TWI0 alternate |
| `MOD_PIN` | 2 | PA6 | Enumeration select, input pull-up |
| `SNS_PIN` | 3 | PA7 | Seated detect, driven low |
| `RESET_PIN` | 10 | PA3 | Reset button. This pass does not read it |
| `P_WARN_PIN` | 0 | PA4 | Yellow warning LED, held low |
| `P_ON_PIN` | 1 | PA5 | Green pump-on LED, held low |
| `PMP_EN_PIN` | 6 | PB1 | Pump enable, held low. Default TWI SDA |
| `PMP_STATUS_PIN` | 7 | PB0 | Pump status. This pass does not read it. Default TWI SCL |
| `MODULE_TYPE` | `kTypePumpModule` | | Reported by `GET_IDENTITY` |
| `FIRMWARE_VERSION` | 1 | | Reported by `GET_IDENTITY` |

`kTypePumpModule` is `0x0300` in `ModuleProtocol.h`. PA0 stays the UPDI
pin.

The image is an ATtiny1614 at 16 MHz from the internal oscillator.
`Upload_UPDI` programs it with SerialUPDI at 115200 on COM3. `fuses`
sets brown-out at 2.6 V and EEPROM save, and leaves the UPDI pin as
UPDI. Those commands are in the firmware `README.md`.

## Tests

Desktop cases and the command that runs them are on the
[module link reference](module-link.md).

The image build is:

```text
pio run -e Upload_UPDI
```

That environment does not run the desktop tests. The TWI interrupt
path is not executed on the desktop.

## Source

- `src/main.cpp`
- `platformio.ini`

Sources in a library root are compiled when one of its headers is
included. `platformio.ini` does not list `lib_deps` for that reason.
