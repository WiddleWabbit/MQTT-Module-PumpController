# Pump reference

[Home](../home.md) · Guide: [Pump](../pump.md)

## Purpose

Drives one LM5060 switch from `nPGD` and reports off, on, or fault.
`PB0` is `nPGD`, active low, pulled up on the board. Low means the
switch has started. High while the switch is off is the idle level.
High while enable is high is still starting, or a fault once it has
lasted the status limit.

Frame bytes for `GET_PUMP_STATE`, `SET_PUMP`, and `RESET_PUMP` are on
the [module link reference](module-link.md). The host's poll and MQTT
behaviour stay in the motherboard tree.

## Classes

`Pump(IPumpPort& port, uint32_t statusLimitMs, uint32_t commandLimitMs)`
stores the port and both limits. The phase starts off. The constructor
does not drive pins.

`update(uint32_t nowMs)` samples `nPGD` while starting or on, then
applies the on-command age. `state()` returns the wire byte. `turnOn`,
`turnOff`, and `reset` change the phase. A `turnOn` while starting or
on stores the command time and leaves the pins alone. Those three, and
`state`, may run from the TWI interrupt. `main.cpp` calls `update`
with interrupts off.

`IPumpPort` is `setEnable`, `statusIsLow`, `setRunningLamp`, and
`setWarningLamp`. `AvrPumpPort` is the ATtiny adapter.
`FakePumpPort` records the same calls. `AvrPumpPort::begin()` drives
enable and both lamps low, and leaves the status pin an input with
no internal pull-up.

## States

| Phase | Enable | `state()` | Green `PA5` | Yellow `PA4` |
| --- | --- | --- | --- | --- |
| Off | low | `0` off | off | off |
| Starting | high | `1` on | off | off |
| On | high | `1` on | on | off |
| Fault | low | `2` fault | off | on |

Starting is reported as on so the host stores the `SET_PUMP` result.
The green lamp stays off until `nPGD` reads low.

```text
Off --turnOn--> Starting --nPGD low--> On --nPGD high for the limit--> Fault
                  |                      |
                  |                      +--on-command age elapsed--> Off
                  +--still high at the status limit--> Fault
                  +--on-command age elapsed--> Off

Fault + turnOn   stays Fault, enable stays low
Fault + turnOff  stays Fault, enable stays low
any + reset      Off, enable low, both lamps off
Off + turnOn     accepted again
Starting or On + turnOn    same phase, enable unchanged, command time moves
```

A second `turnOn` while starting or on stores the command time and
does not restart the `nPGD` window. `turnOff` from starting or on
returns to off. A high after that is the idle level. `nPGD` going low
does not store a new command time.

Enable low is the LM5060 reset. The firmware drops enable when it
latches a fault, and again on `reset`. The reported state stays fault
until `reset`. The next `turnOn` is the rising edge on enable.

## Sequences

Unsigned subtraction times the window, so it is valid across the
millisecond wrap. One millisecond before the limit stays in the
current phase. The limit instant faults.

While on, the window starts at the first high sample. If `nPGD`
returns low before the limit, the pump stays on and the next high
starts a new window.

A fault ignores `nPGD` after that. `nPGD` going low does not clear
it. `reset` does. `turnOn` then starts a new window, including when
`nPGD` is still high.

`loop` passes `millis()`. A command uses the time of the last
`update`. The host's next `GET_PUMP_STATE` is up to one poll interval
later. The lamp follows the phase on the sample that latches it.

The on-command age uses the same unsigned subtraction. It starts at
the `turnOn` that left off, and again at each `turnOn` while starting
or on. One millisecond before `commandLimitMs` stays in the current
phase. That instant turns the pump off: enable low, both lamps off,
state `0`. `PING`, `GET_PUMP_STATE`, and `RESET_PUMP` do not store a
command time. `RESET_PUMP` still returns to off. A sample that both
faults on `nPGD` and reaches the command age stays faulted, because
`nPGD` is sampled first.

## Errors

`turnOn`, `turnOff`, and `reset` have no status return. A rejected
`SET` while faulted still replies `Ok` with state `2`. Payload
rejects are `BadLength` on the link, and those calls do not reach
the pump.

