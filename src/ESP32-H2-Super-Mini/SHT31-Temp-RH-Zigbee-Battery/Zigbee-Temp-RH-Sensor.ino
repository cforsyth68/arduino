#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

#ifndef ZIGBEE_MODE_ED
#error "In Arduino IDE, select Tools -> Zigbee Mode -> Zigbee ED (end device)"
#endif

#include "Zigbee.h"


// -------------------------------------------------------------
// BOM
// -------------------------------------------------------------
// - 1x ESP32-H2 Super Mini                    $4-6
// - SSD1306 .96" OLED Screen                  $2-$4
// - 1x 3.7v LiPo Battery with protection      $4.75  (1000 mAh)
// - 1x SHT31-D breakout board                 $8.49
// - 1x Momentary Button                       $0.35
// - 2x 440k 1% steel resistors                $0.01
// - .1 uF capacitor                           $0.03
//                                            -------
//                                            $21.63
// -------------------------------------------------------------
// Battery voltage-divider configuration
// -------------------------------------------------------------
// Wiring:
//
// Battery positive
//       |
//     470 kΩ
//       |
//       +---------- GPIO4
//       |             |
//     470 kΩ         0.1 µF
//       |             |
//      GND-----------GND

// For a polarized electrolytic capacitor:
//   positive capacitor lead -> GPIO4
//   negative capacitor lead -> GND



// ----------------------------------------------------
// Pin configuration
// ----------------------------------------------------

constexpr uint8_t I2C_SDA_PIN     = 10;
constexpr uint8_t I2C_SCL_PIN     = 11;
constexpr uint8_t BUTTON_PIN      = 12;
constexpr uint8_t BATTERY_ADC_PIN = 4;

// Onboard addressable RGB LED on the ESP32-H2 Super Mini.
// Change this if your board routes its RGB LED to another GPIO.
constexpr uint8_t RGB_LED_PIN     = 8;

// ----------------------------------------------------
// Zigbee configuration
// ----------------------------------------------------

constexpr uint8_t TEMP_SENSOR_ENDPOINT_NUMBER = 10;

ZigbeeTempSensor zbTempSensor(TEMP_SENSOR_ENDPOINT_NUMBER);

// Change these strings to your preferred manufacturer/model names.
constexpr char ZIGBEE_MANUFACTURER[] = "CFDesign";
constexpr char ZIGBEE_MODEL[]        = "ESP32-H2-SHT31D-001";


constexpr float BATTERY_R1_OHMS = 470000.0f;
constexpr float BATTERY_R2_OHMS = 470000.0f;

constexpr float BATTERY_DIVIDER_MULTIPLIER =
    (BATTERY_R1_OHMS + BATTERY_R2_OHMS) /
    BATTERY_R2_OHMS;

// Compare the reported voltage with a multimeter and adjust as needed.
constexpr float BATTERY_CALIBRATION = 1.000f;

constexpr uint8_t BATTERY_SAMPLE_COUNT = 16;

// ----------------------------------------------------
// I2C addresses
// ----------------------------------------------------

constexpr uint8_t OLED_ADDRESS  = 0x3C;
constexpr uint8_t SHT31_ADDRESS = 0x44;

// ----------------------------------------------------
// OLED configuration
// ----------------------------------------------------

constexpr int SCREEN_WIDTH  = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr int OLED_RESET    = -1;

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

Adafruit_SHT31 sht31;

// ----------------------------------------------------
// Timing
// ----------------------------------------------------

// Read and report temperature, humidity, and battery every five minutes.
constexpr unsigned long SENSOR_READ_INTERVAL_MS = 5UL * 60UL * 1000UL;

// Keep the display on for 10 seconds after a short button press.
constexpr unsigned long DISPLAY_ON_TIME_MS = 10UL * 1000UL;

// Button debounce time.
constexpr unsigned long BUTTON_DEBOUNCE_MS = 40;

// Hold the button this long to erase Zigbee pairing information.
constexpr unsigned long FACTORY_RESET_HOLD_MS = 5UL * 1000UL;

