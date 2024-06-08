#pragma once

#include <RadioLib.h>

#include "pins.h"
#include "Speaker.h"

extern SX1262 radio;

void recvLoop();
bool setupRadio();
bool transmit();
