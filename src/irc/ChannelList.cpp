#include "irc/ChannelList.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "core/Log.h"
#include "irc/IrcMessage.h"

namespace channels {
namespace {

constexpr const char* TAG       = "channels";
constexpr const char* kNamespace = "aciddrop";
constexpr const char* kKey       = "chanlist";
constexpr const char* kLegacyKey = "irc_chans";
constexpr size_t      kMaxChannels = 32;

std::vector<IrcChannelConfig> s_channels;

void seedFromLegacy(Preferences& prefs) {
    // Before the list gained per-channel options it was one comma-separated
    // string. Carry it over so an upgrade does not silently stop auto-joining.
    const String legacy = prefs.isKey(kLegacyKey) ? prefs.getString(kLegacyKey)
                                                  : String("#superbowl");

    for (const String& entry : irc::splitList(legacy)) {
        const int space = entry.indexOf(' ');

        IrcChannelConfig channel;
        channel.name = space < 0 ? entry : entry.substring(0, space);
        channel.key  = space < 0 ? String() : entry.substring(space + 1);
        s_channels.push_back(channel);
    }

    LOG_I(TAG, "migrated %u channels from the old format", (unsigned)s_channels.size());
}

} // namespace

void begin() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        LOG_E(TAG, "could not open NVS");
        return;
    }

    s_channels.clear();

    if (prefs.isKey(kKey)) {
        const String raw = prefs.getString(kKey);

        JsonDocument doc;
        const DeserializationError error = deserializeJson(doc, raw);

        if (error) {
            LOG_E(TAG, "stored list is corrupt (%s), starting empty", error.c_str());
        } else {
            for (JsonObject entry : doc.as<JsonArray>()) {
                IrcChannelConfig channel;
                channel.name     = entry["n"].as<String>();
                channel.key      = entry["k"].as<String>();
                channel.autojoin = entry["a"] | true;
                channel.retry    = entry["r"] | true;
                if (!channel.name.isEmpty()) s_channels.push_back(channel);
            }
        }
    } else {
        seedFromLegacy(prefs);
        prefs.end();
        save();
        LOG_I(TAG, "%u channels loaded", (unsigned)s_channels.size());
        return;
    }

    prefs.end();
    LOG_I(TAG, "%u channels loaded", (unsigned)s_channels.size());
}

const std::vector<IrcChannelConfig>& all() { return s_channels; }

size_t count() { return s_channels.size(); }

IrcChannelConfig* find(const String& name) {
    for (auto& channel : s_channels) {
        if (irc::equalsIgnoreCaseIrc(channel.name, name)) return &channel;
    }
    return nullptr;
}

size_t add(const IrcChannelConfig& channel) {
    if (IrcChannelConfig* existing = find(channel.name)) {
        *existing = channel;
        save();
        return existing - s_channels.data();
    }

    if (s_channels.size() >= kMaxChannels) {
        LOG_W(TAG, "channel list is full (%u)", (unsigned)kMaxChannels);
        return s_channels.size();
    }

    s_channels.push_back(channel);
    save();
    return s_channels.size() - 1;
}

void update(size_t index, const IrcChannelConfig& channel) {
    if (index >= s_channels.size()) return;
    s_channels[index] = channel;
    save();
}

void remove(size_t index) {
    if (index >= s_channels.size()) return;
    s_channels.erase(s_channels.begin() + index);
    save();
}

void moveUp(size_t index) {
    if (index == 0 || index >= s_channels.size()) return;
    std::swap(s_channels[index - 1], s_channels[index]);
    save();
}

void rememberJoin(const String& name, const String& key) {
    if (IrcChannelConfig* existing = find(name)) {
        // Only the key can change; leave the user's autojoin choice alone.
        if (!key.isEmpty() && existing->key != key) {
            existing->key = key;
            save();
        }
        return;
    }

    IrcChannelConfig channel;
    channel.name = name;
    channel.key  = key;
    add(channel);
}

void save() {
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();

    for (const auto& channel : s_channels) {
        JsonObject entry = array.add<JsonObject>();
        entry["n"] = channel.name;
        if (!channel.key.isEmpty()) entry["k"] = channel.key;
        entry["a"] = channel.autojoin;
        entry["r"] = channel.retry;
    }

    String raw;
    serializeJson(doc, raw);

    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return;
    prefs.putString(kKey, raw);
    prefs.end();
}

} // namespace channels
