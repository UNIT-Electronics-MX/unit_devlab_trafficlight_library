/**
 * @file readAddress.ino
 * @brief Reads the I2C address a traffic lights module reports and whether it is a factory or user address.
 *
 * Compatible with ESP32, RP2040/RP2350, STM32 and AVR.
 *
 * @author Jonathan Mejorado
 * @organization UNIT Electronics MX
 */

#include <Arduino.h>
#include <Wire.h>
#include <DevLab_TrafficLights.h>

#if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)
  #define I2C_BUS Wire1
  #if defined(PIN_WIRE1_SDA) && defined(PIN_WIRE1_SCL) && \
      PIN_WIRE1_SDA == 24U && PIN_WIRE1_SCL == 25U
    // Pulsar connector pins.
    constexpr int SDA_PIN = 24;
    constexpr int SCL_PIN = 25;
  #else
    // Generic RP2040/RP2350 wiring.
    constexpr int SDA_PIN = 12;
    constexpr int SCL_PIN = 13;
  #endif
#elif defined(ARDUINO_ARCH_ESP32)
  #define I2C_BUS Wire
  constexpr int SDA_PIN = 6;
  constexpr int SCL_PIN = 7;
#elif defined(ARDUINO_ARCH_STM32)
  // STM32duino: default I2C pins of the selected board.
  #define I2C_BUS Wire
  constexpr int SDA_PIN = SDA;
  constexpr int SCL_PIN = SCL;
#elif defined(ARDUINO_ARCH_AVR)
  // AVR has fixed I2C pins (Uno/Nano: A4/A5, Mega: 20/21, Leonardo: 2/3);
  // SDA/SCL come from the board variant and begin() ignores the pin numbers.
  #define I2C_BUS Wire
  constexpr int SDA_PIN = SDA;
  constexpr int SCL_PIN = SCL;
#else
#error "Use an ESP32, RP2040, RP2350, STM32, or AVR master"
#endif
#if defined(ARDUINO_ARCH_AVR)
  constexpr uint32_t I2C_FREQ = 100000UL;  // 400 kHz falla con el level shifter en UNO
#else
  constexpr uint32_t I2C_FREQ = 400000UL;
#endif

// Set this to the address of your module (0x36 unless you changed it).
constexpr uint8_t MODULE_ADDRESS = DevLab_TrafficLights::DEFAULT_ADDRESS;

DevLab_TrafficLights lights(I2C_BUS, MODULE_ADDRESS, I2C_FREQ);

void setup() {
  Serial.begin(115200);
  delay(500);
  if (!lights.beginRecovered(SDA_PIN, SCL_PIN)) {
    Serial.println("Traffic lights module not found; check wiring, power, address and firmware");
    return;
  }
  lights.printInfo(Serial);

  uint8_t reported = 0U;
  bool fromFlash = false;
  if (lights.readAddress(reported) && lights.readAddressSource(fromFlash)) {
    Serial.print("Module reports address 0x");
    Serial.println(reported, HEX);
    Serial.println(fromFlash ? "Address stored by the user (Flash)"
                             : "Factory address");
  } else {
    Serial.println("ERROR: address read failed (firmware older than 1.2?)");
  }
}

void loop() {}
