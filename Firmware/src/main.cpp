#include <Arduino.h>

#include "AvrTwi0Slave.h"
#include "ModuleLink.h"
#include "ModuleProtocol.h"


// ========== Board pins ==========

// megaTinyCore numbers for the ATtiny1614. PA0 stays the UPDI pin.
// PB2 and PB3 are unconnected.
//
// Motherboard I2C is the alternate TWI0 pair. PB0 and PB1 are the
// default TWI pins, and this board uses them for the pump.
const uint8_t SDA_PIN = PIN_PA1;
const uint8_t SCL_PIN = PIN_PA2;
const uint8_t MOD_PIN = PIN_PA6;
const uint8_t SNS_PIN = PIN_PA7;

// On-board reset button. The hardware reset pin is PA0, which stays UPDI.
const uint8_t RESET_PIN = PIN_PA3;

// Yellow warning LED and green pump-on LED. A high lights the LED.
const uint8_t P_WARN_PIN = PIN_PA4;
const uint8_t P_ON_PIN = PIN_PA5;

// LM5060 enable and status. These are AIN10 and AIN11.
const uint8_t PMP_EN_PIN = PIN_PB1;
const uint8_t PMP_STATUS_PIN = PIN_PB0;

static_assert(SDA_PIN == 8, "PA1 is motherboard SDA.");
static_assert(SCL_PIN == 9, "PA2 is motherboard SCL.");
static_assert(MOD_PIN == 2, "PA6 is MOD.");
static_assert(SNS_PIN == 3, "PA7 is SNS.");
static_assert(RESET_PIN == 10, "PA3 is the reset button.");
static_assert(P_WARN_PIN == 0, "PA4 is the yellow warning LED.");
static_assert(P_ON_PIN == 1, "PA5 is the green pump-on LED.");
static_assert(PMP_EN_PIN == 6, "PB1 is pump enable.");
static_assert(PMP_STATUS_PIN == 7, "PB0 is pump status.");


// ========== Product settings ==========

const uint16_t MODULE_TYPE = module_protocol::kTypePumpModule;
const uint16_t FIRMWARE_VERSION = 1;


// ========== Modules ==========

AvrTwi0Slave moduleSlave(SDA_PIN, SCL_PIN);
ModuleLink moduleLink(moduleSlave, MODULE_TYPE, FIRMWARE_VERSION);


// ========== Application ==========

/**
 * Drives SNS low so the motherboard sees this module seated, and holds
 * MOD as an input with pull-up. The pump enable and both LEDs stay
 * low. The slave stays off until MOD is low.
 *
 * @return Nothing.
 */
void setup()
{
  digitalWrite(PMP_EN_PIN, LOW);
  pinMode(PMP_EN_PIN, OUTPUT);

  digitalWrite(P_ON_PIN, LOW);
  digitalWrite(P_WARN_PIN, LOW);
  pinMode(P_ON_PIN, OUTPUT);
  pinMode(P_WARN_PIN, OUTPUT);

  pinMode(SNS_PIN, OUTPUT);
  digitalWrite(SNS_PIN, LOW);
  pinMode(MOD_PIN, INPUT_PULLUP);
}

/**
 * Gates the unconfigured address on MOD. A low MOD enables 0x0A.
 * After SET_ADDRESS the link keeps that address. This loop does not
 * block, so the host's 5 ms MOD gate is met.
 *
 * @return Nothing.
 */
void loop()
{
  const bool modIsLow = digitalRead(MOD_PIN) == LOW;
  moduleLink.update(modIsLow);
}
