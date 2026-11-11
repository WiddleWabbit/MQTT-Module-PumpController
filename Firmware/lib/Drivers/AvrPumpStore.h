#pragma once

#include <stdint.h>

#include "IPumpStore.h"

/**
 * EEPROM for the pump totals and the fault reason. update() programs
 * one byte and can stall the CPU for that program. The pump writes
 * at most one changed byte per update(). Addresses past the EEPROM
 * read as 0xFF.
 */
class AvrPumpStore : public IPumpStore
{
public:
  /**
   * Reads one byte.
   *
   * @param address Byte address.
   * @return Stored byte, or 0xFF past the EEPROM.
   */
  uint8_t read(uint16_t address) override;

  /**
   * Programs one byte when it differs. A matching byte does not stall.
   *
   * @param address Byte address.
   * @param value Byte to store.
   * @return Nothing.
   */
  void write(uint16_t address, uint8_t value) override;
};
