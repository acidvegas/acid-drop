#pragma once

#include <Arduino.h>
#include <lvgl.h>

// The bar across the top: clock on the left, radio and battery indicators on
// the right. Display only - it takes no input, so it cannot swallow presses
// meant for the controls beneath it.

namespace statusbar {

constexpr int32_t kHeight = 26;

void create(lv_obj_t* parent);
void tick();                       // refresh the values, cheap to call often

// Shown in the middle of the bar, e.g. the IRC connection state.
void setTitle(const String& text);

// A dot next to the clock when something wants attention.
void setNotification(bool pending);

lv_obj_t* object();

} // namespace statusbar
