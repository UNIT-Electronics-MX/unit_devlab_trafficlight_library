/**
 * @file multiModule.ino
 * @brief Drives several traffic lights modules on one bus, each with its own address.
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

// Give every module a different address first with i2c/changeAddress.
constexpr uint8_t ADDRESS_A = 0x36;  // factory address
constexpr uint8_t ADDRESS_B = 0x37;
constexpr uint8_t ADDRESS_C = 0x38;

DevLab_TrafficLights lightsA(I2C_BUS, ADDRESS_A, I2C_FREQ);
DevLab_TrafficLights lightsB(I2C_BUS, ADDRESS_B, I2C_FREQ);
DevLab_TrafficLights lightsC(I2C_BUS, ADDRESS_C, I2C_FREQ);
DevLab_TrafficLights *const modules[] = {&lightsA, &lightsB, &lightsC};
constexpr size_t MODULE_COUNT = sizeof(modules) / sizeof(modules[0]);

void setup() {
  Serial.begin(115200);
  delay(500);

  // All objects share one physical bus; the first call also frees a stuck bus.
  if (!lightsA.beginRecovered(SDA_PIN, SCL_PIN)) {
    Serial.println("Module A not found");
  }
  for (size_t i = 1; i < MODULE_COUNT; ++i) {
    if (!modules[i]->begin(SDA_PIN, SCL_PIN, I2C_FREQ)) {
      Serial.print("Module at 0x");
      Serial.print(modules[i]->address(), HEX);
      Serial.println(" not found");
    }
  }
}

void loop() {
  // Chase: exactly one module shows green, the others show red.
  for (size_t active = 0; active < MODULE_COUNT; ++active) {
    for (size_t i = 0; i < MODULE_COUNT; ++i) {
      if (!modules[i]->isConnected()) continue;
      if (!(i == active ? modules[i]->green() : modules[i]->red())) {
        Serial.print("ERROR: set failed at 0x");
        Serial.println(modules[i]->address(), HEX);
      }
    }
    delay(1000U);
  }
}
