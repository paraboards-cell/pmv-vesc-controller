# VESC Configuration

Place your exported VESC Tool `.xml` configuration files here after completing motor detection.

## Required files

| File | Description |
|------|-------------|
| `motor_left.xml` | Master VESC — left front motor (UART-connected to ESP32) |
| `motor_right.xml` | Slave VESC — right front motor (CAN-connected to master) |

## Setup procedure

### 1. Motor detection (do this before any other configuration)

For each VESC independently (disconnect CAN bus first):

1. Open VESC Tool → Connect via USB
2. **Motor Settings → FOC → Motor Parameters → Detect FOC Parameters**
3. With hall sensors: also run **Hall Sensor Detection** (Rotor Position tab)
4. Verify the detected values look sane — resistance 10–500 mΩ, inductance 1–100 µH
5. File → Export XML → save as `motor_left.xml` / `motor_right.xml`

### 2. Input configuration

Set each VESC input to **UART** (not PPM, ADC, or NRF):

- App Settings → General → App to use: **UART**
- App Settings → UART → Baud rate: **115200**

Master VESC only needs UART enabled. The slave only needs CAN forwarding enabled.

### 3. CAN bus setup

- Master: App Settings → General → CAN Status Messages Mode: **Disabled** (or Status 1 for telemetry)
- Slave: App Settings → General → UAVCAN ID (CAN controller ID): **1**  ← matches `VESC_SLAVE_CAN_ID` in `vehicle_config.h`
- Both: CAN baud rate **500 kbit/s**

### 4. Current limits

Edit `main/vehicle_config.h` to match your motor's rated continuous current:

```c
#define MOTOR_MAX_CURRENT_A     40.0f   // ← set to your motor's rated current
#define MOTOR_BRAKE_CURRENT_A   20.0f
#define MOTOR_HANDBRAKE_CURRENT_A 15.0f
```

### 5. Verify before connecting ESP32

With VESC Tool connected via USB, use the real-time data view to confirm:
- Motors spin in the correct direction with positive duty
- Hall sensors report cleanly (no erratic position values)
- No faults at idle

Flip `MOTOR_LEFT_INVERT` / `MOTOR_RIGHT_INVERT` in `vehicle_config.h` rather than reversing motor wires.
