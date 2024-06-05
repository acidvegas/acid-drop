#pragma once


// Board pin definitions ------------------------
#define BOARD_POWERON       10

// Speaker
#define BOARD_I2S_WS        5
#define BOARD_I2S_DOUT      6
#define BOARD_I2S_BCK       7

#define BOARD_I2C_SDA       18
#define BOARD_I2C_SCL       8

#define BOARD_BAT_ADC       4

#define BOARD_TOUCH_INT     16
#define BOARD_KEYBOARD_INT  46

#define BOARD_SDCARD_CS     39
#define BOARD_TFT_CS        12
#define RADIO_CS_PIN        9

#define BOARD_TFT_DC        11
#define BOARD_TFT_BACKLIGHT 42

#define BOARD_SPI_MOSI      41
#define BOARD_SPI_MISO      38
#define BOARD_SPI_SCK       40

#define BOARD_TBOX_G02      2
#define BOARD_TBOX_G01      3
#define BOARD_TBOX_G04      1
#define BOARD_TBOX_G03      15

#define BOARD_ES7210_MCLK   48
#define BOARD_ES7210_LRCK   21
#define BOARD_ES7210_SCK    47
#define BOARD_ES7210_DIN    14

#define RADIO_BUSY_PIN      13
#define RADIO_RST_PIN       17
#define RADIO_DIO1_PIN      45

#define BOARD_BOOT_PIN      0

#define BOARD_BL_PIN        42

#define GPS_RX_PIN          44
#define GPS_TX_PIN          43


// Other definitions ----------------------------
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

// Battery definitions
#define CONV_FACTOR 1.8 // Conversion factor for the ADC to voltage conversion
#define READS       20  // Number of readings for averaging

#define LILYGO_KB_SLAVE_ADDRESS 0x55