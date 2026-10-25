#pragma once

#include <stdint.h>

#include "IPumpPort.h"

/**
 * One pump on the LM5060. nPGD low means the switch has started.
 * A high while enable is high is still starting, or a fault once it
 * has lasted statusLimitMs. A fault stays latched until reset().
 * Starting is reported as on. The green lamp waits for nPGD low.
 * An on command while starting or on records the command time.
 * update turns the switch off when that time is commandLimitMs old.
 * No Arduino types.
 *
 * turnOn, turnOff, reset, and state may run from the TWI interrupt.
 * update runs from the main loop with interrupts off.
 */
class Pump
{
public:
  /**
   * Binds the port. The phase starts off and the constructor does
   * not touch the pins, because globals run before the chip is
   * ready. setup() drives that idle level through the port.
   * statusLimitMs is how long nPGD may stay high before a fault.
   * commandLimitMs is how long starting or on may continue after
   * the last on command. The sketch passes PUMP_STATUS_LIMIT_MS
   * and PUMP_COMMAND_LIMIT_MS.
   *
   * @param port Enable, nPGD, and the two lamps.
   * @param statusLimitMs High-nPGD window, in milliseconds.
   * @param commandLimitMs On-command age, in milliseconds.
   */
  explicit Pump(IPumpPort& port, uint32_t statusLimitMs,
                uint32_t commandLimitMs);

  /**
   * Samples nPGD first. A status fault on this sample stays latched.
   * Then turns the switch off when the last on command is
   * commandLimitMs old. The caller must not run this concurrently
   * with turnOn, turnOff, or reset.
   *
   * @param nowMs Milliseconds from the same clock as the last call.
   *        Unsigned wrap is valid.
   * @return Nothing.
   */
  void update(uint32_t nowMs);

  /**
   * Wire state: off, on, or fault. Starting is reported as on.
   *
   * @return module_protocol pump state byte.
   */
  uint8_t state() const;

  /**
   * Turns the switch on from off. While starting or on, records the
   * command time and leaves enable and the nPGD window alone.
   * Ignored while faulted.
   *
   * @return Nothing.
   */
  void turnOn();

  /**
   * Turns the switch off from starting or on. Ignored while faulted,
   * so only reset() clears a fault.
   *
   * @return Nothing.
   */
  void turnOff();

  /**
   * Clears a fault and turns the switch off. Enable low is the
   * LM5060 reset. A later turnOn() is accepted.
   *
   * @return Nothing.
   */
  void reset();

private:
  enum class Phase : uint8_t
  {
    Off,
    Starting,
    On,
    Fault
  };

  /**
   * Records the on command at the last update time.
   *
   * @return Nothing.
   */
  void _noteOn();

  /**
   * Turns the switch off when the on command is older than the
   * command limit. Off and fault are left as they are.
   *
   * @return Nothing.
   */
  void _applyCommandAge();

  /**
   * Moves to off, enable low, both lamps off.
   *
   * @return Nothing.
   */
  void _enterOff();

  /**
   * Moves to starting and starts the nPGD window from the last
   * update time. Enable high, both lamps off.
   *
   * @return Nothing.
   */
  void _enterStarting();

  /**
   * Moves to on. Enable high, green lamp on, yellow lamp off.
   *
   * @return Nothing.
   */
  void _enterOn();

  /**
   * Latches fault. Enable low, yellow lamp on, green lamp off.
   *
   * @return Nothing.
   */
  void _enterFault();

  /**
   * Drives enable and both lamps from the phase.
   *
   * @return Nothing.
   */
  void _applyOutputs();

  /**
   * Starting: nPGD low becomes on. A high that lasts the limit
   * becomes a fault.
   *
   * @return Nothing.
   */
  void _watchStart();

  /**
   * On: a high that returns low inside the limit stays on. A high
   * that lasts the limit becomes a fault.
   *
   * @return Nothing.
   */
  void _watchRun();

  IPumpPort& _port;
  uint32_t _statusLimitMs;
  uint32_t _commandLimitMs;
  volatile uint32_t _nowMs;
  volatile uint32_t _windowStartedAt;
  volatile uint32_t _lastOnMs;
  volatile Phase _phase;
  volatile bool _highWindowArmed;
};
