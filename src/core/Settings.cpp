#include "core/Settings.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include <esp_mac.h>
#include <nvs_flash.h>

#include <map>

#include "core/Log.h"

namespace settings {
namespace {

constexpr const char* kNamespace = "aciddrop";
constexpr const char* TAG        = "settings";

// --- enum option tables ---------------------------------------------------
const char* const kOptOnOffAuto[]   = {"Off", "On", "Auto", nullptr};
const char* const kOptBootApp[]     = {"Launcher", "IRC", "Last used", nullptr};
const char* const kOptRotation[]    = {"Normal", "Upside down", nullptr};
const char* const kOptTermFont[]    = {"Small (6x14)", "Large (9x20)", nullptr};
const char* const kOptTimestamp[]   = {"None", "HH:MM", "HH:MM:SS", nullptr};
const char* const kOptCpuMhz[]      = {"80 MHz", "160 MHz", "240 MHz", nullptr};
const char* const kOptLogLevel[]    = {"Error", "Warn", "Info", "Debug", nullptr};
const char* const kOptLoraBw[]      = {"125 kHz", "250 kHz", "500 kHz", nullptr};
const char* const kOptNickColor[]   = {"Off", "Hashed", "Random", nullptr};
const char* const kOptClock[]       = {"12 hour", "24 hour", nullptr};

// --- the registry ---------------------------------------------------------
// Columns: key, section, label, help, type, min, max, step, scale, unit,
//          options, defNum, defText, secret, needsRestart
//
// These are spelled DEF_* rather than the obvious single letters because F()
// and friends are already Arduino macros, and #undef-ing them here would take
// Arduino's versions away from the rest of the translation unit.
#define DEF_BOOL(k, sec, lbl, help, def)                    {k, sec, lbl, help, SettingType::Bool,  0, 1, 1, 1, nullptr, nullptr, def, nullptr, false, false}
#define DEF_INT(k, sec, lbl, help, lo, hi, st, unit, def)  {k, sec, lbl, help, SettingType::Int,   lo, hi, st, 1, unit, nullptr, def, nullptr, false, false}
#define DEF_FLOAT(k, sec, lbl, help, lo, hi, st, sc, unit, def) {k, sec, lbl, help, SettingType::Float, lo, hi, st, sc, unit, nullptr, def, nullptr, false, false}
#define DEF_TEXT(k, sec, lbl, help, def)                    {k, sec, lbl, help, SettingType::Text,  0, 0, 0, 1, nullptr, nullptr, 0, def, false, false}
#define DEF_SECRET(k, sec, lbl, help, def)                    {k, sec, lbl, help, SettingType::Text,  0, 0, 0, 1, nullptr, nullptr, 0, def, true,  false}
#define DEF_ENUM(k, sec, lbl, help, opts, def)              {k, sec, lbl, help, SettingType::Enum,  0, 0, 1, 1, nullptr, opts,    def, nullptr, false, false}

const std::vector<SettingDef> kDefs = {
    // --- Device ----------------------------------------------------------
    DEF_TEXT("dev_name",    "Device",  "Device name",      "Shown on the lock screen and used as the WiFi hostname", "acid-drop"),
    DEF_ENUM("clock_fmt",   "Device",  "Clock format",     nullptr, kOptClock, 0),
    DEF_INT("tz_offset",   "Device",  "UTC offset",       "Minutes ahead of UTC. -300 is US Eastern.", -720, 840, 15, "min", -300),
    DEF_BOOL("dst",         "Device",  "Daylight saving",  "Adds one hour while in effect", 1),
    DEF_BOOL("ntp_enable",  "Device",  "Sync clock (NTP)", "Requires WiFi", 1),
    DEF_TEXT("ntp_server",  "Device",  "NTP server",       nullptr, "pool.ntp.org"),
    DEF_ENUM("boot_app",    "Device",  "Start in",         "Which screen to show after boot", kOptBootApp, 0),

    // --- Display ---------------------------------------------------------
    DEF_INT("brightness",  "Display", "Brightness",       nullptr, 5, 255, 5, nullptr, 200),
    DEF_INT("dim_secs",    "Display", "Dim after",        "Seconds of inactivity before dimming. 0 disables.", 0, 600, 5, "s", 20),
    DEF_INT("off_secs",    "Display", "Screen off after", "Seconds of inactivity before the backlight goes out. 0 disables.", 0, 1800, 10, "s", 60),
    DEF_INT("dim_level",   "Display", "Dim level",        nullptr, 1, 128, 1, nullptr, 25),
    DEF_ENUM("rotation",    "Display", "Orientation",      nullptr, kOptRotation, 0),
    DEF_ENUM("term_font",   "Display", "Chat font size",   "The message grid is sized from this", kOptTermFont, 0),
    DEF_INT("term_linesp", "Display", "Line spacing",     "Extra pixels between chat rows", -2, 8, 1, "px", 0),
    DEF_BOOL("sb_seconds",  "Display", "Seconds in clock", "Show seconds in the status bar", 0),
    DEF_BOOL("sb_battpct",  "Display", "Battery percent",  "Show the number next to the battery icon", 1),

    // --- Sound -----------------------------------------------------------
    DEF_BOOL("snd_enable",  "Sound",   "Sound",            nullptr, 1),
    DEF_INT("snd_volume",  "Sound",   "Volume",           nullptr, 0, 21, 1, nullptr, 12),
    DEF_BOOL("snd_boot",    "Sound",   "Boot jingle",      nullptr, 1),
    DEF_BOOL("snd_mention", "Sound",   "Mention alert",    "Beep when your nick is said", 1),
    DEF_BOOL("snd_msg",     "Sound",   "Private message",  "Beep on a new PM", 1),
    DEF_BOOL("snd_connect", "Sound",   "Connect / drop",   "Beep when IRC connects or disconnects", 0),
    DEF_BOOL("snd_key",     "Sound",   "Key clicks",       nullptr, 0),

    // --- Power -----------------------------------------------------------
    DEF_ENUM("cpu_mhz",     "Power",   "CPU speed",        "Lower is cooler and lasts longer", kOptCpuMhz, 2),
    DEF_BOOL("wifi_ps",     "Power",   "WiFi power save",  "Saves power, adds latency to IRC", 0),
    DEF_INT("batt_warn",   "Power",   "Low battery at",   "Warn below this charge", 5, 50, 5, "%", 15),
    DEF_BOOL("batt_beep",   "Power",   "Low battery beep", nullptr, 1),

    // --- WiFi ------------------------------------------------------------
    DEF_BOOL("wifi_enable", "WiFi",    "WiFi",             nullptr, 1),
    DEF_BOOL("wifi_auto",   "WiFi",    "Auto-connect",     "Reconnect to the saved network on boot", 1),
    DEF_TEXT("wifi_ssid",   "WiFi",    "SSID",             nullptr, ""),
    DEF_SECRET("wifi_pass",   "WiFi",    "Password",         nullptr, ""),
    DEF_BOOL("wifi_macrnd", "WiFi",    "Randomize MAC",    "New MAC address on every connect", 0),
    DEF_INT("wifi_retry",  "WiFi",    "Retry delay",      "Seconds between reconnect attempts", 1, 120, 1, "s", 5),

    // --- IRC server ------------------------------------------------------
    DEF_TEXT("irc_server",  "Server", "Server",           nullptr, "irc.supernets.org"),
    DEF_INT("irc_port",    "Server", "Port",             nullptr, 1, 65535, 1, nullptr, 6697),
    DEF_BOOL("irc_tls",     "Server", "TLS",              nullptr, 1),
    DEF_BOOL("irc_tlsverif","Server", "Verify certificate", "Off accepts self-signed certificates", 0),
    DEF_BOOL("irc_fallback","Server", "Plaintext fallback", "Retry on port 6667 if TLS fails", 1),
    DEF_BOOL("irc_autoconn","Server", "Connect on boot",  nullptr, 1),
    DEF_TEXT("irc_nick",    "Identity", "Nick",             nullptr, ""),
    DEF_TEXT("irc_altnick", "Identity", "Alternate nick",   "Used if the first one is taken", ""),
    DEF_TEXT("irc_user",    "Identity", "Username",         nullptr, "tdeck"),
    DEF_TEXT("irc_real",    "Identity", "Real name",        nullptr, "ACID DROP"),
    DEF_TEXT("irc_quitmsg", "Identity", "Quit message",     nullptr, "ACID DROP"),

    // --- IRC auth --------------------------------------------------------
    DEF_BOOL("irc_sasl",    "Authentication", "SASL PLAIN",      "Authenticate during connection registration", 0),
    DEF_TEXT("irc_saslusr", "Authentication", "SASL account",    "Defaults to your nick when empty", ""),
    DEF_SECRET("irc_saslpass","Authentication", "SASL password",   nullptr, ""),
    DEF_SECRET("irc_nspass",  "Authentication", "NickServ password", "Sent as IDENTIFY after connecting, if SASL is off", ""),

    // --- IRC timing (the reconnect/rejoin behaviour) ---------------------
    DEF_INT("irc_joindly", "Connection", "Join delay",     "Wait this long after the welcome (001) before joining", 0, 60000, 500, "ms", 6000),
    DEF_BOOL("irc_recon",   "Connection", "Auto-reconnect", nullptr, 1),
    DEF_INT("irc_recondly","Connection", "Reconnect delay","First retry waits this long, then backs off", 1, 300, 1, "s", 5),
    DEF_INT("irc_reconmax","Connection", "Max backoff",    "Reconnect delay never exceeds this", 5, 900, 5, "s", 120),
    DEF_BOOL("irc_rejoin",  "Connection", "Rejoin on kick", nullptr, 1),
    DEF_INT("irc_kickdly", "Connection", "Kick rejoin delay", nullptr, 1, 300, 1, "s", 3),
    DEF_BOOL("irc_retryjn", "Connection", "Retry failed joins", "Keep trying when a channel is +i, +k, +b or full", 1),
    DEF_INT("irc_lockdly", "Connection", "Join retry delay", nullptr, 1, 300, 1, "s", 5),
    DEF_INT("irc_pingout", "Connection", "Ping timeout",   "Drop the link if the server is silent this long", 30, 900, 10, "s", 260),

    // --- IRC display -----------------------------------------------------
    DEF_BOOL("irc_colors",  "Appearance", "mIRC colors",   "Render ^C colour codes", 1),
    DEF_BOOL("irc_bgcolor", "Appearance", "Background colors", "Needed for ANSI art drawn with coloured spaces", 1),
    DEF_BOOL("irc_ansi",    "Appearance", "ANSI escapes",  "Also parse ESC[ SGR sequences", 1),
    DEF_BOOL("irc_format",  "Appearance", "Bold/italic/underline", "Render ^B ^] ^_ and reverse video", 1),
    DEF_ENUM("irc_nickcol", "Appearance", "Nick colors",   "Hashed keeps a nick the same colour every session", kOptNickColor, 1),
    DEF_ENUM("irc_ts",      "Appearance", "Timestamps",    nullptr, kOptTimestamp, 1),
    DEF_BOOL("irc_joinpart","Appearance", "Show joins/parts", nullptr, 1),
    DEF_BOOL("irc_showmode","Appearance", "Show mode changes", nullptr, 1),
    DEF_BOOL("irc_showraw", "Appearance", "Raw server lines", "Mirror everything into the status window", 1),
    DEF_INT("irc_scrollbk","Appearance", "Scrollback",    "Lines kept per window, held in PSRAM", 100, 5000, 100, "lines", 1000),
    DEF_TEXT("irc_hilight", "Appearance", "Highlight words", "Comma separated, in addition to your nick", ""),
    DEF_BOOL("irc_beepctcp","Appearance", "Allow CTCP",    "Answer VERSION, PING and TIME requests", 1),

    // --- GPS -------------------------------------------------------------
    DEF_BOOL("gps_enable",  "GPS",     "GPS",              "T-Deck Plus only", 1),
    DEF_INT("gps_baud",    "GPS",     "Baud rate",        nullptr, 4800, 115200, 4800, nullptr, 9600),
    DEF_BOOL("gps_statbar", "GPS",     "Status bar icon",  nullptr, 1),

    // --- LoRa ------------------------------------------------------------
    DEF_BOOL("lora_enable", "LoRa",    "LoRa radio",       nullptr, 0),
    DEF_FLOAT("lora_freq",   "LoRa",    "Frequency",        nullptr, 40000, 100000, 10, 100, "MHz", 91500),
    DEF_ENUM("lora_bw",     "LoRa",    "Bandwidth",        nullptr, kOptLoraBw, 0),
    DEF_INT("lora_sf",     "LoRa",    "Spreading factor", nullptr, 6, 12, 1, nullptr, 9),
    DEF_INT("lora_cr",     "LoRa",    "Coding rate",      "4/N", 5, 8, 1, nullptr, 7),
    DEF_INT("lora_power",  "LoRa",    "TX power",         nullptr, -9, 22, 1, "dBm", 17),

    // --- Bluetooth -------------------------------------------------------
    DEF_BOOL("ble_enable",  "Bluetooth", "Bluetooth",      nullptr, 0),
    DEF_TEXT("ble_name",    "Bluetooth", "Advertised name", nullptr, "acid-drop"),

    // --- Advanced --------------------------------------------------------
    DEF_ENUM("log_level",   "Advanced", "Log level",       nullptr, kOptLogLevel, 2),
    DEF_BOOL("log_screen",  "Advanced", "On-screen syslog", "Keep a log ring buffer for the syslog view", 1),
    DEF_BOOL("dev_mode",    "Advanced", "Developer mode",  "Shows raw sockets, heap and frame timing", 0),
};

#undef DEF_BOOL
#undef DEF_INT
#undef DEF_FLOAT
#undef DEF_TEXT
#undef DEF_SECRET
#undef DEF_ENUM

// --- storage --------------------------------------------------------------
Preferences               s_prefs;
std::map<String, int32_t> s_nums;
std::map<String, String>  s_texts;
std::vector<ChangeCb>     s_listeners;
bool                      s_ready = false;

int32_t clampToDef(const SettingDef& def, int32_t value) {
    switch (def.type) {
        case SettingType::Bool: return value ? 1 : 0;
        case SettingType::Enum: {
            int32_t count = 0;
            while (def.options[count]) count++;
            if (value < 0) return 0;
            return value >= count ? count - 1 : value;
        }
        case SettingType::Int:
        case SettingType::Float:
            if (value < def.min) return def.min;
            if (value > def.max) return def.max;
            return value;
        default:
            return value;
    }
}

void notify(const char* key) {
    for (auto& cb : s_listeners) cb(key);
}

bool isNumeric(const SettingDef& def) {
    return def.type != SettingType::Text;
}

} // namespace

void begin() {
    if (!s_prefs.begin(kNamespace, false)) {
        LOG_E(TAG, "could not open NVS namespace");
        return;
    }

    for (const auto& def : kDefs) {
        if (isNumeric(def)) {
            int32_t value = s_prefs.isKey(def.key) ? s_prefs.getInt(def.key) : def.defNum;
            s_nums[def.key] = clampToDef(def, value);
        } else {
            s_texts[def.key] = s_prefs.isKey(def.key) ? s_prefs.getString(def.key)
                                                      : String(def.defText);
        }
    }

    // A blank nick would get us killed by the server on connect, so mint a
    // stable random one on first boot and persist it.
    if (s_texts["irc_nick"].isEmpty()) {
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        char nick[24];
        snprintf(nick, sizeof(nick), "ACID_%02X%02X", mac[4], mac[5]);
        s_texts["irc_nick"] = nick;
        s_prefs.putString("irc_nick", nick);
    }
    if (s_texts["irc_altnick"].isEmpty()) {
        s_texts["irc_altnick"] = s_texts["irc_nick"] + "_";
    }

    s_ready = true;
    LOG_I(TAG, "loaded %u settings", (unsigned)kDefs.size());
}

const std::vector<SettingDef>& defs() { return kDefs; }

const SettingDef* find(const char* key) {
    for (const auto& def : kDefs) {
        if (strcmp(def.key, key) == 0) return &def;
    }
    return nullptr;
}

const char* groupOf(const char* section) {
    // Listed explicitly rather than inferred from the name, so the sections can
    // be titled for the person reading them instead of for a prefix test.
    static const char* const kIrcSections[] = {
        "Server", "Identity", "Authentication", "Connection", "Appearance", nullptr
    };
    for (const char* const* name = kIrcSections; *name; name++) {
        if (strcmp(*name, section) == 0) return kGroupIrc;
    }
    return kGroupSystem;
}

std::vector<const char*> sections(const char* group) {
    std::vector<const char*> out;
    for (const auto& def : kDefs) {
        if (group != nullptr && strcmp(groupOf(def.section), group) != 0) continue;

        bool seen = false;
        for (const char* s : out) {
            if (strcmp(s, def.section) == 0) { seen = true; break; }
        }
        if (!seen) out.push_back(def.section);
    }
    return out;
}

// --- reads ----------------------------------------------------------------
int32_t getInt(const char* key) {
    auto it = s_nums.find(key);
    if (it == s_nums.end()) {
        LOG_E(TAG, "getInt: unknown key '%s'", key);
        return 0;
    }
    return it->second;
}

bool    getBool(const char* key)  { return getInt(key) != 0; }
uint8_t getEnum(const char* key)  { return static_cast<uint8_t>(getInt(key)); }

float getFloat(const char* key) {
    const SettingDef* def = find(key);
    const int32_t raw = getInt(key);
    return def && def->scale > 1 ? static_cast<float>(raw) / def->scale : static_cast<float>(raw);
}

String getText(const char* key) {
    auto it = s_texts.find(key);
    if (it == s_texts.end()) {
        LOG_E(TAG, "getText: unknown key '%s'", key);
        return String();
    }
    return it->second;
}

const char* getEnumLabel(const char* key) {
    const SettingDef* def = find(key);
    if (!def || def->type != SettingType::Enum) return "";
    return def->options[getEnum(key)];
}

// --- writes ---------------------------------------------------------------
void setInt(const char* key, int32_t value) {
    const SettingDef* def = find(key);
    if (!def || !isNumeric(*def)) {
        LOG_E(TAG, "setInt: bad key '%s'", key);
        return;
    }

    const int32_t clamped = clampToDef(*def, value);
    if (s_nums[key] == clamped) return;

    s_nums[key] = clamped;
    s_prefs.putInt(key, clamped);
    notify(def->key);
}

void setBool(const char* key, bool value)     { setInt(key, value ? 1 : 0); }
void setEnum(const char* key, uint8_t index)  { setInt(key, index); }

void setFloat(const char* key, float value) {
    const SettingDef* def = find(key);
    if (!def) return;
    setInt(key, static_cast<int32_t>(lroundf(value * (def->scale > 1 ? def->scale : 1))));
}

void setText(const char* key, const String& value) {
    const SettingDef* def = find(key);
    if (!def || isNumeric(*def)) {
        LOG_E(TAG, "setText: bad key '%s'", key);
        return;
    }
    if (s_texts[key] == value) return;

    s_texts[key] = value;
    s_prefs.putString(key, value);
    notify(def->key);
}

// --- generic --------------------------------------------------------------
String getAsString(const char* key) {
    const SettingDef* def = find(key);
    if (!def) return String();

    switch (def->type) {
        case SettingType::Bool:  return getBool(key) ? "true" : "false";
        case SettingType::Enum:  return String(getEnumLabel(key));
        case SettingType::Int:   return String(getInt(key));
        case SettingType::Float: return String(getFloat(key), 2);
        case SettingType::Text:  return getText(key);
    }
    return String();
}

void setFromString(const char* key, const String& value) {
    const SettingDef* def = find(key);
    if (!def) return;

    switch (def->type) {
        case SettingType::Bool:
            setBool(key, value == "true" || value == "1" || value == "on");
            break;
        case SettingType::Enum: {
            for (int i = 0; def->options[i]; i++) {
                if (value.equalsIgnoreCase(def->options[i])) { setEnum(key, i); return; }
            }
            setInt(key, value.toInt());
            break;
        }
        case SettingType::Int:   setInt(key, value.toInt()); break;
        case SettingType::Float: setFloat(key, value.toFloat()); break;
        case SettingType::Text:  setText(key, value); break;
    }
}

void resetToDefault(const char* key) {
    const SettingDef* def = find(key);
    if (!def) return;
    if (isNumeric(*def)) setInt(key, def->defNum);
    else                 setText(key, def->defText);
}

void resetSection(const char* section) {
    for (const auto& def : kDefs) {
        if (strcmp(def.section, section) == 0) resetToDefault(def.key);
    }
}

void factoryReset() {
    s_prefs.end();
    nvs_flash_erase();
    nvs_flash_init();
    LOG_W(TAG, "factory reset - NVS erased");
}

void onChange(ChangeCb cb) {
    s_listeners.push_back(std::move(cb));
}

// --- JSON -----------------------------------------------------------------
bool exportJson(const String& path, bool includeSecrets) {
    JsonDocument doc;
    doc["_firmware"] = "acid-drop";

    for (const auto& def : kDefs) {
        if (def.secret && !includeSecrets) continue;
        JsonObject section = doc[def.section].isNull() ? doc[def.section].to<JsonObject>()
                                                       : doc[def.section].as<JsonObject>();
        switch (def.type) {
            case SettingType::Bool:  section[def.key] = getBool(def.key); break;
            case SettingType::Int:
            case SettingType::Enum:  section[def.key] = getInt(def.key);  break;
            case SettingType::Float: section[def.key] = getFloat(def.key); break;
            case SettingType::Text:  section[def.key] = getText(def.key); break;
        }
    }

    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        LOG_E(TAG, "export: cannot open %s", path.c_str());
        return false;
    }
    serializeJsonPretty(doc, file);
    file.close();
    LOG_I(TAG, "exported settings to %s", path.c_str());
    return true;
}

bool importJson(const String& path) {
    File file = SD.open(path, FILE_READ);
    if (!file) {
        LOG_E(TAG, "import: cannot open %s", path.c_str());
        return false;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        LOG_E(TAG, "import: %s", err.c_str());
        return false;
    }

    unsigned applied = 0;
    for (const auto& def : kDefs) {
        JsonVariant value = doc[def.section][def.key];
        if (value.isNull()) continue;

        switch (def.type) {
            case SettingType::Bool:  setBool(def.key, value.as<bool>()); break;
            case SettingType::Int:
            case SettingType::Enum:  setInt(def.key, value.as<int32_t>()); break;
            case SettingType::Float: setFloat(def.key, value.as<float>()); break;
            case SettingType::Text:  setText(def.key, value.as<String>()); break;
        }
        applied++;
    }

    LOG_I(TAG, "imported %u settings from %s", applied, path.c_str());
    return true;
}

} // namespace settings
