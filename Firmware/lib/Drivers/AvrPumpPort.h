#pragma once

#include <stdint.h>

#include "IPumpPort.h"

/**
 * MCU side of the pressure AND gate, nPGD, the two lamps, and the
 * pump sense pair on the ATtiny1614. nPGD is open-drain and the
 * board pulls it up, so the status pin is an input with no internal
 * pull-up. High on a lamp pin lights that lamp.
 *
 * The sense pair is ADC0, 10-bit, VDD reference. Voltage is converted
 * first, then current. begin() starts the first pair. readSense()
 * does not wait. A new pair starts only when the previous one has
 * finished and at least 1 ms has passed, so a long run of samples
 * fits in a uint32 sum.
 */
class AvrPumpPort : public IPumpPort
{
public:
  /**
   * Stores the pins. The levels and the ADC are applied in begin().
   *
   * @param enablePin MCU side of the pressure AND gate. PMP_EN_PIN in main.cpp.
   * @param statusPin LM5060 nPGD. PMP_STATUS_PIN in main.cpp.
   * @param runningLampPin Green lamp. P_ON_PIN in main.cpp.
   * @param warningLampPin Yellow lamp. P_WARN_PIN in main.cpp.
   * @param voltagePin Pump-terminal divider. PV_SNS_PIN in main.cpp.
   * @param currentPin INA186 OUT. PI_SNS_PIN in main.cpp.
   */
  AvrPumpPort(uint8_t enablePin, uint8_t statusPin, uint8_t runningLampPin,
              uint8_t warningLampPin, uint8_t voltagePin, uint8_t currentPin);

  /**
   * Drives enable and both lamps low, then makes those pins outputs.
   * The status pin is an input. Starts the first sense pair.
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Drives the MCU side of the pressure AND gate.
   *
   * @param on True to drive the command high.
   * @return Nothing.
   */
  void setEnable(bool on) override;

  /**
   * Reads nPGD.
   *
   * @return True when the status pin reads low.
   */
  bool statusIsLow() override;

  /**
   * Drives the green lamp.
   *
   * @param on True to light the lamp.
   * @return Nothing.
   */
  void setRunningLamp(bool on) override;

  /**
   * Drives the yellow lamp.
   *
   * @param on True to light the lamp.
   * @return Nothing.
   */
  void setWarningLamp(bool on) override;

  /**
   * Returns the latest pair and clears fresh. Starts the next pair
   * when the ADC is idle and the last start was at least 1 ms ago.
   * Does not wait for a conversion.
   *
   * @return Latest pair. Counts are unchanged when fresh is false.
   */
  PumpSense readSense() override;

  /**
   * ADC0 result entry. Forwards to the port begin() armed.
   *
   * @return Nothing.
   */
  static void serviceAdcInterrupt();

private:
  static constexpr uint8_t kStageIdle = 0;
  static constexpr uint8_t kStageVoltage = 1;
  static constexpr uint8_t kStageCurrent = 2;
  static constexpr uint32_t kPairIntervalMs = 1;

  /**
   * Starts the voltage conversion of a new pair.
   *
   * @return Nothing.
   */
  void _startVoltage();

  /**
   * Stores one ADC result. A voltage result starts the current
   * conversion. A current result marks the pair fresh.
   *
   * @return Nothing.
   */
  void _onResult();

  uint8_t _enablePin;
  uint8_t _statusPin;
  uint8_t _runningLampPin;
  uint8_t _warningLampPin;
  uint8_t _voltagePin;
  uint8_t _currentPin;
  uint8_t _voltageMux;
  uint8_t _currentMux;
  volatile uint16_t _voltageCounts;
  volatile uint16_t _currentCounts;
  volatile bool _fresh;
  volatile uint8_t _stage;
  uint32_t _lastStartMs;
  bool _begun;
  static AvrPumpPort* _active;
};
