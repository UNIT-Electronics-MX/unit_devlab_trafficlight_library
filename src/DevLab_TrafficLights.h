#ifndef DEVLAB_TRAFFICLIGHTS_H
#define DEVLAB_TRAFFICLIGHTS_H

#pragma once

#include "DevLabDDP.h"
#include "DevLab_I2C_Orchestrator.h"

class DevLab_TrafficLights
{
public:
    /* DDP Device ID of the UE0183 traffic lights module (DDP_DEVICE_ID in firmware). */
    static constexpr uint16_t DEVICE_ID = 0x0116U;

    /* Factory I2C address of the module (STARTUP_I2C_ADDRESS). The address can
     * be changed with setAddress() and is kept after power loss. */
    static constexpr uint8_t DEFAULT_ADDRESS = 0x36U;

    /* Logical light masks (1 = lit). Green is PA2, yellow PA1 and red PA0. */
    static constexpr uint8_t GREEN = 1U << 0;
    static constexpr uint8_t YELLOW = 1U << 1;
    static constexpr uint8_t RED = 1U << 2;
    static constexpr uint8_t ALL = GREEN | YELLOW | RED;

    explicit DevLab_TrafficLights(TwoWire &wire = Wire, uint8_t address = DEFAULT_ADDRESS, uint32_t clock = 100000UL)
    : _bus(wire, clock), _ddp(_bus, DEVICE_ID), _address(address), _clock(clock) {}

    bool begin();
    bool begin(uint8_t sdaPin, uint8_t sclPin, uint32_t clock = 100000UL);
    bool beginRecovered(uint8_t sdaPin, uint8_t sclPin, uint32_t timeoutUs = 20000, bool restart = false);

    /* Sets all three lights at once (command 0xA8 | mask) and waits for the
     * DDP status. Bits above ALL are ignored. Returns false on an I2C error
     * or a non-zero status; the cached mask() is only updated on success. */
    bool setMask(uint8_t mask);

    /* Reads the current light mask from the module (command 0x80). */
    bool readMask(uint8_t &mask);

    /* Turns one or more lights on or off, keeping the others unchanged. */
    bool setLight(uint8_t lights, bool on);

    /* Reads whether every light in `light` is lit (a single light mask). */
    bool readLight(uint8_t light, bool &on);

    /* Shows only one light, turning the other two off. */
    bool red() { return setMask(RED); }
    bool yellow() { return setMask(YELLOW); }
    bool green() { return setMask(GREEN); }
    bool allOn() { return setMask(ALL); }
    bool allOff() { return setMask(0U); }

    /* I2C address management (firmware 1.2 or later). The new address is
     * stored in the module's Flash and the module restarts (lights off).
     * setAddress() also retargets this object to the new address; both calls
     * return false if the module does not answer as expected. Connect one
     * module at a time while they share the same address. */
    bool setAddress(uint8_t newAddress);
    bool resetFactoryAddress();

    /* Address the module itself reports (command 0x20). */
    bool readAddress(uint8_t &address);

    /* `fromFlash` is true when the address was stored by the user, false when
     * the module runs on its factory address (command 0x24). */
    bool readAddressSource(bool &fromFlash);

    /* Last mask confirmed by the module (updated by setMask/readMask). */
    uint8_t mask() const { return _mask; }

    bool busReady() const { return _busReady; }
    bool isConnected() const { return _verified; }
    uint8_t address() const { return _address; }
    const DevLabDDP::DeviceInfo &deviceInfo() const { return _info; }
    void printInfo(Print &out = Serial) const;

    DevLabDDP::Master &protocol() { return _ddp; }
    DevLab_I2C_Orchestrator &bus() { return _bus; }

private:
    static constexpr uint8_t CMD_READ_MASK = 0x80U;
    static constexpr uint8_t CMD_SET_MASK_BASE = 0xA8U;

    bool verify();

    DevLab_I2C_Orchestrator _bus;
    DevLabDDP::Master _ddp;
    uint8_t _address;
    uint32_t _clock;
    bool _busReady = false;
    bool _verified = false;
    uint8_t _mask = 0U;
    DevLabDDP::DeviceInfo _info;
};

#endif
