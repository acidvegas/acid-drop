#include "board/Display.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include "board/pins.h"
#include "core/Log.h"

AcidLGFX gfx;

AcidLGFX::AcidLGFX() {
    {   // SPI bus. The bus is shared with the SD card and the LoRa radio, so
        // LovyanGFX has to re-assert the bus configuration on every transaction.
        auto cfg = _bus.config();
        cfg.spi_host   = SPI2_HOST;
        cfg.spi_mode   = 0;
        cfg.freq_write = 40000000;
        cfg.freq_read  = 16000000;
        cfg.spi_3wire  = false;
        cfg.use_lock   = true;
        cfg.dma_channel = SPI_DMA_CH_AUTO;
        cfg.pin_sclk   = BOARD_SPI_SCK;
        cfg.pin_mosi   = BOARD_SPI_MOSI;
        cfg.pin_miso   = BOARD_SPI_MISO;
        cfg.pin_dc     = BOARD_TFT_DC;
        _bus.config(cfg);
        _panel.setBus(&_bus);
    }

    {   // ST7789 panel, mounted rotated so the keyboard edge is the bottom.
        auto cfg = _panel.config();
        cfg.pin_cs          = BOARD_TFT_CS;
        cfg.pin_rst         = -1;
        cfg.pin_busy        = -1;
        cfg.panel_width     = 240;
        cfg.panel_height    = 320;
        cfg.offset_x        = 0;
        cfg.offset_y        = 0;
        // Stays 0: this is an offset ADDED to setRotation(), so setting it
        // here as well would rotate twice and leave LVGL and the panel
        // disagreeing about which dimension is which.
        cfg.offset_rotation = 0;
        cfg.dummy_read_pixel = 8;
        cfg.dummy_read_bits  = 1;
        cfg.readable        = false;
        cfg.invert          = true;
        cfg.rgb_order       = false;
        cfg.dlen_16bit      = false;
        cfg.bus_shared      = true;
        _panel.config(cfg);
    }

    {   // PWM backlight.
        auto cfg = _light.config();
        cfg.pin_bl      = BOARD_TFT_BACKLIGHT;
        cfg.invert      = false;
        cfg.freq        = 12000;
        cfg.pwm_channel = 7;
        _light.config(cfg);
        _panel.setLight(&_light);
    }

    {   // GT911 capacitive touch on the shared I2C bus.
        auto cfg = _touch.config();
        cfg.x_min      = 0;
        cfg.x_max      = 239;
        cfg.y_min      = 0;
        cfg.y_max      = 319;
        cfg.pin_int    = BOARD_TOUCH_INT;
        cfg.bus_shared = true;
        cfg.offset_rotation = 0;
        cfg.i2c_port   = 0;
        cfg.i2c_addr   = TOUCH_I2C_ADDR_PRI;
        cfg.pin_sda    = BOARD_I2C_SDA;
        cfg.pin_scl    = BOARD_I2C_SCL;
        cfg.freq       = BOARD_I2C_FREQ;
        _touch.config(cfg);
        _panel.setTouch(&_touch);
    }

    setPanel(&_panel);
}

void AcidLGFX::setTouchAddress(uint8_t address) {
    auto cfg = _touch.config();
    cfg.i2c_addr = address;
    _touch.config(cfg);
}

namespace display {
namespace {

lv_display_t* s_disp       = nullptr;
uint8_t       s_brightness = 200;
bool          s_awake      = true;

// Two partial buffers, a tenth of the screen each. Internal DMA-capable RAM is
// required for SPI DMA; PSRAM buffers cannot be handed to the DMA engine.
constexpr uint32_t kBufLines  = BOARD_TFT_HEIGHT / 10;
constexpr uint32_t kBufPixels = BOARD_TFT_WIDTH * kBufLines;

void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;

    gfx.startWrite();
    gfx.setAddrWindow(area->x1, area->y1, w, h);
    // `true` byte-swaps on the fly: LVGL renders native-endian RGB565 while the
    // panel expects big-endian.
    gfx.writePixels(reinterpret_cast<uint16_t*>(px_map), w * h, true);
    gfx.endWrite();

    lv_display_flush_ready(disp);
}

uint32_t tickCb() {
    return millis();
}

} // namespace

namespace {

// Probes both addresses the GT911 is strapped to in the wild. Returns 0 when
// neither answers, in which case we leave the default and carry on without
// touch rather than refusing to boot.
uint8_t probeTouchAddress() {
    for (uint8_t address : {TOUCH_I2C_ADDR_PRI, TOUCH_I2C_ADDR_ALT}) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) return address;
    }
    return 0;
}

} // namespace

bool begin() {
    const uint8_t touchAddress = probeTouchAddress();
    if (touchAddress != 0) {
        gfx.setTouchAddress(touchAddress);
        LOG_I("display", "GT911 found at 0x%02X", touchAddress);
    } else {
        LOG_W("display", "no touch controller answered; touch will not work");
    }

    if (!gfx.init()) {
        LOG_E("display", "panel init failed");
        return false;
    }

    gfx.setRotation(1);
    gfx.setBrightness(0);   // Stay dark until the first frame is drawn
    gfx.fillScreen(0x0000);

    lv_init();
    lv_tick_set_cb(tickCb);

    const size_t bufBytes = kBufPixels * sizeof(uint16_t);
    void* buf1 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void* buf2 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    if (buf1 == nullptr) {
        LOG_E("display", "could not allocate LVGL draw buffer (%u bytes)", (unsigned)bufBytes);
        return false;
    }
    if (buf2 == nullptr) {
        LOG_W("display", "running with a single draw buffer");
    }

    s_disp = lv_display_create(BOARD_TFT_WIDTH, BOARD_TFT_HEIGHT);
    lv_display_set_flush_cb(s_disp, flushCb);
    lv_display_set_buffers(s_disp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    LOG_I("display", "ST7789 %dx%d up, %u byte draw buffers",
          BOARD_TFT_WIDTH, BOARD_TFT_HEIGHT, (unsigned)bufBytes);
    return true;
}

void setBrightness(uint8_t value) {
    s_brightness = value;
    if (s_awake) gfx.setBrightness(value);
}

uint8_t brightness() {
    return s_brightness;
}

void sleep() {
    if (!s_awake) return;
    s_awake = false;
    // Ramp down so the transition does not read as a glitch.
    for (int v = s_brightness; v >= 0; v -= 8) {
        gfx.setBrightness(v < 0 ? 0 : v);
        delay(4);
    }
    gfx.setBrightness(0);
}

void wake() {
    if (s_awake) return;
    s_awake = true;
    for (int v = 0; v <= s_brightness; v += 8) {
        gfx.setBrightness(v);
        delay(4);
    }
    gfx.setBrightness(s_brightness);
}

bool isAwake() {
    return s_awake;
}

lv_display_t* lvDisplay() {
    return s_disp;
}

} // namespace display
