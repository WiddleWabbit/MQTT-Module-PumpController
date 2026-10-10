#pragma once

#include <stddef.h>
#include <stdint.h>

#include "IModuleSlavePort.h"
#include "ModuleProtocol.h"

/**
 * Records slave-port calls for desktop tests. deliver() invokes the
 * handler the link registered, which is the path the TWI driver uses.
 */
class FakeModuleSlavePort : public IModuleSlavePort
{
public:
  uint8_t enableCount = 0;
  uint8_t disableCount = 0;
  uint8_t setAddressCount = 0;
  uint8_t setReplyCount = 0;
  uint8_t lastEnabledAddress = 0;
  uint8_t lastSetAddress = 0;
  bool enabled = false;
  uint8_t reply[module_protocol::kMaxFrameBytes] = {};
  uint8_t replyLength = 0;

  /**
   * Records enable and the address passed in.
   *
   * @param address7bit Address the link asked to listen on.
   * @return Nothing.
   */
  void enable(uint8_t address7bit) override
  {
    enableCount++;
    lastEnabledAddress = address7bit;
    enabled = true;
  }

  /**
   * Records disable.
   *
   * @return Nothing.
   */
  void disable() override
  {
    disableCount++;
    enabled = false;
  }

  /**
   * Records a committed address.
   *
   * @param address7bit Address written after SET_ADDRESS.
   * @return Nothing.
   */
  void setAddress(uint8_t address7bit) override
  {
    setAddressCount++;
    lastSetAddress = address7bit;
  }

  /**
   * Copies the reply the link queued.
   *
   * @param frame Reply bytes.
   * @param length Number of bytes in frame.
   * @return Nothing.
   */
  void setReply(const uint8_t* frame, uint8_t length) override
  {
    if (frame == nullptr)
    {
      return;
    }
    replyLength = length;
    uint8_t count = length;
    if (count > module_protocol::kMaxFrameBytes)
    {
      count = module_protocol::kMaxFrameBytes;
    }
    for (uint8_t index = 0; index < count; ++index)
    {
      reply[index] = frame[index];
    }
    setReplyCount++;
  }

  /**
   * Stores the handler the link registers from its constructor.
   *
   * @param handler Function called with one master write.
   * @param context Pointer passed back to handler on each write.
   * @return Nothing.
   */
  void setMasterWriteHandler(MasterWriteFn handler, void* context) override
  {
    _writeHandler = handler;
    _writeContext = context;
  }

  /**
   * Delivers one master write to the registered handler. A missing
   * handler does nothing.
   *
   * @param data Frame bytes.
   * @param length Number of bytes in data.
   * @return Nothing.
   */
  void deliver(const uint8_t* data, size_t length)
  {
    if (_writeHandler == nullptr)
    {
      return;
    }
    _writeHandler(data, length, _writeContext);
  }

private:
  MasterWriteFn _writeHandler = nullptr;
  void* _writeContext = nullptr;
};
