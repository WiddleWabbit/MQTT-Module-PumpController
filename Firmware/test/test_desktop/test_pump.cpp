#include <unity.h>

#include "ModuleProtocol.h"
#include "Pump.h"
#include "fakes/FakePumpPort.h"
#include "fakes/FakePumpStore.h"

namespace
{

constexpr uint32_t kLimitMs = 300;
constexpr uint32_t kCommandLimitMs = 3UL * 60UL * 1000UL;
constexpr uint16_t kLoadedCounts = 620;
constexpr uint16_t kTwelveVoltCounts = 720;
constexpr uint16_t kTenVoltCounts = 600;
constexpr uint16_t kRampCounts = 100;

/**
 * Product scale and limits. A test overrides a field when the
 * constructor argument is what it is checking.
 *
 * @param statusLimitMs Load-fault window.
 * @param commandLimitMs On-command age.
 * @return Settings copied into the pump.
 */
PumpConfig makeConfig(uint32_t statusLimitMs, uint32_t commandLimitMs)
{
  PumpConfig config;
  config.statusLimitMs = statusLimitMs;
  config.commandLimitMs = commandLimitMs;
  config.onMilliamps = 300;
  config.offMilliamps = 150;
  config.offDebounceMs = 20;
  config.settledMillivolts = 10000;
  config.cycleMinMs = 200;
  config.millivoltsNum = 50;
  config.millivoltsDen = 3;
  config.milliampsNum = 6600;
  config.milliampsDen = 1023;
  config.overcurrentMilliamps = 5000;
  config.overcurrentLimitMs = 60;
  config.undercurrentMilliamps = 300;
  config.undercurrentLimitMs = 1000;
  config.stuckOnLimitMs = 100;
  return config;
}

/**
 * One pump and the fakes it drives.
 */
struct Rig
{
  FakePumpPort port;
  FakePumpStore store;
  Pump pump;

