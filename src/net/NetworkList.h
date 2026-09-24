#pragma once

#include <Arduino.h>
#include <vector>

// The networks this device has successfully joined, most recent first.
//
// Kept separate from the WiFi settings rows: those describe the network being
// used now, this is the history you can switch back to without retyping a
// password. Bounded, because it lives in NVS and nobody needs the twentieth
// coffee shop they ever visited.

namespace netlist {

struct SavedNetwork {
    String ssid;
    String password;
};

constexpr size_t kMaxNetworks = 5;

void begin();

const std::vector<SavedNetwork>& all();
size_t count();

// Records a network that just associated. An SSID already on the list moves
// to the front and takes the new password; a new one pushes the oldest off
// the end.
void remember(const String& ssid, const String& password);

// Nothing happens if the index is out of range.
void setPassword(size_t index, const String& password);
void remove(size_t index);

// The next network to try after `ssid` has failed, so a device that cannot
// reach the network it used last time works its way through the others.
// Returns false when there is nothing else to try.
bool nextAfter(const String& ssid, SavedNetwork& out);

void save();

} // namespace netlist
