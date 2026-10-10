#pragma once

#include <stdint.h>

#include "IModuleSlavePort.h"
#include "ModuleProtocol.h"

/**
 * TWI0 slave on the alternate pin pair. This board wires motherboard
 * SDA to PA1 and SCL to PA2. PB0 and PB1 are the default TWI pins and
 * are used by the pump, so enable() selects the alternate mux.
 *
 * setAddress writes TWI0.SADDR directly. Wire.begin() ignores a second
 * call while the client is already on, so it cannot commit an address
 * from the stop callback. Bit 0 of SADDR stays clear, which leaves
 * general call off.
 */
class AvrTwi0Slave : public IModuleSlavePort
{
public:
  /**
   * Creates a disabled slave. The link registers the write handler
   * before MOD can go low.
   *
   * @param sdaPin Motherboard SDA. SDA_PIN in main.cpp.
   * @param sclPin Motherboard SCL. SCL_PIN in main.cpp.
   */
  AvrTwi0Slave(uint8_t sdaPin, uint8_t sclPin);

  /**
   * Selects the alternate mux, starts Wire at address7bit, and
   * registers the receive and request callbacks. General call stays off.
   *
   * @param address7bit 7-bit address to acknowledge.
   * @return Nothing.
   */
  void enable(uint8_t address7bit) override;

  /**
   * Stops TWI0 and clears the pin pull-ups. The pins stay inputs, so
   * the motherboard's 4.7 kΩ resistors hold the bus.
   *
   * @return Nothing.
   */
  void disable() override;

  /**
   * Writes TWI0.SADDR. Safe from the stop callback.
   *
   * @param address7bit Assigned 7-bit address.
   * @return Nothing.
   */
  void setAddress(uint8_t address7bit) override;

  /**
   * Copies the reply sent on the next host read.
   *
   * @param frame Reply bytes, including 0xFF pad.
   * @param length Number of bytes in frame, at most 19.
   * @return Nothing.
   */
  void setReply(const uint8_t* frame, uint8_t length) override;

  /**
   * Stores the single protocol handler. Wire's callbacks are plain
   * function pointers, so one driver instance is active.
   *
   * @param handler Function called with one master write.
   * @param context Pointer passed back to handler on each write.
   * @return Nothing.
   */
  void setMasterWriteHandler(MasterWriteFn handler, void* context) override;

private:
  /**
   * Reads the write and calls the registered handler. Runs from the
   * TWI interrupt.
   *
   * @param count Bytes Wire reported.
   * @return Nothing.
   */
  static void _onReceive(int count);

  /**
   * Clocks out the reply stored by setReply. Runs from the TWI
   * interrupt after a repeated start.
   *
   * @return Nothing.
   */
  static void _onRequest();

  /**
   * Clears the internal pull-up on one pin. disable() does this after
   * the peripheral releases the pin.
   *
   * @param pin Pin whose pull-up usePullups() turned on.
   * @return Nothing.
   */
  static void _clearPullup(uint8_t pin);

  uint8_t _sdaPin;
  uint8_t _sclPin;
  static AvrTwi0Slave* _instance;
  MasterWriteFn _writeHandler;
  void* _writeContext;
  uint8_t _reply[module_protocol::kMaxFrameBytes];
  uint8_t _replyLength;
  bool _enabled;
};
