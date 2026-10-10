# Slave port reference

[Home](../home.md) · Guide: [Slave port](../slave-port.md)

## Purpose

One slave port, two adapters. The link enables TWI, commits an address,
and queues a reply. `AvrTwi0Slave` does that on TWI0.
`FakeModuleSlavePort` records the same calls.

## Classes

`IModuleSlavePort`:

| Method | Role |
| --- | --- |
| `enable(address7bit)` | Start listening at a 7-bit address |
| `disable()` | Stop acknowledging |
| `setAddress(address7bit)` | Commit a new address without leaving the bus |
| `setReply(frame, length)` | Copy the next reply. The pointer is valid only for this call |
| `setMasterWriteHandler(handler, context)` | One handler. A later call replaces it |

`AvrTwi0Slave(sdaPin, sclPin)` implements that on `Wire`.
`FakeModuleSlavePort` records `enable`, `disable`, `setAddress`, and
`setReply`. `deliver` invokes the registered handler.

## States

The driver starts disabled. `enable` selects this instance, clears the
reply length, calls `Wire.pins` and `Wire.usePullups`, then
`Wire.begin(address7bit, false)`, and registers `onReceive` and
`onRequest`. `disable` calls `Wire.end()`, clears the two pull-ups,
and clears the enabled flag. `setAddress` writes `TWI0.SADDR` only
while enabled.

## Sequences

`enable` calls `Wire.pins` before `Wire.begin`. `begin` prepares the
pins from the mux that is already selected. The `false` argument
leaves general call off.

`setAddress` writes `TWI0.SADDR` as `address7bit << 1`. Bit 0 stays
clear, so general call stays off. The write is safe from the stop
callback. It does not call `Wire.begin` again: a second `begin` while
the client is already enabled returns without changing `SADDR`.

On this core, `onReceive` runs when the next address or STOP arrives.
A repeated-start read then calls `onRequest` in that same interrupt,
after the write handler returns. `setReply` is in place before the
host clocks the 19 bytes. A `SET_ADDRESS` STOP commits `SADDR` before
the next START.

`disable` stops TWI0. The pins stay inputs, and the internal pull-ups
are cleared. The motherboard's 4.7 kΩ resistors hold the bus.

`enable` turns on the internal pull-ups on `SDA_PIN` and `SCL_PIN`, in
parallel with those resistors.

`_onReceive` reads the bytes `Wire` has and calls the handler. A
missing instance or a missing handler returns. `_onRequest` clocks out
the stored reply. A reply length of 0 sends nothing.

The default TWI pins are PB1 (SDA) and PB0 (SCL). This board uses
those pins for the pump. `SDA_PIN` is PA1 and `SCL_PIN` is PA2, which
is alternate mux 1.

## Errors

`setReply` with a null frame stores a length of 0, so the next read
sends nothing. A length above `kMaxFrameBytes` is stored as 19 bytes.

`setAddress` while disabled does not write `TWI0.SADDR`.

`_onReceive` passes the handler the number of bytes read from `Wire`.
When `Wire`'s count is larger than that, the count is the length
instead. Only the first `kMaxFrameBytes` are stored. The link then
applies its length check to that buffer.

The handler runs in the TWI interrupt and finishes inside
`kClockStretchMaxMs` (40 ms).

## Configuration

The address comes from the link: `kUnconfiguredAddress`, then the
assigned address. The pin numbers come from the constructor. The
sketch passes `SDA_PIN` and `SCL_PIN`, listed on the
[architecture reference](architecture.md).

The part's Wire buffer is 32 bytes. The host reads 19.

## Tests

There is no separate driver test. The module-link tests drive
`FakeModuleSlavePort`. The cases are on the
[module link reference](module-link.md). The interrupt path is not
executed on the desktop. `pio run -e Upload_UPDI` compiles
`AvrTwi0Slave`.

## Source

- `lib/Interfaces/IModuleSlavePort.h`
- `lib/Drivers/AvrTwi0Slave.h`
- `lib/Drivers/AvrTwi0Slave.cpp`
- `test/test_desktop/fakes/FakeModuleSlavePort.h`
