#pragma once

#include <Arduino.h>
#include <vector>

// Reader for WeeChat's relay protocol, the binary one its own clients use.
//
// A message is a 4-byte big-endian length (counting itself), a compression
// byte, then a body: an id string followed by typed objects. Every object is
// introduced by a three-character type code - "str", "int", "hda" and so on -
// which is what makes this parseable without knowing in advance what a given
// command answers with.
//
// Everything here is bounds-checked and fails closed. The input is a socket on
// a device with no memory protection, so a truncated or hostile frame has to
// end with `ok() == false`, never with a read past the end of the buffer.

namespace weechat {

// Object type codes, as the three-character strings that appear on the wire.
enum class Type : uint8_t {
    Char, Int, Long, Str, Buffer, Pointer, Time, HashTable, HData, Info,
    InfoList, Array, Unknown,
};

Type typeFromCode(const char* code);

// One row of an hdata result: the pointers identifying it, and its values as
// strings keyed by name. Values are flattened to String because everything
// this client does with them is display or comparison, and it keeps the
// parser from having to hand out a variant type.
struct HDataItem {
    std::vector<String> pointers;
    std::vector<String> keys;
    std::vector<String> values;

    String value(const char* key) const {
        for (size_t i = 0; i < keys.size() && i < values.size(); i++) {
            if (keys[i] == key) return values[i];
        }
        return String();
    }
    bool has(const char* key) const {
        for (const String& name : keys) if (name == key) return true;
        return false;
    }
    // Pointers run root first, one per h-path level (relay-weechat-msg.c), so
    // the first is the root object and the last is the item itself. For a
    // single-level path they are the same.
    String pointer() const { return pointers.empty() ? String() : pointers[0]; }
};

// One item of an infolist: its variables as parallel name/value arrays.
struct InfoListItem {
    std::vector<String> names;
    std::vector<String> values;

    String value(const char* name) const {
        for (size_t i = 0; i < names.size() && i < values.size(); i++) {
            if (names[i] == name) return values[i];
        }
        return String();
    }
};

// A string-to-string hashtable value is flattened into one String with these
// separators, which cannot occur in WeeChat's local variable names or values.
constexpr char kTableKeyEnd   = '\x1F';
constexpr char kTableEntryEnd = '\x1E';

// Looks up `key` in a hashtable flattened by readValueAsString().
String tableValue(const String& flat, const char* key);

struct HData {
    String                 path;
    std::vector<HDataItem> items;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t length) : m_data(data), m_length(length) {}

    bool ok() const        { return m_ok; }
    size_t remaining() const { return m_ok && m_length > m_offset ? m_length - m_offset : 0; }

    uint8_t  readByte();
    int32_t  readInt();
    uint32_t readUInt();

    // A length-prefixed string. A length of 0xFFFFFFFF means null on the wire
    // and comes back empty here, which is the same thing for every use this
    // client has.
    String readString();

    // Pointer and time are both a one-byte length followed by ASCII.
    String readPointer();
    String readTime();
    String readLong();

    // Reads one object of `type` and renders it as a string.
    String readValueAsString(Type type);

    // Reads a whole hdata object. The type code must already have been read.
    bool readHData(HData& out);

    // Reads a whole infolist object. The type code must already have been read.
    bool readInfoList(std::vector<InfoListItem>& out);

    // Skips one object of `type`, for keys this client does not care about.
    void skipValue(Type type);

    // Reads the next three-byte type code.
    Type readType();

private:
    // Marks the read failed and stops everything after it from doing damage.
    void fail() { m_ok = false; }
    bool want(size_t bytes);

    const uint8_t* m_data;
    size_t         m_length;
    size_t         m_offset = 0;
    bool           m_ok     = true;
    uint8_t        m_depth  = 0;   // nesting of structured values being skipped
};

// Removes WeeChat's own colour and attribute codes from a string.
//
// WeeChat does not send mIRC codes: it sends its own encoding, introduced by
// 0x19 for colour and 0x1A/0x1B/0x1C for attributes, with a different payload
// shape for each variant. Handing that to the mIRC parser renders the codes as
// text, which is the "weird prefixed garbage" it produces.
//
// This is a direct port of gui_color_decode() from WeeChat's src/gui/gui-color.c,
// which is the function WeeChat itself uses to strip colours, so the lengths
// it skips are the lengths WeeChat writes.
String stripColors(const String& text);

// Translates WeeChat's colour and attribute codes into ANSI SGR sequences,
// which the renderer already understands, so colours and backgrounds survive.
//
// Follows how WeeChat's curses GUI applies each code (src/gui/curses/
// gui-curses-window.c). Colours that came from mIRC codes are mapped back to
// their mIRC index through the IRC plugin's own table and drawn in the mIRC
// palette, so a line looks the same here as it does in direct IRC mode.
//
// `optionColors` holds the ANSI rendering of each of WeeChat's own colour
// options, indexed as \x19NN numbers them (see optionColorToAnsi). Without it,
// or for an index it does not cover, such a code resets to the defaults.
String colorsToAnsi(const String& text, const std::vector<String>* optionColors);

// Renders one colour option pair as ANSI: the attributes and foreground of
// `fg` and the background of `bg`, each a value as WeeChat reports it
// (gui_color_get_name: attribute characters, then a colour name or number).
// A palette alias must already be resolved to its number.
String optionColorToAnsi(const String& fg, const String& bg);

// Splits WeeChat's "name:type,name:type" key list into parallel arrays.
void parseKeys(const String& spec, std::vector<String>& names, std::vector<Type>& types);

} // namespace weechat
