#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

// OLED configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// AHT10 configuration
Adafruit_AHTX0 aht;

// Pin definitions
#define MENU_BUTTON 2
#define UP_BUTTON 3
#define DOWN_BUTTON 4
#define OK_BUTTON 5
#define HUMIDIFIER_RELAY 8
#define FAN_RELAY 7

// EEPROM addresses
#define EEPROM_HIGH_LEVEL 0
#define EEPROM_LOW_LEVEL 1
#define EEPROM_MODE 2

// System variables
float temperature, humidity;
int highLevel = 85; // Default high humidity level (85% for mushrooms)
int lowLevel = 80;  // Default low humidity level (80% for mushrooms)
bool autoMode = true; // Default to Auto mode
unsigned long lastInteraction = 0;
const unsigned long TIMEOUT = 10000; // 10 seconds
int interfaceState = 0; // 0: Main, 1: Humidity Level, 2: High Level, 3: Mode, 4: Low Level, 5: Manual Control
int humiditySelection = 0; // 0: High, 1: Low, 2: Mode
int manualSelection = 0; // 0: Humidifier, 1: Fan (for manual control screen)

// Device status
bool humidifierActive = false;
bool fanActive = false;
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 2000; // Read sensor every 2 seconds

// Button debouncing variables
unsigned long lastMenuPress = 0;
unsigned long lastUpPress = 0;
unsigned long lastDownPress = 0;
unsigned long lastRightPress = 0;
const unsigned long DEBOUNCE_DELAY = 200;

// Button state tracking for robust debouncing
bool menuButtonState = HIGH;
bool lastMenuButtonReading = HIGH;
unsigned long lastMenuDebounceTime = 0;
bool upButtonState = HIGH;
bool lastUpButtonReading = HIGH;
unsigned long lastUpDebounceTime = 0;
bool downButtonState = HIGH;
bool lastDownButtonReading = HIGH;
unsigned long lastDownDebounceTime = 0;
bool rightButtonState = HIGH;
bool lastRightButtonReading = HIGH;
unsigned long lastRightDebounceTime = 0;

void setup() {
  Serial.begin(9600);
  Serial.println(F("Mushroom Humidity Control Starting..."));

  // Initialize I2C with a timeout so a stuck sensor/bus can't hang the whole program.
  // If a transaction doesn't complete within 25ms, the Wire hardware is reset automatically.
  Wire.begin();
  Wire.setWireTimeout(25000, true); // 25000 us = 25 ms, resetOnTimeout = true

  // Initialize pins
  pinMode(MENU_BUTTON, INPUT_PULLUP);
  pinMode(UP_BUTTON, INPUT_PULLUP);
  pinMode(DOWN_BUTTON, INPUT_PULLUP);
  pinMode(OK_BUTTON, INPUT_PULLUP);
  pinMode(HUMIDIFIER_RELAY, OUTPUT);
  pinMode(FAN_RELAY, OUTPUT);
  
  // Initialize relays (assuming active LOW)
  digitalWrite(HUMIDIFIER_RELAY, HIGH);
  digitalWrite(FAN_RELAY, HIGH);

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Initializing..."));
  display.display();
  delay(1000);

  // Initialize AHT10
  if (!aht.begin()) {
    Serial.println(F("Could not find a valid AHT10 sensor, check wiring!"));
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("AHT10 not found!"));
    display.println(F("Check wiring"));
    display.display();
    for(;;);
  }

  // Load settings from EEPROM
  loadSettings();
  
  Serial.println(F("System initialized successfully"));
  Serial.print(F("High Level: ")); Serial.println(highLevel);
  Serial.print(F("Low Level: ")); Serial.println(lowLevel);
  Serial.print(F("Auto Mode: ")); Serial.println(autoMode ? "ON" : "OFF");
}

void loop() {
  // Read sensor data periodically
  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
    readSensorData();
    lastSensorRead = millis();
  }

  // Handle button inputs
  handleButtonInputs();

  // Display appropriate interface
  handleDisplay();

  // Control devices in auto mode
  if (autoMode) {
    controlDevices();
  }

  delay(50); // Small delay for stability
}

