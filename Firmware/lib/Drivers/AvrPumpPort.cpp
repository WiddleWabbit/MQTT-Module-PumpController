#include "AvrPumpPort.h"

#if defined(__AVR_ATtiny1614__)

#include <Arduino.h>


// ========== Construction ==========

AvrPumpPort* AvrPumpPort::_active = nullptr;

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
AvrPumpPort::AvrPumpPort(uint8_t enablePin, uint8_t statusPin,
                         uint8_t runningLampPin, uint8_t warningLampPin,
                         uint8_t voltagePin, uint8_t currentPin)
  : _enablePin(enablePin),
    _statusPin(statusPin),
    _runningLampPin(runningLampPin),
    _warningLampPin(warningLampPin),
    _voltagePin(voltagePin),
    _currentPin(currentPin),
    _voltageMux(0),
    _currentMux(0),
    _voltageCounts(0),
    _currentCounts(0),
    _fresh(false),
    _stage(kStageIdle),
    _lastStartMs(0),
    _begun(false)
{
}


// ========== Public API ==========

/**
 * Drives enable and both lamps low, then makes those pins outputs.
 * The status pin is an input. Starts the first sense pair.
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

  _voltageMux = static_cast<uint8_t>(digitalPinToAnalogInput(_voltagePin));
  _currentMux = static_cast<uint8_t>(digitalPinToAnalogInput(_currentPin));
  _active = this;
  _begun = true;

  // 16 MHz / 16 is 1 MHz. SAMPCAP is required above 1 V. 10-bit is
  // RESSEL clear. Sample length 8 clocks suits the low-impedance
  // divider and the INA186. The digital input buffers stay on.
  // The prescaler is changed while the ADC is disabled.
  ADC0.CTRLA = 0;
  ADC0.CTRLB = ADC_SAMPNUM_ACC1_gc;
  ADC0.CTRLC = ADC_PRESC_DIV16_gc | ADC_REFSEL_VDDREF_gc | ADC_SAMPCAP_bm;
  ADC0.CTRLD = ADC_INITDLY_DLY0_gc;
  ADC0.SAMPCTRL = 0x08;
  ADC0.INTFLAGS = ADC_RESRDY_bm;
  ADC0.INTCTRL = ADC_RESRDY_bm;
  ADC0.CTRLA = ADC_ENABLE_bm;
  _startVoltage();
}

/**
 * Drives the MCU side of the pressure AND gate.
 *
 * @param on True to drive the command high.
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

/**
 * Returns the latest pair and clears fresh. Starts the next pair
 * when the ADC is idle and the last start was at least 1 ms ago.
 * Does not wait for a conversion.
 *
 * @return Latest pair. Counts are unchanged when fresh is false.
 */
PumpSense AvrPumpPort::readSense()
{
  PumpSense sense;
  sense.voltageCounts = _voltageCounts;
  sense.currentCounts = _currentCounts;
  sense.fresh = _fresh;
  _fresh = false;

  if (!_begun || _stage != kStageIdle)
  {
    return sense;
  }

  const uint32_t elapsed = static_cast<uint32_t>(millis() - _lastStartMs);
  if (elapsed >= kPairIntervalMs)
  {
    _startVoltage();
  }
  return sense;
}

/**
 * ADC0 result entry. Forwards to the port begin() armed.
 *
 * @return Nothing.
 */
void AvrPumpPort::serviceAdcInterrupt()
{
  if (_active == nullptr)
  {
    return;
  }
  _active->_onResult();
}



// ========== Conversions ==========

/**
 * Stores one ADC result. A voltage result starts the current
 * conversion. A current result marks the pair fresh.
 *
 * @return Nothing.
 */
void AvrPumpPort::_onResult()
{
  const uint16_t counts = ADC0.RES;
  ADC0.INTFLAGS = ADC_RESRDY_bm;

  if (_stage == kStageVoltage)
  {
    _voltageCounts = counts;
    _stage = kStageCurrent;
    ADC0.MUXPOS = _currentMux;
    ADC0.COMMAND = ADC_STCONV_bm;
    return;
  }

  if (_stage == kStageCurrent)
  {
    _currentCounts = counts;
    _fresh = true;
    _stage = kStageIdle;
  }
}


/**
 * Starts the voltage conversion of a new pair.
 *
 * @return Nothing.
 */
void AvrPumpPort::_startVoltage()
{
  _stage = kStageVoltage;
  _lastStartMs = millis();
  ADC0.MUXPOS = _voltageMux;
  ADC0.INTFLAGS = ADC_RESRDY_bm;
  ADC0.COMMAND = ADC_STCONV_bm;
}


// ========== Interrupt ==========

/**
 * Forwards one ADC0 result into the armed port.
 */
ISR(ADC0_RESRDY_vect)
{
  AvrPumpPort::serviceAdcInterrupt();
}

#endif
