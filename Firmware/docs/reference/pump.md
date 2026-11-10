# Pump reference

[Home](../home.md) · Guide: [Pump](../pump.md)

## Purpose

Drives the MCU side of the pressure AND gate from current and `nPGD`,
and keeps a run record the motherboard can read. `PB1` is that enable.
The gate output is LM5060 enable. `PB0` is `nPGD`, active low, pulled
up on the board. Low means the switch has enhanced.

There is no pressure pin. Current on the shunt is how the firmware
tells that the motor is conducting. A hardware overcurrent latch and
a normal pressure stop look the same at the moment current falls:
`nPGD` rises and enable is still high. The phase stays held.
`RESET_PUMP` is what cycles enable and clears an LM5060 latch.

Frame bytes are on the [module link reference](module-link.md). The
host's poll and MQTT behaviour stay in the motherboard tree.

## Classes

`Pump(IPumpPort& port, IPumpStore& store, const PumpConfig& config)`
copies the settings. The phase starts off. The constructor does not
touch the pins or the store, because globals run before the chip is
ready. The first `update` loads the store.

`update(uint32_t nowMs)` samples, moves the phase, applies the
on-command age, then writes at most one changed totals byte.
`state()` returns the wire byte. `readNow`, `readCycle`, and
`readTotals` fill the three payloads. `turnOn`, `turnOff`, and
`reset` change the phase. A `turnOn` while held or running stores
the command time and leaves the pins and the open cycle alone.
Those calls may run from the TWI interrupt. `main.cpp` calls
`update` with interrupts off.

`IPumpPort` is `setEnable`, `statusIsLow`, `setRunningLamp`,
`setWarningLamp`, and `readSense`. `AvrPumpPort` is the ATtiny
adapter. `FakePumpPort` records the same calls and stays fresh, so
every desktop `update` samples. The AVR driver clears fresh and
starts the next pair only when the ADC is idle and at least 1 ms
has passed. `readSense` does not wait.

`IPumpStore` is `read` and `write` of one byte. `AvrPumpStore` is
the EEPROM. `EEPROM.update` skips a byte that already matches. A
byte that changes can stall the CPU for the program, about 3.4 ms.
`FakePumpStore` is 256 bytes that start at `0xFF`.

`AvrPumpPort::begin()` drives enable and both lamps low, leaves the
status pin an input with no internal pull-up, and starts the first
sense pair. ADC0 is 10-bit, VDD reference, prescaler 16 (1 MHz from
the 16 MHz clock), sample length 8. Voltage (AIN5) converts first,
then current (AIN4). The result interrupt stores one result and
starts the current conversion. The digital input buffers on the
sense pins stay enabled. The divider and the INA186 drive them.

## States

| Phase | Enable | `state()` | Green `PB2` | Yellow `PB3` |
| --- | --- | --- | --- | --- |
| Off | low | `0` off | off | off |
| Held | high | `1` on | off | off |
| Running | high | `1` on | on | off |
| Fault | low | `2` fault | off | on |

Held and running are reported as on so the host stores the
`SET_PUMP` result. The green lamp stays off until the phase is
running. The yellow lamp is on for every fault reason. `GET_FAULT`
returns the reason and index 0.

| Reason | Code | Rule |
| --- | --- | --- |
| Slow start | 1 | Current at or above 300 mA and `nPGD` high for 300 ms |
| Overcurrent | 2 | Current at or above 5000 mA for 60 ms, or that current was seen and the terminal then falls below 10 V while enable stays high |
| Undercurrent | 3 | Terminal at or above 10 V and current under 300 mA for 1000 ms |
| Stuck on | 4 | Enable low and the terminal stays at or above 10 V for 100 ms |

The first reason stays until `RESET_PUMP`. A current at or above
5000 mA that falls back under that level while the terminal stays
above 10 V is not remembered. A normal stop under 5000 mA does not
latch overcurrent. Enable high with neither voltage nor current
stays held.