void readSensorData() {
  sensors_event_t humidityEvent, tempEvent;
  aht.getEvent(&humidityEvent, &tempEvent);

  // If the I2C transaction above timed out, Wire will have auto-reset the bus
  // (see Wire.setWireTimeout in setup()). Detect that here so we skip this
  // reading instead of freezing or using garbage data.
  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    Serial.println(F("I2C timeout during AHT10 read - bus reset, skipping this reading"));
    return;
  }

  temperature = tempEvent.temperature;
  humidity = humidityEvent.relative_humidity;

  // Check for valid readings
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println(F("Failed to read from AHT10 sensor!"));
    return;
  }
  
  // Print to serial for debugging
  Serial.print(F("Temperature: ")); Serial.print(temperature); Serial.println(F("°C"));
  Serial.print(F("Humidity: ")); Serial.print(humidity); Serial.println(F("%"));
}

void handleButtonInputs() {
  unsigned long currentMillis = millis();

  // Log raw button states for debugging
  Serial.print(F("Button States - Menu: ")); Serial.print(digitalRead(MENU_BUTTON));
  Serial.print(F(" Up: ")); Serial.print(digitalRead(UP_BUTTON));
  Serial.print(F(" Down: ")); Serial.print(digitalRead(DOWN_BUTTON));
  Serial.print(F(" Right: ")); Serial.println(digitalRead(OK_BUTTON));

  // Handle Menu button with state-based debouncing
  bool menuReading = digitalRead(MENU_BUTTON);
  if (menuReading != lastMenuButtonReading) {
    lastMenuDebounceTime = currentMillis;
  }
  if (currentMillis - lastMenuDebounceTime > DEBOUNCE_DELAY) {
    if (menuReading != menuButtonState) {
      menuButtonState = menuReading;
      if (menuButtonState == LOW && currentMillis - lastMenuPress > DEBOUNCE_DELAY) {
        interfaceState = (interfaceState == 0) ? 1 : 0; // Toggle between Main and Humidity Level
        lastInteraction = currentMillis;
        lastMenuPress = currentMillis;
        Serial.print(F("Menu pressed, state: ")); Serial.println(interfaceState);
      }
    }
  }
  lastMenuButtonReading = menuReading;

  // Handle other buttons based on current state
  if (interfaceState > 0) {
    lastInteraction = currentMillis; // Reset timeout on any valid interaction
    
    if (interfaceState == 1) {
      handleHumidityLevelButtons(currentMillis);
    } else if (interfaceState == 2) {
      handleHighLevelButtons(currentMillis);
    } else if (interfaceState == 3) {
      handleModeButtons(currentMillis);
    } else if (interfaceState == 4) {
      handleLowLevelButtons(currentMillis);
    } else if (interfaceState == 5) {
      handleManualControlButtons(currentMillis);
    }
  }
}

void handleHumidityLevelButtons(unsigned long currentMillis) {
  bool upReading = digitalRead(UP_BUTTON);
  if (upReading != lastUpButtonReading) {
    lastUpDebounceTime = currentMillis;
  }
  if (currentMillis - lastUpDebounceTime > DEBOUNCE_DELAY) {
    if (upReading != upButtonState) {
      upButtonState = upReading;
      if (upButtonState == LOW && currentMillis - lastUpPress > DEBOUNCE_DELAY) {
        humiditySelection = (humiditySelection > 0) ? humiditySelection - 1 : 2; // Cycle: High (0), Low (1), Mode (2)
        lastUpPress = currentMillis;
        Serial.print(F("Up pressed: Selection = ")); Serial.println(humiditySelection);
      }
    }
  }
  lastUpButtonReading = upReading;

  bool downReading = digitalRead(DOWN_BUTTON);
  if (downReading != lastDownButtonReading) {
    lastDownDebounceTime = currentMillis;
  }
  if (currentMillis - lastDownDebounceTime > DEBOUNCE_DELAY) {
    if (downReading != downButtonState) {
      downButtonState = downReading;
      if (downButtonState == LOW && currentMillis - lastDownPress > DEBOUNCE_DELAY) {
        humiditySelection = (humiditySelection < 2) ? humiditySelection + 1 : 0; // Cycle: High (0), Low (1), Mode (2)
        lastDownPress = currentMillis;
        Serial.print(F("Down pressed: Selection = ")); Serial.println(humiditySelection);
      }
    }
  }
  lastDownButtonReading = downReading;

  bool rightReading = digitalRead(OK_BUTTON);
  if (rightReading != lastRightButtonReading) {
    lastRightDebounceTime = currentMillis;
  }
  if (currentMillis - lastRightDebounceTime > DEBOUNCE_DELAY) {
    if (rightReading != rightButtonState) {
      rightButtonState = rightReading;
      if (rightButtonState == LOW && currentMillis - lastRightPress > DEBOUNCE_DELAY) {
        if (humiditySelection == 0) {
          interfaceState = 2; // Go to High Level
        } else if (humiditySelection == 1) {
          interfaceState = 4; // Go to Low Level
        } else if (humiditySelection == 2) {
          interfaceState = 3; // Go to Mode
        }
        lastRightPress = currentMillis;
        Serial.print(F("Right pressed, state: ")); Serial.println(interfaceState);
      }
    }
  }
  lastRightButtonReading = rightReading;
}

