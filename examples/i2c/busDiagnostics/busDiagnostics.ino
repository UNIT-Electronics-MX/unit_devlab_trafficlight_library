/**
 * @file busDiagnostics.ino
 * @brief Raw I2C bus check that never blocks: shows the SDA/SCL levels and the
 *        result of every address, to find wiring or pull-up problems.
 *
 * Uses only Wire (no DevLab classes). Compatible with ESP32, RP2040/RP2350,
 * STM32 and AVR.
 *
 * @author Jonathan Mejorado
 * @organization UNIT Electronics MX
 */

#include <Arduino.h>
#include <Wire.h>

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


const char *levelName(int pin) { return digitalRead(pin) ? "high" : "LOW"; }

// Wire.endTransmission(): 0 ok, 2 NACK on address (nobody there), 3 NACK on
// data, 4 other error, 5 timeout (the bus was held or never released).
const char *resultName(uint8_t code) {
  switch (code) {
    case 0: return "ACK";
    case 2: return "no device";
    case 3: return "NACK data";
    case 4: return "bus error";
    case 5: return "TIMEOUT";
    default: return "error";
  }
}

void printLines(const char *when) {
  Serial.print(when);
  Serial.print(": SDA=");
  Serial.print(levelName(SDA_PIN));
  Serial.print(" SCL=");
  Serial.println(levelName(SCL_PIN));
}

void scan(uint32_t clock) {
  I2C_BUS.setClock(clock);
  Serial.print("--- scan at ");
  Serial.print(clock / 1000UL);
  Serial.println(" kHz");
  uint8_t acks = 0, timeouts = 0, errors = 0;
  for (uint8_t address = 0x08U; address <= 0x77U; ++address) {
    yield();  // keep the watchdog fed while scanning
    uint32_t started = millis();
    I2C_BUS.beginTransmission(address);
    uint8_t code = I2C_BUS.endTransmission();
    uint32_t took = millis() - started;
    if (code == 0) {
      ++acks;
    } else if (code == 5) {
      ++timeouts;
    } else if (code != 2) {
      ++errors;
    }
    if (code != 2 || took > 20UL) {  // report everything except a quick "nobody"
      Serial.print("0x");
      if (address < 0x10U) Serial.print('0');
      Serial.print(address, HEX);
      Serial.print(": ");
      Serial.print(resultName(code));
      Serial.print(" (");
      Serial.print(took);
      Serial.println(" ms)");
    }
  }
  Serial.print("found=");
  Serial.print(acks);
  Serial.print(" timeouts=");
  Serial.print(timeouts);
  Serial.print(" errors=");
  Serial.println(errors);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("I2C bus diagnostics");

  // Lines as seen by the master before any I2C activity.
  pinMode(SDA_PIN, INPUT);
  pinMode(SCL_PIN, INPUT);
  printLines("idle (no pull-up by the sketch)");
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, INPUT_PULLUP);
  printLines("idle (internal pull-up on)");

#if defined(ARDUINO_ARCH_ESP32)
  I2C_BUS.begin(SDA_PIN, SCL_PIN);
  I2C_BUS.setTimeOut(20);  // ms, so a stuck bus cannot trigger the watchdog
#elif defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)
  I2C_BUS.setSDA(SDA_PIN);
  I2C_BUS.setSCL(SCL_PIN);
  I2C_BUS.begin();
#else
  I2C_BUS.begin();
#endif
}

void loop() {
  // Very slow clocks (10 kHz) hang some masters such as the ESP32, so they are not tried.
  scan(400000UL);
  scan(100000UL);
  Serial.println();
  delay(3000U);
}