```text
Off --turnOn, nPGD high--> Held --nPGD low--> Running
                             |                    |
                             |                    +--current falls, nPGD stays low--> Running
                             |                    +--current already low, nPGD high--> Held
                             |                    +--current stays up, nPGD high for the limit--> Fault
                             +--current stays up, nPGD high for the limit--> Fault
                             +--on-command age elapsed--> Off

Running or Held + on-command age --> Off
Fault + turnOn    stays Fault, enable stays low
Fault + turnOff   stays Fault, enable stays low
any + reset       Off, enable low, both lamps off, totals kept
Off + turnOn      accepted again
Held or Running + turnOn    same phase, enable unchanged, command time moves
```

A second `turnOn` while held or running stores the command time and
does not restart the load window or the open cycle. `turnOff` closes
an open cycle, then clears the "cycled since turn on" flag. `nPGD`
going low does not store a new command time.

Enable low is the LM5060 reset. The firmware drops enable when it
latches a fault, and again on `reset`. The reported state stays
fault until `reset`. The next `turnOn` is the rising edge on enable.

Zero current with enable high and `nPGD` high stays held. The old
fault-on-`nPGD`-alone path is gone. The load window starts on the
first sample where a cycle is open, current has not fallen, and
`nPGD` is still high. That arming sample does not fault. A jump
straight to the limit, with no earlier loaded sample, only arms.

While running with a cycle open, `nPGD` high and current still up
stays running through the window, then faults. A short high that
returns low before the limit stays running. With no open cycle,
`nPGD` high while running goes held on that sample.

## Sequences

A fresh sample scales counts, then opens or updates a cycle, then
closes it if current has been at or below `offMilliamps` for
`offDebounceMs`. After that, `nPGD` low folds that same fresh sample
into the minimum and the average. A sample from before `nPGD` went
low affects peaks and soft-start only. A sample that is not fresh
keeps the last millivolts and milliamps and does not add another
average. `nPGD` low on a stale sample waits for the next fresh
sample before it stamps power-good.

A cycle opens at `onMilliamps` while held or running. It closes when
current has been at or below `offMilliamps` for the debounce, and
also on `turnOff`, `reset`, the on-command age, and before a fault.
An off debounce of 0 closes on that same low sample.

Soft-start is the time from the first sample at or above
`onMilliamps` until voltage reaches `settledMillivolts`. It is 0
when the opening sample is already settled, and 0 when voltage
never settles. Power-good time is that same start until `nPGD` goes
low, or 0 when it does not. Both are capped at 65535 ms. Unsigned
subtraction covers the millisecond wrap.

The minimum voltage is the lowest sample after `nPGD` has gone low.
The average is the arithmetic mean of those same samples, not an
RMS. Peaks cover the whole cycle, including the ramp. Lifetime
peaks move on every in-cycle sample and are not cleared when the
next cycle opens.

A completed cycle is one where `nPGD` went low. It increments the
cycle count, adds the run seconds, stores the last run seconds, and
sets the cycled flag. A close before `nPGD` goes low increments
failed starts and does not replace the last run. The cycle payload
still shows that attempt. Fault is not an off entry, so a completed
cycle's flag can remain through the fault. Entering off clears it.

Run seconds are the cycle length divided by 1000, capped at 65535.
`GET_PUMP_NOW` reports that figure only while the cycle is open.

Unsigned subtraction times the load window and the on-command age.
One millisecond before either limit stays in the current phase. The
limit instant takes the fault or the off path. A sample that both
faults and reaches the command age stays faulted, because the load
window is applied first. With no current, that same instant is just
the command age.

`loop` passes `millis()`. A command uses the time of the last
`update`. `GET_PUMP_NOW`, `GET_PUMP_CYCLE`, `GET_PUMP_TOTALS`,
`GET_PUMP_STATE`, `PING`, and `RESET_PUMP` do not store a command
time. `RESET_PUMP` still returns to off.

