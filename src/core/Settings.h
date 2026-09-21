#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

// A single registry describes every tweakable on the device. The NVS layer, the
// Settings app and the JSON import/export are all generated from it, so adding
// an option means adding one row to kDefs in Settings.cpp.

enum class SettingType : uint8_t {
    Bool,
    Int,
    Float,
    Text,
    Enum,
};

struct SettingDef {
    const char*  key;       // NVS key, must be <= 15 chars
    const char*  section;
    const char*  label;
    const char*  help;      // one line shown under the row, may be nullptr
    SettingType  type;

    // Int/Float bounds. Float values are stored scaled by `scale`.
    int32_t      min;
    int32_t      max;
    int32_t      step;
    int32_t      scale;     // Float only: stored = value * scale
    const char*  unit;      // may be nullptr

    // Enum options, nullptr-terminated.
    const char* const* options;

    // Defaults. Only the field matching `type` is meaningful.
    int32_t      defNum;
    const char*  defText;

    bool         secret;        // render masked, omit from plaintext export
    bool         needsRestart;  // flag the row in the UI
};

namespace settings {

using ChangeCb = std::function<void(const char* key)>;

// Loads every key from NVS, filling in defaults for anything unset.
void begin();

const std::vector<SettingDef>& defs();
const SettingDef*              find(const char* key);
std::vector<const char*>       sections();

// Reads. An unknown key returns the type's zero value and logs an error.
bool    getBool(const char* key);
int32_t getInt(const char* key);
float   getFloat(const char* key);
String  getText(const char* key);
uint8_t getEnum(const char* key);              // index into def->options
const char* getEnumLabel(const char* key);

// Writes. Values are clamped to the def's bounds, persisted to NVS and then
// broadcast to listeners. Writing an unchanged value is a no-op.
void setBool(const char* key, bool value);
void setInt(const char* key, int32_t value);
void setFloat(const char* key, float value);
void setText(const char* key, const String& value);
void setEnum(const char* key, uint8_t index);

// Generic access, used by the settings UI and the JSON codec.
String  getAsString(const char* key);
void    setFromString(const char* key, const String& value);

void resetToDefault(const char* key);
void resetSection(const char* section);
void factoryReset();   // wipes NVS entirely; caller should reboot

// Listeners fire after the value has been persisted. `key` is the stable
// pointer from the registry, so listeners can compare with ==, but strcmp is
// clearer and just as cheap here.
void onChange(ChangeCb cb);

// Config import/export. The SD card is mounted by the caller.
bool exportJson(const String& path, bool includeSecrets);
bool importJson(const String& path);

} // namespace settings