  /**
   * Builds a pump with the given limits and the product scale.
   *
   * @param limitMs Load-fault window.
   * @param commandLimitMs On-command age.
   */
  explicit Rig(uint32_t limitMs = kLimitMs,
               uint32_t commandLimitMs = kCommandLimitMs)
    : pump(port, store, makeConfig(limitMs, commandLimitMs))
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
 * Enable high, both lamps off, state on. Held, or running that has
 * not been sampled yet, both look like this until the phase moves.
 *
 * @param rig Pump under test.
 * @return Nothing.
 */
void expectHeld(const Rig& rig)
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

/**
 * Samples a loaded pump and arms the load-fault window at nowMs.
 * nPGD stays high. The arming sample does not fault.
 *
 * @param rig Pump under test.
 * @param nowMs Sample time. turnOn uses the same stamp.
 * @return Nothing.
 */
void armLoaded(Rig& rig, uint32_t nowMs)
{
  rig.port.currentCounts = kLoadedCounts;
  rig.pump.update(nowMs);
  rig.pump.turnOn();
  rig.pump.update(nowMs);
}

/**
 * Programs one changed byte per call until a queued save can finish.
 *
 * @param pump Pump whose store is being flushed.
 * @param nowMs Stamp kept inside the command age.
 * @return Nothing.
 */
void flushStore(Pump& pump, uint32_t nowMs)
{
  for (uint8_t step = 0; step < 40; ++step)
  {
    pump.update(nowMs);
  }
}

/**
 * Reads a big-endian uint16.
 *
 * @param bytes Two bytes, high first.
 * @return Host value.
 */
uint16_t readBeU16(const uint8_t* bytes)
{
  return static_cast<uint16_t>(
      (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

/**
 * Reads a big-endian uint32.
 *
 * @param bytes Four bytes, high first.
 * @return Host value.
 */
uint32_t readBeU32(const uint8_t* bytes)
{
  return (static_cast<uint32_t>(bytes[0]) << 24) |
         (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) |
         static_cast<uint32_t>(bytes[3]);
}

/**
 * Copies the three record payloads.
 *
 * @param pump Pump to read.
 * @param nowPayload Eight-byte destination.
 * @param cyclePayload Sixteen-byte destination.
 * @param totalsPayload Sixteen-byte destination.
 * @return Nothing.
 */
void readRecords(const Pump& pump, uint8_t* nowPayload, uint8_t* cyclePayload,
                 uint8_t* totalsPayload)
{
  pump.readNow(nowPayload);
  pump.readCycle(cyclePayload);
  pump.readTotals(totalsPayload);
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
 * turnOn with no current reports on and holds both lamps off.
 * The old nPGD-only limit does not fault that hold.
 *
 * @return Nothing.
 */
void testZeroCurrentStaysHeldPastTheLimit()
{
  Rig rig;
  rig.pump.update(1000);
  rig.pump.turnOn();

  expectHeld(rig);
  rig.pump.update(1000 + kLimitMs - 1);
  expectHeld(rig);
  rig.pump.update(1000 + kLimitMs + 5000);
  expectHeld(rig);
}

/**
 * A second turnOn while the load window is running does not move
 * it and does not call setEnable.
 *
 * @return Nothing.
 */
void testSecondTurnOnDoesNotRestartTheWindow()
{
  Rig rig;
  armLoaded(rig, 1000);
  const uint32_t writes = rig.port.enableWrites;

  rig.pump.turnOn();

  TEST_ASSERT_EQUAL_UINT32(writes, rig.port.enableWrites);
  rig.pump.update(1000 + kLimitMs - 1);
  expectHeld(rig);
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
  expectHeld(rig);

  rig.port.statusLow = true;
  rig.pump.update(1);

  expectRunning(rig);
}

/**
 * Loaded current with nPGD still high at the limit latches a fault.
 * A later low stays faulted, with enable held low and the yellow
 * lamp on.
 *
 * @return Nothing.
 */
void testStatusHighThroughLimitFaults()
{
  Rig rig;
  armLoaded(rig, 0);

  rig.pump.update(kLimitMs);

  expectFault(rig);
  rig.port.statusLow = true;
  rig.pump.update(kLimitMs + 1000);
  expectFault(rig);
}

/**
 * The last millisecond before the limit is still held.
 *
 * @return Nothing.
 */
void testLimitBoundary()
{
  Rig rig;
  armLoaded(rig, 1000);

  rig.pump.update(1000 + kLimitMs - 1);
  expectHeld(rig);
  rig.pump.update(1000 + kLimitMs);
  expectFault(rig);
}

/**
 * One sample at the limit only arms the window. The next full
 * window is what faults.
 *
 * @return Nothing.
 */
void testUnarmedLimitSampleDoesNotFault()
{
  Rig rig;
  rig.pump.update(0);
  rig.port.currentCounts = kLoadedCounts;
  rig.pump.turnOn();

  rig.pump.update(kLimitMs);
  expectHeld(rig);
  rig.pump.update(kLimitMs + kLimitMs);
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
 * A loaded high that returns low inside the run window stays on.
 * The next high window starts again, and a full window then faults.
 *
 * @return Nothing.
 */
void testStatusBlipWhileRunningStaysOn()
{
  Rig rig;
  rig.pump.update(0);
  rig.port.currentCounts = kLoadedCounts;
  rig.port.statusLow = true;
  rig.pump.turnOn();
  rig.pump.update(0);
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
 * nPGD high for the whole run window, while current stays up,
 * latches a fault.
 *
 * @return Nothing.
 */
void testStatusHighWhileRunningFaults()
{
  Rig rig;
  rig.pump.update(0);
  rig.port.currentCounts = kLoadedCounts;
  rig.port.statusLow = true;
  rig.pump.turnOn();
  rig.pump.update(0);

  rig.port.statusLow = false;
  rig.pump.update(10);
  rig.pump.update(10 + kLimitMs - 1);
  expectRunning(rig);
  rig.pump.update(10 + kLimitMs);
  expectFault(rig);
}

/**
 * nPGD rising with no open cycle leaves held at once. It does not
 * wait out the load window.
 *
 * @return Nothing.
 */
void testRunningWithNoCurrentGoesHeldWhenNpgdRises()
{
  Rig rig;
  rig.pump.update(0);
  rig.port.statusLow = true;
  rig.pump.turnOn();
  expectRunning(rig);

  rig.port.statusLow = false;
  rig.pump.update(10);
  expectHeld(rig);
  rig.pump.update(10 + kLimitMs);
  expectHeld(rig);
}

/**
 * turnOn and turnOff leave a fault latched and enable low.
 *
 * @return Nothing.
 */
void testCommandsWhileFaultedAreIgnored()
{
  Rig rig;
  armLoaded(rig, 0);
  rig.pump.update(kLimitMs);
  expectFault(rig);

  rig.pump.turnOn();
  expectFault(rig);
  rig.pump.turnOff();
  expectFault(rig);
}

/**
 * turnOff from held or running drops enable. A later high is idle.
 *
 * @return Nothing.
 */
void testTurnOffFromHeldAndRunning()
{
  Rig rig;
  rig.pump.update(0);
  rig.pump.turnOn();
  expectHeld(rig);

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
 * reset from fault, running, and off returns to off. turnOn works
 * again, including while nPGD is still high.
 *
 * @return Nothing.
 */
void testResetFromFaultOnAndOff()
{
  Rig rig;
  armLoaded(rig, 0);
  rig.pump.update(kLimitMs);
  expectFault(rig);

  rig.pump.reset();
  expectIdle(rig);

  rig.port.currentCounts = 0;
  rig.pump.update(5000);
  rig.pump.turnOn();
  expectHeld(rig);

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
  armLoaded(rig, kStart);

  rig.pump.update(kStart + kLimitMs - 1);
  expectHeld(rig);
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
  armLoaded(rig, 10);

  rig.pump.update(10 + kShortMs - 1);
  expectHeld(rig);
  rig.pump.update(10 + kShortMs);
  expectFault(rig);
}

/**
 * One millisecond before the on-command age the pump stays on.
 * The age instant turns it off. A later high stays off.
 *
 * @return Nothing.
 */
void testCommandAgeBoundary()
{
  Rig rig;
  rig.pump.update(0);
  rig.port.statusLow = true;
  rig.pump.turnOn();
  expectRunning(rig);

  rig.pump.update(kCommandLimitMs - 1);
  expectRunning(rig);
  rig.pump.update(kCommandLimitMs);
  expectIdle(rig);

  rig.port.statusLow = false;
  rig.pump.update(kCommandLimitMs + 1000);
  expectIdle(rig);
}

/**
 * A later turnOn moves the age and does not rewrite enable.
 * The pump stays on until that later stamp is commandLimitMs old.
 *
 * @return Nothing.
 */
void testRepeatedOnExtendsCommandAge()
{
  Rig rig;
  rig.pump.update(0);
  rig.port.statusLow = true;
  rig.pump.turnOn();
  const uint32_t writes = rig.port.enableWrites;

  rig.pump.update(60000);
  rig.pump.turnOn();
  rig.pump.update(120000);
  rig.pump.turnOn();

  TEST_ASSERT_EQUAL_UINT32(writes, rig.port.enableWrites);
  rig.pump.update(120000 + kCommandLimitMs - 1);
  expectRunning(rig);
  rig.pump.update(120000 + kCommandLimitMs);
  expectIdle(rig);
}

/**
 * A second turnOn while held leaves enable high and does not
 * call setEnable again.
 *
 * @return Nothing.
 */
void testRepeatedOnWhileHeldLeavesEnableHigh()
{
  Rig rig;
  rig.pump.update(1000);
  rig.pump.turnOn();
  const uint32_t writes = rig.port.enableWrites;
  expectHeld(rig);

  rig.pump.turnOn();

  TEST_ASSERT_EQUAL_UINT32(writes, rig.port.enableWrites);
  expectHeld(rig);
}

/**
 * The on-command age is the constructor argument.
 *
 * @return Nothing.
 */
void testCommandLimitIsTheConstructorArgument()
{
  constexpr uint32_t kShortMs = 1000;
  Rig rig(kLimitMs, kShortMs);
  rig.pump.update(10);
  rig.port.statusLow = true;
  rig.pump.turnOn();

  rig.pump.update(10 + kShortMs - 1);
  expectRunning(rig);
  rig.pump.update(10 + kShortMs);
  expectIdle(rig);
}

/**
 * Held turns off when the on command ages out. No current means
 * there is no load fault to win instead.
 *
 * @return Nothing.
 */
void testCommandAgeWhileHeldTurnsOff()
{
  constexpr uint32_t kShortMs = 100;
  Rig rig(kLimitMs, kShortMs);
  rig.pump.update(0);
  rig.pump.turnOn();

  rig.pump.update(kShortMs - 1);
  expectHeld(rig);
  rig.pump.update(kShortMs);
  expectIdle(rig);
}

/**
 * nPGD falling does not move the on-command age. The age still
 * runs from the turnOn that started the pump.
 *
 * @return Nothing.
 */
void testStatusLowDoesNotRefreshCommandAge()
{
  constexpr uint32_t kShortMs = 1000;
  Rig rig(kLimitMs, kShortMs);
  rig.pump.update(0);
  rig.pump.turnOn();
  expectHeld(rig);

  rig.port.statusLow = true;
  rig.pump.update(500);
  expectRunning(rig);
  rig.pump.update(kShortMs - 1);
  expectRunning(rig);
  rig.pump.update(kShortMs);
  expectIdle(rig);
}

/**
 * When the load window and the on-command age elapse on the same
 * sample, the loaded high nPGD stays a fault.
 *
 * @return Nothing.
 */
void testStatusFaultWinsOnTheSharedInstant()
{
  constexpr uint32_t kSharedMs = 100;
  Rig rig(kSharedMs, kSharedMs);
  armLoaded(rig, 0);

  rig.pump.update(kSharedMs);

  expectFault(rig);
}

/**
 * With no current, that same instant is just the command age.
 *
 * @return Nothing.
 */
void testSharedInstantWithNoCurrentTurnsOff()
{
  constexpr uint32_t kSharedMs = 100;
  Rig rig(kSharedMs, kSharedMs);
  rig.pump.update(0);
  rig.pump.turnOn();

  rig.pump.update(kSharedMs);

  expectIdle(rig);
}

/**
 * A fault stays latched across the on-command age. turnOn is still
 * ignored until reset.
 *
 * @return Nothing.
 */
void testCommandAgeLeavesFaultLatched()
{
  Rig rig;
  armLoaded(rig, 0);
  rig.pump.update(kLimitMs);
  expectFault(rig);

  rig.pump.update(kCommandLimitMs + 1000);
  expectFault(rig);
  rig.pump.turnOn();
  expectFault(rig);
}

/**
 * The on-command age uses unsigned subtraction across the
 * millisecond wrap.
 *
 * @return Nothing.
 */
void testCommandAgeUsesUnsignedWrap()
{
  constexpr uint32_t kStart = 0xFFFFFFA0;
  constexpr uint32_t kShortMs = 100;
  Rig rig(kLimitMs, kShortMs);
  rig.pump.update(kStart);
  rig.port.statusLow = true;
  rig.pump.turnOn();

  rig.pump.update(kStart + kShortMs - 1);
  expectRunning(rig);
  rig.pump.update(kStart + kShortMs);
  expectIdle(rig);
}

/**
 * After the on-command age turns the pump off, turnOn is accepted.
 *
 * @return Nothing.
 */
void testTurnOnAfterCommandAge()
{
  constexpr uint32_t kShortMs = 1000;
  Rig rig(kLimitMs, kShortMs);
  rig.pump.update(0);
  rig.port.statusLow = true;
  rig.pump.turnOn();
  rig.pump.update(kShortMs);
  expectIdle(rig);

  rig.pump.turnOn();
  expectRunning(rig);
}

/**
 * Held for a long time at no current still starts cleanly when
 * current arrives and nPGD then falls.
 *
 * @return Nothing.
 */
void testHeldThenCurrentRiseStartsClean()
{
  Rig rig;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(30000);
  expectHeld(rig);

  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kRampCounts;
  rig.pump.update(30000);
  expectHeld(rig);

  rig.port.statusLow = true;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.pump.update(30010);
  expectRunning(rig);

  uint8_t cycle[16];
  rig.pump.readCycle(cycle);
  TEST_ASSERT_EQUAL_UINT16(10, readBeU16(cycle + 10));
}

/**
 * 4 A and nPGD low inside the load window is running, green on,
 * and the cycle is open.
 *
 * @return Nothing.
 */
void testLoadedCurrentAndNpgdLowRuns()
{
  Rig rig;
  rig.port.voltageCounts = kTwelveVoltCounts;
  armLoaded(rig, 0);
  expectHeld(rig);

  rig.port.statusLow = true;
  rig.pump.update(100);

  expectRunning(rig);
  uint8_t now[8];
  rig.pump.readNow(now);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpPhaseRunning, now[0]);
  const uint8_t flags = static_cast<uint8_t>(
      module_protocol::kPumpFlagNpgdLow |
      module_protocol::kPumpFlagEnableHigh);
  TEST_ASSERT_EQUAL_UINT8(flags, now[1]);
  TEST_ASSERT_EQUAL_UINT16(12000, readBeU16(now + 2));
  TEST_ASSERT_EQUAL_UINT16(4000, readBeU16(now + 4));
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(now + 6));
}

/**
 * A current blip that ends before nPGD goes low is one failed
 * start. The phase stays held and the completed totals stay zero.
 *
 * @return Nothing.
 */
void testCurrentBlipBeforeNpgdIsFailedStart()
{
  Rig rig;
  armLoaded(rig, 1000);
  rig.port.currentCounts = 0;
  rig.pump.update(1000);
  rig.pump.update(1020);

  expectHeld(rig);
  rig.pump.update(1000 + kLimitMs);
  expectHeld(rig);

  uint8_t totals[16];
  rig.pump.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT32(0, readBeU32(totals));
  TEST_ASSERT_EQUAL_UINT32(0, readBeU32(totals + 4));
  TEST_ASSERT_EQUAL_UINT16(1, readBeU16(totals + 12));
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(totals + 14));
}

/**
 * Current falling while nPGD stays low closes the cycle and leaves
 * the green lamp on.
 *
 * @return Nothing.
 */
void testCurrentFallWithNpgdLowStaysRunning()
{
  Rig rig;
  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.statusLow = true;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(1000);

  rig.port.currentCounts = 0;
  rig.pump.update(1000);
  expectRunning(rig);
  rig.pump.update(1020);

  expectRunning(rig);
  uint8_t totals[16];
  rig.pump.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT32(1, readBeU32(totals));
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(totals + 12));
}

/**
 * A run that reaches power-good, then loses current and nPGD,
 * returns to held with the cycle record stored.
 *
 * @return Nothing.
 */
void testRunClosesIntoHeldWithRecord()
{
  Rig rig;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kRampCounts;
  rig.pump.update(1000);

  rig.port.statusLow = true;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.pump.update(1009);

  rig.port.voltageCounts = kTenVoltCounts;
  rig.pump.update(1020);

  rig.port.currentCounts = 0;
  rig.port.statusLow = false;
  rig.pump.update(1020);
  expectRunning(rig);
  rig.pump.update(1200);

  expectHeld(rig);
  uint8_t now[8];
  uint8_t cycle[16];
  uint8_t totals[16];
  readRecords(rig.pump, now, cycle, totals);

  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpPhaseHeld, now[0]);
  const uint8_t flags = static_cast<uint8_t>(
      module_protocol::kPumpFlagEnableHigh |
      module_protocol::kPumpFlagCycled);
  TEST_ASSERT_EQUAL_UINT8(flags, now[1]);
  TEST_ASSERT_EQUAL_UINT16(12000, readBeU16(cycle + 0));
  TEST_ASSERT_EQUAL_UINT16(10000, readBeU16(cycle + 2));
  TEST_ASSERT_EQUAL_UINT16(4000, readBeU16(cycle + 4));
  TEST_ASSERT_EQUAL_UINT16(11000, readBeU16(cycle + 6));
  TEST_ASSERT_EQUAL_UINT16(4000, readBeU16(cycle + 8));
  TEST_ASSERT_EQUAL_UINT16(9, readBeU16(cycle + 10));
  TEST_ASSERT_EQUAL_UINT16(9, readBeU16(cycle + 12));
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(cycle + 14));
  TEST_ASSERT_EQUAL_UINT32(1, readBeU32(totals));
  TEST_ASSERT_EQUAL_UINT16(12000, readBeU16(totals + 8));
  TEST_ASSERT_EQUAL_UINT16(4000, readBeU16(totals + 10));
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(totals + 12));

  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kRampCounts;
  rig.port.statusLow = true;
  rig.pump.update(2000);
  rig.pump.readCycle(cycle);
  rig.pump.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT16(1666, readBeU16(cycle + 0));
  TEST_ASSERT_EQUAL_UINT16(12000, readBeU16(totals + 8));
}

/**
 * 720 counts is 12000 mV and 620 counts is 4000 mA.
 *
 * @return Nothing.
 */
void testScaleUsesTheProductFractions()
{
  Rig rig;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.currentCounts = kLoadedCounts;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(0);

  uint8_t now[8];
  rig.pump.readNow(now);
  TEST_ASSERT_EQUAL_UINT16(12000, readBeU16(now + 2));
  TEST_ASSERT_EQUAL_UINT16(4000, readBeU16(now + 4));
}

/**
 * A zero denominator reads as zero instead of dividing.
 *
 * @return Nothing.
 */
void testZeroDenominatorReadsZero()
{
  PumpConfig config = makeConfig(kLimitMs, kCommandLimitMs);
  config.millivoltsDen = 0;
  config.milliampsDen = 0;
  FakePumpPort port;
  FakePumpStore store;
  Pump pump(port, store, config);
  port.voltageCounts = kTwelveVoltCounts;
  port.currentCounts = kLoadedCounts;
  pump.update(0);
  pump.turnOn();
  pump.update(0);

  uint8_t now[8];
  pump.readNow(now);
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(now + 2));
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(now + 4));
}

