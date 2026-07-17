#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee Mode -> Zigbee ED"
#endif

#include "Zigbee.h"

/* Zigbee configuration */
constexpr uint8_t OCCUPANCY_SENSOR_ENDPOINT_NUMBER = 10;

/* Hardware pins */
constexpr uint8_t OLED_SDA_PIN = 10;
constexpr uint8_t OLED_SCL_PIN = 11;
constexpr uint8_t EXTERNAL_BUTTON_PIN = 12;
constexpr uint8_t AM312_PIN = 13;

/* OLED configuration */
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
constexpr int OLED_RESET_PIN = -1;

/* Timing */
constexpr uint32_t DISPLAY_ON_TIME_MS = 10000;
constexpr uint32_t PIR_WARMUP_TIME_MS = 30000;
constexpr uint32_t FACTORY_RESET_HOLD_MS = 3000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t DISPLAY_REFRESH_INTERVAL_MS = 200;
constexpr uint32_t ZIGBEE_STATUS_INTERVAL_MS = 1000;

Adafruit_SSD1306 display(
    OLED_WIDTH,
    OLED_HEIGHT,
    &Wire,
    OLED_RESET_PIN
);

ZigbeeOccupancySensor zbOccupancySensor(
    OCCUPANCY_SENSOR_ENDPOINT_NUMBER
);

/* Operating states */
enum class DeviceState {
  WAITING_FOR_ZIGBEE,
  PIR_WARMUP,
  NORMAL_OPERATION
};

DeviceState deviceState = DeviceState::WAITING_FOR_ZIGBEE;

/* Zigbee state */
bool zigbeeConnected = false;
bool previouslyConnected = false;

/* Occupancy state */
bool currentOccupancy = false;
bool previousOccupancy = false;
bool initialOccupancyReported = false;

/* Display state */
bool displayInitialized = false;
bool displayEnabled = false;
bool displayRequestedByButton = false;

/* Timing state */
uint32_t pirWarmupStartTime = 0;
uint32_t pirReadyTime = 0;
uint32_t displayOffTime = 0;
uint32_t lastDisplayRefreshTime = 0;
uint32_t lastZigbeeStatusTime = 0;

/* External-button state */
bool lastRawButtonState = HIGH;
bool stableButtonState = HIGH;
bool buttonPressHandled = false;
bool factoryResetTriggered = false;

uint32_t lastButtonChangeTime = 0;
uint32_t buttonPressStartTime = 0;

void turnDisplayOn() {
  if (!displayInitialized) {
    return;
  }

  if (!displayEnabled) {
    display.ssd1306_command(SSD1306_DISPLAYON);
    displayEnabled = true;
  }

  lastDisplayRefreshTime = 0;
}

void turnDisplayOff() {
  if (!displayInitialized || !displayEnabled) {
    return;
  }

  display.clearDisplay();
  display.display();

  display.ssd1306_command(SSD1306_DISPLAYOFF);

  displayEnabled = false;
  displayRequestedByButton = false;
}

void wakeDisplayForButtonPress() {
  turnDisplayOn();

  displayRequestedByButton = true;
  displayOffTime = millis() + DISPLAY_ON_TIME_MS;

  Serial.println("Display enabled for 10 seconds.");
}

void drawConnectingScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("Zigbee Setup");

  display.drawLine(
      0,
      11,
      OLED_WIDTH - 1,
      11,
      SSD1306_WHITE
  );

  display.setCursor(0, 16);
  display.println("Waiting to connect...");

  display.setCursor(0, 29);
  display.println("Home Assistant:");

  display.setCursor(0, 40);
  display.println("ZHA > Add device");

  display.setCursor(0, 53);
  display.println("Then reset ESP32");

  display.display();
}

