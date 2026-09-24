#pragma once

#include <Arduino.h>
#include <vector>

// The saved channel list. Each entry carries everything needed to get back
// into that channel unattended: its key for +k, whether to join it at all, and
// whether to keep retrying when the server says no.

struct IrcChannelConfig {
    String name;
    String key;               // for +k channels, empty otherwise
    bool   autojoin = true;
    bool   retry    = true;   // keep trying through +i, +k, +b, +l
};

namespace channels {

// Loads from NVS, migrating the old comma-separated "irc_chans" string if this
// is the first boot after the upgrade.
void begin();

const std::vector<IrcChannelConfig>& all();
size_t count();

// Looked up with IRC casemapping, so #Foo and #foo are the same channel.
// Returns nullptr when the channel is not in the list.
IrcChannelConfig* find(const String& name);

size_t add(const IrcChannelConfig& channel);   // index of the new entry
void   update(size_t index, const IrcChannelConfig& channel);
void   remove(size_t index);

// Records a channel joined at runtime so it comes back after a reconnect.
void rememberJoin(const String& name, const String& key);

void save();

} // namespace channels
