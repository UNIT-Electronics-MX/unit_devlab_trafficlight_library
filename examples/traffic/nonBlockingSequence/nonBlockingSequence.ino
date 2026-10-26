/**
 * @file nonBlockingSequence.ino
 * @brief Runs a green/yellow/red traffic sequence with millis(), without blocking loop().
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

DevLab_TrafficLights lights(I2C_BUS, DevLab_TrafficLights::DEFAULT_ADDRESS, I2C_FREQ);

struct Step {
  uint8_t mask;
  uint32_t durationMs;
};

// The module keeps no timing of its own: the controller picks the sequence.
const Step SEQUENCE[] = {
  {DevLab_TrafficLights::GREEN, 4000UL},
  {DevLab_TrafficLights::YELLOW, 1500UL},
  {DevLab_TrafficLights::RED, 4000UL},
  {DevLab_TrafficLights::RED | DevLab_TrafficLights::YELLOW, 1000UL},  // prepare to go
};
constexpr size_t STEP_COUNT = sizeof(SEQUENCE) / sizeof(SEQUENCE[0]);

size_t step = 0;
uint32_t stepStart = 0;
bool started = false;

void setup() {
  Serial.begin(115200);
  delay(500);
  if (!lights.beginRecovered(SDA_PIN, SCL_PIN)) {
    Serial.println("Traffic lights module not found; check wiring, power and firmware");
    return;
  }
  lights.printInfo(Serial);
}

void loop() {
  if (!lights.isConnected()) return;

  uint32_t now = millis();
  if (started && (uint32_t)(now - stepStart) < SEQUENCE[step].durationMs) {
    // Other work can run here while the current step is shown.
    return;
  }

  if (started) step = (step + 1U) % STEP_COUNT;
  if (lights.setMask(SEQUENCE[step].mask)) {
    started = true;
    stepStart = now;
    Serial.print("Step ");
    Serial.print(step);
    Serial.print(" -> mask 0x");
    Serial.println(lights.mask(), HEX);
  } else {
    Serial.println("ERROR: set lights failed");
    delay(500U);
  }
}
