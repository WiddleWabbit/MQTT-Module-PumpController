#pragma once

/**
 * Pump switch, LM5060 nPGD input, and the two lamps. The AVR driver
 * is the board. Desktop tests use a fake. No Arduino types.
 */
class IPumpPort
{
public:
  virtual ~IPumpPort() = default;

  /**
   * Drives the LM5060 enable. High turns the switch on.
   *
   * @param on True to drive enable high.
   * @return Nothing.
   */
  virtual void setEnable(bool on) = 0;

  /**
   * Reads nPGD. Low means the switch has started.
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
};
