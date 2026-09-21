#pragma once

#include <lvgl.h>

namespace launcher {

void create(lv_obj_t* parent);
void destroy();
void tick();

} // namespace launcher
