#include "core/Settings.h"

#include <Preferences.h>

#include <esp_mac.h>
#include <nvs_flash.h>

#include <map>
#include <cstring>

#include "core/Log.h"
#include "ui/Theme.h"

namespace settings {
namespace {

constexpr const char* kNamespace = "aciddrop";
constexpr const char* TAG        = "settings";

// --- enum option tables ---------------------------------------------------
const char* const kOptCpuMhz[]      = {"80 MHz", "160 MHz", "240 MHz", nullptr};
const char* const kOptLogLevel[]    = {"Error", "Warn", "Info", "Debug", nullptr};
const char* const kOptClock[]       = {"12 hour", "24 hour", nullptr};
// What this device connects to. Exactly one, because all three want the same
// nick and the same attention.
const char* const kOptChatMode[]    = {"Direct IRC", "ZNC bouncer", "WeeChat relay", nullptr};

// --- the registry ---------------------------------------------------------
// Columns: key, section, label, help, type, min, max, step, scale, unit,
//          options, defNum, defText, secret, needsRestart, hidden
//
// These are spelled DEF_* rather than the obvious single letters because F()
// and friends are already Arduino macros, and #undef-ing them here would take
// Arduino's versions away from the rest of the translation unit.
//
// IRC comes first: this is IRC firmware, and the settings screen follows this
// order. Things that are not worth a choice are not here at all - the render
// options, the quit and part text, the username and the real name are fixed in
// IrcClient.cpp, because nobody turns colours off in an IRC client.
#define DEF_BOOL(k, sec, lbl, help, def)                    {k, sec, lbl, help, SettingType::Bool,  0, 1, 1, 1, nullptr, nullptr, def, nullptr, false, false, false}
#define DEF_HIDDEN(k, sec, def)                             {k, sec, k,   nullptr, SettingType::Bool, 0, 1, 1, 1, nullptr, nullptr, def, nullptr, false, false, true}
#define DEF_INT(k, sec, lbl, help, lo, hi, st, unit, def)  {k, sec, lbl, help, SettingType::Int,   lo, hi, st, 1, unit, nullptr, def, nullptr, false, false, false}
#define DEF_FLOAT(k, sec, lbl, help, lo, hi, st, sc, unit, def) {k, sec, lbl, help, SettingType::Float, lo, hi, st, sc, unit, nullptr, def, nullptr, false, false, false}
#define DEF_TEXT(k, sec, lbl, help, def)                    {k, sec, lbl, help, SettingType::Text,  0, 0, 0, 1, nullptr, nullptr, 0, def, false, false, false}
#define DEF_SECRET(k, sec, lbl, help, def)                  {k, sec, lbl, help, SettingType::Text,  0, 0, 0, 1, nullptr, nullptr, 0, def, true,  false, false}
#define DEF_ENUM(k, sec, lbl, help, opts, def)              {k, sec, lbl, help, SettingType::Enum,  0, 0, 1, 1, nullptr, opts,    def, nullptr, false, false, false}
#define DEF_COLOR(k, sec, lbl, def)                         {k, sec, lbl, nullptr, SettingType::Color, 0, 0xFFFFFF, 1, 1, nullptr, nullptr, def, nullptr, false, false, false}

const std::vector<SettingDef> kDefs = {
    // --- IRC server ------------------------------------------------------
    DEF_ENUM("chat_mode",   "Server", "Mode",             "Direct IRC talks to a server itself. ZNC and WeeChat are things that are already connected, and this becomes a window onto them.", kOptChatMode, 0),
    DEF_TEXT("irc_server",  "Server", "Server",           nullptr, "irc.supernets.org"),
    DEF_INT("irc_port",    "Server", "Port",             nullptr, 1, 65535, 1, nullptr, 6697),
    DEF_BOOL("irc_tls",     "Server", "TLS",              nullptr, 1),
    DEF_BOOL("irc_tlsverif","Server", "Verify certificate", "Off by default. TLS still encrypts either way; this additionally checks the server's certificate against the roots built into the firmware, which protects your NickServ and SASL passwords from an intercepted connection.", 0),
    DEF_BOOL("irc_fallback","Server", "Plaintext fallback", "Retry on port 6667 if TLS fails", 1),
    DEF_SECRET("irc_srvpass","Server", "Server password",  "Sent as PASS before registering. For a ZNC bouncer this is user:password, or user/network:password.", ""),

    // --- ZNC ---------------------------------------------------------------
    // A bouncer speaks plain IRC, so this reuses the whole IRC client. The
    // only thing that makes it ZNC is the PASS line, which is where the user,
    // the network and the password all go.
    DEF_TEXT("znc_host",    "ZNC", "Bouncer host",      nullptr, ""),
    DEF_INT("znc_port",    "ZNC", "Bouncer port",      nullptr, 1, 65535, 1, nullptr, 6697),
    DEF_BOOL("znc_tls",     "ZNC", "TLS",               nullptr, 1),
    DEF_TEXT("znc_user",    "ZNC", "ZNC username",      nullptr, ""),
    DEF_SECRET("znc_pass",  "ZNC", "ZNC password",      nullptr, ""),
    DEF_TEXT("znc_network", "ZNC", "Network",           "Which of your ZNC networks to attach to. Leave empty for the default.", ""),

    // --- Relay -----------------------------------------------------------
    // A WeeChat relay is a different thing to connect to, not a different way
    // of connecting to IRC: WeeChat holds the networks and this becomes a
    // window onto it.
    DEF_TEXT("relay_host",  "Relay", "Relay host",       nullptr, ""),
    DEF_INT("relay_port",  "Relay", "Relay port",       nullptr, 1, 65535, 1, nullptr, 9000),
    DEF_SECRET("relay_pass","Relay", "Relay password",   nullptr, ""),
    DEF_BOOL("relay_tls",   "Relay", "TLS",              "Relay certificates are usually self-signed, so this is not verified", 0),
    DEF_INT("relay_backlog","Relay", "Backlog",          "Lines fetched when a buffer is first opened", 10, 200, 10, "lines", 50),

    // --- Identity --------------------------------------------------------
    DEF_TEXT("irc_nick",    "Identity", "Nick",           "Four random digits are appended if it is already taken", ""),

    // --- Authentication --------------------------------------------------
    DEF_BOOL("irc_sasl",    "Authentication", "SASL PLAIN",      "Authenticate during connection registration", 0),
    DEF_TEXT("irc_saslusr", "Authentication", "SASL account",    "Defaults to your nick when empty", ""),
    DEF_SECRET("irc_saslpass","Authentication", "SASL password",   nullptr, ""),
    DEF_SECRET("irc_nspass",  "Authentication", "NickServ password", "Sent as IDENTIFY after connecting, if SASL is off", ""),

    // --- Connection ------------------------------------------------------
    DEF_INT("irc_joinsec", "Connection", "Join delay",     "Wait this long after the welcome (001) before joining", 0, 60, 1, "s", 6),
    DEF_INT("irc_recondly","Connection", "Reconnect delay","First retry waits this long, then backs off", 1, 300, 1, "s", 5),
    DEF_INT("irc_reconmax","Connection", "Max backoff",    "Reconnect delay never exceeds this", 5, 900, 5, "s", 120),
    DEF_BOOL("irc_rejoin",  "Connection", "Rejoin on kick", nullptr, 1),
    DEF_INT("irc_kickdly", "Connection", "Kick rejoin delay", nullptr, 1, 300, 1, "s", 3),
    DEF_BOOL("irc_retryjn", "Connection", "Retry failed joins", "Keep trying when a channel is +i, +k, +b or full", 1),
    DEF_INT("irc_lockdly", "Connection", "Join retry delay", nullptr, 1, 300, 1, "s", 5),
    DEF_INT("irc_pingout", "Connection", "Ping timeout",   "Drop the link if the server is silent this long", 30, 900, 10, "s", 260),

    // --- Chat ------------------------------------------------------------
    DEF_BOOL("irc_ts",      "Chat", "Timestamps",        "Show HH:MM against every line", 1),
    DEF_BOOL("irc_joinpart","Chat", "Show joins/parts",  "Hide the join, part and quit noise on a busy channel", 1),
    DEF_BOOL("irc_filter",  "Chat", "Filter mode",       "Show only what people say. Hides joins, parts, quits, modes, topics and other people's kicks - anything that happens to you is still shown.", 0),
    DEF_INT("irc_scrollbk","Chat", "Scrollback",        "Lines kept per window. This lives in internal RAM, so large values across several windows cost real memory.", 100, 3000, 100, "lines", 400),
    DEF_TEXT("irc_hilight", "Chat", "Highlight words",   "Comma separated, in addition to your nick", ""),
    DEF_TEXT("irc_ignore",  "Chat", "Ignore list",       "Comma separated nicks whose messages are dropped. Wildcards allowed, e.g. bot* or *!*@spam.host", ""),
    // Toggled by the button in the input row, not by a settings row.
    DEF_HIDDEN("irc_topbar", "Chat", 1),

    // --- Theme -----------------------------------------------------------
    // Five colours; everything else on screen is derived from them, so the
    // menu stays short and a custom theme cannot come out incoherent.
    DEF_COLOR("th_accent",  "Theme", "Accent",          theme::kDefaultAccent),
    DEF_COLOR("th_bg",      "Theme", "Background",      theme::kDefaultBackground),
    DEF_COLOR("th_text",    "Theme", "Text",            theme::kDefaultText),
    DEF_COLOR("th_panel",   "Theme", "Status bar",      theme::kDefaultPanel),
    DEF_COLOR("th_input",   "Theme", "Message box",     theme::kDefaultInput),

    // --- Device ----------------------------------------------------------
    DEF_TEXT("dev_name",    "Device",  "Device name",      "Used as the WiFi hostname", "acid-drop"),
    DEF_ENUM("clock_fmt",   "Device",  "Clock format",     nullptr, kOptClock, 0),
    DEF_INT("tz_offset",   "Device",  "UTC offset",       "Minutes ahead of UTC. -300 is US Eastern.", -720, 840, 15, "min", -300),
    DEF_BOOL("dst",         "Device",  "Daylight saving",  "Adds one hour while in effect", 1),
    DEF_BOOL("ntp_enable",  "Device",  "Sync clock (NTP)", "Requires WiFi", 1),
    DEF_TEXT("ntp_server",  "Device",  "NTP server",       nullptr, "pool.ntp.org"),
    DEF_INT("splash_ms",   "Device",  "Splash time",      "How long the boot logo stays up", 0, 5000, 250, "ms", 1500),

    // --- Display ---------------------------------------------------------
    DEF_INT("brightness",  "Display", "Brightness",       nullptr, 5, 255, 5, nullptr, 200),
    DEF_INT("dim_secs",    "Display", "Dim after",        "Seconds of inactivity before dimming. 0 disables.", 0, 600, 5, "s", 20),
    DEF_INT("off_secs",    "Display", "Screen off after", "Seconds of inactivity before the backlight goes out. 0 disables.", 0, 1800, 10, "s", 60),
    DEF_INT("dim_level",   "Display", "Dim level",        nullptr, 1, 128, 1, nullptr, 25),
    DEF_BOOL("sb_seconds",  "Display", "Seconds in clock", "Show seconds in the status bar", 0),
    DEF_BOOL("kb_light",    "Display", "Keyboard backlight", "Light the keyboard at boot instead of waiting for ALT+B", 0),
    DEF_INT("kb_bright",   "Display", "Keyboard brightness", "ALT+B also toggles back to this level", 0, 255, 5, nullptr, 128),
    DEF_INT("ball_vstep",  "Display", "Trackball up/down",   "Pulses per step. Lower is more sensitive.", 1, 8, 1, nullptr, 2),
    DEF_INT("ball_hstep",  "Display", "Trackball left/right","Pulses per step. Lower is more sensitive.", 1, 15, 1, nullptr, 5),

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
    DEF_BOOL("net_static",  "WiFi",    "Static address",   "Off uses DHCP. On requires the address, gateway and mask below.", 0),
    DEF_TEXT("net_ip",      "WiFi",    "IP address",       nullptr, ""),
    DEF_TEXT("net_gw",      "WiFi",    "Gateway",          nullptr, ""),
    DEF_TEXT("net_mask",    "WiFi",    "Subnet mask",      nullptr, "255.255.255.0"),
    DEF_TEXT("net_dns1",    "WiFi",    "DNS server",       nullptr, ""),
    DEF_TEXT("net_dns2",    "WiFi",    "DNS server 2",     nullptr, ""),

    // --- Advanced --------------------------------------------------------
    DEF_ENUM("log_level",   "Advanced", "Log level",       nullptr, kOptLogLevel, 2),
    DEF_BOOL("log_screen",  "Advanced", "Keep log in memory", "Logging always runs; this keeps the last 250 lines in RAM so the System log screen has something to show. Off frees that RAM and leaves the screen empty.", 1),
};

#undef DEF_BOOL
#undef DEF_HIDDEN
#undef DEF_INT
#undef DEF_FLOAT
#undef DEF_TEXT
#undef DEF_SECRET
#undef DEF_COLOR
#undef DEF_ENUM

// --- storage --------------------------------------------------------------
// Keyed by the registry's own string literals and compared with strcmp, so a
// lookup costs no allocation. Keying by String would build one on every read,
// and these are read from the status bar and launcher tick paths.
struct CStrLess {
    bool operator()(const char* a, const char* b) const { return strcmp(a, b) < 0; }
};

Preferences                             s_prefs;
std::map<const char*, int32_t, CStrLess> s_nums;
std::map<const char*, String, CStrLess>  s_texts;
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
        case SettingType::Color:
            // A colour is three bytes, not a range to clamp into.
            return value & 0xFFFFFF;
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
        case SettingType::Color: {
            char buffer[10];
            snprintf(buffer, sizeof(buffer), "#%06lX",
                     static_cast<unsigned long>(getInt(key) & 0xFFFFFF));
            return String(buffer);
        }
    }
    return String();
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

} // namespace settings
