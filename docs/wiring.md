# Wiring Reference

## ESP32 → Master VESC (UART)

```
ESP32 GPIO 17 (TX) ──────── VESC UART RX
ESP32 GPIO 16 (RX) ──────── VESC UART TX
ESP32 GND          ──────── VESC GND  (shared ground — critical)
```

> **Do not connect ESP32 3.3V to VESC 5V.** The VESC UART pins are 3.3V-tolerant on most hardware versions. Verify yours.

## Master VESC ↔ Slave VESC (CAN bus)

```
Master VESC CANH ─────────── Slave VESC CANH
Master VESC CANL ─────────── Slave VESC CANL
```

Use twisted-pair wire. For runs > ~30 cm add 120Ω termination resistors at each end of the bus.

## Castor actuator (GPIO 18)

```
ESP32 GPIO 18 ── [MOSFET gate] ── actuator solenoid/motor
                              └── flyback diode if inductive load
ESP32 GND     ── MOSFET source ── actuator GND
```

GPIO 18 is active-high (HIGH = castor arm down = rear wheels lift).
Adjust `CASTOR_GPIO_PIN` in `vehicle_config.h` if you use a different pin.

## Power

- ESP32: powered from 3.3V LDO off VESC 5V rail, or independent USB/LiPo
- Do not share power rail between ESP32 logic and motor drive without proper decoupling
