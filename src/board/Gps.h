#pragma once

#include <Arduino.h>

// L76K GNSS receiver on Serial1. Present on the T-Deck Plus only; on the plain
// T-Deck the UART simply never produces a sentence and hasFix() stays false.

namespace gps {

void begin();
void loop();

void setEnabled(bool enabled);
bool enabled();

bool     hasFix();
uint8_t  satellites();
double   latitude();
double   longitude();
double   altitudeMeters();
double   speedKnots();
uint32_t fixAgeMs();

// "no fix", "3 sats", "44.9821, -93.2712"
String summary();

// Seconds since the epoch from the GNSS fix, or 0 when there is no valid date.
uint32_t unixTime();

} // namespace gps
