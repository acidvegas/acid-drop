#pragma once

#include "Arduino.h"
#include "WiFi.h"
#include "Wire.h"
#include <Pangodream_18650_CL.h> // Power management

#include "pins.h"

extern Pangodream_18650_CL BL;

String formatBytes(size_t bytes);
char getKeyboardInput();
void printDeviceInfo();
void setBrightness(uint8_t value);
void updateTimeFromNTP();

