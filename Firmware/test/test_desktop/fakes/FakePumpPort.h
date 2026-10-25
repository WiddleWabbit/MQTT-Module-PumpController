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
};
