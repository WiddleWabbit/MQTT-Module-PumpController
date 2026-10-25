#include <unity.h>

#include "ModuleLink.h"
#include "ModuleProtocol.h"
#include "Pump.h"
#include "fakes/FakeModuleSlavePort.h"
#include "fakes/FakePumpPort.h"

/**
 * Runs the pump state-machine tests. Defined in test_pump.cpp.
 *
 * @return Nothing.
 */
void runPumpTests();

namespace
{

constexpr uint16_t kTestType = module_protocol::kTypePumpModule;
constexpr uint16_t kTestFirmwareVersion = 1;
constexpr uint32_t kStatusLimitMs = 300;
constexpr uint32_t kCommandLimitMs = 3UL * 60UL * 1000UL;

/**
 * One link, its slave fake, and the pump the link commands.
 */
struct Board
{
  FakeModuleSlavePort port;
  FakePumpPort pumpPort;
  Pump pump;
  ModuleLink link;

  /**
   * Builds a link with the given identity fields.
   *
   * @param typeId Type id supplied to the link.
   * @param firmwareVersion Firmware version supplied to the link.
   * @param commandLimitMs On-command age passed to the pump.
   */
  explicit Board(uint16_t typeId = kTestType,
                 uint16_t firmwareVersion = kTestFirmwareVersion,
                 uint32_t commandLimitMs = kCommandLimitMs)
    : pump(pumpPort, kStatusLimitMs, commandLimitMs),
      link(port, pump, typeId, firmwareVersion)
  {
  }
};

/**
 * Builds one request frame, including its CRC.
 *
 * @param command Command byte.
 * @param payload Payload bytes, or nullptr when payloadLen is 0.
 * @param payloadLen Payload length.
 * @param out Destination. Must hold payloadLen + 3 bytes.
 * @return On-wire size, including the CRC byte.
 */
size_t buildFrame(uint8_t command, const uint8_t* payload, uint8_t payloadLen,
                  uint8_t* out)
{
  const uint8_t lengthField = static_cast<uint8_t>(2 + payloadLen);
  out[0] = lengthField;
  out[1] = command;
  for (uint8_t index = 0; index < payloadLen; ++index)
  {
    out[2 + index] = payload[index];
  }
  out[lengthField] = module_protocol::crc8Smbus(out, lengthField);
  return static_cast<size_t>(lengthField) + 1U;
}

/**
 * Delivers one CRC-valid command through the fake port.
 *
 * @param port Fake whose handler receives the frame.
 * @param command Command byte.
 * @param payload Payload bytes, or nullptr when payloadLen is 0.
 * @param payloadLen Payload length.
 * @return Nothing.
 */
void writeCommand(FakeModuleSlavePort& port, uint8_t command,
                  const uint8_t* payload, uint8_t payloadLen)
{
  uint8_t frame[module_protocol::kMaxFrameBytes] = {};
  const size_t size = buildFrame(command, payload, payloadLen, frame);
  port.deliver(frame, size);
}

/**
 * Checks a 19-byte padded reply against status and payload.
 *
 * @param port Fake that received setReply.
 * @param status Expected status byte.
 * @param payload Expected payload, or nullptr when payloadLen is 0.
 * @param payloadLen Expected payload length.
 * @return Nothing.
 */
void expectReply(const FakeModuleSlavePort& port, uint8_t status,
                 const uint8_t* payload, uint8_t payloadLen)
{
  uint8_t expected[module_protocol::kMaxFrameBytes];
  for (uint8_t index = 0; index < module_protocol::kMaxFrameBytes; ++index)
  {
    expected[index] = 0xFF;
  }
  const uint8_t lengthField = static_cast<uint8_t>(2 + payloadLen);
  expected[0] = lengthField;
  expected[1] = status;
  for (uint8_t index = 0; index < payloadLen; ++index)
  {
    expected[2 + index] = payload[index];
  }
  expected[lengthField] = module_protocol::crc8Smbus(expected, lengthField);
  TEST_ASSERT_EQUAL(module_protocol::kMaxFrameBytes, port.replyLength);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, port.reply,
                                module_protocol::kMaxFrameBytes);
}

