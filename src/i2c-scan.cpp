//
// SPDX-License-Identifier: MIT
// --------------------------------------
// i2c_scanner
//
// Modified from https://playground.arduino.cc/Main/I2cScanner/
// --------------------------------------

#include <Arduino.h>
#include <Wire.h>
#include "i2c-scan.h"

// Set I2C bus to use: Wire, Wire1, etc.
#define WIRE Wire

void scan_i2c(Stream & ioStream)
{
    uint8_t error, address;
    int nDevices;

    ioStream.printf("Scanning...\n");
    WIRE.begin();

    nDevices = 0;
    for (address = 1; address < 127; address++)
    {
        // The i2c_scanner uses the return value of
        // the Write.endTransmisstion to see if
        // a device did acknowledge to the address.
        WIRE.beginTransmission(address);
        error = WIRE.endTransmission();

        if (error == 0)
        {
            ioStream.printf("I2C device found at address 0x");
            if (address < 16)
                ioStream.printf("0");
            ioStream.printf("%02x", address);
            ioStream.printf("  !\n");

            nDevices++;
        }
        else if (error == 4)
        {
            ioStream.printf("Unknown error at address 0x");
            if (address < 16)
                ioStream.printf("0");
            ioStream.printf("%02x\n", address);
        }
    }
    if (nDevices == 0)
        ioStream.printf("No I2C devices found\n");
    else
        ioStream.printf("done\n");
}
