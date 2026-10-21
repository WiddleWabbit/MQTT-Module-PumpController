#include "ModuleLink.h"

#include "ModuleProtocol.h"


// ========== Construction ==========

/**
 * Binds the slave port and the pump, and registers this link as
 * the master-write handler. The slave stays disabled until update().
 *
 * @param port Port that enables TWI and stores replies.
 * @param pump Pump commanded by GET, SET, and RESET.
 * @param typeId Module type reported by GET_IDENTITY.
 * @param firmwareVersion Firmware version reported by GET_IDENTITY.
 */
ModuleLink::ModuleLink(IModuleSlavePort& port, Pump& pump, uint16_t typeId,
                       uint16_t firmwareVersion)
  : _port(port),
    _pump(pump),
    _typeId(typeId),
    _firmwareVersion(firmwareVersion),
    _address(module_protocol::kUnconfiguredAddress),
    _assigned(false),
    _enabled(false)
{
  _port.setMasterWriteHandler(&_onMasterWriteThunk, this);
}


// ========== Public API ==========

/**
 * Enables 0x0A while MOD is low and this module has no address yet.
 * Disables the slave when MOD rises before assignment. After
 * SET_ADDRESS, MOD is ignored.
 *
 * @param modIsLow True when the MOD pin is low.
 * @return Nothing.
 */
void ModuleLink::update(bool modIsLow)
{
  if (_assigned)
  {
    return;
  }

  if (modIsLow)
  {
    if (!_enabled)
    {
      _enabled = true;
      _address = module_protocol::kUnconfiguredAddress;
      _port.enable(_address);
    }
    return;
  }

  if (_enabled)
  {
    _enabled = false;
    _port.disable();
  }
}


// ========== Master write ==========

/**
 * Forwards one master write to the link stored in context.
 *
 * @param data Frame bytes. Not retained.
 * @param length Number of bytes in data.
 * @param context The ModuleLink that registered this handler.
 * @return Nothing.
 */
void ModuleLink::_onMasterWriteThunk(const uint8_t* data, size_t length,
                                     void* context)
{
  if (context == nullptr)
  {
    return;
  }
  static_cast<ModuleLink*>(context)->_onMasterWrite(data, length);
}

/**
 * Handles one master write, including the stop that ends SET_ADDRESS
 * and the repeated start before a read. Builds the padded reply, or
 * commits an assigned address.
 *
 * @param data Frame bytes. Not retained.
 * @param length Number of bytes in data.
 * @return Nothing.
 */
void ModuleLink::_onMasterWrite(const uint8_t* data, size_t length)
{
  if (data == nullptr || length < module_protocol::kMinFrameBytes)
  {
    _replyStatus(module_protocol::kStatusBadLength);
    return;
  }

  const uint8_t lengthField = data[0];
  const uint8_t maxLengthField =
      static_cast<uint8_t>(2 + module_protocol::kMaxPayload);
  if (lengthField < 2 || lengthField > maxLengthField ||
      length != static_cast<size_t>(lengthField) + 1U)
  {
    _replyStatus(module_protocol::kStatusBadLength);
    return;
  }

  const uint8_t expectedCrc = module_protocol::crc8Smbus(data, lengthField);
  if (expectedCrc != data[lengthField])
  {
    _replyStatus(module_protocol::kStatusBadCrc);
    return;
  }

  const uint8_t payloadLen = static_cast<uint8_t>(lengthField - 2);
  _handleCommand(data[1], data + 2, payloadLen);
}


// ========== Commands ==========

/**
 * Dispatches a CRC-valid command.
 *
 * @param command Command byte.
 * @param payload Payload bytes, unused when payloadLen is 0.
 * @param payloadLen Payload length.
 * @return Nothing.
 */
