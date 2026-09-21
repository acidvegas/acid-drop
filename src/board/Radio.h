#pragma once

#include <Arduino.h>
#include <functional>

// SX1262 LoRa transceiver. Off by default: it shares the SPI bus with the
// display and the SD card, and nothing in the UI needs it yet.

namespace radio {

void begin();
void loop();

bool setEnabled(bool enabled);
bool enabled();

void applySettings();

bool send(const String& text);

// packet, RSSI in dBm, SNR in dB
extern std::function<void(const String&, float, float)> onPacket;

String lastError();

} // namespace radio
