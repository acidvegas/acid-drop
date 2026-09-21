#pragma once

#include <Arduino.h>
#include <lvgl.h>

// The quick settings panel that pulls down from the status bar: radio toggles,
// brightness and volume sliders, a status readout and a few shortcuts.

namespace shade {

void create();
void tick();                 // refresh toggle states and the readout

void open();
void close();
void toggle();
bool isOpen();

// Drag handling, driven by the status bar's pointer events.
void beginDrag();
void dragTo(int32_t offsetY);   // offset from where the drag started
void endDrag();

} // namespace shade
