#pragma once

#include <Arduino.h>

// Battery reading, CPU speed and the inactivity ladder that dims and then
// blanks the screen.

namespace power {

void begin();
void loop();

// Re-reads brightness, timeout and CPU settings.
void applySettings();

uint16_t batteryMillivolts();
uint8_t  batteryPercent();

// The T-Deck does not route the charger's status pin to the ESP32, so this is
// inferred from the pack sitting above its own float voltage. It is a hint for
// the status bar, not a fact.
bool probablyCharging();

// Forces the screen back on and restarts the inactivity timers.
void wake();

} // namespace power
