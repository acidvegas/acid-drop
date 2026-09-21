#include "board/Audio.h"

#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorRTTTL.h>
#include <AudioOutputI2S.h>

#include "board/pins.h"
#include "core/Log.h"
#include "core/Settings.h"

namespace audio {
namespace {

constexpr const char* TAG = "audio";

// The opening of the original firmware's Take On Me jingle, cut short on
// purpose: the splash is held until the tune ends, and the full riff made
// every boot four seconds long.
const char kBoot[] PROGMEM =
    "TakeOnMe:d=8,o=5,b=280:f#,f#,f#,d,p,b4,p,e,p,e,p,e,g#,g#,a,b";

const char kMention[]      PROGMEM = "Mention:d=4,o=6,b=200:16e,16g,16c7";
const char kPrivate[]      PROGMEM = "Query:d=4,o=6,b=200:16c7,16g,16c7";
const char kConnected[]    PROGMEM = "Up:d=4,o=5,b=220:16c,16e,16g";
const char kDisconnected[] PROGMEM = "Down:d=4,o=5,b=220:16g,16e,16c";
const char kLowBattery[]   PROGMEM = "Low:d=4,o=4,b=140:8c,8p,8c";
const char kKey[]          PROGMEM = "Key:d=4,o=7,b=400:32c";

AudioGeneratorRTTTL*    s_generator = nullptr;
AudioOutputI2S*         s_output    = nullptr;
AudioFileSourcePROGMEM* s_source    = nullptr;

bool    s_enabled = true;
uint8_t s_volume  = 12;

void releaseSource() {
    if (s_generator) {
        if (s_generator->isRunning()) s_generator->stop();
        delete s_generator;
        s_generator = nullptr;
    }
    if (s_source) {
        delete s_source;
        s_source = nullptr;
    }
}

const char* tuneFor(Alert kind) {
    switch (kind) {
        case Alert::Boot:           return kBoot;
        case Alert::Mention:        return kMention;
        case Alert::PrivateMessage: return kPrivate;
        case Alert::Connected:      return kConnected;
        case Alert::Disconnected:   return kDisconnected;
        case Alert::LowBattery:     return kLowBattery;
        case Alert::Key:            return kKey;
    }
    return nullptr;
}

// Whether this particular alert is switched on in settings.
bool alertAllowed(Alert kind) {
    switch (kind) {
        case Alert::Boot:           return settings::getBool("snd_boot");
        case Alert::Mention:        return settings::getBool("snd_mention");
        case Alert::PrivateMessage: return settings::getBool("snd_msg");
        case Alert::Connected:
        case Alert::Disconnected:   return settings::getBool("snd_connect");
        case Alert::LowBattery:     return settings::getBool("batt_beep");
        case Alert::Key:            return settings::getBool("snd_key");
    }
    return false;
}

} // namespace

void begin() {
    s_output = new AudioOutputI2S();
    s_output->SetPinout(BOARD_I2S_BCK, BOARD_I2S_WS, BOARD_I2S_DOUT);
    applySettings();
    LOG_I(TAG, "I2S output ready");
}

void applySettings() {
    setEnabled(settings::getBool("snd_enable"));
    setVolume(settings::getInt("snd_volume"));
}

void loop() {
    if (!s_generator) return;

    if (s_generator->isRunning()) {
        if (!s_generator->loop()) {
            releaseSource();
        }
        return;
    }
    releaseSource();
}

void play(const char* rtttl) {
    if (!s_enabled || !s_output || rtttl == nullptr) return;

    // One tune at a time; a new one cuts off whatever is playing.
    releaseSource();

    s_source    = new AudioFileSourcePROGMEM(rtttl, strlen_P(rtttl));
    s_generator = new AudioGeneratorRTTTL();

    if (!s_generator->begin(s_source, s_output)) {
        LOG_W(TAG, "could not start tune");
        releaseSource();
    }
}

void alert(Alert kind) {
    if (!s_enabled || !alertAllowed(kind)) return;
    play(tuneFor(kind));
}

void stop() { releaseSource(); }

bool isPlaying() { return s_generator && s_generator->isRunning(); }

void setVolume(uint8_t level) {
    s_volume = level > 21 ? 21 : level;
    if (s_output) {
        // The I2S output takes a gain multiplier rather than a step index.
        s_output->SetGain(static_cast<float>(s_volume) / 21.0f);
    }
}

void setEnabled(bool enabled) {
    s_enabled = enabled;
    if (!enabled) stop();
}

bool enabled() { return s_enabled; }

} // namespace audio
