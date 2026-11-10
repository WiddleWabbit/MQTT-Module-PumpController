#include "Pump.h"

#include "ModuleProtocol.h"


// ========== Construction ==========

/**
 * Binds the port and the store. The phase starts off and the
 * constructor does not touch the pins or the store, because
 * globals run before the chip is ready. The first update loads
 * the store.
 *
 * @param port Enable, nPGD, the lamps, and the sense pair.
 * @param store Totals kept across reset.
 * @param config Limits and the sense scale. Copied.
 */
Pump::Pump(IPumpPort& port, IPumpStore& store, const PumpConfig& config)
  : _port(port),
    _store(store),
    _config(config),
    _nowMs(0),
    _windowStartedAt(0),
    _lastOnMs(0),
    _phase(Phase::Off),
    _windowArmed(false),
    _npgdLow(false),
    _storeLoaded(false),
    _senseFresh(false),
    _cycleOpen(false),
    _powerGoodSeen(false),
    _softReached(false),
    _minValid(false),
    _belowArmed(false),
    _fallen(false),
    _cycledSinceOn(false),
    _saveActive(false),
    _voltageMv(0),
    _currentMa(0),
    _peakMv(0),
    _peakMa(0),
    _minMv(0),
    _lifePeakMv(0),
    _lifePeakMa(0),
    _softMs(0),
    _powerGoodMs(0),
    _closedSeconds(0),
    _lastRunSeconds(0),
    _failedStarts(0),
    _cycleStartMs(0),
    _belowSinceMs(0),
    _sumMv(0),
    _sumMa(0),
    _avgCount(0),
    _completedCycles(0),
    _totalSeconds(0),
    _nextSequence(1),
    _saveSequence(0),
    _saveSlot(0),
    _saveStep(0)
{
  for (uint8_t index = 0; index < kSlotBytes; ++index)
  {
    _saveImage[index] = 0;
  }
}


// ========== Public API ==========

/**
 * Loads the store on the first call. Samples the sense pair when
 * a new one is ready, then nPGD, then the on-command age. Writes
 * at most one changed totals byte. The caller must not run this
 * concurrently with turnOn, turnOff, reset, or the record reads.
 *
 * @param nowMs Milliseconds from the same clock as the last call.
 *        Unsigned wrap is valid.
 * @return Nothing.
 */
void Pump::update(uint32_t nowMs)
{
  _nowMs = nowMs;
  if (!_storeLoaded)
  {
    _loadStore();
    _storeLoaded = true;
  }
  _takeSense();
  _watchSwitch();
  _applyCommandAge();
  _serviceStore();
}

/**
 * Wire state: off, on, or fault. Held and running are on.
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
 * Writes the GET_PUMP_NOW payload. dest holds 8 bytes.
 *
 * @param dest Destination. Not retained.
 * @return Nothing.
 */
void Pump::readNow(uint8_t* dest) const
{
  const Phase phase = _phase;
  uint8_t phaseByte = module_protocol::kPumpPhaseOff;
  if (phase == Phase::Held)
  {
    phaseByte = module_protocol::kPumpPhaseHeld;
  }
  else if (phase == Phase::Running)
  {
    phaseByte = module_protocol::kPumpPhaseRunning;
  }
  else if (phase == Phase::Fault)
  {
    phaseByte = module_protocol::kPumpPhaseFault;
  }

  uint8_t flags = 0;
  if (_npgdLow)
  {
    flags = static_cast<uint8_t>(flags | module_protocol::kPumpFlagNpgdLow);
  }
  if (phase == Phase::Held || phase == Phase::Running)
  {
    flags = static_cast<uint8_t>(flags | module_protocol::kPumpFlagEnableHigh);
  }
  if (_cycledSinceOn)
  {
    flags = static_cast<uint8_t>(flags | module_protocol::kPumpFlagCycled);
  }

  dest[0] = phaseByte;
  dest[1] = flags;
  _writeU16(dest + 2, _voltageMv);
  _writeU16(dest + 4, _currentMa);
  const uint16_t openSeconds = _cycleOpen ? _cycleSeconds() : 0;
  _writeU16(dest + 6, openSeconds);
}

/**
 * Writes the GET_PUMP_CYCLE payload. dest holds 16 bytes.
 * This is the open cycle, or the last one that closed.
 *
 * @param dest Destination. Not retained.
 * @return Nothing.
 */
