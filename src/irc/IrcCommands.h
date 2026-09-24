#pragma once

#include <Arduino.h>
#include <functional>

class IrcClient;
struct IrcBuffer;

// Slash command handling, kept out of the UI so the parsing is readable on its
// own. Anything not listed here is upper-cased and sent to the server as-is,
// which is what every other IRC client does and is the only way commands the
// firmware has never heard of still work.

namespace irccmd {

struct Context {
    IrcClient* client = nullptr;
    IrcBuffer* window = nullptr;      // the window the command was typed in

    std::function<void(const String&)> echo;          // local feedback line
    std::function<void(size_t)>        selectWindow;
    std::function<void()>              nextWindow;
    std::function<void()>              prevWindow;
    std::function<void()>              closeWindow;
    std::function<void()>              clearWindow;
    std::function<void()>              openSettings;
    std::function<void()>              openChannels;
};

// Runs `line` when it starts with '/'. Returns false when it is ordinary text
// that the caller should send as a message.
bool run(const String& line, const Context& context);

// Every command the client understands, nullptr-terminated, for completion.
const char* const* commandNames();

} // namespace irccmd