/**
 * Fills an identity payload from the values passed to the link.
 *
 * @param typeId Type id supplied at construction.
 * @param firmwareVersion Firmware version supplied at construction.
 * @param out Destination. Must hold kIdentityPayloadLen bytes.
 * @return Nothing.
 */
void fillIdentity(uint16_t typeId, uint16_t firmwareVersion, uint8_t* out)
{
  out[0] = static_cast<uint8_t>(typeId >> 8);
  out[1] = static_cast<uint8_t>(typeId & 0xFF);
  out[2] = module_protocol::kProtocolVersion;
  out[3] = static_cast<uint8_t>(firmwareVersion >> 8);
  out[4] = static_cast<uint8_t>(firmwareVersion & 0xFF);
}

}

/**
 * Unity setup hook. Each test builds its own link.
 *
 * @return Nothing.
 */
void setUp()
{
}

/**
 * Unity teardown hook.
 *
 * @return Nothing.
 */
void tearDown()
{
}

/**
 * CRC-8/SMBus of the ASCII bytes 123456789 is 0xF4.
 *
 * @return Nothing.
 */
void testCrc8SmbusCheckValue()
{
  const uint8_t message[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX8(0xF4, module_protocol::crc8Smbus(message, 9));
}

/**
 * MOD high at boot leaves the slave disabled.
 *
 * @return Nothing.
 */
void testBootWithModHighLeavesSlaveDisabled()
{
  Board board;

  board.link.update(false);

  TEST_ASSERT_FALSE(board.port.enabled);
  TEST_ASSERT_EQUAL(0, board.port.enableCount);
  TEST_ASSERT_EQUAL(0, board.port.disableCount);
  TEST_ASSERT_EQUAL(0, board.port.setAddressCount);
}

/**
 * MOD low enables 0x0A once. MOD high before assignment disables it.
 *
 * @return Nothing.
 */
void testModLowEnablesUnconfiguredAddressAndModHighDisables()
{
  Board board;

  board.link.update(true);
  board.link.update(true);

  TEST_ASSERT_TRUE(board.port.enabled);
  TEST_ASSERT_EQUAL(1, board.port.enableCount);
  TEST_ASSERT_EQUAL(module_protocol::kUnconfiguredAddress,
                    board.port.lastEnabledAddress);
  TEST_ASSERT_EQUAL(0, board.port.setAddressCount);

  board.link.update(false);
  board.link.update(false);

  TEST_ASSERT_FALSE(board.port.enabled);
  TEST_ASSERT_EQUAL(1, board.port.disableCount);
  TEST_ASSERT_EQUAL(0, board.port.setAddressCount);
}

/**
 * PING returns the worked frame 02 00 2A, then 0xFF through byte 19.
 *
 * @return Nothing.
 */
void testPingReplyMatchesWorkedFrame()
{
  Board board;
  board.link.update(true);

  writeCommand(board.port, module_protocol::kCmdPing, nullptr, 0);

  expectReply(board.port, module_protocol::kStatusOk, nullptr, 0);
  const uint8_t worked[] = {0x02, 0x00, 0x2A};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(worked, board.port.reply, 3);
  TEST_ASSERT_EQUAL(0, board.port.setAddressCount);
}

/**
 * GET_IDENTITY for this pump, protocol 1, and firmware 1 is the worked
 * frame 07 00 03 00 01 00 01 D9.
 *
 * @return Nothing.
 */
void testIdentityReportsPumpModule()
{
  Board board;
  board.link.update(true);

  writeCommand(board.port, module_protocol::kCmdGetIdentity, nullptr, 0);

  const uint8_t worked[] = {
    0x07, 0x00, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD9};
  TEST_ASSERT_EQUAL_HEX8(0xD9, module_protocol::crc8Smbus(worked, 7));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(worked, board.port.reply, 8);
  expectReply(board.port, module_protocol::kStatusOk, worked + 2,
              module_protocol::kIdentityPayloadLen);
}

/**
 * A type and firmware version other than the product defaults are
 * what GET_IDENTITY returns.
 *
 * @return Nothing.
 */
void testSuppliedFirmwareVersion()
{
  const uint16_t typeId = 0x1234;
  const uint16_t firmwareVersion = 2;
  Board board(typeId, firmwareVersion);
  board.link.update(true);

  writeCommand(board.port, module_protocol::kCmdGetIdentity, nullptr, 0);

  uint8_t identity[module_protocol::kIdentityPayloadLen];
  fillIdentity(typeId, firmwareVersion, identity);
  expectReply(board.port, module_protocol::kStatusOk, identity,
              module_protocol::kIdentityPayloadLen);
}

/**
 * SET_ADDRESS commits only while listening, and MOD high keeps it.
 * A write before the slave is enabled does not commit. The slot-1
 * request is 03 03 10 F2.
 *
 * @return Nothing.
 */
void testSetAddressCommitsAndSurvivesModHigh()
{
  Board board;
  const uint8_t assigned = 0x10;
  const uint8_t later = 0x11;
  uint8_t request[4] = {};
  const size_t requestSize =
      buildFrame(module_protocol::kCmdSetAddress, &assigned, 1, request);
  const uint8_t worked[] = {0x03, 0x03, 0x10, 0xF2};
  TEST_ASSERT_EQUAL(4, requestSize);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(worked, request, 4);

  writeCommand(board.port, module_protocol::kCmdSetAddress, &assigned, 1);
  TEST_ASSERT_EQUAL(0, board.port.setAddressCount);

  board.link.update(true);
  writeCommand(board.port, module_protocol::kCmdSetAddress, &assigned, 1);

  TEST_ASSERT_EQUAL(1, board.port.setAddressCount);
  TEST_ASSERT_EQUAL(assigned, board.port.lastSetAddress);
  TEST_ASSERT_EQUAL(0, board.port.setReplyCount);
  TEST_ASSERT_TRUE(board.port.enabled);

  board.link.update(false);

  TEST_ASSERT_TRUE(board.port.enabled);
  TEST_ASSERT_EQUAL(0, board.port.disableCount);
  TEST_ASSERT_EQUAL(assigned, board.port.lastSetAddress);

  writeCommand(board.port, module_protocol::kCmdSetAddress, &later, 1);
  TEST_ASSERT_EQUAL(assigned, board.port.lastSetAddress);
  TEST_ASSERT_EQUAL(1, board.port.setAddressCount);

  writeCommand(board.port, module_protocol::kCmdPing, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusOk, nullptr, 0);
}

/**
 * 0x6F is the top assignable address and commits with no reply.
 *
 * @return Nothing.
 */
void testSetAddressAcceptsTopOfRange()
{
  Board board;
  board.link.update(true);
  const uint8_t top = module_protocol::kMaxAssignedAddress;

  writeCommand(board.port, module_protocol::kCmdSetAddress, &top, 1);

  TEST_ASSERT_EQUAL(1, board.port.setAddressCount);
  TEST_ASSERT_EQUAL(top, board.port.lastSetAddress);
  TEST_ASSERT_EQUAL(0, board.port.setReplyCount);
  TEST_ASSERT_TRUE(board.port.enabled);
}

/**
 * A bad CRC or an address outside 0x10..0x6F leaves the slave at 0x0A.
 *
 * @return Nothing.
 */
void testSetAddressRejectsBadCrcAndIllegalAddresses()
{
  Board board;
  board.link.update(true);

  uint8_t frame[8] = {};
  const uint8_t assigned = 0x10;
  const size_t size = buildFrame(module_protocol::kCmdSetAddress, &assigned, 1,
                                 frame);
  frame[size - 1] ^= 0xFF;
  board.port.deliver(frame, size);

  TEST_ASSERT_EQUAL(0, board.port.setAddressCount);
  TEST_ASSERT_EQUAL(module_protocol::kUnconfiguredAddress,
                    board.port.lastEnabledAddress);
  expectReply(board.port, module_protocol::kStatusBadCrc, nullptr, 0);

  const uint8_t repliesBeforeIllegal = board.port.setReplyCount;
  const uint8_t illegal[] = {0x00, 0x0A, 0x70};
  for (uint8_t index = 0; index < 3; ++index)
  {
    writeCommand(board.port, module_protocol::kCmdSetAddress, &illegal[index],
                 1);
  }
  TEST_ASSERT_EQUAL(0, board.port.setAddressCount);
  TEST_ASSERT_EQUAL(repliesBeforeIllegal, board.port.setReplyCount);
  TEST_ASSERT_TRUE(board.port.enabled);
  TEST_ASSERT_EQUAL(module_protocol::kUnconfiguredAddress,
                    board.port.lastEnabledAddress);

  writeCommand(board.port, module_protocol::kCmdPing, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusOk, nullptr, 0);
}

/**
 * ECHO and a sensor command are unknown. A short frame, a mismatched
 * length, a bad CRC, a PING with a payload, and an empty SET_ADDRESS
 * are rejected.
 *
 * @return Nothing.
 */
void testUnknownCommandAndMalformedFrames()
{
  Board board;
  board.link.update(true);

  const uint8_t echoByte = 0x5A;
  writeCommand(board.port, module_protocol::kCmdEcho, &echoByte, 1);
  expectReply(board.port, module_protocol::kStatusUnknownCmd, nullptr, 0);

  writeCommand(board.port, module_protocol::kCmdGetSensorCount, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusUnknownCmd, nullptr, 0);

  const uint8_t truncated[] = {0x02, 0x01};
  board.port.deliver(truncated, sizeof(truncated));
  expectReply(board.port, module_protocol::kStatusBadLength, nullptr, 0);

  uint8_t ping[8] = {};
  const size_t pingSize = buildFrame(module_protocol::kCmdPing, nullptr, 0,
                                     ping);
  ping[pingSize] = 0x00;
  board.port.deliver(ping, pingSize + 1U);
  expectReply(board.port, module_protocol::kStatusBadLength, nullptr, 0);

  ping[2] ^= 0xFF;
  board.port.deliver(ping, pingSize);
  expectReply(board.port, module_protocol::kStatusBadCrc, nullptr, 0);

  const uint8_t extra = 0x01;
  writeCommand(board.port, module_protocol::kCmdPing, &extra, 1);
  expectReply(board.port, module_protocol::kStatusBadLength, nullptr, 0);

  writeCommand(board.port, module_protocol::kCmdSetAddress, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusBadLength, nullptr, 0);
  TEST_ASSERT_EQUAL(0, board.port.setAddressCount);
  TEST_ASSERT_EQUAL(module_protocol::kUnconfiguredAddress,
                    board.port.lastEnabledAddress);
}

/**
 * MOD low, PING, SET_ADDRESS 0x10, MOD high, PING, then GET_IDENTITY.
 *
 * @return Nothing.
 */
void testEnumerationSequence()
{
  Board board;
  const uint8_t assigned = 0x10;

  board.link.update(true);
  writeCommand(board.port, module_protocol::kCmdPing, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusOk, nullptr, 0);

  writeCommand(board.port, module_protocol::kCmdSetAddress, &assigned, 1);
  board.link.update(false);

  TEST_ASSERT_EQUAL(assigned, board.port.lastSetAddress);
  TEST_ASSERT_TRUE(board.port.enabled);
  TEST_ASSERT_EQUAL(0, board.port.disableCount);
  TEST_ASSERT_EQUAL(1, board.port.enableCount);

  writeCommand(board.port, module_protocol::kCmdPing, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusOk, nullptr, 0);

  writeCommand(board.port, module_protocol::kCmdGetIdentity, nullptr, 0);
  const uint8_t worked[] = {
    0x07, 0x00, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD9};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(worked, board.port.reply, 8);
}

/**
 * GET_PUMP_STATE at boot is Ok and off. Enable stays low.
 *
 * @return Nothing.
 */
void testGetPumpStateWhenOff()
{
  Board board;
  board.link.update(true);

  writeCommand(board.port, module_protocol::kCmdGetPumpState, nullptr, 0);

  const uint8_t state = module_protocol::kPumpStateOff;
  expectReply(board.port, module_protocol::kStatusOk, &state, 1);
  TEST_ASSERT_FALSE(board.pumpPort.enable);
  TEST_ASSERT_FALSE(board.pumpPort.runningLamp);
  TEST_ASSERT_FALSE(board.pumpPort.warningLamp);
}

/**
 * GET_PUMP_STATE with a payload is BadLength and leaves the pump off.
 *
 * @return Nothing.
 */
void testGetPumpStateRejectsPayload()
{
  Board board;
  const uint8_t extra = 0;

  writeCommand(board.port, module_protocol::kCmdGetPumpState, &extra, 1);

  expectReply(board.port, module_protocol::kStatusBadLength, nullptr, 0);
  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpStateOff, board.pump.state());
  TEST_ASSERT_FALSE(board.pumpPort.enable);
}

