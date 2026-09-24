#include "irc/ChannelList.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "core/Log.h"
#include "irc/IrcMessage.h"

namespace channels {
namespace {

constexpr const char* TAG       = "channels";
// Its own namespace: the settings registry holds an open read-write handle on
// "aciddrop" for the life of the process, and opening the same namespace a
// second time to write is asking for trouble.
constexpr const char* kNamespace       = "acidchan";
constexpr const char* kLegacyNamespace = "aciddrop";
constexpr const char* kKey             = "chanlist";
constexpr const char* kFixupKey        = "fixup";      // one-time repairs
constexpr uint8_t     kFixupVersion    = 2;
constexpr const char* kLegacyKey       = "irc_chans";
constexpr size_t      kMaxChannels = 32;

std::vector<IrcChannelConfig> s_channels;

void seedFromLegacy() {
    // Before the list gained per-channel options it was one comma-separated
    // string in the settings namespace. Carry it over so an upgrade does not
    // silently stop auto-joining. Opened read-only, alongside the settings
    // registry's own handle.
    Preferences legacyPrefs;
    String legacy = "#superbowl";
    if (legacyPrefs.begin(kLegacyNamespace, true)) {
        if (legacyPrefs.isKey(kLegacyKey)) legacy = legacyPrefs.getString(kLegacyKey);
        legacyPrefs.end();
    }

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
                // Not as<String>(): ArduinoJson's ::String converter falls
                // back to serializing the variant when it is not a string, so
                // a *missing* field yields the four-character text "null"
                // rather than an empty String (ConverterImpl.hpp, the
                // convertFromJson(JsonVariantConst, ::String&) overload). The
                // key is only written when it is non-empty, so every keyless
                // channel loaded with a key of "null" and then tried to join
                // with it. operator|(variant, const char*) tests the type
                // first and hands back the default instead.
                channel.name     = entry["n"] | "";
                channel.key      = entry["k"] | "";
                channel.autojoin = entry["a"] | true;
                channel.retry    = entry["r"] | true;
                if (!channel.name.isEmpty()) s_channels.push_back(channel);
            }
        }
    } else {
        // A fresh list has nothing to repair, and stamping it now stops the
        // migration running on the second boot against flags the user turned
        // off themselves on the first.
        prefs.putUChar(kFixupKey, kFixupVersion);
        prefs.end();
        seedFromLegacy();
        save();
        LOG_I(TAG, "%u channels loaded", (unsigned)s_channels.size());
        return;
    }

    // One-time repair. An earlier part() cleared autojoin on the saved entry,
    // so a channel parted once was silently never joined again - the flag was
    // written by that bug, not chosen by the user, and it persists in NVS
    // where shipping the fix alone cannot reach it.
    const uint8_t fixup = prefs.getUChar(kFixupKey, 0);
    bool repaired = false;
    if (fixup < kFixupVersion) {
        for (auto& channel : s_channels) {
            if (!channel.autojoin) {
                channel.autojoin = true;
                repaired = true;
                LOG_W(TAG, "re-enabled autojoin for %s (cleared by an old bug)",
                      channel.name.c_str());
            }
            // The phantom key above did not stay in memory: once anything
            // saved the list, the literal "null" was written to NVS as a real
            // key, where correcting the read cannot reach it.
            if (channel.key == "null") {
                channel.key = String();
                repaired = true;
                LOG_W(TAG, "dropped the bogus 'null' key on %s",
                      channel.name.c_str());
            }
        }
        prefs.putUChar(kFixupKey, kFixupVersion);
    }

    prefs.end();
    if (repaired) save();

    LOG_I(TAG, "%u channels loaded", (unsigned)s_channels.size());
    for (const auto& channel : s_channels) {
        LOG_I(TAG, "  %s autojoin=%d retry=%d key='%s'",
              channel.name.c_str(), channel.autojoin ? 1 : 0, channel.retry ? 1 : 0,
              channel.key.c_str());
    }
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

void rememberJoin(const String& name, const String& key) {
    if (IrcChannelConfig* existing = find(name)) {
        // Joining a channel by hand is as clear a statement of intent as
        // adding it to the list, so it goes back on autojoin. Parting clears
        // that flag, and without this a channel could never get back on the
        // list once it had been parted once.
        // Unconditional, including clearing it: this is only ever reached from
        // an explicit join, so the key given there is the truth. Skipping the
        // empty case meant a wrong key could never be removed, and it was then
        // sent on every autojoin thereafter.
        bool changed = false;
        if (existing->key != key) {
            existing->key = key;
            changed = true;
        }
        if (!existing->autojoin) {
            existing->autojoin = true;
            changed = true;
        }
        if (changed) save();
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
