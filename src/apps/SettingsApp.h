#pragma once

#include <lvgl.h>

namespace settingsapp {

void create(lv_obj_t* parent);
void destroy();
void tick();

// Returns true when it consumed the back action by leaving a section, false
// when the app itself should close.
bool handleBack();

} // namespace settingsapp
