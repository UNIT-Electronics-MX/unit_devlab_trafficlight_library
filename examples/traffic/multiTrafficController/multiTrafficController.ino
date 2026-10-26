/**
 * @file multiTrafficController.ino
 * @brief Coordinates 2 to 8 traffic lights modules on one bus, with serial
 *        commands to assign their addresses and pick the sequence.
 *
 * Address plan: a module leaves the factory at 0x36. "assign" gives it
 * 0x40 + index, so assigned modules never use 0x36 and you can add the next
 * module without unplugging the previous ones. Light #0 is 0x40, #1 is 0x41...
 *
 * Open Serial at 115200 baud and enter "help".
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

constexpr uint8_t BASE_ADDRESS = 0x40;
constexpr uint8_t MAX_LIGHTS = 8;
constexpr uint8_t FACTORY_ADDRESS = DevLab_TrafficLights::DEFAULT_ADDRESS;
constexpr uint16_t DEVICE_ID = DevLab_TrafficLights::DEVICE_ID;
constexpr uint8_t UNKNOWN_MASK = 0xFFU;

enum Mode : uint8_t {
  MODE_SINGLE,  // one light at a time gets green, the others stay red
  MODE_PAIRS,   // opposite lights share green (4, 6 or 8 lights)
  MODE_WAVE,    // green wave: each light starts `waveOffset` after the previous
  MODE_BLINK,   // flashing yellow
  MODE_RED      // all red
};

DevLab_I2C_Orchestrator bus(I2C_BUS, I2C_FREQ);
DevLabDDP::Master master(bus, DEVICE_ID);

DevLab_TrafficLights *lights[MAX_LIGHTS] = {nullptr};
bool online[MAX_LIGHTS];
bool warned[MAX_LIGHTS];
uint8_t shown[MAX_LIGHTS];

uint8_t lightCount = 4;
Mode mode = MODE_SINGLE;
bool running = false;
uint32_t startMs = 0;
uint32_t lastTickMs = 0;
uint32_t greenMs = 4000UL;
uint32_t yellowMs = 1500UL;
uint32_t clearMs = 0UL;      // all-red pause between phases; 0 = immediate handover
uint32_t waveOffset = 1500UL;
String inputLine;
bool busOk = false;
uint32_t lastBusRetryMs = 0;

void printHexAddress(uint8_t address) {
  Serial.print(F("0x"));
  if (address < 0x10U) Serial.print('0');
  Serial.print(address, HEX);
}

uint8_t addressOf(uint8_t index) { return BASE_ADDRESS + index; }

bool isTrafficModule(uint8_t address) {
  DevLabDDP::DeviceInfo info;
  return master.ping(address) && master.identify(address, info) &&
         info.deviceId == DEVICE_ID;
}

// ---------------------------------------------------------------- lights --

void releaseLights() {
  for (uint8_t i = 0; i < MAX_LIGHTS; ++i) {
    delete lights[i];
    lights[i] = nullptr;
  }
}

// (Re)creates one object per light and reports the ones that do not answer.
void connectLights() {
  releaseLights();
  bool allOnline = true;
  for (uint8_t i = 0; i < lightCount; ++i) {
    lights[i] = new DevLab_TrafficLights(I2C_BUS, addressOf(i), I2C_FREQ);
    online[i] = lights[i]->begin(SDA_PIN, SCL_PIN, I2C_FREQ);
    warned[i] = false;
    shown[i] = UNKNOWN_MASK;
    if (!online[i]) {
      allOnline = false;
      Serial.print(F("Light #"));
      Serial.print(i);
      Serial.print(F(" ("));
      printHexAddress(addressOf(i));
      Serial.println(F(") not found"));
    }
  }
  if (allOnline) {
    Serial.print(lightCount);
    Serial.println(F(" lights online"));
  } else {
    Serial.println(F("Assign the missing lights with: assign [index]"));
  }
}

void setAll(uint8_t mask) {
  for (uint8_t i = 0; i < lightCount; ++i) {
    if (!online[i]) continue;
    if (lights[i]->setMask(mask)) shown[i] = mask;
  }
}

// ------------------------------------------------------------- sequences --

uint8_t groupCount() {
  bool paired = mode == MODE_PAIRS && lightCount >= 4U && (lightCount % 2U) == 0U;
  return paired ? lightCount / 2U : lightCount;
}

uint8_t groupOf(uint8_t index) { return index % groupCount(); }

// Which lamps light i must show `t` ms after the sequence started.
uint8_t desiredMask(uint8_t i, uint32_t t) {
  using TL = DevLab_TrafficLights;
  switch (mode) {
    case MODE_SINGLE:
    case MODE_PAIRS: {
      uint32_t slice = greenMs + yellowMs + clearMs;
      uint32_t pos = t % (slice * groupCount());
      if (groupOf(i) != pos / slice) return TL::RED;
      uint32_t s = pos % slice;
      if (s < greenMs) return TL::GREEN;
      if (s < greenMs + yellowMs) return TL::YELLOW;
      return TL::RED;
    }
    case MODE_WAVE: {
      // Red lasts as long as green plus the clearance, so lights stay offset.
      uint32_t period = greenMs + yellowMs + greenMs + clearMs;
      uint32_t lag = ((uint32_t)i * waveOffset) % period;
      uint32_t local = (t + period - lag) % period;
      if (local < greenMs) return TL::GREEN;
      if (local < greenMs + yellowMs) return TL::YELLOW;
      return TL::RED;
    }
    case MODE_BLINK:
      return ((t / 500UL) % 2UL) ? TL::YELLOW : 0U;
    default:
      return TL::RED;
  }
}

void runSequence() {
  uint32_t now = millis();
  if ((uint32_t)(now - lastTickMs) < 10UL) return;
  lastTickMs = now;
  uint32_t t = now - startMs;

  // Two passes per tick: first the lights that stop showing green, then the
  // ones that start, so two neighbours are never green at the same time.
  for (uint8_t pass = 0; pass < 2U; ++pass) {
    for (uint8_t i = 0; i < lightCount; ++i) {
      if (!online[i]) continue;
      uint8_t wanted = desiredMask(i, t);
      bool turnsGreen = (wanted & DevLab_TrafficLights::GREEN) != 0U;
      if (turnsGreen != (pass == 1U)) continue;
      if (wanted == shown[i]) continue;  // only talk to a module when it changes
      if (lights[i]->setMask(wanted)) {
        shown[i] = wanted;
        warned[i] = false;
      } else if (!warned[i]) {
        warned[i] = true;
        Serial.print(F("ERROR: light #"));
        Serial.print(i);
        Serial.println(F(" did not answer"));
      }
    }
  }
}

void startSequence() {
  startMs = millis();
  lastTickMs = 0;
  running = true;
}

const char *modeName(Mode m) {
  switch (m) {
    case MODE_SINGLE: return "single";
    case MODE_PAIRS: return "pairs";
    case MODE_WAVE: return "wave";
    case MODE_BLINK: return "blink";
    default: return "red";
  }
}

// ----------------------------------------------------------- provisioning --

void scanBus() {
  bool found = false;
  Serial.println(F("Address  Device"));
  for (uint8_t address = 0x08U; address <= 0x77U; ++address) {
    if (!master.ping(address)) continue;
    found = true;
    printHexAddress(address);
    Serial.print(F("     "));
    DevLabDDP::DeviceInfo info;
    if (!master.identify(address, info)) {
      Serial.println(F("non-DDP"));
    } else if (info.deviceId != DEVICE_ID) {
      Serial.println(DevLabDDP::deviceName(info.deviceId));
    } else if (address == FACTORY_ADDRESS) {
      Serial.println(F("traffic-lights, factory address (not assigned)"));
    } else if (address >= BASE_ADDRESS && address < BASE_ADDRESS + MAX_LIGHTS) {
      Serial.print(F("traffic-lights, light #"));
      Serial.println(address - BASE_ADDRESS);
    } else {
      Serial.println(F("traffic-lights, custom address"));
    }
  }
  if (!found) Serial.println(F("--       none"));
}

// Gives the module at `from` the address of light `index`.
void moveModule(uint8_t from, uint8_t index) {
  if (index >= MAX_LIGHTS) {
    Serial.print(F("ERROR index must be 0 to "));
    Serial.println(MAX_LIGHTS - 1);
    return;
  }
  if (!isTrafficModule(from)) {
    Serial.print(F("ERROR no traffic lights module at "));
    printHexAddress(from);
    Serial.println();
    return;
  }
  if (master.ping(addressOf(index))) {
    Serial.print(F("ERROR light #"));
    Serial.print(index);
    Serial.println(F(" is already assigned"));
    return;
  }
  DevLab_TrafficLights module(I2C_BUS, from, I2C_FREQ);
  if (!module.begin(SDA_PIN, SCL_PIN, I2C_FREQ) || !module.setAddress(addressOf(index))) {
    Serial.println(F("ERROR address change failed"));
    return;
  }
  Serial.print(F("OK light #"));
  Serial.print(index);
  Serial.print(F(" is now "));
  printHexAddress(addressOf(index));
  Serial.println(F(" (kept after power loss)"));
}

// Assigns the module at the factory address to `index`, or to the first free one.
void assignModule(int index) {
  if (!isTrafficModule(FACTORY_ADDRESS)) {
    Serial.println(F("ERROR no unassigned module at 0x36; connect the new module first"));
    return;
  }
  if (index < 0) {
    for (uint8_t i = 0; i < MAX_LIGHTS; ++i) {
      if (!master.ping(addressOf(i))) { index = i; break; }
    }
    if (index < 0) {
      Serial.println(F("ERROR all addresses are in use"));
      return;
    }
  }
  moveModule(FACTORY_ADDRESS, (uint8_t)index);
}

// Returns light `index` to the factory address so it can be reassigned.
void factoryModule(uint8_t index) {
  if (index >= MAX_LIGHTS || !isTrafficModule(addressOf(index))) {
    Serial.println(F("ERROR that light is not connected"));
    return;
  }
  if (master.ping(FACTORY_ADDRESS)) {
    Serial.println(F("ERROR 0x36 is in use; assign the unassigned module first"));
    return;
  }
  DevLab_TrafficLights module(I2C_BUS, addressOf(index), I2C_FREQ);
  if (!module.begin(SDA_PIN, SCL_PIN, I2C_FREQ) || !module.resetFactoryAddress()) {
    Serial.println(F("ERROR factory reset failed"));
    return;
  }
  Serial.println(F("OK module is back at 0x36"));
}

// Flashes all three lamps of one light so you can see which one it is.
void identifyLight(uint8_t index) {
  if (index >= lightCount || !online[index]) {
    Serial.println(F("ERROR that light is not online"));
    return;
  }
  for (uint8_t n = 0; n < 4U; ++n) {
    lights[index]->setMask(DevLab_TrafficLights::ALL);
    delay(250U);
    lights[index]->setMask(0U);
    delay(250U);
  }
  shown[index] = UNKNOWN_MASK;
}

// -------------------------------------------------------------- commands --

void printHelp() {
  Serial.println(F("Setup (one new module at a time):"));
  Serial.println(F("  scan                    list modules on the bus"));
  Serial.println(F("  assign [n]              give the module at 0x36 the address of light n (default: first free)"));
  Serial.println(F("  move <from> <to>        change light <from> to index <to>"));
  Serial.println(F("  factory <n>             return light n to 0x36"));
  Serial.println(F("  id <n>                  flash light n to find it"));
  Serial.println(F("Sequence:"));
  Serial.println(F("  count <2-8>             number of lights to coordinate"));
  Serial.println(F("  mode single|pairs|wave|blink|red"));
  Serial.println(F("  start | stop            stop = all red"));
  Serial.println(F("  time <green> <yellow> <clear>   ms; clear = all-red pause, 0 = instant handover"));
  Serial.println(F("  offset <ms>             wave delay between lights"));
  Serial.println(F("  status"));
}

void printStatus() {
  Serial.print(F("mode="));
  Serial.print(modeName(mode));
  Serial.print(running ? " running" : " stopped");
  Serial.print(F(" lights="));
  Serial.println(lightCount);
  Serial.print(F("green="));
  Serial.print(greenMs);
  Serial.print(F("ms yellow="));
  Serial.print(yellowMs);
  Serial.print(F("ms clear="));
  Serial.print(clearMs);
  Serial.print(F("ms offset="));
  Serial.print(waveOffset);
  Serial.println(F("ms"));
  for (uint8_t i = 0; i < lightCount; ++i) {
    Serial.print(F("  #"));
    Serial.print(i);
    Serial.print(F(" "));
    printHexAddress(addressOf(i));
    Serial.print(online[i] ? " online" : " MISSING");
    if (online[i] && shown[i] != UNKNOWN_MASK) {
      Serial.print(F(" mask=0x"));
      Serial.print(shown[i], HEX);
    }
    Serial.println();
  }
  if (mode == MODE_PAIRS && (lightCount < 4U || (lightCount % 2U) != 0U)) {
    Serial.println(F("note: pairs needs 4, 6 or 8 lights; behaving like single"));
  }
}

// Splits "word a b c" into up to four words; returns how many were found.
uint8_t splitWords(String line, String words[4]) {
  uint8_t count = 0;
  line.trim();
  while (line.length() > 0U && count < 4U) {
    int space = line.indexOf(' ');
    if (space < 0) {
      words[count++] = line;
      break;
    }
    words[count++] = line.substring(0, space);
    line = line.substring(space + 1);
    line.trim();
  }
  return count;
}

bool parseNumber(const String &text, long minimum, long maximum, long &value) {
  char *end = nullptr;
  value = strtol(text.c_str(), &end, 10);
  return end != text.c_str() && *end == '\0' && value >= minimum && value <= maximum;
}

void processCommand(const String &line) {
  String w[4];
  uint8_t n = splitWords(line, w);
  if (n == 0U) return;
  w[0].toLowerCase();
  long a = 0, b = 0, c = 0;

  if (w[0] == "help") {
    printHelp();
  } else if (w[0] == "scan") {
    scanBus();
  } else if (w[0] == "assign") {
    if (n == 1U) assignModule(-1);
    else if (parseNumber(w[1], 0, MAX_LIGHTS - 1, a)) assignModule((int)a);
    else Serial.println(F("ERROR index must be a valid light number"));
    connectLights();
  } else if (w[0] == "move" && n == 3U && parseNumber(w[1], 0, MAX_LIGHTS - 1, a) &&
             parseNumber(w[2], 0, MAX_LIGHTS - 1, b)) {
    moveModule(addressOf((uint8_t)a), (uint8_t)b);
    connectLights();
  } else if (w[0] == "factory" && n == 2U && parseNumber(w[1], 0, MAX_LIGHTS - 1, a)) {
    factoryModule((uint8_t)a);
    connectLights();
  } else if (w[0] == "id" && n == 2U && parseNumber(w[1], 0, MAX_LIGHTS - 1, a)) {
    identifyLight((uint8_t)a);
  } else if (w[0] == "count" && n == 2U && parseNumber(w[1], 2, MAX_LIGHTS, a)) {
    lightCount = (uint8_t)a;
    running = false;
    connectLights();
  } else if (w[0] == "mode" && n == 2U) {
    w[1].toLowerCase();
    if (w[1] == "single") mode = MODE_SINGLE;
    else if (w[1] == "pairs") mode = MODE_PAIRS;
    else if (w[1] == "wave") mode = MODE_WAVE;
    else if (w[1] == "blink") mode = MODE_BLINK;
    else if (w[1] == "red") mode = MODE_RED;
    else { Serial.println(F("ERROR mode must be single, pairs, wave, blink or red")); return; }
    if (running) startSequence();
    printStatus();
  } else if (w[0] == "start") {
    startSequence();
    printStatus();
  } else if (w[0] == "stop") {
    running = false;
    setAll(DevLab_TrafficLights::RED);
    Serial.println(F("stopped, all red"));
  } else if (w[0] == "time" && n == 4U && parseNumber(w[1], 200, 60000, a) &&
             parseNumber(w[2], 200, 10000, b) && parseNumber(w[3], 0, 10000, c)) {
    greenMs = a;
    yellowMs = b;
    clearMs = c;
    if (running) startSequence();
    printStatus();
  } else if (w[0] == "offset" && n == 2U && parseNumber(w[1], 0, 60000, a)) {
    waveOffset = a;
    if (running) startSequence();
    printStatus();
  } else if (w[0] == "status") {
    printStatus();
  } else {
    Serial.println(F("ERROR invalid command"));
    printHelp();
  }
}

// beginRecovered() fails, without starting Wire, when SDA or SCL stays low.
bool startBus() {
  busOk = bus.beginRecovered(SDA_PIN, SCL_PIN);
  if (!busOk) {
    Serial.print(F("ERROR: I2C bus stuck low (SDA="));
    Serial.print(digitalRead(SDA_PIN) ? F("high") : F("LOW"));
    Serial.print(F(", SCL="));
    Serial.print(digitalRead(SCL_PIN) ? F("high") : F("LOW"));
    Serial.println(F("). Check wiring, power and pull-ups; disconnect a module."));
  }
  return busOk;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("Multi traffic lights controller"));
  printHelp();
  if (!startBus()) return;
  scanBus();
  connectLights();
}

void loop() {
  if (!busOk) {
    // Keep retrying so the bus recovers once the fault is fixed.
    if ((uint32_t)(millis() - lastBusRetryMs) >= 2000UL) {
      lastBusRetryMs = millis();
      if (startBus()) {
        Serial.println(F("I2C bus recovered"));
        scanBus();
        connectLights();
      }
    }
    return;
  }
  while (Serial.available()) {
    char character = (char)Serial.read();
    if (character == '\r' || character == '\n') {
      if (inputLine.length() > 0U) {
        processCommand(inputLine);
        inputLine = "";
      }
    } else if (inputLine.length() < 48U) {
      inputLine += character;
    } else {
      inputLine = "";
      Serial.println(F("ERROR command is too long"));
    }
  }
  if (running) runSequence();
}
