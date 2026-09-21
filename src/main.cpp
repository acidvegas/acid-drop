// ACID DROP - custom firmware for the LilyGo T-Deck Plus
// https://github.com/acidvegas/acid-drop

#include <Arduino.h>
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
uint32_t s_logoShownAt = 0;

void drawBootLogo() {
    gfx.fillScreen(0x0000);

    const int32_t x = (BOARD_TFT_WIDTH  - logo_width)  / 2;
    const int32_t y = (BOARD_TFT_HEIGHT - logo_height) / 2;

    gfx.drawXBitmap(x, y, logo_bits, logo_width, logo_height, 0x35E0 /* acid green */);
    display::setBrightness(settings::getInt("brightness"));
    s_logoShownAt = millis();
}

// Everything on the shared SPI2 bus has to be deselected before the bus is
// used, or a device with a floating chip-select will sample traffic meant for
// another one as its own commands. The LoRa radio matters most here: it is
// disabled by default, so nothing else ever touches its CS line.
void deselectSpiDevices() {
    pinMode(BOARD_SDCARD_CS, OUTPUT);
    digitalWrite(BOARD_SDCARD_CS, HIGH);

    pinMode(RADIO_CS_PIN, OUTPUT);
    digitalWrite(RADIO_CS_PIN, HIGH);
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
    logging::setKeepHistory(settings::getBool("log_screen"));

    if (!display::begin()) {
        LOG_E(TAG, "display init failed - halting");
        while (true) delay(1000);
    }

    checkRecoveryKey();
    drawBootLogo();

    // Park every chip-select on the shared bus. The SD card and the LoRa radio
    // are both brought up lazily, so nothing else touches SPI during boot.
    bootStage("spi");
    deselectSpiDevices();

    bootStage("power");   power::begin();
    bootStage("audio");   audio::begin();
    bootStage("input");   input::begin();
    bootStage("gps");     gps::begin();
    bootStage("lora");    radio::begin();
    bootStage("bluetooth"); ble::begin();
    bootStage("wifi");    net::begin();

    bootStage("sound");
    audio::alert(Alert::Boot);

    // Hold the splash until the jingle has finished, so the two land together.
    // audio::loop() has to be pumped here: nothing else is running yet, and
    // without it the tune would never advance and isPlaying() would never
    // clear. Capped so a stuck decoder cannot hold up the boot.
    const uint32_t splashMs = settings::getInt("splash_ms");
    const uint32_t splashCap = 20000;
    while (millis() - s_logoShownAt < splashCap) {
        audio::loop();
        const bool minimumMet = millis() - s_logoShownAt >= splashMs;
        if (minimumMet && !audio::isPlaying()) break;
        delay(5);
    }

    LOG_I(TAG, "splash held %lu ms", (unsigned long)(millis() - s_logoShownAt));

    bootStage("ui");
    ui::begin();

    // Draw the first frame here rather than waiting for loop(). If anything
    // later stalls, the UI is already on screen instead of the boot logo.
    bootStage("running");
    lv_refr_now(nullptr);

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
