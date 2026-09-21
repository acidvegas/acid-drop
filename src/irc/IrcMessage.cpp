#include "irc/IrcMessage.h"

namespace {

const String kEmpty;

char ircLower(char c) {
    // RFC 1459 casemapping: []\~ are the uppercase forms of {}|^.
    if (c >= 'A' && c <= 'Z') return c + 32;
    switch (c) {
        case '[':  return '{';
        case ']':  return '}';
        case '\\': return '|';
        case '~':  return '^';
        default:   return c;
    }
}

} // namespace

bool IrcMessage::isNumeric() const {
    if (command.length() != 3) return false;
    for (int i = 0; i < 3; i++) {
        if (!isdigit(static_cast<unsigned char>(command[i]))) return false;
    }
    return true;
}

int IrcMessage::numeric() const {
    return isNumeric() ? command.toInt() : 0;
}

const String& IrcMessage::param(size_t index) const {
    return index < params.size() ? params[index] : kEmpty;
}

String IrcMessage::tag(const char* name) const {
    if (tagsRaw.isEmpty()) return String();

    const String wanted(name);
    int start = 0;
    while (start < static_cast<int>(tagsRaw.length())) {
        int end = tagsRaw.indexOf(';', start);
        if (end < 0) end = tagsRaw.length();

        const String pair = tagsRaw.substring(start, end);
        const int equals = pair.indexOf('=');
        const String key = equals < 0 ? pair : pair.substring(0, equals);

        if (key == wanted) {
            return equals < 0 ? String("") : pair.substring(equals + 1);
        }
        start = end + 1;
    }
    return String();
}

bool IrcMessage::fromServer() const {
    return !prefix.isEmpty() && prefix.indexOf('!') < 0 && prefix.indexOf('.') >= 0;
}

IrcMessage IrcMessage::parse(const String& line) {
    IrcMessage message;

    String rest = line;
    rest.trim();
    if (rest.isEmpty()) return message;

    // @tags
    if (rest[0] == '@') {
        const int space = rest.indexOf(' ');
        if (space < 0) return message;
        message.tagsRaw = rest.substring(1, space);
        rest = rest.substring(space + 1);
        rest.trim();
    }

    // :prefix
    if (!rest.isEmpty() && rest[0] == ':') {
        const int space = rest.indexOf(' ');
        if (space < 0) return message;
        message.prefix = rest.substring(1, space);
        rest = rest.substring(space + 1);
        rest.trim();

        const int bang = message.prefix.indexOf('!');
        const int at   = message.prefix.indexOf('@');
        if (bang > 0) {
            message.nick = message.prefix.substring(0, bang);
            if (at > bang) {
                message.user = message.prefix.substring(bang + 1, at);
                message.host = message.prefix.substring(at + 1);
            } else {
                message.user = message.prefix.substring(bang + 1);
            }
        } else if (at > 0) {
            message.nick = message.prefix.substring(0, at);
            message.host = message.prefix.substring(at + 1);
        } else {
            message.nick = message.prefix;
        }
    }

    // COMMAND
    {
        const int space = rest.indexOf(' ');
        if (space < 0) {
            message.command = rest;
            message.command.toUpperCase();
            return message;
        }
        message.command = rest.substring(0, space);
        message.command.toUpperCase();
        rest = rest.substring(space + 1);
    }

    // params, with everything after " :" kept whole
    while (!rest.isEmpty()) {
        if (rest[0] == ':') {
            message.params.push_back(rest.substring(1));
            break;
        }
        const int space = rest.indexOf(' ');
        if (space < 0) {
            message.params.push_back(rest);
            break;
        }
        if (space > 0) message.params.push_back(rest.substring(0, space));
        rest = rest.substring(space + 1);
        // Leading spaces before the trailing colon are not significant.
        while (!rest.isEmpty() && rest[0] == ' ') rest = rest.substring(1);
    }

    return message;
}

namespace irc {

std::vector<String> splitList(const String& text, char separator) {
    std::vector<String> out;
    int start = 0;
    while (start <= static_cast<int>(text.length())) {
        int end = text.indexOf(separator, start);
        if (end < 0) end = text.length();

        String item = text.substring(start, end);
        item.trim();
        if (!item.isEmpty()) out.push_back(item);

        start = end + 1;
    }
    return out;
}

bool equalsIgnoreCaseIrc(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    for (unsigned int i = 0; i < a.length(); i++) {
        if (ircLower(a[i]) != ircLower(b[i])) return false;
    }
    return true;
}

bool isChannel(const String& name) {
    if (name.isEmpty()) return false;
    const char c = name[0];
    return c == '#' || c == '&' || c == '!' || c == '+';
}

String base64Encode(const uint8_t* data, size_t length) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    String out;
    out.reserve(((length + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < length) {
        const uint32_t triple = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += kAlphabet[(triple >> 18) & 0x3F];
        out += kAlphabet[(triple >> 12) & 0x3F];
        out += kAlphabet[(triple >> 6)  & 0x3F];
        out += kAlphabet[triple & 0x3F];
        i += 3;
    }

    if (i < length) {
        const uint32_t remaining = length - i;
        uint32_t triple = data[i] << 16;
        if (remaining == 2) triple |= data[i + 1] << 8;

        out += kAlphabet[(triple >> 18) & 0x3F];
        out += kAlphabet[(triple >> 12) & 0x3F];
        out += remaining == 2 ? kAlphabet[(triple >> 6) & 0x3F] : '=';
        out += '=';
    }

    return out;
}

} // namespace irc
