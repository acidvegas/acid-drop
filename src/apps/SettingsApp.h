#pragma once

#include <lvgl.h>

namespace settingsapp {

// The Settings app shows everything that is not IRC; the IRC app opens the
// same screen scoped to its own sections.
void create(lv_obj_t* parent);
void createIrc(lv_obj_t* parent);
void destroy();
void tick();

// Returns true when it consumed the back action by leaving a section, false
// when the app itself should close.
bool handleBack();

} // namespace settingsapp
