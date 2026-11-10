#include "AvrPumpStore.h"

#if defined(__AVR_ATtiny1614__)

#include <EEPROM.h>


// ========== Public API ==========

/**
 * Reads one byte.
 *
 * @param address Byte address.
 * @return Stored byte, or 0xFF past the EEPROM.
 */
uint8_t AvrPumpStore::read(uint16_t address)
{
  if (address >= static_cast<uint16_t>(EEPROM.length()))
  {
    return 0xFF;
  }
  return EEPROM.read(static_cast<uint8_t>(address));
}

/**
 * Programs one byte when it differs. A matching byte does not stall.
 *
 * @param address Byte address.
 * @param value Byte to store.
 * @return Nothing.
 */
void AvrPumpStore::write(uint16_t address, uint8_t value)
{
  if (address >= static_cast<uint16_t>(EEPROM.length()))
  {
    return;
  }
  EEPROM.update(static_cast<uint8_t>(address), value);
}

#endif
