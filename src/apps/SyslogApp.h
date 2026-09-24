#pragma once

#include <lvgl.h>

namespace syslogapp {

void create(lv_obj_t* parent);
void destroy();
void tick();

} // namespace syslogapp
