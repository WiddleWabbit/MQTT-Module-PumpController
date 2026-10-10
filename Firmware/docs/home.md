# Pump module firmware

The pump daughter is one module. `src/main.cpp` constructs the bus
adapter and the link. It calls `update()` on the link. It does not run
the steps inside the link.

## How the firmware runs

Each of these is a short page: what that part creates, and what one
pass does.

1. [Architecture](architecture.md) — the loop, and who owns whom.
2. [Module link](module-link.md) — the MOD gate, then one master write.
3. [Slave port](slave-port.md) — TWI0 on the AVR, and the desktop fake.

## When you need a byte, a state, or a test

The [reference index](reference/index.md) is the lookup. Each page
there has the states, the sequences, the errors, and the desktop test
that covers them.

## Wire contract

`lib/Interfaces/ModuleProtocol.h` is the copy of the motherboard
header. The include lines use `stddef.h` and `stdint.h` so this AVR
toolchain can compile it. Frame layout, CRC-8/SMBus, command bytes,
`kModGateMaxMs`, and `kClockStretchMaxMs` live there. If a page in
this tree and that header disagree, the header wins. Change the
motherboard header, then copy it here.

This tree documents the slave. The motherboard's master sequences stay
in the motherboard firmware.
