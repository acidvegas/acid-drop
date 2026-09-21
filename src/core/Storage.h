#pragma once

#include <Arduino.h>

// SD card access, mounted on first use.
//
// Mounting is deliberately not done at boot. The card sits on the same SPI
// host as the display, SD.begin() re-initialises that bus, and with no card in
// the slot it retries internally for a long time - which showed up as the
// device sitting on the boot logo for minutes. Nothing needs the card until
// someone exports settings or a TLS certificate is read, so it waits.

namespace storage {

// Mounts if it is not already mounted. Cheap to call repeatedly: a failed
// mount is remembered so a missing card is not retried on every access.
bool ensureSdCard();

bool sdMounted();

// Allows a later retry after the user has inserted a card.
void forgetSdCard();

} // namespace storage