/**
 * SET on reports on with enable high and both lamps off. SET off
 * reports off and drops enable.
 *
 * @return Nothing.
 */
void testSetPumpOnAndOff()
{
  Board board;
  board.pump.update(0);
  const uint8_t on = module_protocol::kPumpStateOn;
  const uint8_t off = module_protocol::kPumpStateOff;

  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);

  expectReply(board.port, module_protocol::kStatusOk, &on, 1);
  TEST_ASSERT_TRUE(board.pumpPort.enable);
  TEST_ASSERT_FALSE(board.pumpPort.runningLamp);
  TEST_ASSERT_FALSE(board.pumpPort.warningLamp);

  writeCommand(board.port, module_protocol::kCmdSetPump, &off, 1);

  expectReply(board.port, module_protocol::kStatusOk, &off, 1);
  TEST_ASSERT_FALSE(board.pumpPort.enable);
  TEST_ASSERT_FALSE(board.pumpPort.runningLamp);
  TEST_ASSERT_FALSE(board.pumpPort.warningLamp);
}

/**
 * SET with fault, an empty payload, or two bytes is BadLength.
 * The pump stays off.
 *
 * @return Nothing.
 */
void testSetPumpRejectsBadPayload()
{
  Board board;
  const uint8_t fault = module_protocol::kPumpStateFault;
  const uint8_t pair[] = {module_protocol::kPumpStateOn, 0};

  writeCommand(board.port, module_protocol::kCmdSetPump, &fault, 1);
  expectReply(board.port, module_protocol::kStatusBadLength, nullptr, 0);
  writeCommand(board.port, module_protocol::kCmdSetPump, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusBadLength, nullptr, 0);
  writeCommand(board.port, module_protocol::kCmdSetPump, pair, 2);
  expectReply(board.port, module_protocol::kStatusBadLength, nullptr, 0);

  TEST_ASSERT_EQUAL_UINT8(module_protocol::kPumpStateOff, board.pump.state());
  TEST_ASSERT_FALSE(board.pumpPort.enable);
}