// Identify animation: alternate green and blue every 500 ms.
// Four cycles means green + blue repeated four times, then off.
constexpr unsigned long IDENTIFY_FLASH_INTERVAL_MS = 500;
constexpr uint8_t IDENTIFY_FLASH_CYCLES = 4;
constexpr uint8_t IDENTIFY_COLOR_STEPS = IDENTIFY_FLASH_CYCLES * 2;

// ----------------------------------------------------
// State
// ----------------------------------------------------

float temperatureC = NAN;
float temperatureF = NAN;
float humidityPercent = NAN;

float batteryVoltage = NAN;
int batteryPercent = -1;

unsigned long lastSensorReadMillis = 0;
unsigned long displayOffAtMillis = 0;
unsigned long lastButtonChangeMillis = 0;
unsigned long buttonPressedAtMillis = 0;

bool displayIsOn = false;
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
bool zigbeeConnected = false;
bool factoryResetTriggered = false;

// Set by the Zigbee callback and consumed by loop().
volatile bool identifyRequested = false;

// Set by the GPIO interrupt. The interrupt wakes the chip from automatic
// light sleep; normal debouncing and button handling remain in loop().
volatile bool buttonWakeRequested = false;

bool identifyAnimationActive = false;
uint8_t identifyColorStep = 0;
unsigned long nextIdentifyColorAtMillis = 0;

// ----------------------------------------------------
// Power-management functions
// ----------------------------------------------------

void IRAM_ATTR onButtonWake()
{
  buttonWakeRequested = true;
}

void configureAutomaticLightSleep()
{
  // Automatic light sleep only occurs while all FreeRTOS tasks are idle.
  // The Zigbee task can therefore wake the radio for parent polling and
  // other stack work without losing the network relationship.
  esp_pm_config_t powerManagementConfig = {};
  powerManagementConfig.max_freq_mhz = 96;
  powerManagementConfig.min_freq_mhz = 8;
  powerManagementConfig.light_sleep_enable = true;

  const esp_err_t pmResult = esp_pm_configure(&powerManagementConfig);

  if (pmResult == ESP_OK) {
    Serial.println(F("Automatic light sleep enabled."));
  } else {
    Serial.print(F("Unable to enable automatic light sleep: "));
    Serial.println(esp_err_to_name(pmResult));
    Serial.println(
      F("Use a Zigbee End Device partition/build with power management enabled.")
    );
  }

  // GPIO12 is held HIGH by INPUT_PULLUP and goes LOW when pressed.
  // A low-level GPIO wake source keeps the processor awake while the user
  // holds the button, which also permits the five-second factory-reset hold.
  gpio_wakeup_enable(
    static_cast<gpio_num_t>(BUTTON_PIN),
    GPIO_INTR_LOW_LEVEL
  );

  const esp_err_t gpioWakeResult = esp_sleep_enable_gpio_wakeup();

  if (gpioWakeResult != ESP_OK) {
    Serial.print(F("Unable to enable button light-sleep wake: "));
    Serial.println(esp_err_to_name(gpioWakeResult));
  }

  attachInterrupt(
    digitalPinToInterrupt(BUTTON_PIN),
    onButtonWake,
    FALLING
  );
}

// ----------------------------------------------------
// RGB identify functions
// ----------------------------------------------------

void setRgbOff()
{
  rgbLedWrite(RGB_LED_PIN, 0, 0, 0);
}

void showIdentifyColor(uint8_t step)
{
  if ((step % 2) == 0) {
    // Green.
    rgbLedWrite(RGB_LED_PIN, 0, 255, 0);
  } else {
    // Blue.
    rgbLedWrite(RGB_LED_PIN, 0, 0, 255);
  }
}

void onZigbeeIdentify(uint16_t identifyTimeSeconds)
{
  // Keep the Zigbee callback short. The main loop runs the animation.
  identifyRequested = true;
}

void startIdentifyAnimation()
{
  identifyAnimationActive = true;
  identifyColorStep = 0;
  nextIdentifyColorAtMillis = millis() + IDENTIFY_FLASH_INTERVAL_MS;

  showIdentifyColor(identifyColorStep);

  Serial.println(F("Identify pressed: flashing RGB green/blue."));
}