void Pump::readCycle(uint8_t* dest) const
{
  _writeU16(dest + 0, _peakMv);
  _writeU16(dest + 2, _minValid ? _minMv : 0);
  _writeU16(dest + 4, _peakMa);
  _writeU16(dest + 6, _averageMv());
  _writeU16(dest + 8, _averageMa());
  _writeU16(dest + 10, _softReached ? _softMs : 0);
  _writeU16(dest + 12, _powerGoodSeen ? _powerGoodMs : 0);
  _writeU16(dest + 14, _cycleSeconds());
}

/**
 * Writes the GET_PUMP_TOTALS payload. dest holds 16 bytes.
 *
 * @param dest Destination. Not retained.
 * @return Nothing.
 */
void Pump::readTotals(uint8_t* dest) const
{
  _writeU32(dest + 0, _completedCycles);
  _writeU32(dest + 4, _totalSeconds);
  _writeU16(dest + 8, _lifePeakMv);
  _writeU16(dest + 10, _lifePeakMa);
  _writeU16(dest + 12, _failedStarts);
  _writeU16(dest + 14, _lastRunSeconds);
}

/**
 * Turns the switch on from off. While held or running, records
 * the command time and leaves enable and the current window alone.
 * Ignored while faulted.
 *
 * @return Nothing.
 */
void Pump::turnOn()
{
  _npgdLow = _port.statusIsLow();
  const Phase phase = _phase;
  if (phase == Phase::Held || phase == Phase::Running)
  {
    _noteOn();
    return;
  }
  if (phase != Phase::Off)
  {
    return;
  }
  _cycledSinceOn = false;
  _noteOn();
  if (_npgdLow)
  {
    _enterRunning();
    return;
  }
  _enterHeld();
}

/**
 * Turns the switch off from held or running and closes an open
 * cycle. Ignored while faulted, so only reset() clears a fault.
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
 * LM5060 reset. Totals are kept. A later turnOn() is accepted.
 *
 * @return Nothing.
 */
void Pump::reset()
{
  _enterOff();
}


// ========== Samples ==========

/**
 * Reads nPGD and, when a new pair is ready, the sense scale.
 *
 * @return Nothing.
 */
void Pump::_takeSense()
{
  _npgdLow = _port.statusIsLow();
  const PumpSense sense = _port.readSense();
  _senseFresh = sense.fresh;
  if (!sense.fresh)
  {
    return;
  }
  _voltageMv = _scale(sense.voltageCounts, _config.millivoltsNum,
                      _config.millivoltsDen);
  _currentMa = _scale(sense.currentCounts, _config.milliampsNum,
                      _config.milliampsDen);
  _noteSample();
}

/**
 * Opens, updates, or closes a cycle from the latest sample.
 *
 * @return Nothing.
 */
void Pump::_noteSample()
{
  const bool below = _currentMa <= _config.offMilliamps;
  if (!below)
  {
    _belowArmed = false;
    _fallen = false;
  }
  else if (!_belowArmed)
  {
    _belowArmed = true;
    _belowSinceMs = _nowMs;
    _fallen = _config.offDebounceMs == 0;
  }
  else
  {
    const uint32_t elapsed =
        static_cast<uint32_t>(_nowMs - _belowSinceMs);
    _fallen = elapsed >= _config.offDebounceMs;
  }

  const Phase phase = _phase;
  const bool commanded = phase == Phase::Held || phase == Phase::Running;
  if (!_cycleOpen && commanded && _currentMa >= _config.onMilliamps)
  {
    _openCycle();
  }
  if (_cycleOpen)
  {
    _notePeaks();
    _noteSettled();
  }
  if (_cycleOpen && _fallen)
  {
    _closeCycle();
  }
}

/**
 * Moves between held, running, and fault from nPGD and current.
 *
 * @return Nothing.
 */
void Pump::_watchSwitch()
{
  if (_npgdLow)
  {
    _windowArmed = false;
    if (_phase == Phase::Held)
    {
      _enterRunning();
    }
    if (_senseFresh)
    {
      _notePowerGood();
    }
    return;
  }

  if (_cycleOpen && !_fallen)
  {
    if (!_windowArmed)
    {
      _windowArmed = true;
      _windowStartedAt = _nowMs;
      return;
    }
    const uint32_t elapsed =
        static_cast<uint32_t>(_nowMs - _windowStartedAt);
    if (elapsed >= _config.statusLimitMs)
    {
      _enterFault();
    }
    return;
  }

  _windowArmed = false;
  if (_phase == Phase::Running)
  {
    _enterHeld();
  }
}

/**
 * Records power-good time, the running minimum, and the average.
 *
 * @return Nothing.
 */
