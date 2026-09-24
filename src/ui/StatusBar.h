#pragma once

#include <Arduino.h>
#include <lvgl.h>

// The bar across the top: clock on the left, a slot the active screen fills in
// the middle, radio and battery on the right.
//
// The middle slot is how the IRC window list gets here. It used to live in a
// second 30px bar of its own directly underneath this one, which cost two
// lines of backlog to show information this bar had room for.

namespace statusbar {

constexpr int32_t kHeight = 26;

void create(lv_obj_t* parent);
void tick();                       // refresh the values, cheap to call often

// A dot next to the clock when something wants attention.
void setNotification(bool pending);

// Re-applies theme colours to the bar, which is built once and never rebuilt.
void applyTheme();

// The middle of the bar, for the active screen to populate. It is emptied
// whenever a screen is torn down, so a screen that fills it must do so in its
// create() and never hold the pointers past destroy().
lv_obj_t* centerSlot();
void      clearCenterSlot();

lv_obj_t* object();

} // namespace statusbar