void handleIdentifyAnimation()
{
  if (identifyRequested) {
    identifyRequested = false;
    startIdentifyAnimation();
  }

  if (!identifyAnimationActive) {
    return;
  }

  const unsigned long currentMillis = millis();

  if (
    static_cast<long>(
      currentMillis - nextIdentifyColorAtMillis
    ) < 0
  ) {
    return;
  }

  identifyColorStep++;

  if (identifyColorStep >= IDENTIFY_COLOR_STEPS) {
    setRgbOff();
    identifyAnimationActive = false;
    return;
  }

  showIdentifyColor(identifyColorStep);
  nextIdentifyColorAtMillis =
      currentMillis + IDENTIFY_FLASH_INTERVAL_MS;
}

// ----------------------------------------------------
// Battery functions
// ----------------------------------------------------

float readBatteryVoltage()
{
  // Discard the first reading to allow the ADC to settle.
  analogReadMilliVolts(BATTERY_ADC_PIN);
  delay(5);

  uint32_t totalMillivolts = 0;

  for (uint8_t sample = 0; sample < BATTERY_SAMPLE_COUNT; sample++) {
    totalMillivolts += analogReadMilliVolts(BATTERY_ADC_PIN);
    delay(2);
  }

  const float averageAdcMillivolts =
      totalMillivolts / static_cast<float>(BATTERY_SAMPLE_COUNT);

  return
      (averageAdcMillivolts / 1000.0f) *
      BATTERY_DIVIDER_MULTIPLIER *
      BATTERY_CALIBRATION;
}

int batteryVoltageToPercent(float voltage)
{
  // Approximate resting-voltage curve for a single-cell LiPo.
  if (voltage >= 4.20f) return 100;
  if (voltage >= 4.15f) return 95;
  if (voltage >= 4.10f) return 90;
  if (voltage >= 4.05f) return 85;
  if (voltage >= 4.00f) return 80;
  if (voltage >= 3.95f) return 75;
  if (voltage >= 3.90f) return 70;
  if (voltage >= 3.85f) return 60;
  if (voltage >= 3.80f) return 50;
  if (voltage >= 3.75f) return 40;
  if (voltage >= 3.70f) return 30;
  if (voltage >= 3.65f) return 20;
  if (voltage >= 3.55f) return 10;
  if (voltage >= 3.40f) return 5;

  return 0;
}

void readBattery()
{
  batteryVoltage = readBatteryVoltage();
  batteryPercent = batteryVoltageToPercent(batteryVoltage);

  Serial.print(F("Battery: "));
  Serial.print(batteryVoltage, 3);
  Serial.print(F(" V, "));
  Serial.print(batteryPercent);
  Serial.println(F("%"));
}

// ----------------------------------------------------
// Display functions
// ----------------------------------------------------

void turnDisplayOn()
{
  display.ssd1306_command(SSD1306_DISPLAYON);
  displayIsOn = true;
  displayOffAtMillis = millis() + DISPLAY_ON_TIME_MS;
}

void turnDisplayOff()
{
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  displayIsOn = false;
}

void drawBatteryIcon(int x, int y, int percentage)
{
  constexpr int BATTERY_WIDTH = 22;
  constexpr int BATTERY_HEIGHT = 10;

  display.drawRect(
    x,
    y,
    BATTERY_WIDTH,
    BATTERY_HEIGHT,
    SSD1306_WHITE
  );

  display.fillRect(
    x + BATTERY_WIDTH,
    y + 3,
    2,
    4,
    SSD1306_WHITE
  );

  const int clampedPercentage = constrain(percentage, 0, 100);

  const int fillWidth = map(
    clampedPercentage,
    0,
    100,
    0,
    BATTERY_WIDTH - 4
  );

  if (fillWidth > 0) {
    display.fillRect(
      x + 2,
      y + 2,
      fillWidth,
      BATTERY_HEIGHT - 4,
      SSD1306_WHITE
    );
  }
}

