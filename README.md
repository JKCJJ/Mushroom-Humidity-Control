# Mushroom Farm Humidity & Temperature Controller

An Arduino-based controller for a mushroom farm that monitors temperature and humidity with an AHT10 sensor, drives a humidifier and fan via relays, and provides a button-navigated menu on a 128x64 SSD1306 OLED display.

## Features

- **Live readings** of temperature and humidity (AHT10 sensor over I2C), refreshed every 2 seconds
- **OLED display** (SSD1306, 128x64) showing current readings, mode, device status, and the configured humidity range
- **Auto mode**: hysteresis-based control that turns the humidifier + fan on/off to keep humidity between a configurable low/high threshold
- **Manual mode**: toggle the humidifier and fan directly from the menu
- **4-button menu** (Menu / Up / Down / OK) for navigating settings with software debouncing
- **Persistent settings**: high/low humidity thresholds and auto/manual mode are saved to EEPROM and validated/restored on boot
- **I2C timeout protection**: `Wire.setWireTimeout()` auto-resets the bus if the AHT10 hangs, so a bad reading is skipped instead of freezing the loop
- **Menu auto-timeout**: returns to the main screen after 10 seconds of inactivity

## Hardware

| Component | Notes |
|---|---|
| Arduino (Uno/Nano/etc.) | Any AVR board with I2C should work |
| AHT10 temperature/humidity sensor | I2C |
| SSD1306 OLED, 128x64 | I2C, address `0x3C` |
| 4 push buttons | Menu, Up, Down, OK — wired to `INPUT_PULLUP` |
| 2-channel relay module | Drives humidifier and fan (active LOW) |

### Pin Map

| Function | Pin |
|---|---|
| Menu button | D2 |
| Up button | D3 |
| Down button | D4 |
| OK button | D5 |
| Fan relay | D7 |
| Humidifier relay | D8 |

## Dependencies (Arduino Libraries)

Install these via the Arduino IDE Library Manager:

- `Adafruit AHTX0`
- `Adafruit SSD1306`
- `Adafruit GFX Library`
- `Adafruit Unified Sensor`
- `Wire` and `EEPROM` (bundled with the Arduino core)

## Usage

1. Wire up the components per the pin map above.
2. Install the required libraries.
3. Flash `mushroom_aht10.ino` to the board.
4. On first boot, defaults are used (high: 85%, low: 80%, mode: auto) and saved to EEPROM.
5. Press **Menu** to enter settings, navigate with **Up/Down**, and confirm with **OK**.

## Default Settings

- High humidity threshold: 85%
- Low humidity threshold: 80%
- Mode: Auto
- Hysteresis: ±1% around the thresholds to prevent relay chatter
