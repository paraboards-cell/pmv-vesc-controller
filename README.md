# PMV VESC Controller

ESP-IDF firmware for an ESP32 that bridges a Nintendo Joy-Con 2 controller to a dual-VESC motor drive system for a four-wheel personal mobility vehicle (PMV) with differential/tank steering.

## Vehicle summary

- **Drive**: two powered front wheels, two passive rear wheels
- **Steering**: differential speed (tank/skid steer) — no turning wheels
- **Tight turns**: castor mechanism lifts rear wheels at low speed + high steer input
- **Motors**: hall-sensor BLDC, FOC mode via dual VESC (master UART + slave CAN)
- **Controller**: Nintendo Joy-Con 2 over Bluetooth Classic HID

## Hardware requirements

| Component | Notes |
|-----------|-------|
| ESP32 (original) | Must be original ESP32 — S3/C3/C6 lack Bluetooth Classic |
| Dual VESC (6.x or VESC 75/100) | Master–slave CAN wired |
| Joy-Con 2 | Left or Right; uses standard Joy-Con HID protocol |
| Castor actuator | Solenoid or servo on GPIO 18 (active-high) |

## Project structure

```
main/
  main.c              — app_main, event loop, watchdog, telemetry task
  vehicle_config.h    — all tunable constants (pins, limits, speed modes)
components/
  joycon/             — Bluetooth Classic HID host + Joy-Con handshake + parser
  vesc/               — VESC UART packet protocol (encode/decode/CRC16)
  drive/              — tank steering mix, state machine, speed modes
  castor/             — castor GPIO control
vesc-config/
  README.md           — VESC Tool setup procedure
  motor_left.xml      — (add after motor detection)
  motor_right.xml     — (add after motor detection)
docs/
  wiring.md           — pin connections and wiring notes
```

## Quick start

### 1. Prerequisites

```bash
# Install ESP-IDF v5.2 or later
# https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/

. $IDF_PATH/export.sh
```

### 2. Configure

Edit `main/vehicle_config.h` for your hardware:
- UART pins, baud rate
- Motor current limits (match VESC detection results)
- Motor direction inversion flags
- CAN slave ID

### 3. Build and flash

```bash
cd pmv-vesc-controller
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 4. Pair Joy-Con

Hold the **sync button** on the Joy-Con (small button on the rail edge) until the LEDs cycle. The ESP32 scans for any Bluetooth device advertising "Joy-Con" in its name and connects automatically. After ~300 ms the handshake sequence switches the device into full 0x30 report mode.

## Controls

| Input | Action |
|-------|--------|
| Right stick Y | Throttle (push forward = move forward) |
| Right stick X | Steer (differential mix left/right) |
| ZR (held) | Dead-man switch — release to coast |
| ZL (held) + throttle | Reverse |
| Minus button | Toggle parking brake |
| Plus button | Cycle speed mode (Walk → Cruise → Sport) |

## Speed modes

| Mode | Current fraction | Approx top speed |
|------|-----------------|-----------------|
| Walk | 25% | ~3 km/h |
| Cruise | 60% | ~15 km/h |
| Sport | 100% | motor limited |

## Safety

- **Watchdog**: 500 ms Joy-Con timeout → E-stop (controlled decel to zero current)
- **VESC fault polling**: any VESC fault code → E-stop
- **Dead-man switch**: ZR must be held to apply throttle current
- **Parking brake**: COMM_SET_HANDBRAKE holds position with holding current

## References

- [dekuNukem/Nintendo_Switch_Reverse_Engineering](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering) — Joy-Con protocol
- [vedderb/vesc_express](https://github.com/vedderb/vesc_express) — official VESC ESP-IDF reference
- [ESP-IDF esp_hid_host example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/esp_hid_host)
- [VESC Project](https://vesc-project.com)