void drawReadings()
{
  if (!displayIsOn) {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Header.
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(zigbeeConnected ? F("ZB") : F("ZB--"));

  if (batteryPercent >= 0) {
    display.setCursor(70, 0);

    if (batteryPercent < 100) {
      display.print(' ');
    }

    if (batteryPercent < 10) {
      display.print(' ');
    }

    display.print(batteryPercent);
    display.print('%');

    drawBatteryIcon(103, 0, batteryPercent);
  }

  display.drawLine(0, 12, SCREEN_WIDTH - 1, 12, SSD1306_WHITE);

  if (isnan(temperatureF) || isnan(humidityPercent)) {
    display.setTextSize(1);
    display.setCursor(0, 27);
    display.println(F("Sensor reading"));
    display.println(F("not available"));
  } else {
    display.setTextSize(1);
    display.setCursor(0, 18);
    display.print(F("Temperature"));

    display.setTextSize(2);
    display.setCursor(0, 31);
    display.print(temperatureF, 1);
    display.print(F("F"));

    display.setTextSize(1);
    display.setCursor(78, 18);
    display.print(F("Humidity"));

    display.setTextSize(2);
    display.setCursor(78, 31);
    display.print(humidityPercent, 0);
    display.print('%');
  }

  display.setTextSize(1);
  display.setCursor(0, 56);

  if (!isnan(batteryVoltage)) {
    display.print(F("Battery: "));
    display.print(batteryVoltage, 2);
    display.print(F(" V"));
  } else {
    display.print(F("Battery unavailable"));
  }

  display.display();
}

// ----------------------------------------------------
// Sensor and Zigbee reporting functions
// ----------------------------------------------------

void readSHT31()
{
  const float newTemperature = sht31.readTemperature();
  const float newHumidity = sht31.readHumidity();

  if (!isnan(newTemperature) && !isnan(newHumidity)) {
    temperatureC = newTemperature;
    temperatureF = newTemperature * 1.8f + 32.0f;
    humidityPercent = newHumidity;

    Serial.print(F("Temperature: "));
    Serial.print(temperatureF, 1);
    Serial.print(F(" F, Humidity: "));
    Serial.print(humidityPercent, 1);
    Serial.println(F("%"));
  } else {
    Serial.println(F("Failed to read SHT31-D"));
  }
}

void updateZigbeeValues()
{
  if (isnan(temperatureC) || isnan(humidityPercent)) {
    return;
  }

  // Zigbee temperature values are supplied in degrees Celsius.
  zbTempSensor.setTemperature(temperatureC);
  zbTempSensor.setHumidity(humidityPercent);

  if (batteryPercent >= 0) {
    zbTempSensor.setBatteryPercentage(
      static_cast<uint8_t>(constrain(batteryPercent, 0, 100))
    );
  }

  if (!isnan(batteryVoltage)) {
    // Zigbee battery voltage uses units of 100 mV:
    // 4.10 V becomes 41.
    const uint8_t batteryVoltage100mV =
        static_cast<uint8_t>(
          constrain(lroundf(batteryVoltage * 10.0f), 0L, 255L)
        );

    zbTempSensor.setBatteryVoltage(batteryVoltage100mV);
  }
}

void reportZigbeeValues()
{
  if (!Zigbee.connected()) {
    zigbeeConnected = false;
    Serial.println(F("Zigbee not connected; report skipped."));
    return;
  }

  zigbeeConnected = true;

  if (isnan(temperatureC)) {
    Serial.println(F("Temperature unavailable; report skipped."));
    return;
  }

  Serial.print(F("Updating Zigbee temperature attribute: "));
  Serial.print(temperatureC, 2);
  Serial.println(F(" C"));

  const bool temperatureSet =
      zbTempSensor.setTemperature(temperatureC);

  if (!temperatureSet) {
    Serial.println(F("Failed to update Zigbee temperature attribute."));
    return;
  }

  const bool temperatureReported =
      zbTempSensor.reportTemperature();

  Serial.println(
    temperatureReported
      ? F("Zigbee temperature report sent.")
      : F("Zigbee temperature report failed.")
  );

  if (!isnan(humidityPercent)) {
    const bool humiditySet =
        zbTempSensor.setHumidity(humidityPercent);

    const bool humidityReported =
        humiditySet && zbTempSensor.reportHumidity();

    Serial.println(
      humidityReported
        ? F("Zigbee humidity report sent.")
        : F("Zigbee humidity report failed.")
    );
  }

  if (batteryPercent >= 0) {
    const bool batterySet =
        zbTempSensor.setBatteryPercentage(
          static_cast<uint8_t>(
            constrain(batteryPercent, 0, 100)
          )
        );

    const bool batteryReported =
        batterySet && zbTempSensor.reportBatteryPercentage();

    Serial.println(
      batteryReported
        ? F("Zigbee battery percentage report sent.")
        : F("Zigbee battery percentage report failed.")
    );
  }

  if (!isnan(batteryVoltage)) {
    // Zigbee battery voltage uses units of 100 mV.
    // The current Arduino Zigbee API exposes an attribute setter, but no
    // separate manual reportBatteryVoltage() method.
    const uint8_t batteryVoltage100mV =
        static_cast<uint8_t>(
          constrain(lroundf(batteryVoltage * 10.0f), 0L, 255L)
        );

    zbTempSensor.setBatteryVoltage(batteryVoltage100mV);
  }
}

void readAllSensors(bool sendZigbeeReport)
{
  readSHT31();
  readBattery();
  updateZigbeeValues();

  if (sendZigbeeReport) {
    reportZigbeeValues();
  }

  drawReadings();
}

// ----------------------------------------------------
// Button handling
// ----------------------------------------------------

void handleButton()
{
  const bool currentReading = digitalRead(BUTTON_PIN);
  const unsigned long currentMillis = millis();

  if (currentReading != lastButtonReading) {
    lastButtonChangeMillis = currentMillis;
    lastButtonReading = currentReading;
  }

  if ((currentMillis - lastButtonChangeMillis) < BUTTON_DEBOUNCE_MS) {
    return;
  }

  if (currentReading != stableButtonState) {
    stableButtonState = currentReading;

    if (stableButtonState == LOW) {
      // Button was pressed.
      buttonPressedAtMillis = currentMillis;
      factoryResetTriggered = false;

      // A button press starts a new five-minute measurement period:
      // read fresh data, display it, report it to Zigbee, then allow the
      // processor to return to automatic light sleep.
      buttonWakeRequested = false;
      turnDisplayOn();
      readAllSensors(true);
      lastSensorReadMillis = currentMillis;
    } else {
      // Button was released.
      buttonPressedAtMillis = 0;
      factoryResetTriggered = false;
    }
  }

  if (
    stableButtonState == LOW &&
    !factoryResetTriggered &&
    (currentMillis - buttonPressedAtMillis >= FACTORY_RESET_HOLD_MS)
  ) {
    factoryResetTriggered = true;

    Serial.println(F("Factory-resetting Zigbee and rebooting..."));

    turnDisplayOn();
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 18);
    display.println(F("Resetting Zigbee"));
    display.println(F("pairing data..."));
    display.display();

    delay(750);
    Zigbee.factoryReset();
  }
}