void handleHighLevelButtons(unsigned long currentMillis) {
  bool upReading = digitalRead(UP_BUTTON);
  if (upReading != lastUpButtonReading) {
    lastUpDebounceTime = currentMillis;
  }
  if (currentMillis - lastUpDebounceTime > DEBOUNCE_DELAY) {
    if (upReading != upButtonState) {
      upButtonState = upReading;
      if (upButtonState == LOW && currentMillis - lastUpPress > DEBOUNCE_DELAY) {
        highLevel = min(highLevel + 5, 95);
        if (highLevel <= lowLevel) highLevel = lowLevel + 5;
        lastUpPress = currentMillis;
        Serial.print(F("High level increased to: ")); Serial.println(highLevel);
      }
    }
  }
  lastUpButtonReading = upReading;

  bool downReading = digitalRead(DOWN_BUTTON);
  if (downReading != lastDownButtonReading) {
    lastDownDebounceTime = currentMillis;
  }
  if (currentMillis - lastDownDebounceTime > DEBOUNCE_DELAY) {
    if (downReading != downButtonState) {
      downButtonState = downReading;
      if (downButtonState == LOW && currentMillis - lastDownPress > DEBOUNCE_DELAY) {
        highLevel = max(highLevel - 5, lowLevel + 5);
        lastDownPress = currentMillis;
        Serial.print(F("High level decreased to: ")); Serial.println(highLevel);
      }
    }
  }
  lastDownButtonReading = downReading;

  bool rightReading = digitalRead(OK_BUTTON);
  if (rightReading != lastRightButtonReading) {
    lastRightDebounceTime = currentMillis;
  }
  if (currentMillis - lastRightDebounceTime > DEBOUNCE_DELAY) {
    if (rightReading != rightButtonState) {
      rightButtonState = rightReading;
      if (rightButtonState == LOW && currentMillis - lastRightPress > DEBOUNCE_DELAY) {
        EEPROM.update(EEPROM_HIGH_LEVEL, highLevel); // Save value
        interfaceState = 1; // Return to Humidity Level
        lastRightPress = currentMillis;
        Serial.println(F("Right pressed: Saved High Level, back to Humidity Level"));
      }
    }
  }
  lastRightButtonReading = rightReading;
}