/**
 * Soft-start uses unsigned subtraction across the millisecond wrap.
 *
 * @return Nothing.
 */
void testSoftStartUsesUnsignedWrap()
{
  constexpr uint32_t kStart = 0xFFFFFFF0;
  Rig rig;
  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kRampCounts;
  rig.pump.update(kStart);
  rig.pump.turnOn();
  rig.pump.update(kStart);

  uint8_t cycle[16];
  rig.pump.readCycle(cycle);
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(cycle + 10));

  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.pump.update(kStart + 9);
  rig.pump.readCycle(cycle);
  TEST_ASSERT_EQUAL_UINT16(9, readBeU16(cycle + 10));
}

/**
 * A sample that is not fresh keeps the last reading and does not
 * fold another average.
 *
 * @return Nothing.
 */
void testStaleSampleDoesNotExtendAverage()
{
  Rig rig;
  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.statusLow = true;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(1000);

  rig.port.senseFresh = false;
  rig.port.voltageCounts = kTenVoltCounts;
  rig.pump.update(1010);

  uint8_t cycle[16];
  rig.pump.readCycle(cycle);
  TEST_ASSERT_EQUAL_UINT16(12000, readBeU16(cycle + 6));

  rig.port.senseFresh = true;
  rig.pump.update(1020);
  rig.pump.readCycle(cycle);
  TEST_ASSERT_EQUAL_UINT16(11000, readBeU16(cycle + 6));
}

