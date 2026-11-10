#pragma once

/**
 * One ADC pair from the pump sense pins. fresh is true when this
 * pair is new since the previous read. Counts are 10-bit.
 */
struct PumpSense
{
  uint16_t voltageCounts;
  uint16_t currentCounts;
  bool fresh;
};

/**
 * Pump switch, LM5060 nPGD input, the two lamps, and the sense
 * pair. The AVR driver is the board. Desktop tests use a fake.
 * No Arduino types.
 */
class IPumpPort
{
public:
  virtual ~IPumpPort() = default;

  /**
   * Drives the MCU side of the pressure AND gate. High is the
   * command. The gate output is LM5060 enable.
   *
   * @param on True to drive the command high.
   * @return Nothing.
   */
  virtual void setEnable(bool on) = 0;

  /**
   * Reads nPGD. Low means the switch has enhanced.
   *
   * @return True when the status pin reads low.
   */
  virtual bool statusIsLow() = 0;

  /**
   * Drives the green pump-on lamp. High lights it.
   *
   * @param on True to light the lamp.
   * @return Nothing.
   */
  virtual void setRunningLamp(bool on) = 0;

  /**
   * Drives the yellow warning lamp. High lights it.
   *
   * @param on True to light the lamp.
   * @return Nothing.
   */
  virtual void setWarningLamp(bool on) = 0;

  /**
   * Returns the latest voltage and current counts. A new pair is
   * fresh once. The caller does not wait for a conversion.
   *
   * @return Latest pair. Counts are unchanged when fresh is false.
   */
  virtual PumpSense readSense() = 0;
};
