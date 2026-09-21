#pragma once

#include <Arduino.h>

// GT911 capacitive touch, driven over the Arduino Wire bus.
//
// This deliberately does not use LovyanGFX's built-in touch support. That would
// have LovyanGFX configure the I2C peripheral itself, and the keyboard already
// owns the same port and the same pins through Wire. Only one peripheral can
// drive a pad, so the second one to initialise wins and every transaction the
// other makes times out - which shows up as a laggy, unresponsive UI rather
// than as anything that looks like an I2C fault.

namespace touch {

// Probes both addresses the GT911 ships strapped to. Returns false when
// neither answers, in which case read() always reports no touch.
bool begin();
bool present();
uint8_t address();

// Screen coordinates, already mapped for the display's rotation.
bool read(int16_t& x, int16_t& y);

// Mirrors the mapping when the display is mounted upside down.
void setFlipped(bool flipped);

// The controller's own reading, before any rotation is applied. Used by the
// calibration screen to work out which mapping is correct by measurement
// rather than by assuming one.
bool readRaw(uint16_t& rawX, uint16_t& rawY);

// Applies mapping `index` (0-3) to a raw reading.
void applyMapping(uint8_t index, uint16_t rawX, uint16_t rawY, int16_t& x, int16_t& y);
constexpr uint8_t kMappingCount = 4;

} // namespace touch
