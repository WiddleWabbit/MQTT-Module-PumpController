#include "Pump.h"

#include "ModuleProtocol.h"


// ========== Construction ==========

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
Pump::Pump(IPumpPort& port, uint32_t statusLimitMs,
           uint32_t commandLimitMs)
  : _port(port),
    _statusLimitMs(statusLimitMs),
    _commandLimitMs(commandLimitMs),
    _nowMs(0),
    _windowStartedAt(0),
    _lastOnMs(0),
    _phase(Phase::Off),
    _highWindowArmed(false)
{
}


// ========== Public API ==========

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
void Pump::update(uint32_t nowMs)
{
  _nowMs = nowMs;
  const Phase phase = _phase;
  if (phase == Phase::Starting)
  {
    _watchStart();
  }
  else if (phase == Phase::On)
  {
    _watchRun();
  }
  _applyCommandAge();
}

/**
 * Wire state: off, on, or fault. Starting is reported as on.
 *
 * @return module_protocol pump state byte.
 */
uint8_t Pump::state() const
{
  const Phase phase = _phase;
  if (phase == Phase::Fault)
  {
    return module_protocol::kPumpStateFault;
  }
  if (phase == Phase::Off)
  {
    return module_protocol::kPumpStateOff;
  }
  return module_protocol::kPumpStateOn;
}

/**
 * Turns the switch on from off. While starting or on, records the
 * command time and leaves enable and the nPGD window alone.
 * Ignored while faulted.
 *
 * @return Nothing.
 */
void Pump::turnOn()
{
  const Phase phase = _phase;
  if (phase == Phase::Starting || phase == Phase::On)
  {
    _noteOn();
    return;
  }
  if (phase != Phase::Off)
  {
    return;
  }
  _noteOn();
  if (_port.statusIsLow())
  {
    _enterOn();
    return;
  }
  _enterStarting();
}

/**
 * Turns the switch off from starting or on. Ignored while faulted,
 * so only reset() clears a fault.
 *
 * @return Nothing.
 */
void Pump::turnOff()
{
  const Phase phase = _phase;
  if (phase == Phase::Fault || phase == Phase::Off)
  {
    return;
  }
  _enterOff();
}

/**
 * Clears a fault and turns the switch off. Enable low is the
 * LM5060 reset. A later turnOn() is accepted.
 *
 * @return Nothing.
 */
void Pump::reset()
{
  _enterOff();
}


// ========== Phases ==========

/**
 * Records the on command at the last update time.
 *
 * @return Nothing.
 */
void Pump::_noteOn()
{
  _lastOnMs = _nowMs;
}

/**
 * Turns the switch off when the on command is older than the
 * command limit. Off and fault are left as they are.
 *
 * @return Nothing.
 */
void Pump::_applyCommandAge()
{
  const Phase phase = _phase;
  if (phase != Phase::Starting && phase != Phase::On)
  {
    return;
  }
  const uint32_t nowMs = _nowMs;
  const uint32_t lastOnMs = _lastOnMs;
  if (static_cast<uint32_t>(nowMs - lastOnMs) >= _commandLimitMs)
  {
    _enterOff();
  }
}

/**
 * Moves to off, enable low, both lamps off.
 *
 * @return Nothing.
 */
void Pump::_enterOff()
{
  _phase = Phase::Off;
  _highWindowArmed = false;
  _applyOutputs();
}

/**
 * Moves to starting and starts the nPGD window from the last
 * update time. Enable high, both lamps off.
 *
 * @return Nothing.
 */
void Pump::_enterStarting()
{
  _windowStartedAt = _nowMs;
  _phase = Phase::Starting;
  _highWindowArmed = false;
  _applyOutputs();
}

/**
 * Moves to on. Enable high, green lamp on, yellow lamp off.
 *
 * @return Nothing.
 */
void Pump::_enterOn()
{
  _phase = Phase::On;
  _highWindowArmed = false;
  _applyOutputs();
}

/**
 * Latches fault. Enable low, yellow lamp on, green lamp off.
 *
 * @return Nothing.
 */
void Pump::_enterFault()
{
  _phase = Phase::Fault;
  _highWindowArmed = false;
  _applyOutputs();
}

/**
 * Drives enable and both lamps from the phase.
 *
 * @return Nothing.
 */
void Pump::_applyOutputs()
{
  const Phase phase = _phase;
  const bool enable = phase == Phase::Starting || phase == Phase::On;
  _port.setEnable(enable);
  _port.setRunningLamp(phase == Phase::On);
  _port.setWarningLamp(phase == Phase::Fault);
}

/**
 * Starting: nPGD low becomes on. A high that lasts the limit
 * becomes a fault.
 *
 * @return Nothing.
 */
void Pump::_watchStart()
{
  if (_port.statusIsLow())
  {
    _enterOn();
    return;
  }
  const uint32_t nowMs = _nowMs;
  const uint32_t startedAt = _windowStartedAt;
  if (static_cast<uint32_t>(nowMs - startedAt) >= _statusLimitMs)
  {
    _enterFault();
  }
}

/**
 * On: a high that returns low inside the limit stays on. A high
 * that lasts the limit becomes a fault.
 *
 * @return Nothing.
 */
void Pump::_watchRun()
{
  if (_port.statusIsLow())
  {
    _highWindowArmed = false;
    return;
  }
  if (!_highWindowArmed)
  {
    _highWindowArmed = true;
    _windowStartedAt = _nowMs;
    return;
  }
  const uint32_t nowMs = _nowMs;
  const uint32_t startedAt = _windowStartedAt;
  if (static_cast<uint32_t>(nowMs - startedAt) >= _statusLimitMs)
  {
    _enterFault();
  }
}