/**
 * nPGD low on a stale sample waits for the next fresh sample
 * before it stamps power-good.
 *
 * @return Nothing.
 */
void testPowerGoodWaitsForFreshSample()
{
  Rig rig;
  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kRampCounts;
  rig.pump.update(1000);
  rig.pump.turnOn();
  rig.pump.update(1000);

  rig.port.senseFresh = false;
  rig.port.statusLow = true;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.pump.update(1009);
  expectRunning(rig);

  uint8_t cycle[16];
  rig.pump.readCycle(cycle);
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(cycle + 2));
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(cycle + 12));

  rig.port.senseFresh = true;
  rig.pump.update(1020);
  rig.pump.readCycle(cycle);
  TEST_ASSERT_EQUAL_UINT16(12000, readBeU16(cycle + 2));
  TEST_ASSERT_EQUAL_UINT16(20, readBeU16(cycle + 10));
  TEST_ASSERT_EQUAL_UINT16(20, readBeU16(cycle + 12));
}

/**
 * A zero off debounce closes the cycle on the first low sample.
 *
 * @return Nothing.
 */
void testZeroDebounceClosesOnTheSameSample()
{
  PumpConfig config = makeConfig(kLimitMs, kCommandLimitMs);
  config.offDebounceMs = 0;
  FakePumpPort port;
  FakePumpStore store;
  Pump pump(port, store, config);
  port.currentCounts = kLoadedCounts;
  port.voltageCounts = kTwelveVoltCounts;
  port.statusLow = true;
  pump.update(0);
  pump.turnOn();
  pump.update(0);

  port.currentCounts = 0;
  pump.update(50);

  uint8_t now[8];
  uint8_t totals[16];
  pump.readNow(now);
  pump.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpPhaseRunning, now[0]);
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(now + 6));
  TEST_ASSERT_EQUAL_UINT32(1, readBeU32(totals));
}

