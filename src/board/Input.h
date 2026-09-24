#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <functional>

// Three input devices feed LVGL:
//
//   touch     - GT911, read through LovyanGFX, drives an LVGL pointer
//   trackball - four GPIOs pulsed by the ball, mapped to arrow keys
//   keyboard  - the I2C keypad at 0x55, mapped to characters
//
// The trackball and the keyboard share one LVGL keypad device, so focus
// navigation and typing both work no matter which one the user reaches for.

namespace input {

// Called before LVGL consumes a key. Returning true swallows it, which is how
// the IRC app takes its shortcuts without the text area seeing them.
using KeyHook = std::function<bool(uint32_t key)>;

void begin();
void loop();


// The focus group every screen's widgets should join.
lv_group_t* group();

// Re-reads the trackball sensitivity from settings.
void applySettings();

// --- keyboard backlight ---------------------------------------------------
// The keyboard is not part of this firmware: it is a separate ESP32-C3 running
// LilyGo's own code, sitting on the I2C bus as a slave. It accepts two write
// commands, which is the whole of its control surface - there is no LED, buzzer
// or key remapping to reach.

// Sets the backlight now. 0 is off.
void setKeyboardBacklight(uint8_t brightness);

// Applies the saved backlight settings. Called at boot and whenever they
// change.
void applyKeyboardBacklight();

void setKeyHook(KeyHook hook);
void clearKeyHook();

// Fired when the trackball button is held rather than tapped.
void setHoldHandler(std::function<void()> handler);

// millis() of the last touch, ball movement or keypress.
uint32_t lastActivity();
void     noteActivity();

// Trackball sensitivity: pulses required per emitted key event.

} // namespace input
