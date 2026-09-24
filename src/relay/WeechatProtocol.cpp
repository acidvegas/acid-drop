#include "relay/WeechatProtocol.h"

#include <string.h>

#include "ui/TextFormat.h"

namespace weechat {

Type typeFromCode(const char* code) {
    if (strncmp(code, "chr", 3) == 0) return Type::Char;
    if (strncmp(code, "int", 3) == 0) return Type::Int;
    if (strncmp(code, "lon", 3) == 0) return Type::Long;
    if (strncmp(code, "str", 3) == 0) return Type::Str;
    if (strncmp(code, "buf", 3) == 0) return Type::Buffer;
    if (strncmp(code, "ptr", 3) == 0) return Type::Pointer;
    if (strncmp(code, "tim", 3) == 0) return Type::Time;
    if (strncmp(code, "htb", 3) == 0) return Type::HashTable;
    if (strncmp(code, "hda", 3) == 0) return Type::HData;
    if (strncmp(code, "inf", 3) == 0) return Type::Info;
    if (strncmp(code, "inl", 3) == 0) return Type::InfoList;
    if (strncmp(code, "arr", 3) == 0) return Type::Array;
    return Type::Unknown;
}

bool Reader::want(size_t bytes) {
    if (!m_ok) return false;
    // Written this way round so a huge `bytes` cannot wrap the sum past the
    // check on a 32-bit size_t. m_offset never exceeds m_length.
    if (bytes > m_length - m_offset) { fail(); return false; }
    return true;
}

uint8_t Reader::readByte() {
    if (!want(1)) return 0;
    return m_data[m_offset++];
}

uint32_t Reader::readUInt() {
    if (!want(4)) return 0;
    const uint32_t value = (static_cast<uint32_t>(m_data[m_offset])     << 24) |
                           (static_cast<uint32_t>(m_data[m_offset + 1]) << 16) |
                           (static_cast<uint32_t>(m_data[m_offset + 2]) << 8)  |
                            static_cast<uint32_t>(m_data[m_offset + 3]);
    m_offset += 4;
    return value;
}

int32_t Reader::readInt() { return static_cast<int32_t>(readUInt()); }

String Reader::readString() {
    const uint32_t length = readUInt();
    if (!m_ok) return String();

    // 0xFFFFFFFF is the wire's null. Treat it as empty: nothing here needs to
    // tell "absent" from "blank".
    if (length == 0xFFFFFFFFu || length == 0) return String();

    // A length that cannot fit in the frame is a malformed or hostile message.
    if (!want(length)) return String();

    String out;
    out.reserve(length + 1);
    for (uint32_t i = 0; i < length; i++) out += static_cast<char>(m_data[m_offset + i]);
    m_offset += length;
    return out;
}

// Pointer, time and long share a shape: one length byte, then that many ASCII
// characters.
static String readShortAscii(Reader& reader, const uint8_t* data, size_t& offset,
                             size_t length, bool& ok) {
    (void)reader;
    if (!ok || offset >= length) { ok = false; return String(); }

    const uint8_t count = data[offset++];
    if (offset + count > length) { ok = false; return String(); }

    String out;
    out.reserve(count + 1);
    for (uint8_t i = 0; i < count; i++) out += static_cast<char>(data[offset + i]);
    offset += count;
    return out;
}

String Reader::readPointer() {
    if (!m_ok) return String();
    bool ok = true;
    const String out = readShortAscii(*this, m_data, m_offset, m_length, ok);
    if (!ok) { fail(); return String(); }

    // A null pointer arrives as the single character "0"; normalise it to the
    // "0x..." form everything else uses so comparisons are straightforward.
    if (out == "0") return String("0x0");
    return "0x" + out;
}

String Reader::readTime() {
    if (!m_ok) return String();
    bool ok = true;
    const String out = readShortAscii(*this, m_data, m_offset, m_length, ok);
    if (!ok) fail();
    return out;
}

String Reader::readLong() {
    if (!m_ok) return String();
    bool ok = true;
    const String out = readShortAscii(*this, m_data, m_offset, m_length, ok);
    if (!ok) fail();
    return out;
}

Type Reader::readType() {
    if (!want(3)) return Type::Unknown;
    char code[4] = {0};
    code[0] = static_cast<char>(m_data[m_offset]);
    code[1] = static_cast<char>(m_data[m_offset + 1]);
    code[2] = static_cast<char>(m_data[m_offset + 2]);
    m_offset += 3;
    return typeFromCode(code);
}

String Reader::readValueAsString(Type type) {
    switch (type) {
        // A C char on the sending side, and signed: notify_level uses -1.
        case Type::Char:    return String(static_cast<int>(static_cast<int8_t>(readByte())));
        case Type::Int:     return String(readInt());
        case Type::Long:    return readLong();
        case Type::Str:
        case Type::Buffer:  return readString();
        case Type::Pointer: return readPointer();
        case Type::Time:    return readTime();
        case Type::HashTable: {
            // Only string-to-string tables (local variables) are kept, flattened
            // for tableValue(). Any other shape is skipped.
            const size_t start = m_offset;
            const Type keyType   = readType();
            const Type valueType = readType();
            if (keyType != Type::Str || valueType != Type::Str) {
                m_offset = start;
                skipValue(type);
                return String();
            }
            const uint32_t count = readUInt();
            String flat;
            for (uint32_t i = 0; i < count && m_ok && remaining() > 0; i++) {
                flat += readString();
                flat += kTableKeyEnd;
                flat += readString();
                flat += kTableEntryEnd;
            }
            return flat;
        }
        default:
            // Anything structured is skipped rather than flattened: this
            // client only reads scalar keys, and guessing at a rendering for
            // the rest would be worse than admitting it is not handled.
            skipValue(type);
            return String();
    }
}

void Reader::skipValue(Type type) {
    if (!m_ok) return;

    // Structured values nest by recursion, so a frame nesting them deeply
    // would exhaust the stack. WeeChat never nests more than a level or two.
    constexpr uint8_t kMaxDepth = 8;
    if (m_depth >= kMaxDepth) { fail(); return; }
    m_depth++;

    switch (type) {
        case Type::Char:    readByte();    break;
        case Type::Int:     readUInt();    break;
        case Type::Long:    readLong();    break;
        case Type::Str:
        case Type::Buffer:  readString();  break;
        case Type::Pointer: readPointer(); break;
        case Type::Time:    readTime();    break;

        case Type::HashTable: {
            const Type keyType   = readType();
            const Type valueType = readType();
            const uint32_t count = readUInt();
            // Bounded by what is left: a count field claiming millions of
            // entries must not become millions of reads.
            for (uint32_t i = 0; i < count && m_ok && remaining() > 0; i++) {
                skipValue(keyType);
                skipValue(valueType);
            }
            break;
        }

        case Type::HData: {
            HData discard;
            readHData(discard);
            break;
        }

        case Type::Info:
            readString();
            readString();
            break;

        case Type::InfoList: {
            readString();                      // name
            const uint32_t items = readUInt();
            for (uint32_t i = 0; i < items && m_ok && remaining() > 0; i++) {
                const uint32_t vars = readUInt();
                for (uint32_t v = 0; v < vars && m_ok && remaining() > 0; v++) {
                    readString();              // variable name
                    skipValue(readType());
                }
            }
            break;
        }

        case Type::Array: {
            const Type valueType = readType();
            const uint32_t count = readUInt();
            for (uint32_t i = 0; i < count && m_ok && remaining() > 0; i++) {
                skipValue(valueType);
            }
            break;
        }

        case Type::Unknown:
        default:
            // The stream cannot be resynchronised once an unknown type has
            // been seen, because there is no way to know its length.
            fail();
            break;
    }

    m_depth--;
}

bool Reader::readHData(HData& out) {
    out.path = readString();
    const String keySpec = readString();
    const uint32_t count = readUInt();
    if (!m_ok) return false;

    std::vector<String> keyNames;
    std::vector<Type>   keyTypes;
    parseKeys(keySpec, keyNames, keyTypes);

    // The h-path says how many pointers precede each item: one per level.
    uint8_t levels = 1;
    for (unsigned int i = 0; i < out.path.length(); i++) {
        if (out.path[i] == '/') levels++;
    }

    // Every item carries at least one byte per pointer, so a count larger
    // than what is left of the frame is a corrupt field, not a big result.
    if (count > remaining()) { fail(); return false; }

    out.items.reserve(count < 64 ? count : 64);

    for (uint32_t i = 0; i < count; i++) {
        if (!m_ok) return false;

        HDataItem item;
        for (uint8_t level = 0; level < levels; level++) {
            item.pointers.push_back(readPointer());
        }

        item.keys = keyNames;
        for (size_t k = 0; k < keyTypes.size(); k++) {
            item.values.push_back(readValueAsString(keyTypes[k]));
        }

        if (!m_ok) return false;
        out.items.push_back(std::move(item));
    }

    return m_ok;
}

bool Reader::readInfoList(std::vector<InfoListItem>& out) {
    readString();                              // infolist name
    const uint32_t count = readUInt();
    if (!m_ok || count > remaining()) { fail(); return false; }

    for (uint32_t i = 0; i < count && m_ok; i++) {
        const uint32_t vars = readUInt();
        if (!m_ok || vars > remaining()) { fail(); return false; }

        InfoListItem item;
        for (uint32_t v = 0; v < vars && m_ok; v++) {
            item.names.push_back(readString());
            item.values.push_back(readValueAsString(readType()));
        }
        if (!m_ok) return false;
        out.push_back(std::move(item));
    }
    return m_ok;
}

String tableValue(const String& flat, const char* key) {
    const size_t keyLength = strlen(key);
    unsigned int start = 0;
    while (start < flat.length()) {
        const int keyEnd = flat.indexOf(kTableKeyEnd, start);
        if (keyEnd < 0) break;
        const int entryEnd = flat.indexOf(kTableEntryEnd, keyEnd + 1);
        if (entryEnd < 0) break;

        if (static_cast<size_t>(keyEnd) - start == keyLength &&
            strncmp(flat.c_str() + start, key, keyLength) == 0) {
            return flat.substring(keyEnd + 1, entryEnd);
        }
        start = entryEnd + 1;
    }
    return String();
}

namespace {

// Codes from src/gui/gui-color.h.
constexpr char kColorChar      = '\x19';
constexpr char kSetAttrChar    = '\x1A';
constexpr char kRemoveAttrChar = '\x1B';
constexpr char kResetChar      = '\x1C';

// The attribute flags that may appear before a colour number: blink, dim,
// bold, reverse, italic, underline, keep-attributes.
bool isAttrFlag(char c) {
    return c == '%' || c == '.' || c == '*' || c == '!' || c == '/' ||
           c == '_' || c == '|';
}

bool isDigitAt(const String& text, unsigned int index) {
    return index < text.length() && isdigit(static_cast<unsigned char>(text[index]));
}

// Skips the attribute flags then a colour number, which is five digits when
// extended and two otherwise.
void skipColorSpec(const String& text, unsigned int& i, bool allowAttrs) {
    if (i < text.length() && text[i] == '@') {
        i++;
        if (allowAttrs) while (i < text.length() && isAttrFlag(text[i])) i++;
        if (i + 4 < text.length()) i += 5;
        return;
    }
    if (allowAttrs) while (i < text.length() && isAttrFlag(text[i])) i++;
    if (i + 1 < text.length()) i += 2;
}

// Attribute characters that follow 0x1A (set) and 0x1B (remove).
constexpr char kAttrBold      = '\x01';
constexpr char kAttrReverse   = '\x02';
constexpr char kAttrItalic    = '\x03';
constexpr char kAttrUnderline = '\x04';

// WeeChat's basic colours, by index into gui_weechat_colors
// (src/gui/curses/gui-curses-color.c), as the mIRC index the IRC plugin maps
// to that name (irc_color_to_weechat in src/plugins/irc/irc-color.c). Index 0
// is "default", which is 99 in mIRC.
constexpr uint8_t kBasicToMirc[17] = {
    99, 1, 14, 5, 4, 3, 9, 7, 8, 2, 12, 6, 13, 10, 11, 15, 0,
};

// mIRC colours 16-98 as the xterm numbers the IRC plugin turns them into, from
// the same table. Used backwards to recover the mIRC index.
constexpr uint8_t kMircExtendedXterm[83] = {
     52,  94, 100,  58,  22,  29,  23,  24,  17,  54,  53,  89,  88, 130, 142,  64,
     28,  35,  30,  25,  18,  91,  90, 125, 124, 166, 184, 106,  34,  49,  37,  33,
     19, 129, 127, 161, 196, 208, 226, 154,  46,  86,  51,  75,  21, 171, 201, 198,
    203, 215, 227, 191,  83, 122,  87, 111,  63, 177, 207, 205, 217, 223, 229, 193,
    157, 158, 159, 153, 147, 183, 219, 212,  16, 233, 235, 237, 239, 241, 244, 247,
    250, 254, 231,
};

// One colour as read from a code: a basic index or an extended (xterm)
// number, plus the attribute flags that may precede it.
struct ColorSpec {
    bool valid    = false;
    bool extended = false;
    int  number   = 0;
    bool bold = false, reverse = false, italic = false, underline = false;
    bool keepAttrs = false;
};

bool readDigits(const String& text, unsigned int at, unsigned int count, int& out) {
    if (at + count > text.length()) return false;
    out = 0;
    for (unsigned int k = 0; k < count; k++) {
        if (!isdigit(static_cast<unsigned char>(text[at + k]))) return false;
        out = out * 10 + (text[at + k] - '0');
    }
    return true;
}

void readAttrFlags(const String& text, unsigned int& i, ColorSpec& spec) {
    while (i < text.length() && isAttrFlag(text[i])) {
        switch (text[i]) {
            case '*': spec.bold      = true; break;
            case '!': spec.reverse   = true; break;
            case '/': spec.italic    = true; break;
            case '_': spec.underline = true; break;
            case '|': spec.keepAttrs = true; break;
            default:  break;   // blink and dim are not drawn here
        }
        i++;
    }
}

// Mirrors gui_window_string_apply_color_fg(): attribute flags, then five
// digits after '@' or two otherwise.
void readForeground(const String& text, unsigned int& i, ColorSpec& spec) {
    if (i < text.length() && text[i] == '@') {
        i++;
        readAttrFlags(text, i, spec);
        if (i + 4 < text.length()) {
            spec.extended = true;
            spec.valid    = readDigits(text, i, 5, spec.number);
            i += 5;
        }
        return;
    }
    readAttrFlags(text, i, spec);
    if (i + 1 < text.length()) {
        spec.valid = readDigits(text, i, 2, spec.number) && spec.number < 17;
        i += 2;
    }
}

// Mirrors gui_window_string_apply_color_bg(): no attribute flags.
void readBackground(const String& text, unsigned int& i, ColorSpec& spec) {
    if (i < text.length() && text[i] == '@') {
        if (i + 5 < text.length()) {
            spec.extended = true;
            spec.valid    = readDigits(text, i + 1, 5, spec.number);
            i += 6;
        }
        return;
    }
    if (i + 1 < text.length()) {
        spec.valid = readDigits(text, i, 2, spec.number) && spec.number < 17;
        i += 2;
    }
}

void appendMirc(String& out, bool foreground, uint8_t index) {
    if (index >= 99) {
        out += foreground ? "\x1B[39m" : "\x1B[49m";
        return;
    }
    const lv_color_t colour = textfmt::mircColor(index, lv_color_black());
    char sgr[24];
    snprintf(sgr, sizeof(sgr), "\x1B[%u;2;%u;%u;%um", foreground ? 38 : 48,
             colour.red, colour.green, colour.blue);
    out += sgr;
}

void appendColor(String& out, bool foreground, const ColorSpec& spec) {
    if (!spec.extended) {
        appendMirc(out, foreground, kBasicToMirc[spec.number]);
        return;
    }
    for (uint8_t k = 0; k < sizeof(kMircExtendedXterm); k++) {
        if (kMircExtendedXterm[k] == spec.number) {
            appendMirc(out, foreground, 16 + k);
            return;
        }
    }
    if (spec.number < 256) {
        char sgr[24];
        snprintf(sgr, sizeof(sgr), "\x1B[%u;5;%dm", foreground ? 38 : 48, spec.number);
        out += sgr;
    }
}

// A foreground colour without the keep flag clears every attribute it does
// not set itself, as gui_window_set_custom_color_fg() does.
void appendAttrs(String& out, const ColorSpec& spec) {
    if (!spec.keepAttrs) out += "\x1B[22;23;24;27m";
    if (spec.bold)      out += "\x1B[1m";
    if (spec.reverse)   out += "\x1B[7m";
    if (spec.italic)    out += "\x1B[3m";
    if (spec.underline) out += "\x1B[4m";
}

void appendAttrChange(String& out, char attr, bool set) {
    switch (attr) {
        case kAttrBold:      out += set ? "\x1B[1m" : "\x1B[22m"; break;
        case kAttrReverse:   out += set ? "\x1B[7m" : "\x1B[27m"; break;
        case kAttrItalic:    out += set ? "\x1B[3m" : "\x1B[23m"; break;
        case kAttrUnderline: out += set ? "\x1B[4m" : "\x1B[24m"; break;
        default:             break;   // blink and dim are not drawn here
    }
}

} // namespace

String stripColors(const String& text) {
    String out;
    out.reserve(text.length());

    unsigned int i = 0;
    while (i < text.length()) {
        const char c = text[i];

        if (c == kColorChar) {
            i++;
            if (i >= text.length()) break;

            switch (text[i]) {
                case 'F':                       // foreground
                    i++;
                    skipColorSpec(text, i, true);
                    break;

                case 'B':                       // background, no attributes
                    i++;
                    skipColorSpec(text, i, false);
                    break;

                case '*': {                     // foreground and background
                    i++;
                    skipColorSpec(text, i, true);
                    // ',' is the pre-2.6 separator and is still accepted, so
                    // old lines in the backlog keep working.
                    if (i < text.length() && (text[i] == ',' || text[i] == '~')) {
                        i++;
                        skipColorSpec(text, i, false);
                    }
                    break;
                }

                case '@':                       // extended colour, five digits
                    i++;
                    if (isDigitAt(text, i) && isDigitAt(text, i + 1) &&
                        isDigitAt(text, i + 2) && isDigitAt(text, i + 3) &&
                        isDigitAt(text, i + 4)) {
                        i += 5;
                    }
                    break;

                case 'E':                       // emphasis
                case kResetChar:
                    i++;
                    break;

                case 'b':                       // bar colour: one more char
                    i++;
                    if (i < text.length()) i++;
                    break;

                default:                        // a plain two-digit index
                    if (isDigitAt(text, i) && isDigitAt(text, i + 1)) i += 2;
                    break;
            }
            continue;
        }

        if (c == kSetAttrChar || c == kRemoveAttrChar) {
            i++;
            if (i < text.length()) i++;   // the attribute character itself
            continue;
        }

        if (c == kResetChar) { i++; continue; }

        out += c;
        i++;
    }

    return out;
}

String colorsToAnsi(const String& text, const std::vector<String>* optionColors) {
    String out;
    out.reserve(text.length() + text.length() / 2);

    unsigned int i = 0;
    while (i < text.length()) {
        const char c = text[i];

        if (c == kColorChar) {
            i++;
            if (i >= text.length()) break;

            switch (text[i]) {
                case 'F': {
                    i++;
                    ColorSpec fg;
                    readForeground(text, i, fg);
                    if (fg.valid) {
                        appendAttrs(out, fg);
                        appendColor(out, true, fg);
                    }
                    break;
                }

                case 'B': {
                    i++;
                    ColorSpec bg;
                    readBackground(text, i, bg);
                    if (bg.valid) appendColor(out, false, bg);
                    break;
                }

                case '*': {
                    i++;
                    ColorSpec fg;
                    ColorSpec bg;
                    readForeground(text, i, fg);
                    // ',' is the pre-2.6 separator and is still accepted.
                    if (i < text.length() && (text[i] == ',' || text[i] == '~')) {
                        i++;
                        readBackground(text, i, bg);
                    }
                    // WeeChat applies neither half unless both are valid.
                    if (fg.valid && bg.valid) {
                        appendAttrs(out, fg);
                        appendColor(out, true, fg);
                        appendColor(out, false, bg);
                    }
                    break;
                }

                case '@':                       // colour pair: colours unknown here
                    i++;
                    if (isDigitAt(text, i) && isDigitAt(text, i + 1) &&
                        isDigitAt(text, i + 2) && isDigitAt(text, i + 3) &&
                        isDigitAt(text, i + 4)) {
                        i += 5;
                    }
                    break;

                case 'E':                       // emphasis
                    i++;
                    break;

                case kResetChar:                // colours only, attributes kept
                    i++;
                    out += "\x1B[39;49m";
                    break;

                case 'b':                       // bar colour: one more char
                    i++;
                    if (i < text.length()) i++;
                    break;

                default:
                    // One of WeeChat's own colour options, by index: a full
                    // style reset, then that option's colours and attributes
                    // (gui_window_set_weechat_color). Unknown ones only reset.
                    if (isDigitAt(text, i) && isDigitAt(text, i + 1)) {
                        const size_t index = (text[i] - '0') * 10 + (text[i + 1] - '0');
                        i += 2;
                        out += "\x1B[0m";
                        if (optionColors && index < optionColors->size()) {
                            out += (*optionColors)[index];
                        }
                    }
                    break;
            }
            continue;
        }

        if (c == kSetAttrChar || c == kRemoveAttrChar) {
            i++;
            if (i < text.length()) {
                appendAttrChange(out, text[i], c == kSetAttrChar);
                i++;
            }
            continue;
        }

        if (c == kResetChar) {
            out += "\x1B[0m";
            i++;
            continue;
        }

        out += c;
        i++;
    }

    return out;
}

String optionColorToAnsi(const String& fg, const String& bg) {
    // gui_weechat_colors order (src/gui/curses/gui-curses-color.c), which is
    // what the basic indices in kBasicToMirc refer to.
    static const char* const kNames[17] = {
        "default", "black", "darkgray", "red", "lightred", "green", "lightgreen",
        "brown", "yellow", "blue", "lightblue", "magenta", "lightmagenta",
        "cyan", "lightcyan", "gray", "white",
    };

    // A value is attribute characters followed by a name or an xterm number.
    auto parse = [](const String& value, ColorSpec& spec) {
        unsigned int i = 0;
        readAttrFlags(value, i, spec);
        const String name = value.substring(i);
        for (int n = 0; n < 17; n++) {
            if (name == kNames[n]) {
                spec.valid  = true;
                spec.number = n;
                return;
            }
        }
        int number = 0;
        if (!name.isEmpty() && readDigits(name, 0, name.length(), number)) {
            spec.valid    = true;
            spec.extended = true;
            spec.number   = number;
        }
    };

    String out;
    ColorSpec fgSpec;
    parse(fg, fgSpec);
    if (fgSpec.bold)      out += "\x1B[1m";
    if (fgSpec.reverse)   out += "\x1B[7m";
    if (fgSpec.italic)    out += "\x1B[3m";
    if (fgSpec.underline) out += "\x1B[4m";
    if (fgSpec.valid) appendColor(out, true, fgSpec);

    ColorSpec bgSpec;
    parse(bg, bgSpec);
    if (bgSpec.valid) appendColor(out, false, bgSpec);
    return out;
}

void parseKeys(const String& spec, std::vector<String>& names, std::vector<Type>& types) {
    names.clear();
    types.clear();

    int start = 0;
    while (start <= static_cast<int>(spec.length())) {
        int comma = spec.indexOf(',', start);
        if (comma < 0) comma = spec.length();

        const String pair = spec.substring(start, comma);
        const int colon = pair.indexOf(':');
        if (colon > 0) {
            names.push_back(pair.substring(0, colon));
            const String code = pair.substring(colon + 1);
            types.push_back(code.length() >= 3 ? typeFromCode(code.c_str()) : Type::Unknown);
        }

        if (comma >= static_cast<int>(spec.length())) break;
        start = comma + 1;
    }
}

} // namespace weechat
