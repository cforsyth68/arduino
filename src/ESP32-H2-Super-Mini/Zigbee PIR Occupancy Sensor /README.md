# Zigbee PIR Occupancy Sensor

A Zigbee occupancy sensor built with an **ESP32-H2**, an **AM312 mini PIR motion sensor**, and an **SSD1306 OLED display**.

## Wiring

### AM312 PIR Sensor

| AM312 Pin | ESP32-H2 Connection |
| --------- | ------------------- |
| `VCC`     | `3.3V`              |
| `OUT`     | `GPIO13`            |
| `GND`     | `GND`               |

### SSD1306 OLED Display

| OLED Pin | ESP32-H2 Connection |
| -------- | ------------------- |
| `VCC`    | `3.3V`              |
| `GND`    | `GND`               |
| `SDA`    | `GPIO10`            |
| `SCL`    | `GPIO11`            |

### External Button

| Button Connection | ESP32-H2 Connection |
| ----------------- | ------------------- |
| One side          | `GPIO12`            |
| Other side        | `GND`               |

The button uses the ESP32-H2’s internal pull-up resistor, so no external resistor is required.

## Button Behavior

### Brief Press

A brief button press turns on the OLED display for **10 seconds**.

### Three-Second Hold

Holding the button for **3 seconds**:

1. Factory-resets the Zigbee network settings.
2. Restarts the ESP32-H2.
3. Places the device in a state where it can join a new Zigbee network.

## Display Behavior

The OLED display operates as follows:

1. It remains on while the device is waiting to join a Zigbee network.
2. It displays instructions for connecting the device.
3. It remains on during the AM312 sensor warm-up period.
4. It turns off automatically when the warm-up period finishes.
5. After startup, a brief button press wakes the display for **10 seconds**.
