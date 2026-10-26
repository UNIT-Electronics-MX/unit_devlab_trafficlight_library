#include "DevLab_TrafficLights.h"

bool DevLab_TrafficLights::verify() {
    return (_verified = _ddp.matchesExpectedDevice(_address, &_info));
}

bool DevLab_TrafficLights::begin() {
    _verified = false;
    _bus.setClock(_clock);
    _busReady = _bus.begin();
    if (!_busReady) return false;
    return verify();
}

bool DevLab_TrafficLights::begin(uint8_t sdaPin, uint8_t sclPin, uint32_t clock) {
    _verified = false;
    _clock = clock;
    _bus.setClock(_clock);
    _busReady = _bus.begin(sdaPin, sclPin);
    if (!_busReady) return false;
    return verify();
}

bool DevLab_TrafficLights::beginRecovered(uint8_t sdaPin, uint8_t sclPin, uint32_t timeoutUs, bool restart) {
    _verified = false;
    _bus.setClock(_clock);
    _busReady = _bus.beginRecovered(sdaPin, sclPin, timeoutUs, restart);
    if (!_busReady) return false;
    return verify();
}

void DevLab_TrafficLights::printInfo(Print &out) const {
    // DevLabDDP::deviceName() may not know 0x0116 yet, so print the identity here.
    out.print("addr=0x"); if (_address < 0x10U) out.print('0'); out.print(_address, HEX);
    out.print(" id=0x0"); out.print(_info.deviceId, HEX);
    out.print(" type=traffic-lights");
    out.print(" protocol="); out.print(_info.protocolMajor); out.print('.'); out.print(_info.protocolMinor);
    out.print(" firmware="); out.print(_info.firmwareMajor); out.print('.'); out.print(_info.firmwareMinor);
    out.print(" hardware="); out.print(_info.hardwareMajor); out.print('.'); out.print(_info.hardwareMinor);
    out.print(" caps=0x"); out.print(_info.capabilities, HEX);
    out.print(" match=");
    out.println(_info.deviceId == DEVICE_ID ? "yes" : "NO");
}

bool DevLab_TrafficLights::setMask(uint8_t mask) {
    if (!_verified) return false;
    mask &= ALL;
    uint8_t status = 0xFFU;
    if (!_ddp.readCommand(_address, CMD_SET_MASK_BASE | mask, &status, 1U, 2U) ||
        status != 0U) return false;
    _mask = mask;
    return true;
}

bool DevLab_TrafficLights::readMask(uint8_t &mask) {
    if (!_verified) return false;
    uint8_t raw = 0U;
    if (!_ddp.readCommand(_address, CMD_READ_MASK, &raw, 1U, 2U) ||
        (raw & ~ALL) != 0U) return false;
    _mask = mask = raw;
    return true;
}

bool DevLab_TrafficLights::setLight(uint8_t lights, bool on) {
    lights &= ALL;
    // Resync with the module first so the other lights are not overwritten
    // with a stale cached value.
    uint8_t current = 0U;
    if (!readMask(current)) return false;
    return setMask(on ? (current | lights) : (current & ~lights));
}

bool DevLab_TrafficLights::readLight(uint8_t light, bool &on) {
    uint8_t current = 0U;
    if (!readMask(current)) return false;
    on = (current & light & ALL) == (light & ALL) && (light & ALL) != 0U;
    return true;
}

bool DevLab_TrafficLights::setAddress(uint8_t newAddress) {
    if (!_verified || newAddress == _address) return false;
    // setI2cAddress() rejects 0x08..0x77 violations and busy new addresses,
    // waits for the module to restart and checks it answers on the new one.
    if (!_ddp.setI2cAddress(_address, newAddress)) return false;
    _address = newAddress;
    _mask = 0U;  // the module restarts with all lights off
    return verify();
}

bool DevLab_TrafficLights::resetFactoryAddress() {
    if (!_verified) return false;
    uint8_t response = 0xFFU;
    if (!_ddp.readCommand(_address, CMD_RESET_FACTORY, &response, 1U, 10U) ||
        (response & 0x0FU) != RESP_FACTORY_RESET) return false;
    delay(300U);  // the module erases its Flash record and restarts
    _address = DEFAULT_ADDRESS;
    _mask = 0U;
    return verify();
}

bool DevLab_TrafficLights::readAddress(uint8_t &address) {
    if (!_verified) return false;
    return _ddp.getI2cAddress(_address, address);
}

bool DevLab_TrafficLights::readAddressSource(bool &fromFlash) {
    if (!_verified) return false;
    uint8_t raw = 0xFFU;
    if (!_ddp.readCommand(_address, CMD_GET_I2C_STATUS, &raw, 1U, 2U) || raw > 1U) return false;
    fromFlash = raw == 1U;
    return true;
}