/**
 * SET on or off while faulted returns fault and leaves enable low.
 *
 * @return Nothing.
 */
void testSetPumpWhileFaultedStaysFault()
{
  Board board;
  board.pump.update(0);
  const uint8_t on = module_protocol::kPumpStateOn;
  const uint8_t off = module_protocol::kPumpStateOff;
  const uint8_t fault = module_protocol::kPumpStateFault;
  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);
  board.pump.update(kStatusLimitMs);

  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);
  expectReply(board.port, module_protocol::kStatusOk, &fault, 1);
  TEST_ASSERT_FALSE(board.pumpPort.enable);
  TEST_ASSERT_TRUE(board.pumpPort.warningLamp);

  writeCommand(board.port, module_protocol::kCmdSetPump, &off, 1);
  expectReply(board.port, module_protocol::kStatusOk, &fault, 1);
  TEST_ASSERT_FALSE(board.pumpPort.enable);
  TEST_ASSERT_FALSE(board.pumpPort.runningLamp);
  TEST_ASSERT_TRUE(board.pumpPort.warningLamp);
}

/**
 * RESET from fault returns off. A later SET on is accepted.
 *
 * @return Nothing.
 */
void testResetClearsFault()
{
  Board board;
  board.pump.update(0);
  const uint8_t on = module_protocol::kPumpStateOn;
  const uint8_t off = module_protocol::kPumpStateOff;
  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);
  board.pump.update(kStatusLimitMs);

  writeCommand(board.port, module_protocol::kCmdResetPump, nullptr, 0);

  expectReply(board.port, module_protocol::kStatusOk, &off, 1);
  TEST_ASSERT_FALSE(board.pumpPort.enable);
  TEST_ASSERT_FALSE(board.pumpPort.warningLamp);

  board.pumpPort.statusLow = true;
  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);
  expectReply(board.port, module_protocol::kStatusOk, &on, 1);
  TEST_ASSERT_TRUE(board.pumpPort.enable);
  TEST_ASSERT_TRUE(board.pumpPort.runningLamp);
  TEST_ASSERT_FALSE(board.pumpPort.warningLamp);
}

