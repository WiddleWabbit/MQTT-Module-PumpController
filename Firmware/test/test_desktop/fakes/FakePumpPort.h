#pragma once

#include "IPumpPort.h"

/**
 * Records pump-port calls and supplies the nPGD level tests choose.
 */
class FakePumpPort : public IPumpPort
{
public:
  bool enable = false;
  bool runningLamp = false;
  bool warningLamp = false;
  bool statusLow = false;
  uint16_t voltageCounts = 0;
  uint16_t currentCounts = 0;
  bool senseFresh = true;
  uint32_t enableWrites = 0;

  /**
   * Records the enable level.
   *
   * @param on True when the pump drives enable high.
   * @return Nothing.
   */
  void setEnable(bool on) override
  {
    enable = on;
    enableWrites = enableWrites + 1;
  }

  /**
   * Returns the nPGD level the test stored.
   *
   * @return True when the test has set statusLow.
   */
  bool statusIsLow() override
  {
    return statusLow;
  }

  /**
   * Records the green lamp.
   *
   * @param on True when the lamp is lit.
   * @return Nothing.
   */
  void setRunningLamp(bool on) override
  {
    runningLamp = on;
  }

  /**
   * Records the yellow lamp.
   *
   * @param on True when the lamp is lit.
   * @return Nothing.
   */
  void setWarningLamp(bool on) override
  {
    warningLamp = on;
  }

  /**
   * Returns the counts the test stored. senseFresh stays as the
   * test left it, so each update can sample. The AVR driver clears
   * its own fresh flag.
   *
   * @return Pair stored by the test.
   */
  PumpSense readSense() override
  {
    PumpSense sense;
    sense.voltageCounts = voltageCounts;
    sense.currentCounts = currentCounts;
    sense.fresh = senseFresh;
    return sense;
  }
};