void ModuleLink::_handleCommand(uint8_t command, const uint8_t* payload,
                                uint8_t payloadLen)
{
  switch (command)
  {
    case module_protocol::kCmdPing:
      if (payloadLen != 0)
      {
        _replyStatus(module_protocol::kStatusBadLength);
        return;
      }
      _replyStatus(module_protocol::kStatusOk);
      return;

    case module_protocol::kCmdGetIdentity:
      if (payloadLen != 0)
      {
        _replyStatus(module_protocol::kStatusBadLength);
        return;
      }
      {
        const uint8_t identity[module_protocol::kIdentityPayloadLen] = {
          static_cast<uint8_t>(_typeId >> 8),
          static_cast<uint8_t>(_typeId & 0xFF),
          module_protocol::kProtocolVersion,
          static_cast<uint8_t>(_firmwareVersion >> 8),
          static_cast<uint8_t>(_firmwareVersion & 0xFF)
        };
        _replyPayload(module_protocol::kStatusOk, identity,
                      module_protocol::kIdentityPayloadLen);
      }
      return;

    case module_protocol::kCmdGetPumpState:
      if (payloadLen != 0)
      {
        _replyStatus(module_protocol::kStatusBadLength);
        return;
      }
      _replyPumpState();
      return;

    case module_protocol::kCmdSetPump:
      if (payloadLen != module_protocol::kPumpSetPayloadLen)
      {
        _replyStatus(module_protocol::kStatusBadLength);
        return;
      }
      if (payload[0] == module_protocol::kPumpStateOff)
      {
        _pump.turnOff();
      }
      else if (payload[0] == module_protocol::kPumpStateOn)
      {
        _pump.turnOn();
      }
      else
      {
        _replyStatus(module_protocol::kStatusBadLength);
        return;
      }
      _replyPumpState();
      return;

    case module_protocol::kCmdResetPump:
      if (payloadLen != 0)
      {
        _replyStatus(module_protocol::kStatusBadLength);
        return;
      }
      _pump.reset();
      _replyPumpState();
      return;

    case module_protocol::kCmdSetAddress:
      if (payloadLen != module_protocol::kSetAddressPayloadLen)
      {
        _replyStatus(module_protocol::kStatusBadLength);
        return;
      }
      if (!_enabled || _assigned ||
          !module_protocol::isAssignableAddress(payload[0]))
      {
        return;
      }
      _assigned = true;
      _address = payload[0];
      _port.setAddress(_address);
      return;

    default:
      _replyStatus(module_protocol::kStatusUnknownCmd);
      return;
  }
}

/**
 * Queues a status reply with an empty payload, padded to 19 bytes.
 *
 * @param status Response status byte.
 * @return Nothing.
 */
void ModuleLink::_replyStatus(uint8_t status)
{
  _replyPayload(status, nullptr, 0);
}

/**
 * Queues an Ok reply whose payload is the current pump state.
 *
 * @return Nothing.
 */
void ModuleLink::_replyPumpState()
{
  const uint8_t state = _pump.state();
  _replyPayload(module_protocol::kStatusOk, &state,
                module_protocol::kPumpStatePayloadLen);
}

/**
 * Queues a padded 19-byte reply.
 *
 * @param status Response status byte.
 * @param payload Payload bytes, or nullptr when payloadLen is 0.
 * @param payloadLen Payload length.
 * @return Nothing.
 */
void ModuleLink::_replyPayload(uint8_t status, const uint8_t* payload,
                               uint8_t payloadLen)
{
  uint8_t frame[module_protocol::kMaxFrameBytes];
  for (uint8_t index = 0; index < module_protocol::kMaxFrameBytes; ++index)
  {
    frame[index] = 0xFF;
  }

  const uint8_t lengthField = static_cast<uint8_t>(2 + payloadLen);
  frame[0] = lengthField;
  frame[1] = status;
  for (uint8_t index = 0; index < payloadLen; ++index)
  {
    frame[static_cast<uint8_t>(2 + index)] = payload[index];
  }
  frame[lengthField] = module_protocol::crc8Smbus(frame, lengthField);
  _port.setReply(frame, module_protocol::kMaxFrameBytes);
}
