#include "AvrPumpPort.h"

#if defined(__AVR_ATtiny1614__)

#include <Arduino.h>


// ========== Construction ==========

/**
 * Stores the pins. The levels are applied in begin().
 *
 * @param enablePin LM5060 enable. PMP_EN_PIN in main.cpp.
 * @param statusPin LM5060 nPGD. PMP_STATUS_PIN in main.cpp.
 * @param runningLampPin Green lamp. P_ON_PIN in main.cpp.
 * @param warningLampPin Yellow lamp. P_WARN_PIN in main.cpp.
 */
AvrPumpPort::AvrPumpPort(uint8_t enablePin, uint8_t statusPin,
                         uint8_t runningLampPin, uint8_t warningLampPin)
  : _enablePin(enablePin),
    _statusPin(statusPin),
    _runningLampPin(runningLampPin),
    _warningLampPin(warningLampPin)
{
}


// ========== Public API ==========

/**
 * Drives enable and both lamps low, then makes those pins outputs.
 * The status pin is an input. The external pull-up holds nPGD high
 * until the switch starts.
 *
 * @return Nothing.
 */
void AvrPumpPort::begin()
{
  digitalWrite(_enablePin, LOW);
  pinMode(_enablePin, OUTPUT);
  digitalWrite(_runningLampPin, LOW);
  pinMode(_runningLampPin, OUTPUT);
  digitalWrite(_warningLampPin, LOW);
  pinMode(_warningLampPin, OUTPUT);
  pinMode(_statusPin, INPUT);
}

/**
 * Drives the LM5060 enable.
 *
 * @param on True to drive enable high.
 * @return Nothing.
 */
void AvrPumpPort::setEnable(bool on)
{
  digitalWrite(_enablePin, on ? HIGH : LOW);
}

/**
 * Reads nPGD.
 *
 * @return True when the status pin reads low.
 */
bool AvrPumpPort::statusIsLow()
{
  return digitalRead(_statusPin) == LOW;
}

/**
 * Drives the green lamp.
 *
 * @param on True to light the lamp.
 * @return Nothing.
 */
void AvrPumpPort::setRunningLamp(bool on)
{
  digitalWrite(_runningLampPin, on ? HIGH : LOW);
}

/**
 * Drives the yellow lamp.
 *
 * @param on True to light the lamp.
 * @return Nothing.
 */
void AvrPumpPort::setWarningLamp(bool on)
{
  digitalWrite(_warningLampPin, on ? HIGH : LOW);
}

#endif
