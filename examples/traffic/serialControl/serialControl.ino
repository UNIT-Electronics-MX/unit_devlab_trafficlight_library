/**
 * @file serialControl.ino
 * @brief Controls each light of the traffic lights module from the Serial monitor.
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
String inputLine;

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  red | yellow | green    show only that light");
  Serial.println("  all | off               all lights on / off");
  Serial.println("  on <r|y|g>              turn a light on, keep the rest");
  Serial.println("  off <r|y|g>             turn a light off, keep the rest");
  Serial.println("  status                  read the lights from the module");
}

bool parseLight(const String &text, uint8_t &light) {
  if (text == "r" || text == "red") light = DevLab_TrafficLights::RED;
  else if (text == "y" || text == "yellow") light = DevLab_TrafficLights::YELLOW;
  else if (text == "g" || text == "green") light = DevLab_TrafficLights::GREEN;
  else return false;
  return true;
}

void printStatus() {
  uint8_t mask = 0U;
  if (!lights.readMask(mask)) {
    Serial.println("ERROR: read failed");
    return;
  }
  Serial.print("Red=");
  Serial.print((mask & DevLab_TrafficLights::RED) ? "on" : "off");
  Serial.print(" Yellow=");
  Serial.print((mask & DevLab_TrafficLights::YELLOW) ? "on" : "off");
  Serial.print(" Green=");
  Serial.println((mask & DevLab_TrafficLights::GREEN) ? "on" : "off");
}

void processCommand(String line) {
  line.trim();
  line.toLowerCase();
  uint8_t light = 0U;
  bool ok = true;

  if (line == "status") {
    printStatus();
    return;
  } else if (line == "all") {
    ok = lights.allOn();
  } else if (line == "off") {
    ok = lights.allOff();
  } else if (parseLight(line, light)) {
    ok = lights.setMask(light);
  } else if (line.startsWith("on ") && parseLight(line.substring(3), light)) {
    ok = lights.setLight(light, true);
  } else if (line.startsWith("off ") && parseLight(line.substring(4), light)) {
    ok = lights.setLight(light, false);
  } else {
    Serial.println("ERROR invalid command");
    printHelp();
    return;
  }

  if (!ok) {
    Serial.println("ERROR I2C command failed");
    return;
  }
  printStatus();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  if (!lights.beginRecovered(SDA_PIN, SCL_PIN)) {
    Serial.println("Traffic lights module not found; check wiring, power and firmware");
    return;
  }
  lights.printInfo(Serial);
  printHelp();
}

void loop() {
  if (!lights.isConnected()) return;

  while (Serial.available()) {
    char character = (char)Serial.read();
    if (character == '\r' || character == '\n') {
      if (inputLine.length() > 0U) {
        processCommand(inputLine);
        inputLine = "";
      }
    } else if (inputLine.length() < 32U) {
      inputLine += character;
    } else {
      inputLine = "";
      Serial.println("ERROR command is too long");
    }
  }
}