void handleLowLevelButtons(unsigned long currentMillis) {
  bool upReading = digitalRead(UP_BUTTON);
  if (upReading != lastUpButtonReading) {
    lastUpDebounceTime = currentMillis;
  }
  if (currentMillis - lastUpDebounceTime > DEBOUNCE_DELAY) {
    if (upReading != upButtonState) {
      upButtonState = upReading;
      if (upButtonState == LOW && currentMillis - lastUpPress > DEBOUNCE_DELAY) {
        lowLevel = min(lowLevel + 5, highLevel - 5);
        lastUpPress = currentMillis;
        Serial.print(F("Low level increased to: ")); Serial.println(lowLevel);
      }
    }
  }
  lastUpButtonReading = upReading;

  bool downReading = digitalRead(DOWN_BUTTON);
  if (downReading != lastDownButtonReading) {
    lastDownDebounceTime = currentMillis;
  }
  if (currentMillis - lastDownDebounceTime > DEBOUNCE_DELAY) {
    if (downReading != downButtonState) {
      downButtonState = downReading;
      if (downButtonState == LOW && currentMillis - lastDownPress > DEBOUNCE_DELAY) {
        lowLevel = max(lowLevel - 5, 60);
        lastDownPress = currentMillis;
        Serial.print(F("Low level decreased to: ")); Serial.println(lowLevel);
      }
    }
  }
  lastDownButtonReading = downReading;

  bool rightReading = digitalRead(OK_BUTTON);
  if (rightReading != lastRightButtonReading) {
    lastRightDebounceTime = currentMillis;
  }
  if (currentMillis - lastRightDebounceTime > DEBOUNCE_DELAY) {
    if (rightReading != rightButtonState) {
      rightButtonState = rightReading;
      if (rightButtonState == LOW && currentMillis - lastRightPress > DEBOUNCE_DELAY) {
        EEPROM.update(EEPROM_LOW_LEVEL, lowLevel); // Save value
        interfaceState = 1; // Return to Humidity Level
        lastRightPress = currentMillis;
        Serial.println(F("Right pressed: Saved Low Level, back to Humidity Level"));
      }
    }
  }
  lastRightButtonReading = rightReading;
}

void handleModeButtons(unsigned long currentMillis) {
  bool upReading = digitalRead(UP_BUTTON);
  if (upReading != lastUpButtonReading) {
    lastUpDebounceTime = currentMillis;
  }
  if (currentMillis - lastUpDebounceTime > DEBOUNCE_DELAY) {
    if (upReading != upButtonState) {
      upButtonState = upReading;
      if (upButtonState == LOW && currentMillis - lastUpPress > DEBOUNCE_DELAY) {
        autoMode = true;
        lastUpPress = currentMillis;
        Serial.println(F("Up pressed: Selected Auto Mode"));
      }
    }
  }
  lastUpButtonReading = upReading;

  bool downReading = digitalRead(DOWN_BUTTON);
  if (downReading != lastDownButtonReading) {
    lastDownDebounceTime = currentMillis;
  }
  if (currentMillis - lastDownDebounceTime > DEBOUNCE_DELAY) {
    if (downReading != downButtonState) {
      downButtonState = downReading;
      if (downButtonState == LOW && currentMillis - lastDownPress > DEBOUNCE_DELAY) {
        autoMode = false;
        lastDownPress = currentMillis;
        Serial.println(F("Down pressed: Selected Manual Mode"));
      }
    }
  }
  lastDownButtonReading = downReading;

  bool rightReading = digitalRead(OK_BUTTON);
  if (rightReading != lastRightButtonReading) {
    lastRightDebounceTime = currentMillis;
  }
  if (currentMillis - lastRightDebounceTime > DEBOUNCE_DELAY) {
    if (rightReading != rightButtonState) {
      rightButtonState = rightReading;
      if (rightButtonState == LOW && currentMillis - lastRightPress > DEBOUNCE_DELAY) {
        EEPROM.update(EEPROM_MODE, autoMode); // Save mode
        interfaceState = autoMode ? 1 : 5; // Auto: back to Humidity Level, Manual: go to Manual Control
        lastRightPress = currentMillis;
        Serial.print(F("Right pressed: Saved Mode, state: ")); Serial.println(interfaceState);
      }
    }
  }
  lastRightButtonReading = rightReading;
}

