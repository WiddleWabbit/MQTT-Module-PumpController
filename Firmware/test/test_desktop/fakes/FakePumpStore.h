#pragma once

#include <stdint.h>

#include "IPumpStore.h"

/**
 * 256-byte store that starts at 0xFF, matching a blank EEPROM.
 * Addresses past the end read as 0xFF and ignore writes.
 */
class FakePumpStore : public IPumpStore
{
public:
  static constexpr uint16_t kSize = 256;

  /**
   * Fills the store with 0xFF.
   */
  FakePumpStore()
  {
    for (uint16_t index = 0; index < kSize; ++index)
    {
      _bytes[index] = 0xFF;
    }
  }

  /**
   * Reads one byte.
   *
   * @param address Byte address.
   * @return Stored byte, or 0xFF past the end.
   */
  uint8_t read(uint16_t address) override
  {
    if (address >= kSize)
    {
      return 0xFF;
    }
    return _bytes[address];
  }

  /**
   * Writes one byte. Past the end does nothing.
   *
   * @param address Byte address.
   * @param value Byte to store.
   * @return Nothing.
   */
  void write(uint16_t address, uint8_t value) override
  {
    if (address >= kSize)
    {
      return;
    }
    _bytes[address] = value;
  }

private:
  uint8_t _bytes[kSize];
};
