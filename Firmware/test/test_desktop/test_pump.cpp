#include <unity.h>

#include "ModuleProtocol.h"
#include "Pump.h"
#include "fakes/FakePumpPort.h"

namespace
{

constexpr uint32_t kLimitMs = 300;

/**
 * One pump and the port it drives.
 */
struct Rig
{
  FakePumpPort port;
  Pump pump;

  /**
   * Builds a pump with the given high-nPGD limit.
   *
   * @param limitMs Window passed to the pump.
   */
  explicit Rig(uint32_t limitMs = kLimitMs)
    : pump(port, limitMs)
  {
  }
};

/**
 * Enable low, both lamps off, state off.
 *
 * @param rig Pump under test.
 * @return Nothing.
 */
void expectIdle(const Rig& rig)
{
  TEST_ASSERT_FALSE(rig.port.enable);
  TEST_ASSERT_FALSE(rig.port.runningLamp);
  TEST_ASSERT_FALSE(rig.port.warningLamp);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpStateOff, rig.pump.state());
}

/**
 * Enable high, both lamps off, state on. nPGD has not gone low yet.
 *
 * @param rig Pump under test.
 * @return Nothing.
 */
void expectStarting(const Rig& rig)
{
  TEST_ASSERT_TRUE(rig.port.enable);
  TEST_ASSERT_FALSE(rig.port.runningLamp);
  TEST_ASSERT_FALSE(rig.port.warningLamp);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpStateOn, rig.pump.state());
}

/**
 * Enable high, green on, yellow off, state on.
 *
 * @param rig Pump under test.
 * @return Nothing.
 */
void expectRunning(const Rig& rig)
{
  TEST_ASSERT_TRUE(rig.port.enable);
  TEST_ASSERT_TRUE(rig.port.runningLamp);
  TEST_ASSERT_FALSE(rig.port.warningLamp);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpStateOn, rig.pump.state());
}

/**
 * Enable low, yellow on, green off, state fault.
 *
 * @param rig Pump under test.
 * @return Nothing.
 */
void expectFault(const Rig& rig)
{
  TEST_ASSERT_FALSE(rig.port.enable);
  TEST_ASSERT_FALSE(rig.port.runningLamp);
  TEST_ASSERT_TRUE(rig.port.warningLamp);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpStateFault, rig.pump.state());
}

}

/**
 * Status high at boot leaves the switch off. A later high does too.
 *
 * @return Nothing.
 */
void testBootWithStatusHighStaysOff()
{
  Rig rig;

  expectIdle(rig);
  rig.pump.update(0);
  expectIdle(rig);
  rig.pump.update(100000);
  expectIdle(rig);
}

/**
 * turnOn reports on and holds both lamps off until the limit.
 *
 * @return Nothing.
 */
void testTurnOnBeforeLimitReportsOnWithLampsOff()
{
  Rig rig;
  rig.pump.update(1000);

  rig.pump.turnOn();

  expectStarting(rig);
  rig.pump.update(1000 + kLimitMs - 1);
  expectStarting(rig);
}

/**
 * A second turnOn while starting does not move the window.
 *
 * @return Nothing.
 */
void testSecondTurnOnDoesNotRestartTheWindow()
{
  Rig rig;
  rig.pump.update(1000);
  rig.pump.turnOn();
  rig.pump.update(1200);

  rig.pump.turnOn();

  rig.pump.update(1000 + kLimitMs - 1);
  expectStarting(rig);
  rig.pump.update(1000 + kLimitMs);
  expectFault(rig);
}

/**
 * nPGD falling inside the start window lights the green lamp.
 *
 * @return Nothing.
 */
void testStatusLowDuringStartTurnsGreenOn()
{
  Rig rig;
  rig.pump.update(0);
  rig.pump.turnOn();
  expectStarting(rig);

  rig.port.statusLow = true;
  rig.pump.update(1);

  expectRunning(rig);
}

/**
 * nPGD still high at the limit latches a fault. A later low stays
 * faulted, with enable held low and the yellow lamp on.
 *
 * @return Nothing.
 */
void testStatusHighThroughLimitFaults()
{
  Rig rig;
  rig.pump.update(0);
  rig.pump.turnOn();

  rig.pump.update(kLimitMs);

  expectFault(rig);
  rig.port.statusLow = true;
  rig.pump.update(kLimitMs + 1000);
  expectFault(rig);
}

/**
 * The last millisecond before the limit is still starting.
 *
 * @return Nothing.
 */
void testLimitBoundary()
{
  Rig rig;
  rig.pump.update(1000);
  rig.pump.turnOn();

  rig.pump.update(1000 + kLimitMs - 1);
  expectStarting(rig);
  rig.pump.update(1000 + kLimitMs);
  expectFault(rig);
}

/**
 * nPGD already low at turnOn is running, green lamp on.
 *
 * @return Nothing.
 */
void testStatusLowAtTurnOnIsRunning()
{
  Rig rig;
  rig.pump.update(0);
  rig.port.statusLow = true;

  rig.pump.turnOn();

  expectRunning(rig);
  rig.pump.turnOn();
  expectRunning(rig);
}

