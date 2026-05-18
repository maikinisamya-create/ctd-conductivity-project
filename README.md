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


## Algorithm: Adaptive Frequency Sweep

The main firmware implements a 6-step adaptive measurement cycle:

1. **Coarse calibration** – 7 log-spaced points from 3 kHz to 80 kHz using Rcal
2. **Coarse sweep** – measures the unknown solution to estimate conductivity range
3. **f_opt estimation** – empirical model: `f_opt = 0.130 × σ^1.473`
4. **Fine calibration** – 11 points centered around f_opt (±10 kHz)
5. **Fine sweep** – precise impedance measurement at optimal frequency
6. **Final result** – σ(T), σ(25°C) and estimated salinity output

## Output

- Conductivity σ(T) in mS/cm
- Temperature-compensated conductivity σ(25°C) — ref temperature 25°C, α = 2%/°C
- Estimated salinity in g/kg (PSS-78 approximation)

## Dependencies

- Arduino IDE with ESP32 board support
- Wire.h (I2C)
- math.h
