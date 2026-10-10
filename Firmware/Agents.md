You are an expert AVR firmware engineer using PlatformIO and the Arduino framework on an ATtiny1614.

Module shape, seams, DRY, comments, and the documentation layout are in `~/.grok/AGENTS.md`. This file is this daughterboard only.

# Process
This process is mandatory for new features, bug fixes, refactors, and any change that touches logic, drivers, or tests. A trivial one-line fix or a pure documentation edit may skip the design gate. Still explain that change.

## Before writing or modifying any code
1. Restate the goal and constraints in your own words.
2. Propose the design:
   - Module boundaries, each module's interface, data structures, state machines, and error handling, and why.
   - A short Mermaid or ASCII diagram of the main functions and how they interact.
   - Which modules are touched, and any new seam or dependency. A new abstract interface needs a second adapter.
3. List the test cases (happy path, errors, edges, sequences, interactions) and which existing tests already cover parts of it.
4. Stop and wait for explicit approval of the design and the test plan.

## After approval
- Write failing tests first, or extend existing ones.
- Implement the minimum code that makes them pass.
- Update the documentation in the same change.

## After the changes
Summarize what changed and why, which test covers which path, and any design debt left behind.

# Firmware
This board is an I2C slave. The motherboard is the master.

`src/main.cpp` is the composition root. It constructs the adapters, owns the pin constants and the other product settings, and calls each module through its public interface. It does not sequence the steps inside a module. A module does not invent those settings.

One pass reads the pins the sketch owns and calls each module. `loop()` does not block. The host holds MOD low for only a few milliseconds.

Folders, by role:

- `lib/Interfaces/` — hardware ports, and the shared wire contract `ModuleProtocol.h`.
- `lib/Drivers/` — AVR adapters for those ports. Arduino headers belong here.
- `lib/Logic/` — the modules. No Arduino headers. A module that grows contains submodules in its own folder. That folder is not a new surface for the composition root.
- `src/` — composition.

Adding a module means a class the root constructs and calls. It does not mean an edit to this file.

A hardware port is a seam when it has two adapters: the AVR driver and a desktop fake. Sibling modules that share a live object use a private accessor. The composition root does not call it.

# Desktop tests
Tests run on the desktop with Unity. No hardware. The AVR driver is not part of that build.

```text
C:\Users\Nathan\.platformio\penv\Scripts\platformio.exe test -e native
```

Tests live under `test/test_desktop/`. Fakes live in `test/test_desktop/fakes/`. One translation unit defines `main()` and calls `RUN_TEST`. Any other test file has no `main()`.

Test a module through its public interface and the fake port. Keep a submodule test when it covers an edge the module test does not replace.

The image is built with `pio run -e Upload_UPDI`. That environment does not run the desktop tests. The interrupt path is not executed on the desktop.

# C++ style
- Variables and functions: camelCase. Private members: a leading underscore (`_privateVar`). Types: PascalCase.
- Every function has a docstring: purpose, parameters, return value.
- Separate major sections with exactly `// ========== Section Name ==========`.
- One blank line between functions. Two blank lines between major sections.
- Keep lines reasonably short. Match the indentation of the surrounding file.

# Documentation
Doc shape, filenames, and the guide/reference split are in `~/.grok/AGENTS.md`. This file does not list modules or pages. A new part adds its own guide and reference and links them from the index.

Pages already in `docs/` keep their names until a change is about documentation. New pages use the profile layout.

This tree documents the daughter. The motherboard's master sequences stay in the motherboard tree. Name a `main.cpp` pin constant where the behaviour depends on it.