/**
 * A high that returns low inside the run window stays on. The next
 * high window starts again, and a full window then faults.
 *
 * @return Nothing.
 */
void testStatusBlipWhileRunningStaysOn()
{
  Rig rig;
  rig.pump.update(0);
  rig.port.statusLow = true;
  rig.pump.turnOn();
  expectRunning(rig);

  rig.port.statusLow = false;
  rig.pump.update(10);
  rig.pump.update(10 + kLimitMs - 1);
  expectRunning(rig);

  rig.port.statusLow = true;
  rig.pump.update(10 + kLimitMs);
  expectRunning(rig);

  rig.port.statusLow = false;
  rig.pump.update(8000);
  rig.pump.update(8000 + kLimitMs - 1);
  expectRunning(rig);
  rig.pump.update(8000 + kLimitMs);
  expectFault(rig);
}

/**
 * nPGD high for the whole run window latches a fault.
 *
 * @return Nothing.
 */
void testStatusHighWhileRunningFaults()
{
  Rig rig;
  rig.pump.update(0);
  rig.port.statusLow = true;
  rig.pump.turnOn();

  rig.port.statusLow = false;
  rig.pump.update(10);
  rig.pump.update(10 + kLimitMs - 1);
  expectRunning(rig);
  rig.pump.update(10 + kLimitMs);
  expectFault(rig);
}

/**
 * turnOn and turnOff leave a fault latched and enable low.
 *
 * @return Nothing.
 */
void testCommandsWhileFaultedAreIgnored()
{
  Rig rig;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(kLimitMs);
  expectFault(rig);

  rig.pump.turnOn();
  expectFault(rig);
  rig.pump.turnOff();
  expectFault(rig);
}

/**
 * turnOff from starting or on drops enable. A later high is idle.
 *
 * @return Nothing.
 */
void testTurnOffFromStartingAndOn()
{
  Rig rig;
  rig.pump.update(0);
  rig.pump.turnOn();
  expectStarting(rig);

  rig.pump.turnOff();
  expectIdle(rig);
  rig.pump.update(10000);
  expectIdle(rig);

  rig.port.statusLow = true;
  rig.pump.turnOn();
  expectRunning(rig);
  rig.port.statusLow = false;
  rig.pump.turnOff();
  expectIdle(rig);
  rig.pump.update(20000);
  expectIdle(rig);
}

/**
 * reset from fault, on, and off returns to off. turnOn works again,
 * including while nPGD is still high.
 *
 * @return Nothing.
 */
void testResetFromFaultOnAndOff()
{
  Rig rig;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(kLimitMs);
  expectFault(rig);

  rig.pump.reset();
  expectIdle(rig);

  rig.pump.update(5000);
  rig.pump.turnOn();
  expectStarting(rig);

  rig.port.statusLow = true;
  rig.pump.update(5001);
  expectRunning(rig);

  rig.pump.reset();
  expectIdle(rig);

  rig.pump.reset();
  expectIdle(rig);

  rig.pump.turnOn();
  expectRunning(rig);
}

/**
 * The window uses unsigned subtraction across the millisecond wrap.
 *
 * @return Nothing.
 */
void testLimitUsesUnsignedWrap()
{
  constexpr uint32_t kStart = 0xFFFFFFF0;
  Rig rig;
  rig.pump.update(kStart);
  rig.pump.turnOn();

  rig.pump.update(kStart + kLimitMs - 1);
  expectStarting(rig);
  rig.pump.update(kStart + kLimitMs);
  expectFault(rig);
}

/**
 * The limit is the constructor argument, not a fixed 300 ms.
 *
 * @return Nothing.
 */
void testStatusLimitIsTheConstructorArgument()
{
  constexpr uint32_t kShortMs = 50;
  Rig rig(kShortMs);
  rig.pump.update(10);
  rig.pump.turnOn();

  rig.pump.update(10 + kShortMs - 1);
  expectStarting(rig);
  rig.pump.update(10 + kShortMs);
  expectFault(rig);
}

/**
 * Runs the pump state-machine tests. The desktop main() calls this.
 *
 * @return Nothing.
 */
void runPumpTests()
{
  RUN_TEST(testBootWithStatusHighStaysOff);
  RUN_TEST(testTurnOnBeforeLimitReportsOnWithLampsOff);
  RUN_TEST(testSecondTurnOnDoesNotRestartTheWindow);
  RUN_TEST(testStatusLowDuringStartTurnsGreenOn);
  RUN_TEST(testStatusHighThroughLimitFaults);
  RUN_TEST(testLimitBoundary);
  RUN_TEST(testStatusLowAtTurnOnIsRunning);
  RUN_TEST(testStatusBlipWhileRunningStaysOn);
  RUN_TEST(testStatusHighWhileRunningFaults);
  RUN_TEST(testCommandsWhileFaultedAreIgnored);
  RUN_TEST(testTurnOffFromStartingAndOn);
  RUN_TEST(testResetFromFaultOnAndOff);
  RUN_TEST(testLimitUsesUnsignedWrap);
  RUN_TEST(testStatusLimitIsTheConstructorArgument);
}