void Pump::_notePowerGood()
{
  if (!_cycleOpen)
  {
    return;
  }
  if (!_powerGoodSeen)
  {
    _powerGoodSeen = true;
    const uint32_t elapsed =
        static_cast<uint32_t>(_nowMs - _cycleStartMs);
    _powerGoodMs = _capMs(elapsed);
    _minMv = _voltageMv;
    _minValid = true;
  }
  else if (_voltageMv < _minMv)
  {
    _minMv = _voltageMv;
  }

  _sumMv = _sumMv + _voltageMv;
  _sumMa = _sumMa + _currentMa;
  _avgCount = _avgCount + 1;
}

/**
 * Raises the cycle and lifetime peaks from the current sample.
 *
 * @return Nothing.
 */
void Pump::_notePeaks()
{
  if (_voltageMv > _peakMv)
  {
    _peakMv = _voltageMv;
  }
  if (_currentMa > _peakMa)
  {
    _peakMa = _currentMa;
  }
  if (_voltageMv > _lifePeakMv)
  {
    _lifePeakMv = _voltageMv;
  }
  if (_currentMa > _lifePeakMa)
  {
    _lifePeakMa = _currentMa;
  }
}

/**
 * Records soft-start when the terminal voltage has reached the
 * settled level.
 *
 * @return Nothing.
 */
