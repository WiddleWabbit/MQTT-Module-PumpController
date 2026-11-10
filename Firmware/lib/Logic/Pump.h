#pragma once

#include <stdint.h>

#include "IPumpPort.h"
#include "IPumpStore.h"

/**
 * Product settings for one pump. The sketch owns the values.
 * The scale turns 10-bit counts into millivolts and milliamps:
 * result = counts * num / den.
 */
struct PumpConfig
{
  uint32_t statusLimitMs;
  uint32_t commandLimitMs;
  uint16_t onMilliamps;
  uint16_t offMilliamps;
  uint32_t offDebounceMs;
  uint16_t settledMillivolts;
  uint32_t cycleMinMs;
  uint16_t millivoltsNum;
  uint16_t millivoltsDen;
  uint16_t milliampsNum;
  uint16_t milliampsDen;
  uint16_t overcurrentMilliamps;
  uint32_t overcurrentLimitMs;
  uint16_t undercurrentMilliamps;
  uint32_t undercurrentLimitMs;
  uint32_t stuckOnLimitMs;
};

/**
 * One pump on the LM5060. Enable is the MCU side of the pressure
 * AND gate. Held is enable high while nPGD is high and the motor
 * is not holding current: the pressure switch is doing its job.
 * A fault latches the first reason: current up while nPGD stays
 * high for statusLimitMs, current at or above overcurrentMilliamps
 * for overcurrentLimitMs, that current followed by the terminal
 * falling below settledMillivolts, current below
 * undercurrentMilliamps while the terminal stays settled for
 * undercurrentLimitMs, or the terminal staying settled while
 * enable is low for stuckOnLimitMs. Running is enable high after
 * nPGD has gone low.
 * The green lamp follows running. The yellow lamp follows fault.
 * Held and running are both reported as on.
 *
 * A cycle opens when current rises through onMilliamps and closes
 * when it falls through offMilliamps for offDebounceMs. Totals for
 * a cycle at least cycleMinMs long are written to the store.
 *
 * turnOn, turnOff, reset, state, readFault, and the three record
 * reads may run from the TWI interrupt. update runs from the main
 * loop with interrupts off. No Arduino types.
 */
class Pump
{
public:
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
  Pump(IPumpPort& port, IPumpStore& store, const PumpConfig& config);

  /**
   * Loads the store on the first call. Samples the sense pair when
   * a new one is ready, then the fault watches, then nPGD, then the
   * on-command age. Writes at most one changed totals byte. The
   * caller must not run this
   * concurrently with turnOn, turnOff, reset, or the record reads.
   *
   * @param nowMs Milliseconds from the same clock as the last call.
   *        Unsigned wrap is valid.
   * @return Nothing.
   */
  void update(uint32_t nowMs);

  /**
   * Wire state: off, on, or fault. Held and running are on.
   *
   * @return module_protocol pump state byte.
   */
  uint8_t state() const;

  /**
   * Writes the GET_PUMP_NOW payload. dest holds 8 bytes.
   *
   * @param dest Destination. Not retained.
   * @return Nothing.
   */
  void readNow(uint8_t* dest) const;

  /**
   * Writes the GET_PUMP_CYCLE payload. dest holds 16 bytes.
   * This is the open cycle, or the last one that closed.
   *
   * @param dest Destination. Not retained.
   * @return Nothing.
   */
  void readCycle(uint8_t* dest) const;

  /**
   * Writes the GET_PUMP_TOTALS payload. dest holds 16 bytes.
   *
   * @param dest Destination. Not retained.
   * @return Nothing.
   */
  void readTotals(uint8_t* dest) const;

  /**
   * Writes the GET_FAULT payload. dest holds 2 bytes: reason, then
   * index. The pump index is 0. Reason 0 is no fault.
   *
   * @param dest Destination. Not retained.
   * @return Nothing.
   */
  void readFault(uint8_t* dest) const;

  /**
   * Turns the switch on from off. While held or running, records
   * the command time and leaves enable and the current window alone.
   * Ignored while faulted.
   *
   * @return Nothing.
   */
  void turnOn();

  /**
   * Turns the switch off from held or running and closes an open
   * cycle. Ignored while faulted, so only reset() clears a fault.
   *
   * @return Nothing.
   */
  void turnOff();

  /**
   * Clears a fault and turns the switch off. Enable low is the
   * LM5060 reset. Totals are kept. A later turnOn() is accepted.
   *
   * @return Nothing.
   */
  void reset();

private:
  enum class Phase : uint8_t
  {
    Off,
    Held,
    Running,
    Fault
  };

  static constexpr uint8_t kSlotBytes = 32;
  static constexpr uint8_t kSlotCount = 8;
  static constexpr uint8_t kMagic = 0xC3;

  /**
   * Records the on command at the last update time.
   *
   * @return Nothing.
   */
  void _noteOn();

  /**
   * Turns the switch off when the on command is older than the
   * command limit. Off and fault are left as they are. An open
   * cycle is closed first.
   *
   * @return Nothing.
   */
  void _applyCommandAge();

  /**
   * Reads nPGD and, when a new pair is ready, the sense scale.
   *
   * @return Nothing.
   */
  void _takeSense();

  /**
   * Opens, updates, or closes a cycle from the latest sample.
   *
   * @return Nothing.
   */
  void _noteSample();

  /**
   * Moves between held, running, and fault from nPGD and current.
   *
   * @return Nothing.
   */
  void _watchSwitch();

  /**
   * Latches overcurrent from a sustained high sample or from a
   * high sample followed by the terminal voltage falling.
   *
   * @return Nothing.
   */
  void _watchOvercurrent();

  /**
   * Latches undercurrent when the terminal stays settled and the
   * current stays below the floor.
   *
   * @return Nothing.
   */
  void _watchUndercurrent();

