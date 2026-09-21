#pragma once

#include <lvgl.h>

namespace channelsapp {

void create(lv_obj_t* parent);
void destroy();
void tick();

// True when it closed an editor instead of the whole app.
bool handleBack();

} // namespace channelsapp