/**
 * RESET with a payload is BadLength and leaves the fault latched.
 *
 * @return Nothing.
 */
void testResetRejectsPayload()
{
  Board board;
  board.pump.update(0);
  const uint8_t on = module_protocol::kPumpStateOn;
  const uint8_t extra = 0;
  const uint8_t fault = module_protocol::kPumpStateFault;
  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);
  board.pump.update(kStatusLimitMs);

  writeCommand(board.port, module_protocol::kCmdResetPump, &extra, 1);

  expectReply(board.port, module_protocol::kStatusBadLength, nullptr, 0);
  TEST_ASSERT_EQUAL_UINT8(fault, board.pump.state());
  TEST_ASSERT_FALSE(board.pumpPort.enable);
  TEST_ASSERT_TRUE(board.pumpPort.warningLamp);
}

/**
 * GET during the start window is on. After the limit it is fault,
 * enable is low, and the yellow lamp is on.
 *
 * @return Nothing.
 */
void testStartTimeoutReportsFault()
{
  Board board;
  board.pump.update(0);
  const uint8_t on = module_protocol::kPumpStateOn;
  const uint8_t fault = module_protocol::kPumpStateFault;
  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);

  board.pump.update(kStatusLimitMs - 1);
  writeCommand(board.port, module_protocol::kCmdGetPumpState, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusOk, &on, 1);
  TEST_ASSERT_TRUE(board.pumpPort.enable);
  TEST_ASSERT_FALSE(board.pumpPort.runningLamp);
  TEST_ASSERT_FALSE(board.pumpPort.warningLamp);

  board.pump.update(kStatusLimitMs);
  writeCommand(board.port, module_protocol::kCmdGetPumpState, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusOk, &fault, 1);
  TEST_ASSERT_FALSE(board.pumpPort.enable);
  TEST_ASSERT_TRUE(board.pumpPort.warningLamp);
}

