#include <Arduino.h>

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


// ========== Application ==========

void setup()
{

}


void loop()
{

}
