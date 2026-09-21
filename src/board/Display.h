#pragma once

#include <LovyanGFX.hpp>
#include <lvgl.h>

// LovyanGFX device for the T-Deck's ST7789 panel and its GT911 touch panel.
//
// LovyanGFX owns I2C port 0 and applies the display rotation to touch
// coordinates itself. Everything else on that bus - the keyboard, the boot
// recovery key - goes through lgfx::i2c rather than Arduino's Wire, because
// only one driver can own a port and a second owner does not fail loudly: it
// just makes the other side's transactions time out.
class AcidLGFX : public lgfx::LGFX_Device {
public:
    AcidLGFX();

private:
    lgfx::Panel_ST7789   _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Light_PWM      _light;
    lgfx::Touch_GT911    _touch;

public:
    // GT911 panels are strapped to 0x5D or 0x14 depending on the batch.
    void setTouchAddress(uint8_t address);
};

extern AcidLGFX gfx;

namespace display {

// Brings up the panel, allocates LVGL draw buffers and registers the display
// driver. Must run before any other LVGL call.
bool begin();

// 0-255. Ramps rather than stepping so it does not flash.
void setBrightness(uint8_t value);
uint8_t brightness();

// Backlight off but the panel and LVGL stay alive, so the UI keeps updating.
void sleep();
void wake();
bool isAwake();

lv_display_t* lvDisplay();

} // namespace display