/**
 * SET on with nPGD already low reports on and lights the green lamp.
 *
 * @return Nothing.
 */
void testRunningReportsOnAndGreen()
{
  Board board;
  board.pump.update(0);
  board.pumpPort.statusLow = true;
  const uint8_t on = module_protocol::kPumpStateOn;

  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);
  expectReply(board.port, module_protocol::kStatusOk, &on, 1);
  writeCommand(board.port, module_protocol::kCmdGetPumpState, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusOk, &on, 1);

  TEST_ASSERT_TRUE(board.pumpPort.enable);
  TEST_ASSERT_TRUE(board.pumpPort.runningLamp);
  TEST_ASSERT_FALSE(board.pumpPort.warningLamp);
}

/**
 * A second SET on leaves enable high, does not call setEnable
 * again, and moves the on-command age to that command.
 *
 * @return Nothing.
 */
void testRepeatedSetOnRefreshesCommandAge()
{
  constexpr uint32_t kShortMs = 1000;
  Board board(kTestType, kTestFirmwareVersion, kShortMs);
  board.pump.update(0);
  board.pumpPort.statusLow = true;
  const uint8_t on = module_protocol::kPumpStateOn;
  const uint8_t off = module_protocol::kPumpStateOff;

  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);
  expectReply(board.port, module_protocol::kStatusOk, &on, 1);
  const uint32_t writes = board.pumpPort.enableWrites;

  board.pump.update(600);
  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);
  expectReply(board.port, module_protocol::kStatusOk, &on, 1);

  TEST_ASSERT_EQUAL_UINT32(writes, board.pumpPort.enableWrites);
  TEST_ASSERT_TRUE(board.pumpPort.enable);
  TEST_ASSERT_TRUE(board.pumpPort.runningLamp);

  board.pump.update(600 + kShortMs - 1);
  TEST_ASSERT_EQUAL_UINT8(on, board.pump.state());
  TEST_ASSERT_TRUE(board.pumpPort.enable);

  board.pump.update(600 + kShortMs);
  TEST_ASSERT_EQUAL_UINT8(off, board.pump.state());
  TEST_ASSERT_FALSE(board.pumpPort.enable);
  TEST_ASSERT_FALSE(board.pumpPort.runningLamp);
}

