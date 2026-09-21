#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "board/Display.h"
#include "board/pins.h"
#include "core/Log.h"
#include "core/Settings.h"

LV_FONT_DECLARE(acid_mono_10);
LV_FONT_DECLARE(acid_mono_15);

void setup() {
    logging::begin(115200);
    delay(200);
    LOG_I("boot", "ACID DROP starting");

    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    delay(50);

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ);

    settings::begin();

    if (!display::begin()) {
        LOG_E("boot", "display failed, halting");
        return;
    }

    lv_obj_t* label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(label, &acid_mono_10, 0);
    lv_label_set_text(label, "ACID DROP\n\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90\n\xe2\x96\x88\xe2\x96\x93\xe2\x96\x92\xe2\x96\x91");
    lv_obj_center(label);

    display::setBrightness(settings::getInt("brightness"));
}

void loop() {
    lv_timer_handler();
    delay(5);
}
