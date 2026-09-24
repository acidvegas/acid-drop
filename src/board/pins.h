#pragma once

// LilyGo T-Deck / T-Deck Plus (ESP32-S3FN16R8)
//
// Only what this firmware actually drives is listed. The board also carries an
// L76K GNSS module, an SX1262 LoRa radio, an ES7210 microphone codec and a
// microSD slot; none of them is used, so their pins are not defined here. The
// two exceptions are the SD and LoRa chip-selects, which still have to be
// parked high so a floating select cannot make either device answer traffic
// meant for the display - see parkUnusedSpiDevices() in main.cpp.

// Power ------------------------------------------------------------------------------------------
#define BOARD_POWERON        10  // Master enable for the peripheral rail

// I2C bus (keyboard, touch) ----------------------------------------------------------------------
#define BOARD_I2C_SDA        18
#define BOARD_I2C_SCL        8
#define BOARD_I2C_FREQ       400000

#define KEYBOARD_I2C_ADDR    0x55
#define TOUCH_I2C_ADDR_PRI   0x5D  // GT911 default
#define TOUCH_I2C_ADDR_ALT   0x14  // GT911 alternate strap

// Display (ST7789 over SPI) ----------------------------------------------------------------------
#define BOARD_TFT_CS         12
#define BOARD_TFT_DC         11
#define BOARD_TFT_BACKLIGHT  42
#define BOARD_TFT_WIDTH      320
#define BOARD_TFT_HEIGHT     240

// Shared SPI bus (display, SD card, LoRa) --------------------------------------------------------
#define BOARD_SPI_SCK        40
#define BOARD_SPI_MOSI       41
#define BOARD_SPI_MISO       38

// Touch (GT911) ----------------------------------------------------------------------------------
#define BOARD_TOUCH_INT      16

// Keyboard ---------------------------------------------------------------------------------------
#define BOARD_KEYBOARD_INT   46

// Trackball --------------------------------------------------------------------------------------
#define BOARD_TRACKBALL_UP    3
#define BOARD_TRACKBALL_DOWN  15
#define BOARD_TRACKBALL_LEFT  1
#define BOARD_TRACKBALL_RIGHT 2
#define BOARD_TRACKBALL_CLICK 0

// SD card ----------------------------------------------------------------------------------------
#define BOARD_SDCARD_CS      39

// LoRa (SX1262) - chip-select only, purely so it can be parked high ------------------------------
#define RADIO_CS_PIN         9

// Audio out (MAX98357 I2S) -----------------------------------------------------------------------
#define BOARD_I2S_WS         5
#define BOARD_I2S_DOUT       6
#define BOARD_I2S_BCK        7

// Battery ----------------------------------------------------------------------------------------
#define BOARD_BAT_ADC        4
#define BATTERY_CONV_FACTOR  2.0f   // On-board 100k/100k divider
#define BATTERY_SAMPLES      16
#define BATTERY_MIN_MV       3300
#define BATTERY_MAX_MV       4200