// ----------------------------------------------------
// Zigbee setup
// ----------------------------------------------------

void setupZigbee()
{
  zbTempSensor.setManufacturerAndModel(
    ZIGBEE_MANUFACTURER,
    ZIGBEE_MODEL
  );

  zbTempSensor.setMinMaxValue(-40.0f, 125.0f);
  zbTempSensor.setDefaultValue(20.0f);
  zbTempSensor.setTolerance(0.1f);

  // Home Assistant exposes this Zigbee Identify command as a
  // button entity whose action is shown as "Press".
  zbTempSensor.onIdentify(onZigbeeIdentify);

  // Humidity: minimum, maximum, tolerance, default value.
  zbTempSensor.addHumiditySensor(0.0f, 100.0f, 1.0f, 50.0f);

  // Battery-powered endpoint. Battery voltage is in 100 mV units.
  const uint8_t initialPercent =
      static_cast<uint8_t>(constrain(batteryPercent, 0, 100));

  const uint8_t initialVoltage100mV =
      static_cast<uint8_t>(
        constrain(lroundf(batteryVoltage * 10.0f), 0L, 255L)
      );

  zbTempSensor.setPowerSource(
    ZB_POWER_SOURCE_BATTERY,
    initialPercent,
    initialVoltage100mV
  );

  Zigbee.addEndpoint(&zbTempSensor);

  esp_zb_cfg_t zigbeeConfig = ZIGBEE_DEFAULT_ED_CONFIG();
  zigbeeConfig.nwk_cfg.zed_cfg.keep_alive = 10000;

  Zigbee.setTimeout(10000);

  Serial.println(F("Starting Zigbee..."));

  if (!Zigbee.begin(&zigbeeConfig, false)) {
    Serial.println(F("Zigbee failed to start. Rebooting."));
    delay(1000);
    ESP.restart();
  }

  Serial.println(F("Waiting for Zigbee network..."));

  // Do not block forever. The Zigbee stack continues trying afterward.
  const unsigned long joinStartedAt = millis();

  while (
    !Zigbee.connected() &&
    (millis() - joinStartedAt < 30000UL)
  ) {
    Serial.print('.');
    delay(250);
  }

  Serial.println();

  zigbeeConnected = Zigbee.connected();

  if (zigbeeConnected) {
    Serial.println(F("Connected to Zigbee network."));

    updateZigbeeValues();

    const bool initialTemperatureReport =
        zbTempSensor.reportTemperature();

    Serial.println(
      initialTemperatureReport
        ? F("Initial Zigbee temperature report sent.")
        : F("Initial Zigbee temperature report failed.")
    );

    const bool initialHumidityReport =
        zbTempSensor.reportHumidity();

    Serial.println(
      initialHumidityReport
        ? F("Initial Zigbee humidity report sent.")
        : F("Initial Zigbee humidity report failed.")
    );

    const bool initialBatteryReport =
        zbTempSensor.reportBatteryPercentage();

    Serial.println(
      initialBatteryReport
        ? F("Initial Zigbee battery report sent.")
        : F("Initial Zigbee battery report failed.")
    );
  } else {
    Serial.println(
      F("Not joined yet. Put Home Assistant into pairing mode, then reboot.")
    );
  }

  // Keep normal periodic Zigbee reporting aligned with the five-minute
  // application measurement interval. Button presses still report manually.
  zbTempSensor.setReporting(300, 300, 0.1f);
  zbTempSensor.setHumidityReporting(300, 300, 1.0f);
}

