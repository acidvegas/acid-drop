// ACID DROP - custom firmware for the LilyGo T-Deck Plus
// https://github.com/acidvegas/acid-drop

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <lvgl.h>

#include "board/Audio.h"
#include "board/Ble.h"
#include "board/Display.h"
#include "board/Gps.h"
#include "board/Input.h"
#include "board/Power.h"
#include "board/Radio.h"
#include "board/pins.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "net/WifiService.h"
#include "ui/BootLogo.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace {

constexpr const char* TAG = "boot";

// Boot progress, written to the panel as well as the log.
//
// Serial is not dependable on this board: the S3's USB-JTAG maps DTR and RTS
// onto the boot strapping pins, so attaching a monitor tends to reset the
// device into ROM download mode instead of showing you the log. The display is
// already up by the time anything interesting can fail, so it is the more
// reliable instrument - if the device wedges, the last stage it printed is
// still on screen.
void bootStage(const char* name) {
    LOG_I(TAG, "stage: %s", name);

    gfx.fillRect(0, BOARD_TFT_HEIGHT - 16, BOARD_TFT_WIDTH, 16, 0x0000);
    gfx.setTextSize(1);
    gfx.setTextColor(0x35E0, 0x0000);
    gfx.drawString(name, 4, BOARD_TFT_HEIGHT - 13);
}

// The XBM logo is 1bpp, so it is drawn straight to the panel before LVGL takes
// over the framebuffer.
void drawBootLogo() {
    gfx.fillScreen(0x0000);

    const int32_t x = (BOARD_TFT_WIDTH  - logo_width)  / 2;
    const int32_t y = (BOARD_TFT_HEIGHT - logo_height) / 2;

    gfx.drawXBitmap(x, y, logo_bits, logo_width, logo_height, 0x35E0 /* acid green */);
    display::setBrightness(settings::getInt("brightness"));
}

bool mountSdCard() {
    pinMode(BOARD_SDCARD_CS, OUTPUT);
    digitalWrite(BOARD_SDCARD_CS, HIGH);

    // The card shares SPI2 with the display, which LovyanGFX has already set up.
    if (!SD.begin(BOARD_SDCARD_CS, SPI, 4000000U)) {
        LOG_I(TAG, "no SD card");
        return false;
    }

    LOG_I(TAG, "SD card mounted, %llu MB", SD.cardSize() / (1024ULL * 1024ULL));
    return true;
}

// Holding a key during boot is the escape hatch when a setting has made the
// device unusable.
void checkRecoveryKey() {
    delay(120);   // let the keyboard controller come up

    Wire.requestFrom(static_cast<uint8_t>(KEYBOARD_I2C_ADDR), static_cast<uint8_t>(1));
    if (!Wire.available()) return;

    const int key = Wire.read();
    if (key != 'w' && key != 'W') return;

    LOG_W(TAG, "recovery key held, erasing settings");
    gfx.fillScreen(0x0000);
    gfx.setTextColor(0xF800);
    gfx.setTextSize(2);
    gfx.drawString("SETTINGS ERASED", 40, 110);
    settings::factoryReset();
    delay(1500);
    ESP.restart();
}

} // namespace

void setup() {
    logging::begin(115200);
    delay(150);
    LOG_I(TAG, "ACID DROP starting");

    // Peripheral power rail first; nothing else on the board answers without it.
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    delay(60);

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ);

    settings::begin();
    logging::setLevel(static_cast<LogLevel>(settings::getEnum("log_level")));

    if (!display::begin()) {
        LOG_E(TAG, "display init failed - halting");
        while (true) delay(1000);
    }

    checkRecoveryKey();
    drawBootLogo();

    bootStage("spi");
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    bootStage("sd card");
    mountSdCard();

    bootStage("power");   power::begin();
    bootStage("audio");   audio::begin();
    bootStage("input");   input::begin();
    bootStage("gps");     gps::begin();
    bootStage("lora");    radio::begin();
    bootStage("bluetooth"); ble::begin();
    bootStage("wifi");    net::begin();

    bootStage("sound");
    audio::alert(Alert::Boot);
    delay(700);          // let the logo and the jingle land

    bootStage("ui");
    ui::begin();

    bootStage("running");

    LOG_I(TAG, "boot complete, %u KB heap free", ESP.getFreeHeap() / 1024);
}

void loop() {
    // Drivers first, so LVGL sees this frame's input.
    input::loop();
    power::loop();
    audio::loop();
    gps::loop();
    radio::loop();
    net::loop();

    ui::loop();

    // lv_timer_handler returns how long it can safely be left alone.
    const uint32_t idleMs = lv_timer_handler();
    delay(idleMs > 20 ? 20 : (idleMs < 2 ? 2 : idleMs));
}