void drawWarmupScreen() {
  const uint32_t elapsedTime =
      millis() - pirWarmupStartTime;

  uint32_t remainingSeconds = 0;

  if (elapsedTime < PIR_WARMUP_TIME_MS) {
    remainingSeconds =
        (PIR_WARMUP_TIME_MS - elapsedTime + 999) / 1000;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("Zigbee Connected");

  display.drawLine(
      0,
      11,
      OLED_WIDTH - 1,
      11,
      SSD1306_WHITE
  );

  display.setCursor(0, 18);
  display.println("AM312 warming up");

  display.setTextSize(2);
  display.setCursor(0, 33);
  display.print(remainingSeconds);
  display.println(" sec");

  display.setTextSize(1);
  display.setCursor(0, 55);
  display.println("Screen turns off");

  display.display();
}

void drawNormalScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("AM312 Zigbee Sensor");

  display.drawLine(
      0,
      11,
      OLED_WIDTH - 1,
      11,
      SSD1306_WHITE
  );

  display.setCursor(0, 18);
  display.print("Zigbee: ");

  if (zigbeeConnected) {
    display.println("Connected");
  } else {
    display.println("Disconnected");
  }

  display.setCursor(0, 32);
  display.print("Motion: ");

  if (currentOccupancy) {
    display.println("DETECTED");
  } else {
    display.println("Clear");
  }

  display.setCursor(0, 46);
  display.print("Button: ");

  if (stableButtonState == LOW) {
    display.println("Pressed");
  } else {
    display.println("Released");
  }

  if (displayRequestedByButton) {
    const int32_t remainingMs =
        static_cast<int32_t>(displayOffTime - millis());

    uint32_t remainingSeconds = 0;

    if (remainingMs > 0) {
      remainingSeconds =
          (static_cast<uint32_t>(remainingMs) + 999) / 1000;
    }

    display.setCursor(0, 56);
    display.print("Screen off: ");
    display.print(remainingSeconds);
    display.print("s");
  }

  display.display();
}

void drawFactoryResetScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("Factory Reset");

  display.drawLine(
      0,
      11,
      OLED_WIDTH - 1,
      11,
      SSD1306_WHITE
  );

  display.setCursor(0, 21);
  display.println("Clearing Zigbee");

  display.setCursor(0, 34);
  display.println("network settings...");

  display.setCursor(0, 50);
  display.println("Restarting");

  display.display();
}

void drawDisplay() {
  if (!displayInitialized || !displayEnabled) {
    return;
  }

  switch (deviceState) {
    case DeviceState::WAITING_FOR_ZIGBEE:
      drawConnectingScreen();
      break;

    case DeviceState::PIR_WARMUP:
      drawWarmupScreen();
      break;

    case DeviceState::NORMAL_OPERATION:
      drawNormalScreen();
      break;
  }
}

void initializeDisplay() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  if (!display.begin(
          SSD1306_SWITCHCAPVCC,
          OLED_ADDRESS
      )) {
    Serial.println("SSD1306 initialization failed.");
    displayInitialized = false;
    return;
  }

  displayInitialized = true;

  display.clearDisplay();
  display.display();

  /*
   * Keep the display on during initial Zigbee commissioning.
   */
  turnDisplayOn();
  drawConnectingScreen();

  Serial.println("SSD1306 initialized.");
}

void performFactoryReset() {
  factoryResetTriggered = true;

  Serial.println();
  Serial.println("External button held for 3 seconds.");
  Serial.println("Factory-resetting Zigbee network settings...");
  Serial.flush();

  turnDisplayOn();
  drawFactoryResetScreen();

  delay(1000);

  /*
   * true requests a restart after clearing Zigbee settings.
   */
  Zigbee.factoryReset(true);

  /*
   * Remain here if the automatic restart is delayed.
   */
  while (true) {
    delay(1000);
  }
}

void handleExternalButton() {
  const bool rawButtonState =
      digitalRead(EXTERNAL_BUTTON_PIN);

  if (rawButtonState != lastRawButtonState) {
    lastRawButtonState = rawButtonState;
    lastButtonChangeTime = millis();
  }

  if (millis() - lastButtonChangeTime <
      BUTTON_DEBOUNCE_MS) {
    return;
  }

  if (rawButtonState != stableButtonState) {
    stableButtonState = rawButtonState;

    if (stableButtonState == LOW) {
      /*
       * Button has just been pressed.
       */
      buttonPressStartTime = millis();
      buttonPressHandled = false;
      factoryResetTriggered = false;

      /*
       * Wake immediately so the user receives feedback.
       */
      turnDisplayOn();
    } else {
      /*
       * Button has just been released.
       */
      if (!buttonPressHandled &&
          !factoryResetTriggered) {
        wakeDisplayForButtonPress();
      }

      buttonPressHandled = false;
      factoryResetTriggered = false;
    }
  }

  /*
   * Trigger factory reset while the button is still held.
   */
  if (stableButtonState == LOW &&
      !buttonPressHandled &&
      millis() - buttonPressStartTime >=
          FACTORY_RESET_HOLD_MS) {

    buttonPressHandled = true;
    performFactoryReset();
  }
}