  /**
   * Latches a stuck output when enable is low and the terminal
   * stays settled.
   *
   * @return Nothing.
   */
  void _watchStuckOn();

  /**
   * Clears the fault reason and the fault-watch timers.
   *
   * @return Nothing.
   */
  void _clearFaultWatch();

  /**
   * Records power-good time, the running minimum, and the average.
   *
   * @return Nothing.
   */
  void _notePowerGood();

  /**
   * Moves to off, enable low, both lamps off. Closes an open cycle.
   *
   * @return Nothing.
   */
  void _enterOff();

  /**
   * Moves to held. Enable high, both lamps off.
   *
   * @return Nothing.
   */
  void _enterHeld();

  /**
   * Moves to running. Enable high, green lamp on, yellow lamp off.
   *
   * @return Nothing.
   */
  void _enterRunning();

  /**
   * Latches fault with the first reason. A later call keeps that
   * reason. Closes an open cycle. Enable low, yellow on.
   *
   * @param reason Pump fault code. Not zero.
   * @return Nothing.
   */
  void _enterFault(uint8_t reason);

  /**
   * Drives enable and both lamps from the phase.
   *
   * @return Nothing.
   */
  void _applyOutputs();

  /**
   * Starts a cycle at the current sample.
   *
   * @return Nothing.
   */
  void _openCycle();

  /**
   * Closes the open cycle and updates the totals.
   *
   * @return Nothing.
   */
  void _closeCycle();

  /**
   * Raises the cycle and lifetime peaks from the current sample.
   *
   * @return Nothing.
   */
  void _notePeaks();

  /**
   * Records soft-start when the terminal voltage has reached the
   * settled level.
   *
   * @return Nothing.
   */
  void _noteSettled();

  /**
   * Loads the newest valid totals slot. An empty store stays zero.
   *
   * @return Nothing.
   */
  void _loadStore();

  /**
   * Queues the current totals. A save already in progress restarts.
   *
   * @return Nothing.
   */
  void _armSave();

  /**
   * Writes at most one changed byte of the queued slot.
   *
   * @return Nothing.
   */
  void _serviceStore();

  /**
   * Fills one save image from the RAM totals.
   *
   * @param image 32-byte destination.
   * @param sequence Sequence stored in the slot.
   * @return Nothing.
   */
  void _fillImage(uint8_t* image, uint32_t sequence) const;

  /**
   * Scales one conversion. A zero denominator returns 0.
   *
   * @param counts ADC counts.
   * @param num Scale numerator.
   * @param den Scale denominator.
   * @return Scaled value, truncated.
   */
  static uint16_t _scale(uint16_t counts, uint16_t num, uint16_t den);

  /**
   * Writes a big-endian uint16.
   *
   * @param dest Two-byte destination.
   * @param value Value to write.
   * @return Nothing.
   */
  static void _writeU16(uint8_t* dest, uint16_t value);

  /**
   * Writes a big-endian uint32.
   *
   * @param dest Four-byte destination.
   * @param value Value to write.
   * @return Nothing.
   */
  static void _writeU32(uint8_t* dest, uint32_t value);

  /**
   * Caps a millisecond span at the uint16 wire field.
   *
   * @param elapsedMs Elapsed milliseconds.
   * @return Elapsed, or 65535 when it does not fit.
   */
  static uint16_t _capMs(uint32_t elapsedMs);

  /**
   * Run seconds for the open cycle, or the closed cycle.
   *
   * @return Seconds, capped at 65535.
   */
  uint16_t _cycleSeconds() const;

  /**
   * Average millivolts for the cycle payload.
   *
   * @return Truncated mean, or 0 when no power-good sample exists.
   */
  uint16_t _averageMv() const;

  /**
   * Average milliamps for the cycle payload.
   *
   * @return Truncated mean, or 0 when no power-good sample exists.
   */
  uint16_t _averageMa() const;

  IPumpPort& _port;
  IPumpStore& _store;
  PumpConfig _config;
  volatile uint32_t _nowMs;
  volatile uint32_t _windowStartedAt;
  volatile uint32_t _lastOnMs;
  volatile Phase _phase;
  volatile bool _windowArmed;
  volatile bool _npgdLow;
  volatile uint8_t _faultReason;
  bool _outputWasSettled;
  bool _overcurrentSeen;
  bool _overcurrentArmed;
  bool _underArmed;
  bool _stuckArmed;
  uint32_t _overcurrentSinceMs;
  uint32_t _underSinceMs;
  uint32_t _stuckSinceMs;

  bool _storeLoaded;
  bool _senseFresh;
  bool _cycleOpen;
  bool _powerGoodSeen;
  bool _softReached;
  bool _minValid;
  bool _belowArmed;
  bool _fallen;
  bool _cycledSinceOn;
  bool _saveActive;
  uint16_t _voltageMv;
  uint16_t _currentMa;
  uint16_t _peakMv;
  uint16_t _peakMa;
  uint16_t _minMv;
  uint16_t _lifePeakMv;
  uint16_t _lifePeakMa;
  uint16_t _softMs;
  uint16_t _powerGoodMs;
  uint16_t _closedSeconds;
  uint16_t _lastRunSeconds;
  uint16_t _failedStarts;
  uint32_t _cycleStartMs;
  uint32_t _belowSinceMs;
  uint32_t _sumMv;
  uint32_t _sumMa;
  uint32_t _avgCount;
  uint32_t _completedCycles;
  uint32_t _totalSeconds;
  uint32_t _nextSequence;
  uint32_t _saveSequence;
  uint8_t _saveSlot;
  uint8_t _saveStep;
  uint8_t _saveImage[kSlotBytes];
};
