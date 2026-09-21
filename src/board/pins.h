#pragma once

// LilyGo T-Deck / T-Deck Plus (ESP32-S3FN16R8)
//
// The Plus adds an on-board L76K GNSS module on Serial1 and a larger battery.
// Everything else is pin-identical, so ACID_BOARD_TDECK_PLUS only gates GPS.

#ifndef ACID_BOARD_TDECK_PLUS
#define ACID_BOARD_TDECK_PLUS 1
#endif

// Power ------------------------------------------------------------------------------------------
#define BOARD_POWERON        10  // Master enable for the peripheral rail

// I2C bus (keyboard, touch, ES7210) --------------------------------------------------------------
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

// LoRa (SX1262) ----------------------------------------------------------------------------------
#define RADIO_CS_PIN         9
#define RADIO_BUSY_PIN       13
#define RADIO_RST_PIN        17
#define RADIO_DIO1_PIN       45
#define RADIO_FREQ_DEFAULT   915.0f

// GNSS (L76K, T-Deck Plus only) ------------------------------------------------------------------
#define BOARD_GPS_RX         44  // ESP32 RX  <- GPS TX
#define BOARD_GPS_TX         43  // ESP32 TX  -> GPS RX
#define BOARD_GPS_BAUD       9600

// Audio out (MAX98357 I2S) -----------------------------------------------------------------------
#define BOARD_I2S_WS         5
#define BOARD_I2S_DOUT       6
#define BOARD_I2S_BCK        7

// Audio in (ES7210) ------------------------------------------------------------------------------
#define BOARD_ES7210_MCLK    48
#define BOARD_ES7210_LRCK    21
#define BOARD_ES7210_SCK     47
#define BOARD_ES7210_DIN     14

// Battery ----------------------------------------------------------------------------------------
#define BOARD_BAT_ADC        4
#define BATTERY_CONV_FACTOR  2.0f   // On-board 100k/100k divider
#define BATTERY_SAMPLES      16
#define BATTERY_MIN_MV       3300
#define BATTERY_MAX_MV       4200