The pump does not send `Busy`. The host retries a busy reply only a
few times, which is shorter than this window.

## Configuration

| Constant in `src/main.cpp` | Value | Role |
| --- | --- | --- |
| `PUMP_STATUS_LIMIT_MS` | 300 | How long `nPGD` may stay high before a fault |
| `PUMP_COMMAND_LIMIT_MS` | 180000 | How long starting or on may continue after the last `SET_PUMP` on |

The schematic timer note, from the 0.47 µF capacitor on `TIMER`, is
about 157 ms startup, 72 ms transition, and 85 ms overcurrent. 300 ms
is past those, so a high that is still starting, or a dip the switch
may still ride through, is not a fault. Pins are on the
[architecture reference](architecture.md).

## Tests

`test/test_desktop/test_pump.cpp` drives `FakePumpPort`.
`runPumpTests()` is called from `test_module_link.cpp`, which owns
`main()`.

| Test | Path |
| --- | --- |
| `testBootWithStatusHighStaysOff` | Status high at boot, and much later, stays off with both lamps off |
| `testTurnOnBeforeLimitReportsOnWithLampsOff` | `turnOn` reports on, enable high, both lamps off, through the last millisecond |
| `testSecondTurnOnDoesNotRestartTheWindow` | A second `turnOn` still faults at the original limit |
| `testStatusLowDuringStartTurnsGreenOn` | `nPGD` low inside the window lights green |
| `testStatusHighThroughLimitFaults` | High at the limit faults. A later low stays faulted, enable low, yellow on |
| `testLimitBoundary` | One millisecond before the limit is still starting. The limit instant faults |
| `testStatusLowAtTurnOnIsRunning` | `nPGD` already low at `turnOn` is on, green lit. A second `turnOn` stays there |
| `testStatusBlipWhileRunningStaysOn` | A high that returns low stays on. The next full window faults |
| `testStatusHighWhileRunningFaults` | A high held for the run window faults |
| `testCommandsWhileFaultedAreIgnored` | `turnOn` and `turnOff` leave the fault latched and enable low |
| `testTurnOffFromStartingAndOn` | `turnOff` drops enable. A later high stays off |
| `testResetFromFaultOnAndOff` | `reset` from fault, on, and off returns to off. `turnOn` works again while `nPGD` is high |
| `testLimitUsesUnsignedWrap` | The window crosses `0xFFFFFFFF` |
| `testStatusLimitIsTheConstructorArgument` | A 50 ms limit faults at 50 ms |
| `testCommandAgeBoundary` | Still on at 179999 ms. Off at 180000 ms. A later high stays off |
| `testRepeatedOnExtendsCommandAge` | On at 0, 60000, and 120000 ms. Enable is not written again. Off 180000 ms after the last on |
| `testRepeatedOnWhileStartingLeavesEnableHigh` | A second `turnOn` while starting does not call `setEnable` |
| `testCommandLimitIsTheConstructorArgument` | A 1000 ms command age turns the pump off at 1000 ms |
| `testCommandAgeWhileStartingTurnsOff` | Starting turns off at the command age, before the status limit |
| `testStatusLowDoesNotRefreshCommandAge` | `nPGD` going low leaves the command age on the original `turnOn` |
| `testStatusFaultWinsOnTheSharedInstant` | Status limit and command age on the same sample stay faulted |
| `testCommandAgeLeavesFaultLatched` | A fault stays faulted across the command age. `turnOn` is ignored |
| `testCommandAgeUsesUnsignedWrap` | The command age crosses `0xFFFFFFFF` |
| `testTurnOnAfterCommandAge` | `turnOn` is accepted after the command age turns the pump off |

Link cases that send the three commands are on the
[module link reference](module-link.md).

```text
C:\Users\Nathan\.platformio\penv\Scripts\platformio.exe test -e native
```

## Source

- `lib/Logic/Pump.h`
- `lib/Logic/Pump.cpp`
- `lib/Interfaces/IPumpPort.h`
- `lib/Drivers/AvrPumpPort.h`
- `lib/Drivers/AvrPumpPort.cpp`
- `test/test_desktop/test_pump.cpp`
- `test/test_desktop/fakes/FakePumpPort.h`
