#pragma once

#include <lvgl.h>

namespace settingsapp {

// One settings screen for the whole device. The IRC sections come first, and
// the screens that are not a list of values - channels, WiFi, the log, about -
// are reachable from it as shortcut rows.
void create(lv_obj_t* parent);
void destroy();
void tick();

// Returns true when it consumed the back action by leaving a section, false
// when the app itself should close.
bool handleBack();

} // namespace settingsapp
