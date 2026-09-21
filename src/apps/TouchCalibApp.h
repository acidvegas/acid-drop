#pragma once

#include <lvgl.h>

namespace touchcalib {

void create(lv_obj_t* parent);
void destroy();
void tick();

} // namespace touchcalib
