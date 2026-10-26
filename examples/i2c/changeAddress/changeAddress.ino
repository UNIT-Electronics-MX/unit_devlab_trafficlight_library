/**
 * @file changeAddress.ino
 * @brief Scans the bus and changes or restores the I2C address of a traffic lights module.
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

constexpr uint16_t EXPECTED_DEVICE_ID = DevLab_TrafficLights::DEVICE_ID;
DevLab_I2C_Orchestrator bus(I2C_BUS, I2C_FREQ);
DevLabDDP::Master master(bus, EXPECTED_DEVICE_ID);
String inputLine;

bool parseAddress(const String &text, uint8_t &address) {
  char *end = nullptr;
  long value = strtol(text.c_str(), &end, 16);
  if (end == text.c_str() || *end != '\0' || value < 0x08L || value > 0x77L) {
    return false;
  }
  address = (uint8_t)value;
  return true;
}

void printHexAddress(uint8_t address) {
  Serial.print("0x");
  if (address < 0x10U) Serial.print('0');
  Serial.print(address, HEX);
}

void scanBus() {
  bool found = false;
  Serial.println("Address  Device");
  for (uint8_t address = 0x08U; address <= 0x77U; ++address) {
    if (!master.ping(address)) continue;
    found = true;
    printHexAddress(address);
    Serial.print("     ");
    DevLabDDP::DeviceInfo info;
    if (!master.identify(address, info)) {
      Serial.println("non-DDP");
    } else if (info.deviceId == EXPECTED_DEVICE_ID) {
      Serial.println("traffic-lights (UE0183)");
    } else {
      Serial.println(DevLabDDP::deviceName(info.deviceId));
    }
  }
  if (!found) Serial.println("--       none");
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  scan");
  Serial.println("  change <current_hex> <new_hex>");
  Serial.println("  factory                 restore the factory address 0x36");
  Serial.println("  factory <current_hex>   same, when several modules are connected");
  Serial.println("Examples:");
  Serial.println("  change 36 37");
  Serial.println("  factory");
  Serial.println("Connect one module at a time while they share an address.");
}

// Checks that `address` holds a traffic lights module and is not the target.
bool isTrafficModule(uint8_t address) {
  DevLabDDP::DeviceInfo info;
  return master.identify(address, info) && info.deviceId == EXPECTED_DEVICE_ID;
}

void changeAddress(uint8_t oldAddress, uint8_t newAddress) {
  if (!isTrafficModule(oldAddress)) {
    Serial.println("ERROR current address does not contain a traffic lights module");
    return;
  }
  if (master.ping(newAddress)) {
    Serial.println("ERROR new address is already in use");
    return;
  }
  Serial.print("Changing ");
  printHexAddress(oldAddress);
  Serial.print(" -> ");
  printHexAddress(newAddress);
  Serial.println("...");

  DevLab_TrafficLights lights(I2C_BUS, oldAddress, I2C_FREQ);
  if (!lights.begin() || !lights.setAddress(newAddress)) {
    Serial.println("ERROR address change failed");
    return;
  }
  Serial.print("OK module is now at ");
  printHexAddress(lights.address());
  Serial.println(" (kept after power loss)");
  scanBus();
}

void factoryReset(uint8_t address) {
  if (!isTrafficModule(address)) {
    Serial.println("ERROR current address does not contain a traffic lights module");
    return;
  }
  if (address != DevLab_TrafficLights::DEFAULT_ADDRESS &&
      master.ping(DevLab_TrafficLights::DEFAULT_ADDRESS)) {
    Serial.println("ERROR factory address 0x36 is already in use");
    return;
  }
  DevLab_TrafficLights lights(I2C_BUS, address, I2C_FREQ);
  if (!lights.begin() || !lights.resetFactoryAddress()) {
    Serial.println("ERROR factory reset failed");
    return;
  }
  Serial.print("OK module is back at ");
  printHexAddress(lights.address());
  Serial.println();
  scanBus();
}

// Finds the only traffic lights module on the bus. Returns false (and says
// why) when there are none or several; then "factory <address>" is needed.
bool findSingleModule(uint8_t &address) {
  uint8_t count = 0U;
  for (uint8_t candidate = 0x08U; candidate <= 0x77U; ++candidate) {
    if (!master.ping(candidate) || !isTrafficModule(candidate)) continue;
    if (++count == 1U) address = candidate;
  }
  if (count == 0U) {
    Serial.println("ERROR no traffic lights module found");
  } else if (count > 1U) {
    Serial.println("ERROR several modules found; use: factory <current_hex>");
  }
  return count == 1U;
}

void processCommand(String line) {
  line.trim();
  line.toLowerCase();

  if (line == "scan") {
    scanBus();
    return;
  }

  if (line == "factory") {
    uint8_t address;
    if (findSingleModule(address)) factoryReset(address);
    return;
  }

  if (line.startsWith("factory ")) {
    String text = line.substring(8);
    text.trim();
    uint8_t address;
    if (!parseAddress(text, address)) {
      Serial.println("ERROR address must be hexadecimal from 08 to 77");
      return;
    }
    factoryReset(address);
    return;
  }

  int firstSpace = line.indexOf(' ');
  int secondSpace = firstSpace < 0 ? -1 : line.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0 ||
      line.substring(0, firstSpace) != "change") {
    Serial.println("ERROR invalid command");
    printHelp();
    return;
  }

  String oldText = line.substring(firstSpace + 1, secondSpace);
  String newText = line.substring(secondSpace + 1);
  oldText.trim();
  newText.trim();

  uint8_t oldAddress, newAddress;
  if (!parseAddress(oldText, oldAddress) ||
      !parseAddress(newText, newAddress) ||
      oldAddress == newAddress) {
    Serial.println("ERROR addresses must be different hexadecimal values from 08 to 77");
    return;
  }
  changeAddress(oldAddress, newAddress);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  bus.beginRecovered(SDA_PIN, SCL_PIN);

  Serial.print("Expected DDP device ID: 0x");
  Serial.println(EXPECTED_DEVICE_ID, HEX);
  printHelp();
  scanBus();
}

void loop() {
  while (Serial.available()) {
    char character = (char)Serial.read();
    if (character == '\r' || character == '\n') {
      if (inputLine.length() > 0U) {
        processCommand(inputLine);
        inputLine = "";
      }
    } else if (inputLine.length() < 64U) {
      inputLine += character;
    } else {
      inputLine = "";
      Serial.println("ERROR command is too long");
    }
  }
}