/**
 * The on-command age closes an open cycle and clears the cycled flag.
 *
 * @return Nothing.
 */
void testCommandAgeClosesOpenCycle()
{
  constexpr uint32_t kShortMs = 1000;
  Rig rig(kLimitMs, kShortMs);
  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.statusLow = true;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(0);

  rig.pump.update(kShortMs);

  expectIdle(rig);
  uint8_t now[8];
  uint8_t cycle[16];
  uint8_t totals[16];
  readRecords(rig.pump, now, cycle, totals);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpFlagNpgdLow, now[1]);
  TEST_ASSERT_EQUAL_UINT16(1, readBeU16(cycle + 14));
  TEST_ASSERT_EQUAL_UINT32(1, readBeU32(totals));
  TEST_ASSERT_EQUAL_UINT16(1, readBeU16(totals + 14));
}

/**
 * A repeated turnOn refreshes the command age and leaves the open
 * cycle's start time alone.
 *
 * @return Nothing.
 */
void testRepeatedTurnOnLeavesOpenCycle()
{
  Rig rig;
  rig.port.currentCounts = kLoadedCounts;
  rig.port.statusLow = true;
  rig.pump.update(5000);
  rig.pump.turnOn();
  rig.pump.update(5000);
  rig.pump.update(6000);
  const uint32_t writes = rig.port.enableWrites;

  rig.pump.turnOn();
  rig.pump.update(6500);

  TEST_ASSERT_EQUAL_UINT32(writes, rig.port.enableWrites);
  expectRunning(rig);
  uint8_t now[8];
  rig.pump.readNow(now);
  TEST_ASSERT_EQUAL_UINT16(1, readBeU16(now + 6));
}

/**
 * turnOff closes an open cycle, then clears the cycled flag.
 *
 * @return Nothing.
 */
void testTurnOffClosesCycle()
{
  Rig rig;
  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.statusLow = true;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(0);
  rig.pump.update(250);

  rig.pump.turnOff();

  expectIdle(rig);
  uint8_t now[8];
  uint8_t totals[16];
  rig.pump.readNow(now);
  rig.pump.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpFlagNpgdLow, now[1]);
  TEST_ASSERT_EQUAL_UINT32(1, readBeU32(totals));
}

/**
 * reset clears a fault and keeps the totals gathered before it.
 *
 * @return Nothing.
 */
void testResetKeepsTotals()
{
  Rig rig;
  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.statusLow = true;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(0);
  rig.pump.update(250);
  rig.pump.turnOff();

  rig.port.statusLow = false;
  armLoaded(rig, 1000);
  rig.pump.update(1000 + kLimitMs);
  expectFault(rig);

  rig.pump.reset();

  expectIdle(rig);
  uint8_t totals[16];
  rig.pump.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT32(1, readBeU32(totals));
  TEST_ASSERT_EQUAL_UINT16(1, readBeU16(totals + 12));
}

/**
 * A cycle of at least 200 ms is still there after a new pump loads
 * the same store. The cycle detail is not.
 *
 * @return Nothing.
 */
