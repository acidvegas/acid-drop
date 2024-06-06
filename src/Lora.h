#pragma once

#include <RadioLib.h>

#include "pins.h"

extern SX1262 radio;

bool setupRadio();
bool transmit();
void recvLoop();