void Pump::_noteSettled()
{
  if (_softReached || _voltageMv < _config.settledMillivolts)
  {
    return;
  }
  _softReached = true;
  const uint32_t elapsed = static_cast<uint32_t>(_nowMs - _cycleStartMs);
  _softMs = _capMs(elapsed);
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
 * command limit. Off and fault are left as they are. An open
 * cycle is closed first.
 *
 * @return Nothing.
 */
void Pump::_applyCommandAge()
{
  const Phase phase = _phase;
  if (phase != Phase::Held && phase != Phase::Running)
  {
    return;
  }
  const uint32_t elapsed = static_cast<uint32_t>(_nowMs - _lastOnMs);
  if (elapsed >= _config.commandLimitMs)
  {
    _enterOff();
  }
}

/**
 * Moves to off, enable low, both lamps off. Closes an open cycle.
 *
 * @return Nothing.
 */
void Pump::_enterOff()
{
  if (_cycleOpen)
  {
    _closeCycle();
  }
  _cycledSinceOn = false;
  _phase = Phase::Off;
  _windowArmed = false;
  _applyOutputs();
}

/**
 * Moves to held. Enable high, both lamps off.
 *
 * @return Nothing.
 */
void Pump::_enterHeld()
{
  _phase = Phase::Held;
  _applyOutputs();
}

/**
 * Moves to running. Enable high, green lamp on, yellow lamp off.
 *
 * @return Nothing.
 */
void Pump::_enterRunning()
{
  _phase = Phase::Running;
  _windowArmed = false;
  _applyOutputs();
}

/**
 * Latches fault. Closes an open cycle. Enable low, yellow on.
 *
 * @return Nothing.
 */
void Pump::_enterFault()
{
  if (_cycleOpen)
  {
    _closeCycle();
  }
  _phase = Phase::Fault;
  _windowArmed = false;
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
  const bool enable = phase == Phase::Held || phase == Phase::Running;
  _port.setEnable(enable);
  _port.setRunningLamp(phase == Phase::Running);
  _port.setWarningLamp(phase == Phase::Fault);
}

/**
 * Starts a cycle at the current sample.
 *
 * @return Nothing.
 */
void Pump::_openCycle()
{
  _cycleOpen = true;
  _cycleStartMs = _nowMs;
  _powerGoodSeen = false;
  _softReached = false;
  _minValid = false;
  _powerGoodMs = 0;
  _softMs = 0;
  _peakMv = 0;
  _peakMa = 0;
  _minMv = 0;
  _sumMv = 0;
  _sumMa = 0;
  _avgCount = 0;
  _closedSeconds = 0;
  _windowArmed = false;
}

/**
 * Closes the open cycle and updates the totals.
 *
 * @return Nothing.
 */
void Pump::_closeCycle()
{
  const uint32_t duration = static_cast<uint32_t>(_nowMs - _cycleStartMs);
  _cycleOpen = false;
  _windowArmed = false;
  // The uint16 store truncates. A longer run is reported as 65535.
  _closedSeconds = static_cast<uint16_t>(duration / 1000UL);
  if (_closedSeconds != duration / 1000UL)
  {
    _closedSeconds = 65535;
  }
  if (_powerGoodSeen)
  {
    _completedCycles = _completedCycles + 1;
    _totalSeconds = _totalSeconds + _closedSeconds;
    _lastRunSeconds = _closedSeconds;
    _cycledSinceOn = true;
    if (duration >= _config.cycleMinMs)
    {
      _armSave();
    }
  }
  else
  {
    _failedStarts = static_cast<uint16_t>(_failedStarts + 1);
  }
}


// ========== Store ==========

/**
 * Loads the newest valid totals slot. An empty store stays zero.
 *
 * @return Nothing.
 */
void Pump::_loadStore()
{
  uint32_t bestSequence = 0;
  bool found = false;
  uint8_t image[kSlotBytes];
  for (uint8_t slot = 0; slot < kSlotCount; ++slot)
  {
    const uint16_t base = static_cast<uint16_t>(slot * kSlotBytes);
    if (_store.read(base) != kMagic)
    {
      continue;
    }
    for (uint8_t index = 0; index < kSlotBytes; ++index)
    {
      image[index] = _store.read(static_cast<uint16_t>(base + index));
    }
    const uint8_t crc = module_protocol::crc8Smbus(image + 2, 30);
    if (crc != image[1])
    {
      continue;
    }
    const uint32_t sequence = static_cast<uint32_t>(image[2]) |
                              (static_cast<uint32_t>(image[3]) << 8) |
                              (static_cast<uint32_t>(image[4]) << 16) |
                              (static_cast<uint32_t>(image[5]) << 24);
    if (found && sequence <= bestSequence)
    {
      continue;
    }
    found = true;
    bestSequence = sequence;
    _completedCycles = static_cast<uint32_t>(image[6]) |
                       (static_cast<uint32_t>(image[7]) << 8) |
                       (static_cast<uint32_t>(image[8]) << 16) |
                       (static_cast<uint32_t>(image[9]) << 24);
    _totalSeconds = static_cast<uint32_t>(image[10]) |
                    (static_cast<uint32_t>(image[11]) << 8) |
                    (static_cast<uint32_t>(image[12]) << 16) |
                    (static_cast<uint32_t>(image[13]) << 24);
    _lifePeakMv = static_cast<uint16_t>(
        image[14] | (static_cast<uint16_t>(image[15]) << 8));
    _lifePeakMa = static_cast<uint16_t>(
        image[16] | (static_cast<uint16_t>(image[17]) << 8));
    _failedStarts = static_cast<uint16_t>(
        image[18] | (static_cast<uint16_t>(image[19]) << 8));
    _lastRunSeconds = static_cast<uint16_t>(
        image[20] | (static_cast<uint16_t>(image[21]) << 8));
  }
  if (found)
  {
    _nextSequence = bestSequence + 1;
  }
}

/**
 * Queues the current totals. A save already in progress restarts.
 *
 * @return Nothing.
 */
void Pump::_armSave()
{
  _saveSequence = _nextSequence;
  _nextSequence = _nextSequence + 1;
  _saveSlot = static_cast<uint8_t>((_saveSequence - 1U) % kSlotCount);
  _fillImage(_saveImage, _saveSequence);
  _saveStep = 0;
  _saveActive = true;
}

/**
 * Writes at most one changed byte of the queued slot.
 *
 * @return Nothing.
 */
void Pump::_serviceStore()
{
  if (!_saveActive)
  {
    return;
  }
  // Offset 0 is cleared first. The magic byte is written last, so a
  // partial slot does not load over the previous record.
  const uint16_t base = static_cast<uint16_t>(_saveSlot * kSlotBytes);
  while (_saveStep < kSlotBytes + 1U)
  {
    uint8_t offset = 0;
    uint8_t value = 0;
    if (_saveStep == 0)
    {
      offset = 0;
      value = 0;
    }
    else if (_saveStep < 31)
    {
      offset = static_cast<uint8_t>(_saveStep + 1);
      value = _saveImage[offset];
    }
    else if (_saveStep == 31)
    {
      offset = 1;
      value = _saveImage[1];
    }
    else
    {
      offset = 0;
      value = kMagic;
    }
    const uint16_t address = static_cast<uint16_t>(base + offset);
    if (_store.read(address) != value)
    {
      _store.write(address, value);
      _saveStep = static_cast<uint8_t>(_saveStep + 1);
      if (_saveStep > kSlotBytes)
      {
        _saveActive = false;
      }
      return;
    }
    _saveStep = static_cast<uint8_t>(_saveStep + 1);
  }
  _saveActive = false;
}

/**
 * Fills one save image from the RAM totals.
 *
 * @param image 32-byte destination.
 * @param sequence Sequence stored in the slot.
 * @return Nothing.
 */
void Pump::_fillImage(uint8_t* image, uint32_t sequence) const
{
  for (uint8_t index = 0; index < kSlotBytes; ++index)
  {
    image[index] = 0;
  }
  image[0] = kMagic;
  image[2] = static_cast<uint8_t>(sequence & 0xFF);
  image[3] = static_cast<uint8_t>((sequence >> 8) & 0xFF);
  image[4] = static_cast<uint8_t>((sequence >> 16) & 0xFF);
  image[5] = static_cast<uint8_t>((sequence >> 24) & 0xFF);
  image[6] = static_cast<uint8_t>(_completedCycles & 0xFF);
  image[7] = static_cast<uint8_t>((_completedCycles >> 8) & 0xFF);
  image[8] = static_cast<uint8_t>((_completedCycles >> 16) & 0xFF);
  image[9] = static_cast<uint8_t>((_completedCycles >> 24) & 0xFF);
  image[10] = static_cast<uint8_t>(_totalSeconds & 0xFF);
  image[11] = static_cast<uint8_t>((_totalSeconds >> 8) & 0xFF);
  image[12] = static_cast<uint8_t>((_totalSeconds >> 16) & 0xFF);
  image[13] = static_cast<uint8_t>((_totalSeconds >> 24) & 0xFF);
  image[14] = static_cast<uint8_t>(_lifePeakMv & 0xFF);
  image[15] = static_cast<uint8_t>((_lifePeakMv >> 8) & 0xFF);
  image[16] = static_cast<uint8_t>(_lifePeakMa & 0xFF);
  image[17] = static_cast<uint8_t>((_lifePeakMa >> 8) & 0xFF);
  image[18] = static_cast<uint8_t>(_failedStarts & 0xFF);
  image[19] = static_cast<uint8_t>((_failedStarts >> 8) & 0xFF);
  image[20] = static_cast<uint8_t>(_lastRunSeconds & 0xFF);
  image[21] = static_cast<uint8_t>((_lastRunSeconds >> 8) & 0xFF);
  image[1] = module_protocol::crc8Smbus(image + 2, 30);
}


// ========== Encoding ==========

/**
 * Scales one conversion. A zero denominator returns 0.
 *
 * @param counts ADC counts.
 * @param num Scale numerator.
 * @param den Scale denominator.
 * @return Scaled value, truncated.
 */
uint16_t Pump::_scale(uint16_t counts, uint16_t num, uint16_t den)
{
  if (den == 0)
  {
    return 0;
  }
  const uint32_t scaled =
      (static_cast<uint32_t>(counts) * num) / den;
  return static_cast<uint16_t>(scaled);
}

/**
 * Writes a big-endian uint16.
 *
 * @param dest Two-byte destination.
 * @param value Value to write.
 * @return Nothing.
 */
void Pump::_writeU16(uint8_t* dest, uint16_t value)
{
  dest[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
  dest[1] = static_cast<uint8_t>(value & 0xFF);
}

/**
 * Writes a big-endian uint32.
 *
 * @param dest Four-byte destination.
 * @param value Value to write.
 * @return Nothing.
 */
void Pump::_writeU32(uint8_t* dest, uint32_t value)
{
  dest[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  dest[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  dest[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  dest[3] = static_cast<uint8_t>(value & 0xFF);
}

/**
 * Caps a millisecond span at the uint16 wire field.
 *
 * @param elapsedMs Elapsed milliseconds.
 * @return Elapsed, or 65535 when it does not fit.
 */
uint16_t Pump::_capMs(uint32_t elapsedMs)
{
  if (elapsedMs > 65535UL)
  {
    return 65535;
  }
  return static_cast<uint16_t>(elapsedMs);
}

/**
 * Run seconds for the open cycle, or the closed cycle.
 *
 * @return Seconds, capped at 65535.
 */
uint16_t Pump::_cycleSeconds() const
{
  if (!_cycleOpen)
  {
    return _closedSeconds;
  }
  const uint32_t duration = static_cast<uint32_t>(_nowMs - _cycleStartMs);
  const uint32_t seconds = duration / 1000UL;
  if (seconds > 65535UL)
  {
    return 65535;
  }
  return static_cast<uint16_t>(seconds);
}

/**
 * Average millivolts for the cycle payload.
 *
 * @return Truncated mean, or 0 when no power-good sample exists.
 */
uint16_t Pump::_averageMv() const
{
  if (_avgCount == 0)
  {
    return 0;
  }
  return static_cast<uint16_t>(_sumMv / _avgCount);
}

/**
 * Average milliamps for the cycle payload.
 *
 * @return Truncated mean, or 0 when no power-good sample exists.
 */
uint16_t Pump::_averageMa() const
{
  if (_avgCount == 0)
  {
    return 0;
  }
  return static_cast<uint16_t>(_sumMa / _avgCount);
}
