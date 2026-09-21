#pragma once

#include <Arduino.h>

// Bluetooth LE. The controller is only brought up when it is switched on,
// because the stack costs tens of kilobytes of RAM that IRC scrollback would
// rather have.

namespace ble {

void begin();

void setEnabled(bool enabled);
bool enabled();
bool connected();

String advertisedName();

} // namespace ble