void handleManualControlButtons(unsigned long currentMillis) {
  bool upReading = digitalRead(UP_BUTTON);
  if (upReading != lastUpButtonReading) {
    lastUpDebounceTime = currentMillis;
  }
  if (currentMillis - lastUpDebounceTime > DEBOUNCE_DELAY) {
    if (upReading != upButtonState) {
      upButtonState = upReading;
      if (upButtonState == LOW && currentMillis - lastUpPress > DEBOUNCE_DELAY) {
        manualSelection = (manualSelection == 0) ? 1 : 0; // Toggle between Humidifier (0) and Fan (1)
        lastUpPress = currentMillis;
        Serial.print(F("Up pressed: Manual Selection = ")); Serial.println(manualSelection);
      }
    }
  }
  lastUpButtonReading = upReading;

  bool downReading = digitalRead(DOWN_BUTTON);
  if (downReading != lastDownButtonReading) {
    lastDownDebounceTime = currentMillis;
  }
  if (currentMillis - lastDownDebounceTime > DEBOUNCE_DELAY) {
    if (downReading != downButtonState) {
      downButtonState = downReading;
      if (downButtonState == LOW && currentMillis - lastDownPress > DEBOUNCE_DELAY) {
        manualSelection = (manualSelection == 0) ? 1 : 0; // Toggle between Humidifier (0) and Fan (1)
        lastDownPress = currentMillis;
        Serial.print(F("Down pressed: Manual Selection = ")); Serial.println(manualSelection);
      }
    }
  }
  lastDownButtonReading = downReading;

  bool rightReading = digitalRead(OK_BUTTON);
  if (rightReading != lastRightButtonReading) {
    lastRightDebounceTime = currentMillis;
  }
  if (currentMillis - lastRightDebounceTime > DEBOUNCE_DELAY) {
    if (rightReading != rightButtonState) {
      rightButtonState = rightReading;
      if (rightButtonState == LOW && currentMillis - lastRightPress > DEBOUNCE_DELAY) {
        if (manualSelection == 0) {
          humidifierActive = !humidifierActive;
          setHumidifier(humidifierActive);
          Serial.print(F("Right pressed: Humidifier toggled to ")); Serial.println(humidifierActive ? "ON" : "OFF");
        } else {
          fanActive = !fanActive;
          setFan(fanActive);
          Serial.print(F("Right pressed: Fan toggled to ")); Serial.println(fanActive ? "ON" : "OFF");
        }
        lastRightPress = currentMillis;
      }
    }
  }
  lastRightButtonReading = rightReading;
}

void handleDisplay() {
  // Check for timeout in settings mode
  if (interfaceState > 0 && millis() - lastInteraction > TIMEOUT) {
    interfaceState = 0; // Return to main screen
    Serial.println(F("Timeout - returning to main screen"));
  }

  // Display appropriate screen
  switch (interfaceState) {
    case 0:
      displayMainScreen();
      break;
    case 1:
      displayHumidityLevel();
      break;
    case 2:
      displayHighLevel();
      break;
    case 3:
      displayMode();
      break;
    case 4:
      displayLowLevel();
      break;
    case 5:
      displayManualControl();
      break;
  }
}

void displayMainScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  // Temperature
  display.setCursor(5, 0);
  display.print(F("T:"));
  display.print(temperature, 1);
  display.print(F("C"));
  
  // Humidity
  display.setCursor(5, 20);
  display.print(F("H:"));
  display.print(humidity, 1);
  display.print(F("%"));
  
  // Status indicators
  display.setTextSize(1);
  display.setCursor(0, 45);
  display.print(F("Mode: "));
  display.print(autoMode ? F("AUTO") : F("MANUA"));
  
  display.setCursor(0, 55);
  display.print(F("H:"));
  display.print(humidifierActive ? F("ON") : F("OFF"));
  display.print(F(" F:"));
  display.print(fanActive ? F("ON") : F("OFF"));
  
  // Settings range
  display.setCursor(70, 45);
  display.print(F("Range:"));
  display.setCursor(70, 55);
  display.print(lowLevel);
  display.print(F("-"));
  display.print(highLevel);
  display.print(F("%"));
  
  display.display();
}

void displayHumidityLevel() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(10, 0);
  display.println(F("Settings"));
  
  display.setTextSize(1);
  display.setCursor(10, 25);
  display.print(F("High Level: "));
  display.print(highLevel);
  display.println(F("%"));
  if (humiditySelection == 0) {
    display.setCursor(0, 25);
    display.print(F(">"));
  }
  
  display.setCursor(10, 35);
  display.print(F("Low Level:  "));
  display.print(lowLevel);
  display.println(F("%"));
  if (humiditySelection == 1) {
    display.setCursor(0, 35);
    display.print(F(">"));
  }
  
  display.setCursor(10, 45);
  display.print(F("Mode"));
  if (humiditySelection == 2) {
    display.setCursor(0, 45);
    display.print(F(">"));
  }
  
  display.setCursor(0, 55);
  display.println(F("UP/DOWN:sele OK:conf"));
  
  display.display();
}

