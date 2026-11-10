#pragma once

#include <stdint.h>

/**
 * Byte store for the pump totals. The AVR adapter is the EEPROM.
 * Desktop tests use a fake. The pump writes one changed byte per
 * update so a save does not hold the loop for a whole record.
 */
class IPumpStore
{
public:
  virtual ~IPumpStore() = default;

  /**
   * Reads one byte.
   *
   * @param address Byte address.
   * @return Stored byte. Addresses past the store read as 0xFF.
   */
  virtual uint8_t read(uint16_t address) = 0;

  /**
   * Writes one byte. The AVR program stalls the CPU for that byte.
   *
   * @param address Byte address.
   * @param value Byte to store.
   * @return Nothing.
   */
  virtual void write(uint16_t address, uint8_t value) = 0;
};
