#pragma once

#include <stdint.h>

#include "IPumpPort.h"

/**
 * LM5060 enable, nPGD, and the two lamps on the ATtiny1614.
 * nPGD is open-drain and the board pulls it up, so the status pin
 * is an input with no internal pull-up. High on a lamp pin lights
 * that lamp. begin() runs from setup(), before the slave is enabled.
 */
class AvrPumpPort : public IPumpPort
{
public:
  /**
   * Stores the pins. The levels are applied in begin().
   *
   * @param enablePin LM5060 enable. PMP_EN_PIN in main.cpp.
   * @param statusPin LM5060 nPGD. PMP_STATUS_PIN in main.cpp.
   * @param runningLampPin Green lamp. P_ON_PIN in main.cpp.
   * @param warningLampPin Yellow lamp. P_WARN_PIN in main.cpp.
   */
  AvrPumpPort(uint8_t enablePin, uint8_t statusPin, uint8_t runningLampPin,
              uint8_t warningLampPin);

  /**
   * Drives enable and both lamps low, then makes those pins outputs.
   * The status pin is an input. The external pull-up holds nPGD high
   * until the switch starts.
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Drives the LM5060 enable.
   *
   * @param on True to drive enable high.
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

private:
  uint8_t _enablePin;
  uint8_t _statusPin;
  uint8_t _runningLampPin;
  uint8_t _warningLampPin;
};