void testLongCyclePersists()
{
  FakePumpPort port;
  FakePumpStore store;
  const PumpConfig config = makeConfig(kLimitMs, kCommandLimitMs);
  Pump pump(port, store, config);
  port.currentCounts = kLoadedCounts;
  port.voltageCounts = kTwelveVoltCounts;
  port.statusLow = true;
  pump.update(0);
  pump.turnOn();
  pump.update(0);
  port.currentCounts = 0;
  port.statusLow = false;
  pump.update(0);
  pump.update(200);
  flushStore(pump, 200);

  Pump loaded(port, store, config);
  loaded.update(1);
  uint8_t cycle[16];
  uint8_t totals[16];
  loaded.readCycle(cycle);
  loaded.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT32(1, readBeU32(totals));
  TEST_ASSERT_EQUAL_UINT16(12000, readBeU16(totals + 8));
  TEST_ASSERT_EQUAL_UINT16(4000, readBeU16(totals + 10));
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(cycle + 0));
  TEST_ASSERT_EQUAL_UINT16(0, readBeU16(cycle + 4));
}

/**
 * A cycle shorter than 200 ms stays in RAM and is gone from a new
 * pump on the same store.
 *
 * @return Nothing.
 */
void testShortCycleDoesNotPersist()
{
  FakePumpPort port;
  FakePumpStore store;
  const PumpConfig config = makeConfig(kLimitMs, kCommandLimitMs);
  Pump pump(port, store, config);
  port.currentCounts = kLoadedCounts;
  port.voltageCounts = kTwelveVoltCounts;
  port.statusLow = true;
  pump.update(1000);
  pump.turnOn();
  pump.update(1000);
  port.currentCounts = 0;
  port.statusLow = false;
  pump.update(1000);
  pump.update(1100);

  uint8_t totals[16];
  pump.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT32(1, readBeU32(totals));
  flushStore(pump, 1100);

  Pump loaded(port, store, config);
  loaded.update(1);
  loaded.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT32(0, readBeU32(totals));
}

/**
 * A later long cycle's saved image includes the earlier short cycle
 * and the failed start.
 *
 * @return Nothing.
 */
void testLaterLongCyclePersistsEarlierTotals()
{
  FakePumpPort port;
  FakePumpStore store;
  const PumpConfig config = makeConfig(kLimitMs, kCommandLimitMs);
  Pump pump(port, store, config);

  port.currentCounts = kLoadedCounts;
  port.voltageCounts = kTwelveVoltCounts;
  pump.update(1000);
  pump.turnOn();
  pump.update(1000);
  port.currentCounts = 0;
  pump.update(1000);
  pump.update(1100);

  port.currentCounts = kLoadedCounts;
  port.statusLow = true;
  pump.update(2000);
  port.currentCounts = 0;
  port.statusLow = false;
  pump.update(2050);
  pump.update(2100);

  port.currentCounts = kLoadedCounts;
  port.statusLow = true;
  pump.update(3000);
  port.currentCounts = 0;
  port.statusLow = false;
  pump.update(3000);
  pump.update(3200);
  flushStore(pump, 3200);

  Pump loaded(port, store, config);
  loaded.update(1);
  uint8_t totals[16];
  loaded.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT32(2, readBeU32(totals));
  TEST_ASSERT_EQUAL_UINT16(1, readBeU16(totals + 12));
  TEST_ASSERT_EQUAL_UINT16(12000, readBeU16(totals + 8));
  TEST_ASSERT_EQUAL_UINT16(4000, readBeU16(totals + 10));
}

/**
 * A save stopped after a few bytes does not load.
 *
 * @return Nothing.
 */
void testTruncatedSaveDoesNotLoad()
{
  FakePumpPort port;
  FakePumpStore store;
  const PumpConfig config = makeConfig(kLimitMs, kCommandLimitMs);
  Pump pump(port, store, config);
  port.currentCounts = kLoadedCounts;
  port.voltageCounts = kTwelveVoltCounts;
  port.statusLow = true;
  pump.update(0);
  pump.turnOn();
  pump.update(0);
  port.currentCounts = 0;
  port.statusLow = false;
  pump.update(0);
  pump.update(200);
  for (uint8_t step = 0; step < 5; ++step)
  {
    pump.update(200);
  }

  Pump loaded(port, store, config);
  loaded.update(1);
  uint8_t totals[16];
  loaded.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT32(0, readBeU32(totals));
}

/**
 * A finished slot stays the one that loads when the next save is
 * abandoned after a few bytes.
 *
 * @return Nothing.
 */
void testTruncatedSaveLeavesPreviousSlot()
{
  FakePumpPort port;
  FakePumpStore store;
  const PumpConfig config = makeConfig(kLimitMs, kCommandLimitMs);
  Pump pump(port, store, config);
  port.currentCounts = kLoadedCounts;
  port.voltageCounts = kTwelveVoltCounts;
  port.statusLow = true;
  pump.update(0);
  pump.turnOn();
  pump.update(0);
  port.currentCounts = 0;
  port.statusLow = false;
  pump.update(0);
  pump.update(200);
  flushStore(pump, 200);

  port.currentCounts = kLoadedCounts;
  port.statusLow = true;
  pump.update(1000);
  port.currentCounts = 0;
  port.statusLow = false;
  pump.update(1000);
  pump.update(1200);
  for (uint8_t step = 0; step < 5; ++step)
  {
    pump.update(1200);
  }

  Pump loaded(port, store, config);
  loaded.update(1);
  uint8_t totals[16];
  loaded.readTotals(totals);
  TEST_ASSERT_EQUAL_UINT32(1, readBeU32(totals));
}

/**
 * 5000 mA for 60 ms latches overcurrent and drops enable. One
 * millisecond earlier stays held. 775 counts is 5000 mA.
 *
 * @return Nothing.
 */
