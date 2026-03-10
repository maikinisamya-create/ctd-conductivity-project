# CTD Conductivity Measurement Project

This repository contains the development of a conductivity measurement system
based on the CN0349 analog front-end and an ESP32 microcontroller.

## Hardware
- AD5934 (Impedance converter)
- ADG715 (Analog multiplexer)
- ESP32
- Conductivity probe (CTD cell)

## Current tests
- I2C scan to detect CN0349 devices

Expected I2C addresses:
- AD5934 : 0x0D
- ADG715 : 0x48