The store is eight slots of 32 bytes. Byte 0 is the magic `0xC3`,
written last. Byte 1 is CRC-8/SMBus over bytes 2..31. Bytes 2..5 are
the sequence, little-endian. Then completed cycles, total seconds,
lifetime peak millivolts, lifetime peak milliamps, failed starts,
and last run seconds. The rest is zero. The newest valid slot wins.
An empty `0xFF` store loads as zeros and the next sequence is 1.
A save clears the magic first, writes the body, then the CRC, then
the magic. Power loss before the magic leaves the previous slot.
A save already in progress restarts at a new sequence.

Cycles shorter than `cycleMinMs` update RAM and do not start a
write. The next cycle that is long enough saves the RAM image, so
the earlier short cycles and failed starts persist with it. A
session of only failed starts is not written. The in-progress cycle
is not in the image. After the next reset, `GET_PUMP_CYCLE` detail
is zero until a new cycle opens.

## Errors

`turnOn`, `turnOff`, and `reset` have no status return. A rejected
`SET` while faulted still replies `Ok` with state `2`. Payload
rejects are `BadLength` on the link, and those calls do not reach
the pump.

The pump does not send `Busy`. The host retries a busy reply only a
few times, which is shorter than this window.

A zero scale denominator reads as 0. The product denominators are
not zero.

Readings assume a 3.300 V ADC reference. One count is about 17 mV
or about 6.5 mA. The 3.3 V rail's tolerance scales every reading.
The divider's filter is about 2 ms, so the soft-start time reads a
bit long next to the gate's about 9 ms note. A sag shorter than the
brown-out response can reset the chip and never appear in the record.

One programmed EEPROM byte can stall for about 3.4 ms inside the
interrupt-off section of `loop`. That is close to the host's 5 ms
MOD gate. The record is not written in one `update`, which would
miss MOD. The stall for a single changed byte is still inside that
section.

## Configuration

| Constant in `src/main.cpp` | Value | Role |
| --- | --- | --- |
| `PUMP_STATUS_LIMIT_MS` | 300 | How long current may stay up while `nPGD` stays high |
| `PUMP_COMMAND_LIMIT_MS` | 180000 | How long held or running may continue after the last `SET_PUMP` on |
| `PUMP_ON_MILLIAMPS` | 300 | Current that opens a cycle |
| `PUMP_OFF_MILLIAMPS` | 150 | Current that starts the off debounce |
| `PUMP_OFF_DEBOUNCE_MS` | 20 | How long current must stay down before the cycle closes |
| `PUMP_SETTLED_MILLIVOLTS` | 10000 | Voltage that ends the soft-start time |
| `PUMP_CYCLE_MIN_MS` | 200 | Shortest cycle that starts a store write |
| `PUMP_MILLIVOLTS_NUM` / `DEN` | 50 / 3 | `mV = counts * 50 / 3`. 720 counts is 12000 mV |
| `PUMP_MILLIAMPS_NUM` / `DEN` | 6600 / 1023 | `mA = counts * 6600 / 1023`. 620 counts is 4000 mA. 775 counts is 5000 mA |
| `PUMP_OVERCURRENT_MILLIAMPS` | 5000 | Current that arms the overcurrent watch |
| `PUMP_OVERCURRENT_LIMIT_MS` | 60 | How long that current may last. Inside the LM5060's about 85 ms timer |
| `PUMP_UNDERCURRENT_MILLIAMPS` | 300 | Current below this, while the terminal stays settled, arms undercurrent |
| `PUMP_UNDERCURRENT_LIMIT_MS` | 1000 | How long that pair may last |
| `PUMP_STUCK_ON_LIMIT_MS` | 100 | How long the terminal may stay settled while enable is low |

The scale is for a 10-bit conversion against 3.300 V. The divider is
100 kΩ over 24 kΩ. The shunt is 20 mΩ into an INA186A1 at gain 25,
so the output is 0.5 V/A and zero current is 0 V. Full scale is
about 17050 mV and 6600 mA.

