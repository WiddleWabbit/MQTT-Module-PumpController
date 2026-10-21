# Module link

[Home](home.md) · Reference: [Module link](reference/module-link.md)

## Summary

`ModuleLink` is the slave side of the motherboard module protocol for
this pump board. It listens at `0x0A` only while MOD is low, then
keeps the assigned address. It reports the type and firmware version
supplied at construction, and protocol version 1.

`GET_PUMP_STATE`, `SET_PUMP`, and `RESET_PUMP` call the pump and reply
with its state byte. A command this board does not implement returns
`UnknownCmd`.

The module includes no Arduino headers.

## Where it sits

`main.cpp` constructs it with `AvrTwi0Slave`, the pump, `MODULE_TYPE`
(`kTypePumpModule`), and `FIRMWARE_VERSION`. `loop()` calls
`update(modIsLow)`. A master write arrives through the handler the
constructor registered, from the TWI receive callback or from
`FakeModuleSlavePort::deliver`.

## What it creates

```text
ModuleLink
  IModuleSlavePort    enable, disable, setAddress, setReply
  Pump                state for GET, SET, and RESET
```

The link does not create the pump. `main.cpp` does, and passes it in.
The link cannot be copied: the port stores the link as the write
context.

## One update

```mermaid
flowchart TD
  upd["update(modIsLow)"] --> assigned{"assigned?"}
  assigned -->|yes| keep["leave the port as it is"]
  assigned -->|no| low{"modIsLow?"}
  low -->|yes| en["enable 0x0A if it is off"]
  low -->|no| dis["disable if it is on"]
```

A master write is a separate path. The port calls the handler from the
TWI interrupt, or the fake calls it from `deliver`. Commands, rejects,
and the padded reply are in the [reference](reference/module-link.md).

## What is stored, and who reads it

`_typeId` and `_firmwareVersion` come from the constructor.
`GET_IDENTITY` reads them.

`_address` starts at `kUnconfiguredAddress`. `update` enables that
address while MOD is low. An accepted `SET_ADDRESS` replaces it and
sets `_assigned`. `_enabled` records whether the port is listening.
Once `_assigned` is set, `update` leaves the port alone.

The link does not keep the last reply. `setReply` copies it into the
port before the handler returns. The host reads that copy.

## See also

- [Module link reference](reference/module-link.md) — states, commands, errors, tests.
- [Pump](pump.md) — what the state byte means.
- [Slave port](slave-port.md)
- [Architecture](architecture.md)