/**
 * PING and GET_PUMP_STATE leave the on-command age on the last
 * SET on. RESET from on turns the pump off.
 *
 * @return Nothing.
 */
void testPollAndResetDoNotRefreshCommandAge()
{
  constexpr uint32_t kShortMs = 1000;
  Board board(kTestType, kTestFirmwareVersion, kShortMs);
  board.pump.update(0);
  board.pumpPort.statusLow = true;
  const uint8_t on = module_protocol::kPumpStateOn;
  const uint8_t off = module_protocol::kPumpStateOff;

  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);
  board.pump.update(400);
  writeCommand(board.port, module_protocol::kCmdPing, nullptr, 0);
  board.pump.update(700);
  writeCommand(board.port, module_protocol::kCmdGetPumpState, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusOk, &on, 1);

  board.pump.update(kShortMs - 1);
  TEST_ASSERT_EQUAL_UINT8(on, board.pump.state());
  TEST_ASSERT_TRUE(board.pumpPort.enable);

  board.pump.update(kShortMs);
  TEST_ASSERT_EQUAL_UINT8(off, board.pump.state());
  TEST_ASSERT_FALSE(board.pumpPort.enable);

  board.pump.update(0);
  board.pumpPort.statusLow = true;
  writeCommand(board.port, module_protocol::kCmdSetPump, &on, 1);
  board.pump.update(500);
  writeCommand(board.port, module_protocol::kCmdResetPump, nullptr, 0);
  expectReply(board.port, module_protocol::kStatusOk, &off, 1);
  TEST_ASSERT_FALSE(board.pumpPort.enable);
  TEST_ASSERT_FALSE(board.pumpPort.runningLamp);

  board.pump.update(500 + kShortMs - 1);
  TEST_ASSERT_EQUAL_UINT8(off, board.pump.state());
  TEST_ASSERT_FALSE(board.pumpPort.enable);
}

/**
 * Runs the module-link desktop tests.
 *
 * @return Unity result.
 */
int main()
{
  UNITY_BEGIN();
  RUN_TEST(testCrc8SmbusCheckValue);
  RUN_TEST(testBootWithModHighLeavesSlaveDisabled);
  RUN_TEST(testModLowEnablesUnconfiguredAddressAndModHighDisables);
  RUN_TEST(testPingReplyMatchesWorkedFrame);
  RUN_TEST(testIdentityReportsPumpModule);
  RUN_TEST(testSuppliedFirmwareVersion);
  RUN_TEST(testSetAddressCommitsAndSurvivesModHigh);
  RUN_TEST(testSetAddressAcceptsTopOfRange);
  RUN_TEST(testSetAddressRejectsBadCrcAndIllegalAddresses);
  RUN_TEST(testUnknownCommandAndMalformedFrames);
  RUN_TEST(testEnumerationSequence);
  RUN_TEST(testGetPumpStateWhenOff);
  RUN_TEST(testGetPumpStateRejectsPayload);
  RUN_TEST(testSetPumpOnAndOff);
  RUN_TEST(testSetPumpRejectsBadPayload);
  RUN_TEST(testSetPumpWhileFaultedStaysFault);
  RUN_TEST(testResetClearsFault);
  RUN_TEST(testResetRejectsPayload);
  RUN_TEST(testStartTimeoutReportsFault);
  RUN_TEST(testRunningReportsOnAndGreen);
  RUN_TEST(testRepeatedSetOnRefreshesCommandAge);
  RUN_TEST(testPollAndResetDoNotRefreshCommandAge);
  runPumpTests();
  return UNITY_END();
}