void displayHighLevel() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(10, 0);
  display.println(F("High L"));
  
  display.setCursor(10, 22);
  display.print(highLevel);
  display.println(F(" %"));
  
  display.setTextSize(1);
  display.setCursor(0, 45);
  display.println(F("UP/DOWN: adjust"));
  display.setCursor(0, 55);
  display.println(F("OK : save"));
  
  display.display();
}

void displayLowLevel() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(10, 0);
  display.println(F("Low L"));
  
  display.setCursor(10, 22);
  display.print(lowLevel);
  display.println(F(" %"));
  
  display.setTextSize(1);
  display.setCursor(0, 45);
  display.println(F("UP/DOWN: adjust"));
  display.setCursor(0, 55);
  display.println(F("OK : save"));
  
  display.display();
}

void displayMode() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(10, 0);
  display.println(F("Mode"));
  
  display.setTextSize(1);
  display.setCursor(10, 25);
  display.print(F("Mode: "));
  display.println(autoMode ? F("AUTO") : F("MANUAL"));
  
  display.setCursor(0, 45);
  display.println(F("UP: Auto  DOWN: Manua"));
  display.setCursor(0, 55);
  display.println(F("OK: confirm"));
  
  display.display();
}

void displayManualControl() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(10, 0);
  display.println(F("Manual"));
  
  display.setTextSize(1);
  display.setCursor(10, 20);
  display.print(F("Humidifier: "));
  display.println(humidifierActive ? F("ON") : F("OFF"));
  if (manualSelection == 0) {
    display.setCursor(0, 20);
    display.print(F(">"));
  }
  
  display.setCursor(10, 30);
  display.print(F("Fan: "));
  display.println(fanActive ? F("ON") : F("OFF"));
  if (manualSelection == 1) {
    display.setCursor(0, 30);
    display.print(F(">"));
  }
  
  display.setCursor(0, 45);
  display.println(F("UP/DOWN: sele OK:togg"));
  display.setCursor(0, 55);
  display.println(F("MENU : Save"));
  
  display.display();
}

void controlDevices() {
  if (!autoMode) return; // Only control in auto mode
  
  // Hysteresis control to prevent rapid switching
  static bool deviceState = false;
  
  // Control both humidifier and fan together
  if (humidity < lowLevel - 1) { // 2% hysteresis
    if (!deviceState) {
      setHumidifier(true);
      setFan(true);
      deviceState = true;
      Serial.println(F("Humidifier and Fan ON - humidity too low"));
    }
  } else if (humidity > highLevel + 1) {
    if (deviceState) {
      setHumidifier(false);
      setFan(false);
      deviceState = false;
      Serial.println(F("Humidifier and Fan OFF - humidity too high"));
    }
  }
}

void setHumidifier(bool state) {
  humidifierActive = state;
  digitalWrite(HUMIDIFIER_RELAY, state ? LOW : HIGH); // Assuming active LOW relay
}

void setFan(bool state) {
  fanActive = state;
  digitalWrite(FAN_RELAY, state ? LOW : HIGH); // Assuming active LOW relay
}

void loadSettings() {
  // Load settings from EEPROM with validation
  int savedHigh = EEPROM.read(EEPROM_HIGH_LEVEL);
  int savedLow = EEPROM.read(EEPROM_LOW_LEVEL);
  bool savedMode = EEPROM.read(EEPROM_MODE);
  
  // Validate settings
  if (savedHigh >= 65 && savedHigh <= 95 && 
      savedLow >= 60 && savedLow <= 90 && 
      savedLow < savedHigh) {
    highLevel = savedHigh;
    lowLevel = savedLow;
  } else {
    // Use defaults and save them
    highLevel = 85;
    lowLevel = 80;
    EEPROM.update(EEPROM_HIGH_LEVEL, highLevel);
    EEPROM.update(EEPROM_LOW_LEVEL, lowLevel);
  }
  
  // Load mode (0 or 1, anything else defaults to true)
  autoMode = (savedMode == 0) ? false : true;
  if (savedMode != 0 && savedMode != 1) {
    autoMode = true;
    EEPROM.update(EEPROM_MODE, autoMode);
  }
}
