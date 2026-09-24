#include "net/NetworkList.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "core/Log.h"

namespace netlist {
namespace {

constexpr const char* TAG       = "netlist";
// Its own namespace, for the same reason the channel list has one: the
// settings registry holds an open read-write handle on "aciddrop" for the life
// of the process.
constexpr const char* kNamespace = "acidwifi";
constexpr const char* kKey       = "netlist";

std::vector<SavedNetwork> s_networks;

} // namespace

void begin() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) {
        LOG_W(TAG, "no saved networks");
        return;
    }

    s_networks.clear();

    if (prefs.isKey(kKey)) {
        const String raw = prefs.getString(kKey);

        JsonDocument doc;
        const DeserializationError error = deserializeJson(doc, raw);
        if (error) {
            LOG_E(TAG, "saved list is corrupt (%s), starting empty", error.c_str());
        } else {
            for (JsonObject entry : doc.as<JsonArray>()) {
                SavedNetwork network;
                // Not as<String>(): a missing field would come back as the
                // four-character text "null" rather than empty. Same trap the
                // channel list fell into.
                network.ssid     = entry["s"] | "";
                network.password = entry["p"] | "";
                if (!network.ssid.isEmpty()) s_networks.push_back(network);
            }
        }
    }

    prefs.end();
    LOG_I(TAG, "%u saved networks", (unsigned)s_networks.size());
}

const std::vector<SavedNetwork>& all() { return s_networks; }

size_t count() { return s_networks.size(); }

void remember(const String& ssid, const String& password) {
    if (ssid.isEmpty()) return;

    // Already known: take the new password and move it to the front, because
    // the list is ordered by how recently it worked.
    for (size_t i = 0; i < s_networks.size(); i++) {
        if (s_networks[i].ssid == ssid) {
            s_networks[i].password = password;
            if (i != 0) {
                const SavedNetwork moved = s_networks[i];
                s_networks.erase(s_networks.begin() + i);
                s_networks.insert(s_networks.begin(), moved);
            }
            save();
            return;
        }
    }

    s_networks.insert(s_networks.begin(), SavedNetwork{ssid, password});
    while (s_networks.size() > kMaxNetworks) s_networks.pop_back();

    save();
    LOG_I(TAG, "remembered %s", ssid.c_str());
}

void setPassword(size_t index, const String& password) {
    if (index >= s_networks.size()) return;
    s_networks[index].password = password;
    save();
}

void remove(size_t index) {
    if (index >= s_networks.size()) return;
    LOG_I(TAG, "forgetting %s", s_networks[index].ssid.c_str());
    s_networks.erase(s_networks.begin() + index);
    save();
}

bool nextAfter(const String& ssid, SavedNetwork& out) {
    if (s_networks.empty()) return false;

    // Find where the failing network sits and take the one after it, wrapping
    // round. An unknown SSID starts from the top of the list.
    size_t start = 0;
    for (size_t i = 0; i < s_networks.size(); i++) {
        if (s_networks[i].ssid == ssid) { start = i + 1; break; }
    }

    if (start >= s_networks.size()) start = 0;

    // Nothing gained by handing back the network that just failed.
    if (s_networks.size() == 1 && s_networks[0].ssid == ssid) return false;

    out = s_networks[start];
    return true;
}

void save() {
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();

    for (const auto& network : s_networks) {
        JsonObject entry = array.add<JsonObject>();
        entry["s"] = network.ssid;
        if (!network.password.isEmpty()) entry["p"] = network.password;
    }

    String raw;
    serializeJson(doc, raw);

    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return;
    prefs.putString(kKey, raw);
    prefs.end();
}

} // namespace netlist