The schematic timer note, from the 0.47 µF capacitor on `TIMER`, is
about 157 ms startup, 72 ms transition, and 85 ms overcurrent. 300 ms
is past those. Pins are on the
[architecture reference](architecture.md).

## Tests

`test/test_desktop/test_pump.cpp` drives `FakePumpPort` and
`FakePumpStore`. `runPumpTests()` is called from
`test_module_link.cpp`, which owns `main()`.

| Test | Path |
| --- | --- |
| `testBootWithStatusHighStaysOff` | Status high at boot, and much later, stays off |
| `testZeroCurrentStaysHeldPastTheLimit` | `turnOn` with no current stays held past the old nPGD limit |
| `testSecondTurnOnDoesNotRestartTheWindow` | A later `turnOn` still faults at the original loaded sample's limit, and does not call `setEnable` |
| `testStatusLowDuringStartTurnsGreenOn` | `nPGD` low while held lights green |
| `testStatusHighThroughLimitFaults` | Loaded current with `nPGD` high at the limit faults. A later low stays faulted |
| `testLimitBoundary` | One millisecond before the limit is still held. The limit instant faults |
| `testUnarmedLimitSampleDoesNotFault` | The sample at the limit only arms. The next full window faults |
| `testStatusLowAtTurnOnIsRunning` | `nPGD` already low at `turnOn` is running. A second `turnOn` stays there |
| `testStatusBlipWhileRunningStaysOn` | A loaded high that returns low stays running. The next full window faults |
| `testStatusHighWhileRunningFaults` | Loaded current and `nPGD` high for the run window faults |
| `testRunningWithNoCurrentGoesHeldWhenNpgdRises` | No open cycle and `nPGD` high leaves held on that sample |
| `testCommandsWhileFaultedAreIgnored` | `turnOn` and `turnOff` leave the fault latched |
| `testTurnOffFromHeldAndRunning` | `turnOff` drops enable. A later high stays off |
| `testResetFromFaultOnAndOff` | `reset` from fault, running, and off returns to off. `turnOn` works again |
| `testLimitUsesUnsignedWrap` | The load window crosses `0xFFFFFFFF` |
| `testStatusLimitIsTheConstructorArgument` | A 50 ms limit faults at 50 ms |
| `testCommandAgeBoundary` | Still on at 179999 ms. Off at 180000 ms |
| `testRepeatedOnExtendsCommandAge` | On at 0, 60000, and 120000 ms. Enable is not written again |
| `testRepeatedOnWhileHeldLeavesEnableHigh` | A second `turnOn` while held does not call `setEnable` |
| `testCommandLimitIsTheConstructorArgument` | A 1000 ms command age turns the pump off at 1000 ms |
| `testCommandAgeWhileHeldTurnsOff` | Held with no current turns off at the command age |
| `testStatusLowDoesNotRefreshCommandAge` | `nPGD` going low leaves the command age on the original `turnOn` |
| `testStatusFaultWinsOnTheSharedInstant` | Load window and command age on the same loaded sample stay faulted |
| `testSharedInstantWithNoCurrentTurnsOff` | That same instant with no current turns off |
| `testCommandAgeLeavesFaultLatched` | A fault stays faulted across the command age |
| `testCommandAgeUsesUnsignedWrap` | The command age crosses `0xFFFFFFFF` |
| `testTurnOnAfterCommandAge` | `turnOn` is accepted after the command age turns the pump off |
| `testHeldThenCurrentRiseStartsClean` | Held for 30 s at no current, then current and `nPGD` low, runs with a 10 ms soft-start |
| `testLoadedCurrentAndNpgdLowRuns` | 4000 mA and `nPGD` low inside the window is running. Now reports 12000 mV and 4000 mA |
| `testCurrentBlipBeforeNpgdIsFailedStart` | Current that ends before `nPGD` is one failed start. The phase stays held |
| `testCurrentFallWithNpgdLowStaysRunning` | Current falling while `nPGD` stays low closes the cycle and leaves green on |
| `testRunClosesIntoHeldWithRecord` | Ramp, then power-good, then current and `nPGD` fall. Minimum ignores the ramp. Averages are the two good samples. The next cycle does not clear the lifetime peak |
| `testScaleUsesTheProductFractions` | 720 counts is 12000 mV. 620 counts is 4000 mA |
| `testZeroDenominatorReadsZero` | A zero denominator reads as 0 |
| `testSoftStartUsesUnsignedWrap` | Soft-start crosses `0xFFFFFFFF` and reports 9 ms |
| `testStaleSampleDoesNotExtendAverage` | A sample that is not fresh does not change the average |
| `testPowerGoodWaitsForFreshSample` | `nPGD` low on a stale sample waits for the next fresh sample |
| `testZeroDebounceClosesOnTheSameSample` | Debounce 0 closes on the first low sample and stays running while `nPGD` is low |
| `testCommandAgeClosesOpenCycle` | The command age closes the cycle and clears the cycled flag. The `nPGD` flag still follows the pin |
| `testRepeatedTurnOnLeavesOpenCycle` | A repeated `turnOn` refreshes the age and leaves the cycle start |
| `testTurnOffClosesCycle` | `turnOff` closes the cycle and clears the cycled flag |
| `testResetKeepsTotals` | `reset` clears a fault and keeps the completed count and the failed start |
| `testLongCyclePersists` | A cycle of 200 ms reloads its count and lifetime peaks. The cycle detail does not |
| `testShortCycleDoesNotPersist` | A cycle under 200 ms is in RAM and is absent from a new pump |
| `testLaterLongCyclePersistsEarlierTotals` | The long cycle's image includes the earlier short cycle and the failed start |
| `testTruncatedSaveDoesNotLoad` | Five writes after a save starts do not load |
| `testTruncatedSaveLeavesPreviousSlot` | An abandoned next save leaves the finished slot |
| `testOvercurrentAtSixtyMsFaults` | 5000 mA for 60 ms is overcurrent. 59 ms stays held |
| `testOvercurrentCollapseFaults` | A high sample then a falling terminal is overcurrent |
| `testOvercurrentClearsWhenRunContinues` | Current falling under 5000 mA while the terminal stays up clears the episode |
| `testNormalStopIsNotOvercurrent` | A run under 5000 mA can lose terminal voltage and stay held |
| `testOvercurrentUsesUnsignedWrap` | The 60 ms window wraps with `millis` |
| `testUndercurrentAtOneSecondFaults` | 12 V and no current for 1 s is undercurrent |
| `testUndercurrentResetsWhenVoltageFalls` | Voltage falling restarts that window |
| `testConductingCurrentIsNotUndercurrent` | 303 mA at 12 V stays running |
| `testStuckOnAtOneHundredMsFaults` | Enable low and 12 V for 100 ms is stuck on |
| `testStuckOnResetsWhenVoltageFalls` | Voltage falling restarts that window |
| `testSlowStartReasonStays` | The loaded `nPGD` window reports slow start, and a later high current leaves that reason |
| `testResetClearsFaultReason` | `reset` clears the reason. A later `turnOn` runs |

Link cases for the three record commands are on the
[module link reference](module-link.md).

```text
C:\Users\Nathan\.platformio\penv\Scripts\platformio.exe test -e native
```

## Source

- `lib/Logic/Pump.h`
- `lib/Logic/Pump.cpp`
- `lib/Interfaces/IPumpPort.h`
- `lib/Interfaces/IPumpStore.h`
- `lib/Drivers/AvrPumpPort.h`
- `lib/Drivers/AvrPumpPort.cpp`
- `lib/Drivers/AvrPumpStore.h`
- `lib/Drivers/AvrPumpStore.cpp`
- `test/test_desktop/test_pump.cpp`
- `test/test_desktop/fakes/FakePumpPort.h`
- `test/test_desktop/fakes/FakePumpStore.h`