void handleZigbeeStatus() {
  zigbeeConnected = Zigbee.connected();

  if (zigbeeConnected && !previouslyConnected) {
    Serial.println();
    Serial.println("Connected to Zigbee network.");
    Serial.println("Starting AM312 warm-up period.");

    deviceState = DeviceState::PIR_WARMUP;

    pirWarmupStartTime = millis();
    pirReadyTime = millis() + PIR_WARMUP_TIME_MS;

    initialOccupancyReported = false;

    /*
     * Keep the OLED awake during warm-up.
     */
    displayRequestedByButton = false;
    turnDisplayOn();
    drawWarmupScreen();
  }

  if (!zigbeeConnected && previouslyConnected) {
    Serial.println();
    Serial.println("Zigbee connection lost.");

    deviceState = DeviceState::WAITING_FOR_ZIGBEE;

    displayRequestedByButton = false;
    turnDisplayOn();
    drawConnectingScreen();
  }

  previouslyConnected = zigbeeConnected;

  if (!zigbeeConnected &&
      millis() - lastZigbeeStatusTime >=
          ZIGBEE_STATUS_INTERVAL_MS) {

    lastZigbeeStatusTime = millis();
    Serial.print(".");
  }
}

void handleWarmup() {
  if (deviceState != DeviceState::PIR_WARMUP) {
    return;
  }

  if (static_cast<int32_t>(
          millis() - pirReadyTime
      ) < 0) {
    return;
  }

  deviceState = DeviceState::NORMAL_OPERATION;

  Serial.println();
  Serial.println("AM312 warm-up complete.");
  Serial.println("Turning OLED off.");

  /*
   * Read and report the sensor's initial state.
   */
  currentOccupancy =
      digitalRead(AM312_PIN) == HIGH;

  previousOccupancy = currentOccupancy;

  zbOccupancySensor.setOccupancy(currentOccupancy);

  const bool reportSucceeded =
      zbOccupancySensor.report();

  initialOccupancyReported = true;

  Serial.print("Initial occupancy: ");
  Serial.println(
      currentOccupancy ? "Occupied" : "Clear"
  );

  if (!reportSucceeded) {
    Serial.println(
        "Warning: initial occupancy report failed."
    );
  }

  /*
   * The display remains off until a brief button press.
   */
  turnDisplayOff();
}

void handleOccupancySensor() {
  currentOccupancy =
      digitalRead(AM312_PIN) == HIGH;

  if (!zigbeeConnected) {
    return;
  }

  if (deviceState != DeviceState::NORMAL_OPERATION) {
    return;
  }

  if (!initialOccupancyReported) {
    return;
  }

  if (currentOccupancy == previousOccupancy) {
    return;
  }

  previousOccupancy = currentOccupancy;

  zbOccupancySensor.setOccupancy(currentOccupancy);

  const bool reportSucceeded =
      zbOccupancySensor.report();

  Serial.print(
      currentOccupancy
          ? "Motion detected - reporting occupied"
          : "Motion cleared - reporting unoccupied"
  );

  Serial.println(
      reportSucceeded ? "." : " failed."
  );
}

void handleDisplayTimeout() {
  /*
   * Never shut the display off while commissioning or warming up.
   */
  if (deviceState != DeviceState::NORMAL_OPERATION) {
    return;
  }

  if (!displayRequestedByButton ||
      !displayEnabled) {
    return;
  }

  if (static_cast<int32_t>(
          millis() - displayOffTime
      ) >= 0) {

    Serial.println("Display timeout reached.");
    turnDisplayOff();
  }
}

void handleDisplayRefresh() {
  if (!displayEnabled) {
    return;
  }

  if (millis() - lastDisplayRefreshTime <
      DISPLAY_REFRESH_INTERVAL_MS) {
    return;
  }

  lastDisplayRefreshTime = millis();
  drawDisplay();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(EXTERNAL_BUTTON_PIN, INPUT_PULLUP);
  pinMode(AM312_PIN, INPUT);

  initializeDisplay();

  zbOccupancySensor.setManufacturerAndModel(
      "CharlesForsythDesign",
      "AM312-PIR-Sensor"
  );

  Zigbee.addEndpoint(&zbOccupancySensor);

  Serial.println();
  Serial.println("Starting Zigbee...");

  if (!Zigbee.begin()) {
    Serial.println("Zigbee failed to start.");
    Serial.println("Restarting...");
    delay(1000);
    ESP.restart();
  }

  Serial.println("Zigbee stack started.");
  Serial.println("Waiting to join a Zigbee network.");
  Serial.println("Open ZHA Add Device, then reset the ESP32-H2.");

  deviceState = DeviceState::WAITING_FOR_ZIGBEE;

  turnDisplayOn();
  drawConnectingScreen();
}

void loop() {
  handleExternalButton();
  handleZigbeeStatus();
  handleWarmup();
  handleOccupancySensor();
  handleDisplayTimeout();
  handleDisplayRefresh();

  delay(10);
}
