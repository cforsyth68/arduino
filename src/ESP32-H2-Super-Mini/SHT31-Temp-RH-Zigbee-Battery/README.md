# ESP32-H2 Zigbee Temperature & Humidity Sensor

A battery-powered Zigbee environmental sensor built around an **ESP32-H2 Super Mini**, **SHT31-D temperature and humidity sensor**, and **0.96-inch SSD1306 OLED display**.

The device connects directly to a Zigbee network such as **Home Assistant ZHA** or **Zigbee2MQTT**. It reports temperature, humidity, battery percentage, and battery voltage while using automatic light sleep to reduce power consumption.

A momentary push button wakes the device, takes a fresh measurement, sends the latest readings over Zigbee, and displays the data for 10 seconds.

> This project accompanies a step-by-step video series. Video links will be added to the [Video series](#video-series) section.

---

## Features

- ESP32-H2 Zigbee End Device
- Direct Zigbee connection without Wi-Fi or ESPHome
- SHT31-D temperature and humidity measurements
- Temperature reported to Zigbee in degrees Celsius
- Temperature displayed locally in degrees Fahrenheit
- Humidity reporting
- Battery voltage measurement through a resistor divider
- Estimated LiPo battery percentage
- 128 × 64 SSD1306 OLED display
- Five-minute automatic measurement and reporting interval
- Automatic Zigbee-compatible light sleep between tasks
- Push-button wake and immediate sensor report
- Ten-second OLED timeout
- Five-second button hold for Zigbee factory reset
- Home Assistant Zigbee Identify support using the onboard RGB LED

---

## How it works

During normal operation, the device:

1. Reads temperature and humidity from the SHT31-D.
2. Measures the LiPo battery voltage through GPIO4.
3. Updates the Zigbee temperature, humidity, battery percentage, and battery-voltage attributes.
4. Sends reports to the Zigbee coordinator.
5. Turns off the OLED.
6. Allows the ESP32-H2 to enter automatic light sleep while the Zigbee stack is idle.
7. Repeats the measurement and reporting cycle every five minutes.

Automatic light sleep does not disconnect the device from Zigbee. The ESP32-H2 can wake briefly when the Zigbee stack needs to poll its parent router and then return to sleep.

### Button behavior

A momentary button is connected between **GPIO12** and **GND**.

| Action | Result |
|---|---|
| Short press | Wakes the device, reads all sensors, sends a Zigbee report, and displays the current data for 10 seconds |
| Hold for 5 seconds | Erases Zigbee pairing information and restarts the device |
| Home Assistant Identify command | Flashes the onboard RGB LED green and blue |

A short button press also restarts the five-minute measurement timer. The next scheduled report occurs five minutes after that button-triggered reading.

---

## Reported Zigbee data

The firmware creates a Zigbee temperature-sensor endpoint and exposes the following data:

- Temperature
- Relative humidity
- Battery percentage
- Battery voltage
- Identify command

The configured device identity is:

```text
Manufacturer: CFDesign
Model: ESP32-H2-SHT31D-001
Endpoint: 10
```

These values can be changed in the sketch:

```cpp
constexpr char ZIGBEE_MANUFACTURER[] = "CFDesign";
constexpr char ZIGBEE_MODEL[]        = "ESP32-H2-SHT31D-001";
```

---

## Bill of materials

Prices are approximate and will vary by seller, quantity, and shipping.

| Qty. | Component | Important requirements | Approx. cost |
|---:|---|---|---:|
| 1 | ESP32-H2 Super Mini | ESP32-H2 board with 802.15.4/Zigbee support | $4.00–$6.00 |
| 1 | SHT31-D breakout board | I²C interface; default address `0x44` | $8.49 |
| 1 | SSD1306 OLED display | 0.96-inch, 128 × 64, I²C, address `0x3C` | $2.00–$4.00 |
| 1 | Protected 3.7 V LiPo battery | Single-cell battery with protection; approximately 1000 mAh used in the prototype | $4.75 |
| 1 | Normally-open momentary push button | Connects GPIO12 to GND when pressed | $0.35 |
| 2 | 470 kΩ, 1% resistors | Battery-voltage divider; metal-film resistors recommended | $0.02 |
| 1 | 0.1 µF capacitor | Battery ADC filtering; ceramic capacitor recommended | $0.03 |
| — | Hookup wire | Suitable for the selected assembly method | Varies |
| — | Pin headers or connectors | Optional; depends on whether the unit is soldered directly or made removable | Varies |
| — | Perfboard or custom PCB | Optional for the final assembled unit | Varies |
| — | Enclosure and hardware | Optional; may be 3D printed | Varies |

**Estimated core electronics cost:** approximately **$19.64–$23.64**, excluding wire, connectors, PCB, enclosure, charger, shipping, and tools.

### Battery-charging note

Use only a charging arrangement designed for a **single-cell LiPo battery**. Do not assume every ESP32-H2 Super Mini board includes a LiPo charging circuit. Confirm the capabilities and pinout of the exact board being used.

A protected battery helps protect against common battery fault conditions, but it does not replace a proper LiPo charger.

---

## Pin assignments

| Function | ESP32-H2 GPIO |
|---|---:|
| I²C SDA | GPIO10 |
| I²C SCL | GPIO11 |
| Momentary button | GPIO12 |
| Battery ADC input | GPIO4 |
| Onboard addressable RGB LED | GPIO8 |

The RGB LED pin may differ on some ESP32-H2 Super Mini board revisions. Update `RGB_LED_PIN` if necessary.

---

## I²C devices

Both the SHT31-D and OLED share the same I²C bus.

| Device | I²C address |
|---|---:|
| SSD1306 OLED | `0x3C` |
| SHT31-D | `0x44` |

### Shared I²C wiring

| ESP32-H2 | SSD1306 OLED | SHT31-D |
|---|---|---|
| 3.3 V | VCC | VIN/VCC |
| GND | GND | GND |
| GPIO10 | SDA | SDA |
| GPIO11 | SCL | SCL |

Confirm that both breakout boards are compatible with 3.3 V operation.

---

## Push-button wiring

Connect a normally-open momentary button as follows:

```text
GPIO12 ---- momentary button ---- GND
```

The firmware enables the ESP32 internal pull-up resistor, so an external button pull-up is not normally required.

The input is:

- `HIGH` while the button is released
- `LOW` while the button is pressed

GPIO12 is also configured as a light-sleep wake source.

---

## Battery-voltage measurement

The ESP32-H2 cannot safely measure a full LiPo voltage directly through its ADC pin. The project uses two equal-value resistors to divide the battery voltage approximately in half.

### Voltage-divider wiring

```text
Battery positive
      |
    470 kΩ
      |
      +---------------- GPIO4
      |                   |
    470 kΩ               0.1 µF
      |                   |
     GND-----------------GND
```

The capacitor filters ADC noise and should be connected as close to GPIO4 and GND as practical.

### Capacitor polarity

A **0.1 µF ceramic capacitor** is recommended and has no polarity.

If a polarized capacitor is used instead:

- Positive lead → GPIO4
- Negative lead → GND

### Divider calculation

With two equal 470 kΩ resistors:

```text
ADC voltage ≈ battery voltage ÷ 2
```

The firmware multiplies the measured ADC voltage by two to estimate the actual battery voltage.

### Calibration

ADC measurements vary between boards. Compare the displayed battery voltage with a trusted multimeter and adjust:

```cpp
constexpr float BATTERY_CALIBRATION = 1.000f;
```

Example:

```cpp
constexpr float BATTERY_CALIBRATION = 1.035f;
```

Do not change the resistor constants unless the physical resistor values are also changed:

```cpp
constexpr float BATTERY_R1_OHMS = 470000.0f;
constexpr float BATTERY_R2_OHMS = 470000.0f;
```

### Battery percentage limitations

The displayed percentage is estimated from a lookup table based on resting LiPo voltage. It is not a laboratory-grade state-of-charge measurement.

Reported percentage can be affected by:

- Battery load
- Charging
- Battery temperature
- Cell age
- ADC tolerance
- Resistor tolerance
- Board regulator behavior

For greater state-of-charge accuracy, a dedicated fuel-gauge IC can be used in a future version.

---

## OLED display

The OLED is normally off to reduce power consumption.

Pressing the button causes the display to show:

- Zigbee connection status
- Battery percentage
- Battery icon
- Temperature in degrees Fahrenheit
- Relative humidity
- Battery voltage

The OLED remains on for 10 seconds:

```cpp
constexpr unsigned long DISPLAY_ON_TIME_MS = 10UL * 1000UL;
```

Sending the SSD1306 display-off command reduces display consumption, but the OLED breakout board may continue drawing standby current. A future low-power revision could switch OLED power with a MOSFET or load switch.

---

## Measurement and reporting interval

The default sensor interval is five minutes:

```cpp
constexpr unsigned long SENSOR_READ_INTERVAL_MS =
    5UL * 60UL * 1000UL;
```

The Zigbee reporting configuration is also set to 300 seconds:

```cpp
zbTempSensor.setReporting(300, 300, 0.1f);
zbTempSensor.setHumidityReporting(300, 300, 1.0f);
```

The temperature change threshold is `0.1 °C`, and the humidity change threshold is `1% RH`.

The sketch manually reports the current data after every scheduled measurement and every short button press.

---

## Light-sleep operation

The project uses ESP-IDF automatic power management:

```cpp
powerManagementConfig.max_freq_mhz = 96;
powerManagementConfig.min_freq_mhz = 8;
powerManagementConfig.light_sleep_enable = true;
```

Automatic light sleep occurs only when FreeRTOS tasks are idle. This allows the Zigbee stack to remain responsible for network polling and radio activity.

The main loop yields regularly:

```cpp
delay(50);
```

This gives the Zigbee and idle tasks time to run and allows automatic light sleep when no work is pending.

### Expected serial message

When light sleep is configured successfully:

```text
Automatic light sleep enabled.
```

If power management is unavailable in the selected build:

```text
Unable to enable automatic light sleep: ...
```

The exact Arduino-ESP32 build and partition configuration must support power management and tickless idle.

---

## Software requirements

### Arduino IDE

Use a current Arduino IDE installation with ESP32 board support that includes ESP32-H2 and Arduino Zigbee APIs.

### Board configuration

Select settings appropriate for the exact ESP32-H2 Super Mini board. The critical Zigbee setting is:

```text
Tools → Zigbee Mode → Zigbee ED
```

The sketch intentionally stops compilation when Zigbee End Device mode is not selected:

```cpp
#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee Mode -> Zigbee ED (end device)"
#endif
```

Other board-menu names may vary with the installed Arduino-ESP32 version.

### Required libraries

Install these libraries through the Arduino Library Manager:

- Adafruit GFX Library
- Adafruit SSD1306
- Adafruit SHT31 Library

The following headers are supplied by the ESP32 Arduino core and ESP-IDF:

- `Arduino.h`
- `Wire.h`
- `Zigbee.h`
- `esp_pm.h`
- `esp_sleep.h`
- `driver/gpio.h`

---

## Compiling and uploading

1. Install Arduino IDE.
2. Install ESP32 board support.
3. Install the required Adafruit libraries.
4. Connect the ESP32-H2 Super Mini by USB.
5. Select the appropriate ESP32-H2 board.
6. Select **Zigbee ED** as the Zigbee mode.
7. Select the correct serial port.
8. Open the project sketch.
9. Compile the sketch.
10. Upload it to the ESP32-H2.
11. Open Serial Monitor at **115200 baud**.

---

## Pairing with Home Assistant

The unit can be joined to a Zigbee network managed by Home Assistant.

### ZHA

1. Open Home Assistant.
2. Go to **Settings → Devices & services**.
3. Open the **Zigbee Home Automation** integration.
4. Select **Add device**.
5. Power on or restart the sensor.
6. Wait for the device to join.
7. Assign it a useful name and area.

### Zigbee2MQTT

1. Enable **Permit join** in Zigbee2MQTT.
2. Power on or restart the sensor.
3. Wait for the interview to complete.
4. Rename the device as needed.
5. Disable **Permit join** after pairing.

### Pairing window

During startup, the firmware waits up to 30 seconds for a network connection while printing progress to Serial Monitor. The Zigbee stack may continue attempting to join afterward.

If the device was previously paired to another network, perform a factory reset before pairing it again.

---

## Factory reset

To erase Zigbee network information:

1. Press and hold the external button.
2. Continue holding it for at least five seconds.
3. The OLED will display a reset message.
4. The firmware calls `Zigbee.factoryReset()`.
5. Pair the unit with the desired Zigbee network again.

The reset duration is controlled by:

```cpp
constexpr unsigned long FACTORY_RESET_HOLD_MS =
    5UL * 1000UL;
```

---

## Identify function

Home Assistant may expose an Identify control as a button entity, sometimes displayed with the action **Press**.

When activated, the onboard RGB LED alternates between green and blue. This helps identify a specific physical sensor when multiple units are installed.

If the LED does not work, confirm the onboard RGB LED GPIO for the exact board revision and update:

```cpp
constexpr uint8_t RGB_LED_PIN = 8;
```

---

## Serial diagnostics

Open Serial Monitor at:

```text
115200 baud
```

Useful messages include:

```text
Automatic light sleep enabled.
SHT31-D initialized
Starting Zigbee...
Connected to Zigbee network.
Temperature: ...
Battery: ...
Zigbee temperature report sent.
Zigbee humidity report sent.
Zigbee battery percentage report sent.
```

---

## Troubleshooting

### Sketch will not compile

Confirm:

- An ESP32-H2 board is selected.
- Zigbee mode is set to **Zigbee ED**.
- The required Adafruit libraries are installed.
- The installed ESP32 Arduino core includes the Zigbee API used by the sketch.

### SHT31-D is not found

The expected address is `0x44`.

Check:

- Sensor power
- Common ground
- SDA on GPIO10
- SCL on GPIO11
- Solder joints
- Breakout-board address configuration

### OLED does not display

The expected address is `0x3C`.

Check:

- OLED power
- Common ground
- SDA and SCL wiring
- Whether the module uses address `0x3C` or `0x3D`
- Whether the display is currently in its normal off state

Press the button to turn the display on for 10 seconds.

### Battery voltage is inaccurate

- Measure the battery with a multimeter.
- Confirm both divider resistors are 470 kΩ.
- Confirm the resistor midpoint connects to GPIO4.
- Confirm the capacitor connects from GPIO4 to GND.
- Adjust `BATTERY_CALIBRATION`.
- Avoid calibrating while the battery is charging or under an unusual load.

### Device does not join Zigbee

- Put ZHA or Zigbee2MQTT into pairing mode.
- Confirm **Zigbee ED** was selected before compilation.
- Move the sensor closer to the coordinator or a compatible Zigbee router.
- Factory-reset the sensor if it was paired previously.
- Review Serial Monitor output.

### Button does not wake the display

- Confirm the button connects GPIO12 to GND.
- Confirm the button is normally open.
- Verify GPIO12 reads LOW while pressed.
- Check for incorrect button pin numbering on the physical board.

### Light sleep is not enabled

Check Serial Monitor for the power-management error. Confirm the installed Arduino-ESP32 configuration supports:

- Power management
- Tickless idle
- Automatic light sleep
- ESP32-H2 Zigbee End Device operation

### Battery life is lower than expected

Development-board current can be dominated by components other than the ESP32-H2, including:

- Power LEDs
- Voltage regulators
- USB circuitry
- OLED standby current
- Sensor breakout-board regulators or LEDs
- Battery-voltage divider current
- Zigbee parent polling interval

Measure current at the battery rather than estimating battery life from the ESP32-H2 chip specification alone.

---

## Suggested video series

Replace each placeholder with the final video URL.

1. **Project overview and component selection**  
   `[Video URL to be added]`

2. **Understanding the schematic and pin assignments**  
   `[Video URL to be added]`

3. **Breadboard wiring: ESP32-H2, SHT31-D, and OLED**  
   `[Video URL to be added]`

4. **Adding the push button and battery-voltage divider**  
   `[Video URL to be added]`

5. **Configuring Arduino IDE for ESP32-H2 Zigbee**  
   `[Video URL to be added]`

6. **Installing libraries and uploading the firmware**  
   `[Video URL to be added]`

7. **Pairing the sensor with Home Assistant ZHA**  
   `[Video URL to be added]`

8. **Pairing the sensor with Zigbee2MQTT**  
   `[Video URL to be added]`

9. **Understanding Zigbee light sleep and battery operation**  
   `[Video URL to be added]`

10. **Calibrating battery voltage and troubleshooting**  
    `[Video URL to be added]`

11. **Moving from breadboard to perfboard or a custom PCB**  
    `[Video URL to be added]`

12. **Designing and assembling the enclosure**  
    `[Video URL to be added]`

---

## Possible future improvements

- Dedicated battery fuel-gauge IC
- Dedicated LiPo charging and power-path board
- MOSFET-controlled OLED power
- Custom PCB
- 3D-printable enclosure
- Celsius/Fahrenheit display option
- Configurable reporting interval from Home Assistant
- Configurable temperature and humidity reporting thresholds
- Low-battery warning
- Charging-status reporting
- External antenna option
- Additional environmental sensors
- Zigbee OTA firmware updates

---

## Safety

Lithium-polymer batteries can be damaged by short circuits, overcharging, over-discharging, puncture, heat, or incorrect polarity.

- Use a protected LiPo cell.
- Use a charger designed for a single-cell LiPo battery.
- Verify polarity before connecting power.
- Insulate exposed conductors.
- Do not charge an unattended or damaged battery.
- Do not connect a LiPo battery directly to an input that is not designed for its voltage.
- Confirm the capabilities of the exact ESP32-H2 board before connecting a battery.

This project is provided for educational and prototyping purposes. Verify the design and construction before unattended or permanent installation.

---

## License

Add the licensing terms for the source code and documentation here.

Example placeholders:

```text
Source code license: To be determined
Documentation license: Copyright © [Year] [Name]. All rights reserved.
```

---

## Credits

Designed as an educational ESP32-H2, Zigbee, and Home Assistant project.

Project author: **[Your name or channel name]**

Video channel: **[Channel URL to be added]**