void testOvercurrentAtSixtyMsFaults()
{
  constexpr uint16_t kOverCounts = 775;
  Rig rig;
  rig.port.currentCounts = kOverCounts;
  rig.port.voltageCounts = kRampCounts;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(0);

  rig.pump.update(59);
  expectHeld(rig);
  rig.pump.update(60);

  expectFault(rig);
  uint8_t fault[2];
  rig.pump.readFault(fault);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpFaultOvercurrent, fault[0]);
  TEST_ASSERT_EQUAL_UINT8(0, fault[1]);
}

/**
 * A high sample followed by the terminal falling, while enable stays
 * high, latches overcurrent before the 60 ms timer.
 *
 * @return Nothing.
 */
void testOvercurrentCollapseFaults()
{
  constexpr uint16_t kOverCounts = 775;
  Rig rig;
  rig.port.currentCounts = kOverCounts;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.statusLow = true;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(0);
  expectRunning(rig);

  rig.port.currentCounts = 0;
  rig.port.voltageCounts = 0;
  rig.pump.update(30);

  expectFault(rig);
  uint8_t fault[2];
  rig.pump.readFault(fault);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpFaultOvercurrent, fault[0]);
}

/**
 * Current that falls back under 5000 mA while the terminal stays up
 * ends the overcurrent episode. A later stop is not that fault.
 *
 * @return Nothing.
 */
void testOvercurrentClearsWhenRunContinues()
{
  constexpr uint16_t kOverCounts = 775;
  Rig rig;
  rig.port.currentCounts = kOverCounts;
  rig.port.voltageCounts = kRampCounts;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(0);

  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.statusLow = true;
  rig.pump.update(30);
  expectRunning(rig);

  rig.port.currentCounts = 0;
  rig.port.voltageCounts = 0;
  rig.port.statusLow = false;
  rig.pump.update(40);
  rig.pump.update(60);

  expectHeld(rig);
  uint8_t fault[2];
  rig.pump.readFault(fault);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kFaultNone, fault[0]);
}

/**
 * A normal run under 5000 mA can lose terminal voltage without an
 * overcurrent fault.
 *
 * @return Nothing.
 */
void testNormalStopIsNotOvercurrent()
{
  Rig rig;
  rig.port.currentCounts = kLoadedCounts;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.statusLow = true;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(0);
  rig.pump.update(500);

  rig.port.currentCounts = 0;
  rig.port.voltageCounts = 0;
  rig.port.statusLow = false;
  rig.pump.update(500);
  rig.pump.update(520);

  expectHeld(rig);
  uint8_t fault[2];
  rig.pump.readFault(fault);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kFaultNone, fault[0]);
}

/**
 * The overcurrent window uses unsigned subtraction across the wrap.
 *
 * @return Nothing.
 */
void testOvercurrentUsesUnsignedWrap()
{
  constexpr uint32_t kStart = 0xFFFFFFF0;
  constexpr uint16_t kOverCounts = 775;
  Rig rig;
  rig.port.currentCounts = kOverCounts;
  rig.pump.update(kStart);
  rig.pump.turnOn();
  rig.pump.update(kStart);

  rig.pump.update(kStart + 59);
  expectHeld(rig);
  rig.pump.update(kStart + 60);
  expectFault(rig);
}

/**
 * Terminal voltage at 10 V with current under 300 mA for 1 s latches
 * undercurrent. One millisecond earlier stays running.
 *
 * @return Nothing.
 */
void testUndercurrentAtOneSecondFaults()
{
  Rig rig;
  rig.port.currentCounts = 0;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.statusLow = true;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(0);

  rig.pump.update(999);
  expectRunning(rig);
  rig.pump.update(1000);

  expectFault(rig);
  uint8_t fault[2];
  rig.pump.readFault(fault);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpFaultUndercurrent, fault[0]);
  TEST_ASSERT_EQUAL_UINT8(0, fault[1]);
}

/**
 * Voltage falling below 10 V restarts the undercurrent window.
 *
 * @return Nothing.
 */
void testUndercurrentResetsWhenVoltageFalls()
{
  Rig rig;
  rig.port.currentCounts = 0;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.statusLow = true;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(0);
  rig.pump.update(500);

  rig.port.voltageCounts = 0;
  rig.pump.update(600);
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.pump.update(600);
  rig.pump.update(1599);
  expectRunning(rig);
  rig.pump.update(1600);
  expectFault(rig);
}

/**
 * 303 mA at 12 V is conducting, so the undercurrent window stays idle.
 *
 * @return Nothing.
 */
void testConductingCurrentIsNotUndercurrent()
{
  constexpr uint16_t kConductingCounts = 47;
  Rig rig;
  rig.port.currentCounts = kConductingCounts;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.port.statusLow = true;
  rig.pump.update(0);
  rig.pump.turnOn();
  rig.pump.update(0);
  rig.pump.update(1000);

  expectRunning(rig);
  uint8_t fault[2];
  rig.pump.readFault(fault);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kFaultNone, fault[0]);
}

/**
 * Enable low with the terminal still at 10 V for 100 ms latches
 * stuck on. One millisecond earlier stays off.
 *
 * @return Nothing.
 */
void testStuckOnAtOneHundredMsFaults()
{
  Rig rig;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.pump.update(0);
  rig.pump.update(99);
  expectIdle(rig);
  rig.pump.update(100);

  expectFault(rig);
  uint8_t fault[2];
  rig.pump.readFault(fault);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpFaultStuckOn, fault[0]);
}

/**
 * The terminal falling restarts the stuck-on window.
 *
 * @return Nothing.
 */
