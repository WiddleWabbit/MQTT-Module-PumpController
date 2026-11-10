#include <Arduino.h>

#include "AvrPumpPort.h"
#include "AvrPumpStore.h"
#include "AvrTwi0Slave.h"
#include "ModuleLink.h"
#include "ModuleProtocol.h"
#include "Pump.h"


// ========== Board pins ==========

// megaTinyCore numbers for the ATtiny1614. PA0 stays the UPDI pin.
//
// Motherboard I2C is the alternate TWI0 pair. PB0 and PB1 are the
// default TWI pins, and this board uses them for the pump.
const uint8_t SDA_PIN = PIN_PA1;
const uint8_t SCL_PIN = PIN_PA2;
const uint8_t MOD_PIN = PIN_PA6;
const uint8_t SNS_PIN = PIN_PA7;

// On-board reset button. The hardware reset pin is PA0, which stays UPDI.
const uint8_t RESET_PIN = PIN_PA3;

// INA186 OUT and the pump-terminal divider. ADC0 AIN4 and AIN5.
const uint8_t PI_SNS_PIN = PIN_PA4;
const uint8_t PV_SNS_PIN = PIN_PA5;

// Yellow warning LED and green pump-on LED. A high lights the LED.
const uint8_t P_WARN_PIN = PIN_PB3;
const uint8_t P_ON_PIN = PIN_PB2;

// MCU side of the pressure AND gate, and LM5060 nPGD.
const uint8_t PMP_EN_PIN = PIN_PB1;
const uint8_t PMP_STATUS_PIN = PIN_PB0;

static_assert(SDA_PIN == 8, "PA1 is motherboard SDA.");
static_assert(SCL_PIN == 9, "PA2 is motherboard SCL.");
static_assert(MOD_PIN == 2, "PA6 is MOD.");
static_assert(SNS_PIN == 3, "PA7 is SNS.");
static_assert(RESET_PIN == 10, "PA3 is the reset button.");
static_assert(PI_SNS_PIN == 0, "PA4 is pump current.");
static_assert(PV_SNS_PIN == 1, "PA5 is pump voltage.");
static_assert(P_WARN_PIN == 4, "PB3 is the yellow warning LED.");
static_assert(P_ON_PIN == 5, "PB2 is the green pump-on LED.");
static_assert(PMP_EN_PIN == 6, "PB1 is pump enable.");
static_assert(PMP_STATUS_PIN == 7, "PB0 is pump status.");


// ========== Product settings ==========

const uint16_t MODULE_TYPE = module_protocol::kTypePumpModule;
const uint16_t FIRMWARE_VERSION = 2;

// How long current may stay up while nPGD stays high. Past the
// LM5060 timers on this board: startup about 157 ms, transition
// about 72 ms, overcurrent about 85 ms.
const uint32_t PUMP_STATUS_LIMIT_MS = 300;

// How long held or running may continue after the last SET_PUMP on.
// The motherboard repeats that command about once a minute.
const uint32_t PUMP_COMMAND_LIMIT_MS = 3UL * 60UL * 1000UL;

// A cycle opens at 300 mA and closes after 150 mA for 20 ms.
// 10000 mV is settled. A cycle shorter than 200 ms is kept in RAM
// and is written with the next longer one.
const uint16_t PUMP_ON_MILLIAMPS = 300;
const uint16_t PUMP_OFF_MILLIAMPS = 150;
const uint32_t PUMP_OFF_DEBOUNCE_MS = 20;
const uint16_t PUMP_SETTLED_MILLIVOLTS = 10000;
const uint32_t PUMP_CYCLE_MIN_MS = 200;

// 10-bit counts at 3.300 V. 720 counts is 12000 mV. 620 counts is
// 4000 mA. mV = counts * 50 / 3. mA = counts * 6600 / 1023.
const uint16_t PUMP_MILLIVOLTS_NUM = 50;
const uint16_t PUMP_MILLIVOLTS_DEN = 3;
const uint16_t PUMP_MILLIAMPS_NUM = 6600;
const uint16_t PUMP_MILLIAMPS_DEN = 1023;


// ========== Modules ==========

AvrTwi0Slave moduleSlave(SDA_PIN, SCL_PIN);
AvrPumpPort pumpPort(PMP_EN_PIN, PMP_STATUS_PIN, P_ON_PIN, P_WARN_PIN,
                     PV_SNS_PIN, PI_SNS_PIN);
AvrPumpStore pumpStore;
const PumpConfig pumpConfig = {
  PUMP_STATUS_LIMIT_MS,
  PUMP_COMMAND_LIMIT_MS,
  PUMP_ON_MILLIAMPS,
  PUMP_OFF_MILLIAMPS,
  PUMP_OFF_DEBOUNCE_MS,
  PUMP_SETTLED_MILLIVOLTS,
  PUMP_CYCLE_MIN_MS,
  PUMP_MILLIVOLTS_NUM,
  PUMP_MILLIVOLTS_DEN,
  PUMP_MILLIAMPS_NUM,
  PUMP_MILLIAMPS_DEN
};
Pump pump(pumpPort, pumpStore, pumpConfig);
ModuleLink moduleLink(moduleSlave, pump, MODULE_TYPE, FIRMWARE_VERSION);


// ========== Application ==========

/**
 * Drives the pump idle, starts the sense pair, then SNS low so the
 * motherboard sees this module seated, and holds MOD as an input
 * with pull-up. Enable and both lamps are low. The slave stays off
 * until MOD is low.
 *
 * @return Nothing.
 */
void setup()
{
  pumpPort.begin();

  pinMode(SNS_PIN, OUTPUT);
  digitalWrite(SNS_PIN, LOW);
  pinMode(MOD_PIN, INPUT_PULLUP);
}

/**
 * Samples the pump, then gates the unconfigured address on MOD.
 * A low MOD enables 0x0A. After SET_ADDRESS the link keeps that
 * address. Pump commands and the record reads run from the TWI
 * interrupt, so the sample runs with interrupts off. The sample
 * also applies the on-command age and may program one EEPROM byte.
 * This loop does not block, so the host's 5 ms MOD gate is met.
 *
 * @return Nothing.
 */
void loop()
{
  noInterrupts();
  pump.update(millis());
  interrupts();

  const bool modIsLow = digitalRead(MOD_PIN) == LOW;
  moduleLink.update(modIsLow);
}
