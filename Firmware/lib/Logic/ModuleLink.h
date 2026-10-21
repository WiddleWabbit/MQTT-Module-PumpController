#pragma once

#include <stddef.h>
#include <stdint.h>

#include "IModuleSlavePort.h"
#include "Pump.h"

/**
 * Slave side of the motherboard module protocol for one pump module.
 * Listens at 0x0A only while MOD is low, then keeps the assigned address.
 * Reports the supplied type and firmware version, and protocol 1.
 * Pump commands read and command the supplied pump.
 * No Arduino types.
 */
class ModuleLink
{
public:
  /**
   * Binds the slave port and the pump, and registers this link as
   * the master-write handler. The slave stays disabled until update().
   *
   * @param port Port that enables TWI and stores replies.
   * @param pump Pump commanded by GET, SET, and RESET.
   * @param typeId Module type reported by GET_IDENTITY.
   * @param firmwareVersion Firmware version reported by GET_IDENTITY.
   */
  explicit ModuleLink(IModuleSlavePort& port, Pump& pump, uint16_t typeId,
                      uint16_t firmwareVersion);

  /**
   * Copying would leave the port's write context pointing at the
   * source object.
   *
   * @param other Source link. Unused.
   */
  ModuleLink(const ModuleLink& other) = delete;

  /**
   * Assignment would leave the port's write context pointing at the
   * source object.
   *
   * @param other Source link. Unused.
   * @return Nothing.
   */
  ModuleLink& operator=(const ModuleLink& other) = delete;

  /**
   * Enables 0x0A while MOD is low and this module has no address yet.
   * Disables the slave when MOD rises before assignment. After
   * SET_ADDRESS, MOD is ignored.
   *
   * @param modIsLow True when the MOD pin is low.
   * @return Nothing.
   */
  void update(bool modIsLow);

private:
  /**
   * Forwards one master write to the link stored in context.
   *
   * @param data Frame bytes. Not retained.
   * @param length Number of bytes in data.
   * @param context The ModuleLink that registered this handler.
   * @return Nothing.
   */
  static void _onMasterWriteThunk(const uint8_t* data, size_t length,
                                  void* context);

  /**
   * Handles one master write, including the stop that ends SET_ADDRESS
   * and the repeated start before a read. Builds the padded reply, or
   * commits an assigned address.
   *
   * @param data Frame bytes. Not retained.
   * @param length Number of bytes in data.
   * @return Nothing.
   */
  void _onMasterWrite(const uint8_t* data, size_t length);

  /**
   * Dispatches a CRC-valid command.
   *
   * @param command Command byte.
   * @param payload Payload bytes, unused when payloadLen is 0.
   * @param payloadLen Payload length.
   * @return Nothing.
   */
  void _handleCommand(uint8_t command, const uint8_t* payload,
                      uint8_t payloadLen);

  /**
   * Queues a status reply with an empty payload, padded to 19 bytes.
   *
   * @param status Response status byte.
   * @return Nothing.
   */
  void _replyStatus(uint8_t status);

  /**
   * Queues a padded 19-byte reply.
   *
   * @param status Response status byte.
   * @param payload Payload bytes, or nullptr when payloadLen is 0.
   * @param payloadLen Payload length.
   * @return Nothing.
   */
  void _replyPayload(uint8_t status, const uint8_t* payload,
                     uint8_t payloadLen);

  /**
   * Queues an Ok reply whose payload is the current pump state.
   *
   * @return Nothing.
   */
  void _replyPumpState();

  IModuleSlavePort& _port;
  Pump& _pump;
  uint16_t _typeId;
  uint16_t _firmwareVersion;
  uint8_t _address;
  bool _assigned;
  bool _enabled;
};
