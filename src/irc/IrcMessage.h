#pragma once

#include <Arduino.h>
#include <vector>

// RFC 1459 / RFC 2812 message, with IRCv3 message tags.
//
//   [@tags] [:prefix] COMMAND [params...] [:trailing]
//
// Parsing is done in place on a copy of the line; params point into it.

struct IrcMessage {
    String              tagsRaw;    // without the leading '@'
    String              prefix;     // without the leading ':'
    String              nick;       // prefix up to '!', or the whole prefix
    String              user;
    String              host;
    String              command;    // upper-cased
    std::vector<String> params;     // trailing is the last entry

    bool isNumeric() const;
    int  numeric() const;           // 0 when the command is not a numeric

    // Convenience: params[index], or an empty string when out of range.
    const String& param(size_t index) const;

    // Looks up an IRCv3 tag, e.g. "time" from "@time=2026-09-20T12:00:00.000Z".
    String tag(const char* name) const;

    // True when the prefix names a server rather than a user.
    bool fromServer() const;

    static IrcMessage parse(const String& line);
};

namespace irc {

// Splits a comma-separated list, trimming whitespace and dropping blanks.
std::vector<String> splitList(const String& text, char separator = ',');

// Case-insensitive compare using RFC 1459 casemapping, where {}|^ are the
// lowercase forms of []\~. Servers compare nicks and channels this way, so
// matching any other way will eventually route a message to the wrong window.
bool equalsIgnoreCaseIrc(const String& a, const String& b);

// True when `name` starts with a channel prefix.
bool isChannel(const String& name);

// base64, for SASL PLAIN.
String base64Encode(const uint8_t* data, size_t length);

} // namespace irc
