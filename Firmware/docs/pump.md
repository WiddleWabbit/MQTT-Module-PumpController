# Pump

[Home](home.md) · Reference: [Pump](reference/pump.md)

## Summary

`Pump` drives the MCU side of the pressure AND gate, the two lamps,
and the run record. Enable high does not by itself mean the switch
is on: the pressure input has to be high as well. `nPGD` low means
the switch has enhanced. Current through the shunt is what opens and
closes a cycle.

Held is enable high while `nPGD` is high and the motor is not holding
current. Running is enable high after `nPGD` has gone low. The first
fault reason latches: slow start, overcurrent, undercurrent, or a
stuck output. The green lamp follows running. The yellow lamp follows
fault. Held and running are both reported as on. `GET_FAULT` reports
the reason.

## Where it sits

`main.cpp` constructs `AvrPumpPort` and `AvrPumpStore`, then `Pump`
with those and the `PumpConfig` built from the product constants. It
passes the pump to `ModuleLink`. `loop()` calls `update(millis())`
with interrupts off. The link calls `turnOn`, `turnOff`, `reset`,
`state`, `readFault`, and the three record reads from the TWI
interrupt.
`setup()` calls `AvrPumpPort::begin()` before it drives `SNS_PIN`.

## What it creates

```text
Pump
  IPumpPort       enable, nPGD, lamps, voltage and current counts
  IPumpStore      totals and the fault reason, kept across reset
```

`main.cpp` supplies `AvrPumpPort` and `AvrPumpStore`. Tests supply
`FakePumpPort` and `FakePumpStore`.

## One update

```text
update(nowMs)
  load the store the first time
  sample nPGD, and the sense pair when a new one is ready
    open a cycle at 300 mA while held or running
    track peaks and soft-start
    close the cycle after 150 mA for the off debounce
  if nPGD is low: running, and fold a fresh sample into the average
  if current is still up and nPGD is still high:
    arm the load window, then fault at the limit
  if running and the load has already fallen: held
  on-command age: held or running turns off
  program at most one changed totals byte, or the fault byte
```

`turnOn` from off stores the command time. If `nPGD` is already low
the phase is running. Otherwise it is held, which is still reported
as on. A later `turnOn` while held or running stores the command time
again and leaves enable and the open cycle alone. `turnOff` from held
or running returns to off and closes an open cycle. `turnOn` and
`turnOff` leave a fault latched. `reset` returns to off from any
phase and keeps the totals. A later `turnOn` is accepted.

Current that falls while `nPGD` stays low closes the cycle and leaves
the green lamp on. The green lamp follows `nPGD`, not the current.

## What is stored, and who reads it

The phase, the on-command time, the open or last cycle, and the
totals. `state()` returns `0` off, `1` on, or `2` fault. The three
record reads fill `GET_PUMP_NOW`, `GET_PUMP_CYCLE`, and
`GET_PUMP_TOTALS`. The host reads those replies.

Totals for a cycle of at least `PUMP_CYCLE_MIN_MS` are written to the
store, one changed byte per `update`. A shorter cycle stays in RAM
until a later long cycle saves the image. After a chip reset the cycle
detail is gone. The counts, lifetime peaks, failed starts, and last
run seconds come back from the store on the first `update`.

The latched reason is byte 224. A chip reset loads it and comes back
faulted, with enable low and the yellow lamp on. `reset()` clears the
RAM fault immediately. The next `update` writes `0` to that byte, and
that pass does not also program a totals byte. A chip reset before
that write comes back faulted.

## See also

- [Pump reference](reference/pump.md) — states, the record, tests.
- [Module link](module-link.md)
- [Architecture](architecture.md)
