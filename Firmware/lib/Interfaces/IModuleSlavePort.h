#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * I2C slave port used by the module link. The desktop fake records calls.
 * The AVR driver talks to TWI0. One handler receives master writes.
 */
class IModuleSlavePort
{
public:
  /**
   * Receives one master write. On the AVR this runs from the TWI
   * interrupt and finishes inside kClockStretchMaxMs. The desktop fake
   * calls it from deliver.
   */
  using MasterWriteFn = void (*)(const uint8_t* data, size_t length,
                                 void* context);

  virtual ~IModuleSlavePort() = default;

  /**
   * Starts listening at a 7-bit address.
   *
   * @param address7bit Address to acknowledge, normally 0x0A.
   * @return Nothing.
   */
  virtual void enable(uint8_t address7bit) = 0;

  /**
   * Stops acknowledging every address.
   *
   * @return Nothing.
   */
  virtual void disable() = 0;

  /**
   * Commits a new 7-bit address without leaving the bus.
   * Called from the stop callback after SET_ADDRESS.
   *
   * @param address7bit Assigned address, 0x10..0x6F.
   * @return Nothing.
   */
  virtual void setAddress(uint8_t address7bit) = 0;

  /**
   * Copies the next reply, including 0xFF pad. The pointer is only
   * valid for this call.
   *
   * @param frame Bytes to clock out on the next read.
   * @param length Number of bytes in frame.
   * @return Nothing.
   */
  virtual void setReply(const uint8_t* frame, uint8_t length) = 0;

  /**
   * Registers the single protocol handler. A later call replaces it.
   *
   * @param handler Function called with one master write.
   * @param context Pointer passed back to handler on each write.
   * @return Nothing.
   */
  virtual void setMasterWriteHandler(MasterWriteFn handler,
                                     void* context) = 0;
};
