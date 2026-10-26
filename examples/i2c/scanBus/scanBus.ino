/**
 * @file scanBus.ino
 * @brief Scans the I2C bus and reports DDP devices, flagging the UE0183 traffic lights module.
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

DevLab_I2C_Orchestrator bus(I2C_BUS, I2C_FREQ);
DevLabDDP::Master master(bus, DevLab_TrafficLights::DEVICE_ID);

void setup() {
  Serial.begin(115200);
  delay(500);
  bus.beginRecovered(SDA_PIN, SCL_PIN);

  bool found = false;
  Serial.println("Address  Device");
  for (uint8_t address = 0x08U; address <= 0x77U; ++address) {
    if (!master.ping(address)) continue;
    found = true;
    Serial.print("0x");
    if (address < 0x10U) Serial.print('0');
    Serial.print(address, HEX);
    Serial.print("     ");
    DevLabDDP::DeviceInfo info;
    if (!master.identify(address, info)) {
      Serial.println("non-DDP");
    } else if (info.deviceId == DevLab_TrafficLights::DEVICE_ID) {
      Serial.println("traffic-lights (UE0183)");
    } else {
      Serial.println(DevLabDDP::deviceName(info.deviceId));
    }
  }
  if (!found) Serial.println("--       none");
  Serial.println("The traffic lights address is fixed at 0x36.");
}

void loop() {}
