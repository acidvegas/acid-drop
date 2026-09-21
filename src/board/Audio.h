#pragma once

#include <Arduino.h>

// Speaker output over I2S. Sounds are RTTTL ringtones, which are tiny, easy to
// edit and already how the original firmware's boot jingle was written.

enum class Alert : uint8_t {
    Boot,
    Mention,
    PrivateMessage,
    Connected,
    Disconnected,
    LowBattery,
    Key,
};

namespace audio {

void begin();
void loop();                      // must run often while a tune is playing

void applySettings();

void play(const char* rtttl);
void alert(Alert kind);
void stop();
bool isPlaying();

void setVolume(uint8_t level);    // 0-21
void setEnabled(bool enabled);
bool enabled();

} // namespace audio