// ----------------------------------------------------
// Setup
// ----------------------------------------------------

void setup()
{
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BATTERY_ADC_PIN, INPUT);

  configureAutomaticLightSleep();

  // Initialize the onboard addressable RGB LED in the off state.
  setRgbOff();

  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println(F("SSD1306 initialization failed"));

    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.display();

  if (!sht31.begin(SHT31_ADDRESS)) {
    Serial.println(F("SHT31-D not found at address 0x44"));
  } else {
    Serial.println(F("SHT31-D initialized"));
  }

  // Initial measurements must be available before Zigbee endpoint setup.
  readAllSensors(false);

  setupZigbee();

  lastSensorReadMillis = millis();

  // Leave the display off during normal operation.
  turnDisplayOff();
}

// ----------------------------------------------------
// Main loop
// ----------------------------------------------------

void loop()
{
  handleButton();
  handleIdentifyAnimation();

  const unsigned long currentMillis = millis();

  // Track connection status for the OLED.
  zigbeeConnected = Zigbee.connected();

  if (
    (currentMillis - lastSensorReadMillis) >=
    SENSOR_READ_INTERVAL_MS
  ) {
    lastSensorReadMillis = currentMillis;
    readAllSensors(true);
  }

  if (
    displayIsOn &&
    static_cast<long>(currentMillis - displayOffAtMillis) >= 0
  ) {
    turnDisplayOff();
  }

  // This delay yields to the Zigbee and idle tasks. When the Zigbee build
  // has CONFIG_PM_ENABLE and CONFIG_FREERTOS_USE_TICKLESS_IDLE enabled, the
  // ESP32-H2 automatically enters light sleep during the idle portion.
  delay(50);
}