void testStuckOnResetsWhenVoltageFalls()
{
  Rig rig;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.pump.update(0);
  rig.pump.update(50);
  rig.port.voltageCounts = 0;
  rig.pump.update(50);
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.pump.update(50);
  rig.pump.update(149);
  expectIdle(rig);
  rig.pump.update(150);
  expectFault(rig);
}

/**
 * The loaded nPGD window reports slow start, and that reason stays
 * when the overcurrent sample arrives later.
 *
 * @return Nothing.
 */
void testSlowStartReasonStays()
{
  Rig rig;
  armLoaded(rig, 0);
  rig.pump.update(kLimitMs);

  uint8_t fault[2];
  rig.pump.readFault(fault);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpFaultSlowStart, fault[0]);

  rig.port.currentCounts = 775;
  rig.pump.update(kLimitMs + 60);
  rig.pump.readFault(fault);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpFaultSlowStart, fault[0]);
  expectFault(rig);
}

/**
 * reset clears the reason and a later turnOn is accepted.
 *
 * @return Nothing.
 */
void testResetClearsFaultReason()
{
  Rig rig;
  rig.port.voltageCounts = kTwelveVoltCounts;
  rig.pump.update(0);
  rig.pump.update(100);
  expectFault(rig);

  rig.pump.reset();
  uint8_t fault[2];
  rig.pump.readFault(fault);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kFaultNone, fault[0]);
  expectIdle(rig);

  rig.port.voltageCounts = 0;
  rig.port.statusLow = true;
  rig.pump.turnOn();
  expectRunning(rig);
}

/**
 * Runs the pump state-machine tests. The desktop main() calls this.
 *
 * @return Nothing.
 */
void runPumpTests()
{
  RUN_TEST(testBootWithStatusHighStaysOff);
  RUN_TEST(testZeroCurrentStaysHeldPastTheLimit);
  RUN_TEST(testSecondTurnOnDoesNotRestartTheWindow);
  RUN_TEST(testStatusLowDuringStartTurnsGreenOn);
  RUN_TEST(testStatusHighThroughLimitFaults);
  RUN_TEST(testLimitBoundary);
  RUN_TEST(testUnarmedLimitSampleDoesNotFault);
  RUN_TEST(testStatusLowAtTurnOnIsRunning);
  RUN_TEST(testStatusBlipWhileRunningStaysOn);
  RUN_TEST(testStatusHighWhileRunningFaults);
  RUN_TEST(testRunningWithNoCurrentGoesHeldWhenNpgdRises);
  RUN_TEST(testCommandsWhileFaultedAreIgnored);
  RUN_TEST(testTurnOffFromHeldAndRunning);
  RUN_TEST(testResetFromFaultOnAndOff);
  RUN_TEST(testLimitUsesUnsignedWrap);
  RUN_TEST(testStatusLimitIsTheConstructorArgument);
  RUN_TEST(testCommandAgeBoundary);
  RUN_TEST(testRepeatedOnExtendsCommandAge);
  RUN_TEST(testRepeatedOnWhileHeldLeavesEnableHigh);
  RUN_TEST(testCommandLimitIsTheConstructorArgument);
  RUN_TEST(testCommandAgeWhileHeldTurnsOff);
  RUN_TEST(testStatusLowDoesNotRefreshCommandAge);
  RUN_TEST(testStatusFaultWinsOnTheSharedInstant);
  RUN_TEST(testSharedInstantWithNoCurrentTurnsOff);
  RUN_TEST(testCommandAgeLeavesFaultLatched);
  RUN_TEST(testCommandAgeUsesUnsignedWrap);
  RUN_TEST(testTurnOnAfterCommandAge);
  RUN_TEST(testHeldThenCurrentRiseStartsClean);
  RUN_TEST(testLoadedCurrentAndNpgdLowRuns);
  RUN_TEST(testCurrentBlipBeforeNpgdIsFailedStart);
  RUN_TEST(testCurrentFallWithNpgdLowStaysRunning);
  RUN_TEST(testRunClosesIntoHeldWithRecord);
  RUN_TEST(testScaleUsesTheProductFractions);
  RUN_TEST(testZeroDenominatorReadsZero);
  RUN_TEST(testSoftStartUsesUnsignedWrap);
  RUN_TEST(testStaleSampleDoesNotExtendAverage);
  RUN_TEST(testPowerGoodWaitsForFreshSample);
  RUN_TEST(testZeroDebounceClosesOnTheSameSample);
  RUN_TEST(testCommandAgeClosesOpenCycle);
  RUN_TEST(testRepeatedTurnOnLeavesOpenCycle);
  RUN_TEST(testTurnOffClosesCycle);
  RUN_TEST(testResetKeepsTotals);
  RUN_TEST(testLongCyclePersists);
  RUN_TEST(testShortCycleDoesNotPersist);
  RUN_TEST(testLaterLongCyclePersistsEarlierTotals);
  RUN_TEST(testTruncatedSaveDoesNotLoad);
  RUN_TEST(testTruncatedSaveLeavesPreviousSlot);
  RUN_TEST(testOvercurrentAtSixtyMsFaults);
  RUN_TEST(testOvercurrentCollapseFaults);
  RUN_TEST(testOvercurrentClearsWhenRunContinues);
  RUN_TEST(testNormalStopIsNotOvercurrent);
  RUN_TEST(testOvercurrentUsesUnsignedWrap);
  RUN_TEST(testUndercurrentAtOneSecondFaults);
  RUN_TEST(testUndercurrentResetsWhenVoltageFalls);
  RUN_TEST(testConductingCurrentIsNotUndercurrent);
  RUN_TEST(testStuckOnAtOneHundredMsFaults);
  RUN_TEST(testStuckOnResetsWhenVoltageFalls);
  RUN_TEST(testSlowStartReasonStays);
  RUN_TEST(testResetClearsFaultReason);
}
