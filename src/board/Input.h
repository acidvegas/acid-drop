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

lv_indev_t* keypad();
lv_indev_t* pointer();

// The focus group every screen's widgets should join.
lv_group_t* group();

void setKeyHook(KeyHook hook);
void clearKeyHook();

// Fired when the trackball button is held rather than tapped.
void setHoldHandler(std::function<void()> handler);

// millis() of the last touch, ball movement or keypress.
uint32_t lastActivity();
void     noteActivity();

// Trackball sensitivity: pulses required per emitted key event.
void setBallDivisor(uint8_t divisor);

} // namespace input
