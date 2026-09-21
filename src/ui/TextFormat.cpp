#include "ui/TextFormat.h"

namespace textfmt {
namespace {

// --- mIRC palette ---------------------------------------------------------
// 0-15 are the classic colours, 16-98 the mIRC 7 extension, 99 is "default".
const uint32_t kMirc[99] = {
    0xFFFFFF, 0x000000, 0x00007F, 0x009300, 0xFF0000, 0x7F0000, 0x9C009C, 0xFC7F00,
    0xFFFF00, 0x00FC00, 0x009393, 0x00FFFF, 0x0000FC, 0xFF00FF, 0x7F7F7F, 0xD2D2D2,
    0x470000, 0x472100, 0x474700, 0x324700, 0x004700, 0x00472C, 0x004747, 0x002747,
    0x000047, 0x2E0047, 0x470047, 0x47002A, 0x740000, 0x743A00, 0x747400, 0x517400,
    0x007400, 0x007449, 0x007474, 0x004074, 0x000074, 0x4B0074, 0x740074, 0x740045,
    0xB50000, 0xB56300, 0xB5B500, 0x7DB500, 0x00B500, 0x00B571, 0x00B5B5, 0x0063B5,
    0x0000B5, 0x7500B5, 0xB500B5, 0xB5006B, 0xFF0000, 0xFF8C00, 0xFFFF00, 0xB2FF00,
    0x00FF00, 0x00FFA0, 0x00FFFF, 0x008CFF, 0x0000FF, 0xA500FF, 0xFF00FF, 0xFF0098,
    0xFF5959, 0xFFB459, 0xFFFF71, 0xCFFF60, 0x6FFF6F, 0x65FFC9, 0x6DFFFF, 0x59B4FF,
    0x5959FF, 0xC459FF, 0xFF66FF, 0xFF59BC, 0xFF9C9C, 0xFFD39C, 0xFFFF9C, 0xE2FF9C,
    0x9CFF9C, 0x9CFFDB, 0x9CFFFF, 0x9CD3FF, 0x9C9CFF, 0xDC9CFF, 0xFF9CFF, 0xFF94D3,
    0x000000, 0x131313, 0x282828, 0x363636, 0x4D4D4D, 0x656565, 0x818181, 0x9F9F9F,
    0xBCBCBC, 0xE2E2E2, 0xFFFFFF,
};

// --- code page 437 --------------------------------------------------------
// Only the halves that differ from ASCII are stored.
const uint16_t kCp437Low[32] = {
    0x0020, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
};

const uint16_t kCp437High[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

// --- IRC control characters ----------------------------------------------
constexpr char CTRL_BOLD      = 0x02;
constexpr char CTRL_COLOR     = 0x03;
constexpr char CTRL_HEXCOLOR  = 0x04;
constexpr char CTRL_RESET     = 0x0F;
constexpr char CTRL_MONO      = 0x11;
constexpr char CTRL_REVERSE   = 0x16;
constexpr char CTRL_ITALIC    = 0x1D;
constexpr char CTRL_STRIKE    = 0x1E;
constexpr char CTRL_UNDERLINE = 0x1F;
constexpr char CTRL_ESC       = 0x1B;

bool isControl(char c) {
    switch (c) {
        case CTRL_BOLD: case CTRL_COLOR: case CTRL_HEXCOLOR: case CTRL_RESET:
        case CTRL_MONO: case CTRL_REVERSE: case CTRL_ITALIC: case CTRL_STRIKE:
        case CTRL_UNDERLINE: case CTRL_ESC:
            return true;
        default:
            return false;
    }
}

uint8_t hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0xFF;
}

lv_color_t fromRgb24(uint32_t rgb) {
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Decodes one UTF-8 sequence. Returns the number of bytes consumed, or 0 when
// the bytes at `p` are not a valid sequence.
uint8_t decodeUtf8(const uint8_t* p, size_t available, uint32_t& out) {
    const uint8_t b0 = p[0];

    if (b0 < 0x80) { out = b0; return 1; }

    uint8_t  length;
    uint32_t code;
    if      ((b0 & 0xE0) == 0xC0) { length = 2; code = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { length = 3; code = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { length = 4; code = b0 & 0x07; }
    else return 0;

    if (available < length) return 0;
    for (uint8_t i = 1; i < length; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
        code = (code << 6) | (p[i] & 0x3F);
    }

    // Reject overlong encodings and surrogates: they would let the same glyph
    // arrive two different ways, which breaks the "is this UTF-8?" sniff.
    if (length == 2 && code < 0x80)    return 0;
    if (length == 3 && code < 0x800)   return 0;
    if (length == 4 && code < 0x10000) return 0;
    if (code >= 0xD800 && code <= 0xDFFF) return 0;

    out = code;
    return length;
}

// Reads up to `maxDigits` decimal digits starting at `i`, advancing `i`.
// Returns -1 when there were none.
int readNumber(const String& text, int& i, int maxDigits) {
    int value  = 0;
    int digits = 0;
    while (i < static_cast<int>(text.length()) && digits < maxDigits &&
           isdigit(static_cast<unsigned char>(text[i]))) {
        value = value * 10 + (text[i] - '0');
        i++;
        digits++;
    }
    return digits ? value : -1;
}

struct PenState {
    lv_color_t fg;
    lv_color_t bg;
    uint8_t    flags;
    bool       reverse;
};

void applyReverse(const PenState& pen, const FormatOptions& opt, TermCell& cell) {
    if (pen.reverse) {
        cell.fg = (pen.flags & CELL_HAS_BG) ? pen.bg : opt.defaultBg;
        cell.bg = pen.fg;
        cell.flags = pen.flags | CELL_HAS_BG;
    } else {
        cell.fg = pen.fg;
        cell.bg = pen.bg;
        cell.flags = pen.flags;
    }
}

// Handles ESC [ ... m. `i` points at '[' on entry and just past the final byte
// on return. Sequences that are not SGR are consumed and ignored.
void parseAnsiSgr(const String& text, int& i, const FormatOptions& opt, PenState& pen) {
    const int length = text.length();
    i++;  // skip '['

    int params[16];
    int count = 0;
    while (i < length && count < 16) {
        const int value = readNumber(text, i, 3);
        params[count++] = value < 0 ? 0 : value;
        if (i < length && text[i] == ';') { i++; continue; }
        break;
    }

    // Skip any intermediate bytes, then the final byte.
    while (i < length && text[i] >= 0x20 && text[i] <= 0x2F) i++;
    const char final = i < length ? text[i] : 0;
    if (i < length) i++;

    if (final != 'm') return;   // cursor moves and friends: nothing to render
    if (count == 0) { params[count++] = 0; }

    for (int p = 0; p < count; p++) {
        const int code = params[p];
        switch (code) {
            case 0:
                pen.fg = opt.defaultFg;
                pen.bg = opt.defaultBg;
                pen.flags = 0;
                pen.reverse = false;
                break;
            case 1: pen.flags |= CELL_BOLD; break;
            case 3: pen.flags |= CELL_ITALIC; break;
            case 4: pen.flags |= CELL_UNDERLINE; break;
            case 7: pen.reverse = true; break;
            case 9: pen.flags |= CELL_STRIKE; break;
            case 22: pen.flags &= ~CELL_BOLD; break;
            case 23: pen.flags &= ~CELL_ITALIC; break;
            case 24: pen.flags &= ~CELL_UNDERLINE; break;
            case 27: pen.reverse = false; break;
            case 29: pen.flags &= ~CELL_STRIKE; break;
            case 39: pen.fg = opt.defaultFg; break;
            case 49: pen.bg = opt.defaultBg; pen.flags &= ~CELL_HAS_BG; break;
            case 38:
            case 48: {
                // 38;5;n (256 colour) or 38;2;r;g;b (truecolour)
                const bool isFg = code == 38;
                lv_color_t colour = isFg ? opt.defaultFg : opt.defaultBg;
                if (p + 1 < count && params[p + 1] == 5 && p + 2 < count) {
                    colour = xterm256(static_cast<uint8_t>(params[p + 2]));
                    p += 2;
                } else if (p + 1 < count && params[p + 1] == 2 && p + 4 < count) {
                    colour = lv_color_make(params[p + 2], params[p + 3], params[p + 4]);
                    p += 4;
                } else {
                    break;
                }
                if (isFg) {
                    pen.fg = colour;
                } else if (opt.background) {
                    pen.bg = colour;
                    pen.flags |= CELL_HAS_BG;
                }
                break;
            }
            default:
                if (code >= 30 && code <= 37) {
                    pen.fg = xterm256(code - 30);
                } else if (code >= 90 && code <= 97) {
                    pen.fg = xterm256(code - 90 + 8);
                } else if (code >= 40 && code <= 47) {
                    if (opt.background) { pen.bg = xterm256(code - 40); pen.flags |= CELL_HAS_BG; }
                } else if (code >= 100 && code <= 107) {
                    if (opt.background) { pen.bg = xterm256(code - 100 + 8); pen.flags |= CELL_HAS_BG; }
                }
                break;
        }
    }
}

} // namespace

lv_color_t mircColor(uint8_t index, lv_color_t fallback) {
    if (index >= 99) return fallback;
    return fromRgb24(kMirc[index]);
}

lv_color_t xterm256(uint8_t index) {
    // 0-15: the same sixteen colours a terminal would use.
    static const uint32_t kBase[16] = {
        0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
        0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
    };
    if (index < 16) return fromRgb24(kBase[index]);

    if (index < 232) {
        // 6x6x6 colour cube.
        const uint8_t n = index - 16;
        static const uint8_t kLevels[6] = {0, 95, 135, 175, 215, 255};
        return lv_color_make(kLevels[(n / 36) % 6], kLevels[(n / 6) % 6], kLevels[n % 6]);
    }

    // 24 step greyscale ramp.
    const uint8_t grey = 8 + (index - 232) * 10;
    return lv_color_make(grey, grey, grey);
}

uint32_t cp437ToUnicode(uint8_t byte) {
    if (byte < 0x20) return kCp437Low[byte];
    if (byte < 0x7F) return byte;
    if (byte == 0x7F) return 0x2302;
    return kCp437High[byte - 0x80];
}

bool isValidUtf8(const char* data, size_t length) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    size_t i = 0;
    while (i < length) {
        uint32_t code;
        const uint8_t used = decodeUtf8(p + i, length - i, code);
        if (used == 0) return false;
        i += used;
    }
    return true;
}

void toCells(const String& text, const FormatOptions& opt, std::vector<TermCell>& out) {
    out.clear();
    if (text.isEmpty()) return;

    // Decide the encoding once for the whole line. Mixing would let a stray
    // high byte flip the rest of an otherwise fine UTF-8 line into mojibake.
    const bool utf8 = isValidUtf8(text.c_str(), text.length());

    PenState pen{opt.defaultFg, opt.defaultBg, 0, false};

    const int length = text.length();
    out.reserve(length);

    for (int i = 0; i < length;) {
        const char c = text[i];

        if (isControl(c)) {
            if (c == CTRL_ESC) {
                if (opt.ansi && i + 1 < length && text[i + 1] == '[') {
                    i++;                       // move onto '['
                    parseAnsiSgr(text, i, opt, pen);
                } else {
                    i++;                       // lone ESC: drop it
                }
                continue;
            }

            i++;
            if (!opt.attributes && c != CTRL_COLOR && c != CTRL_HEXCOLOR && c != CTRL_RESET) continue;

            switch (c) {
                case CTRL_BOLD:      pen.flags ^= CELL_BOLD; break;
                case CTRL_ITALIC:    pen.flags ^= CELL_ITALIC; break;
                case CTRL_UNDERLINE: pen.flags ^= CELL_UNDERLINE; break;
                case CTRL_STRIKE:    pen.flags ^= CELL_STRIKE; break;
                case CTRL_MONO:      break;   // we are already monospace
                case CTRL_REVERSE:   pen.reverse = !pen.reverse; break;
                case CTRL_RESET:
                    pen.fg = opt.defaultFg;
                    pen.bg = opt.defaultBg;
                    pen.flags = 0;
                    pen.reverse = false;
                    break;

                case CTRL_COLOR: {
                    // ^C[fg[,bg]]; bare ^C resets to the default colours.
                    const int fg = readNumber(text, i, 2);
                    if (fg < 0) {
                        pen.fg = opt.defaultFg;
                        pen.bg = opt.defaultBg;
                        pen.flags &= ~CELL_HAS_BG;
                        break;
                    }
                    if (opt.mircColors) pen.fg = mircColor(fg, opt.defaultFg);

                    if (i + 1 < length && text[i] == ',' &&
                        isdigit(static_cast<unsigned char>(text[i + 1]))) {
                        i++;   // skip ','
                        const int bg = readNumber(text, i, 2);
                        if (bg >= 0 && opt.mircColors && opt.background) {
                            if (bg >= 99) {
                                pen.bg = opt.defaultBg;
                                pen.flags &= ~CELL_HAS_BG;
                            } else {
                                pen.bg = mircColor(bg, opt.defaultBg);
                                pen.flags |= CELL_HAS_BG;
                            }
                        }
                    }
                    break;
                }

                case CTRL_HEXCOLOR: {
                    // ^4RRGGBB[,RRGGBB]
                    auto readHex = [&](lv_color_t& dest) -> bool {
                        if (i + 6 > length) return false;
                        uint32_t rgb = 0;
                        for (int k = 0; k < 6; k++) {
                            const uint8_t nibble = hexVal(text[i + k]);
                            if (nibble == 0xFF) return false;
                            rgb = (rgb << 4) | nibble;
                        }
                        i += 6;
                        dest = fromRgb24(rgb);
                        return true;
                    };

                    lv_color_t colour;
                    if (!readHex(colour)) {
                        pen.fg = opt.defaultFg;
                        pen.bg = opt.defaultBg;
                        pen.flags &= ~CELL_HAS_BG;
                        break;
                    }
                    if (opt.mircColors) pen.fg = colour;

                    if (i < length && text[i] == ',') {
                        const int save = i;
                        i++;
                        if (readHex(colour)) {
                            if (opt.mircColors && opt.background) {
                                pen.bg = colour;
                                pen.flags |= CELL_HAS_BG;
                            }
                        } else {
                            i = save;
                        }
                    }
                    break;
                }

                default: break;
            }
            continue;
        }

        // A printable character.
        uint32_t code;
        if (utf8) {
            const uint8_t used = decodeUtf8(reinterpret_cast<const uint8_t*>(text.c_str()) + i,
                                            length - i, code);
            i += used ? used : 1;
            if (!used) code = 0xFFFD;
        } else {
            code = cp437ToUnicode(static_cast<uint8_t>(c));
            i++;
        }

        if (code == '\t') {
            // Expand to the next multiple of eight so aligned art survives.
            const size_t stop = (out.size() / 8 + 1) * 8;
            TermCell blank{' ', pen.fg, pen.bg, pen.flags};
            applyReverse(pen, opt, blank);
            blank.ch = ' ';
            while (out.size() < stop) out.push_back(blank);
            continue;
        }
        if (code == '\r' || code == '\n') continue;

        TermCell cell{code, pen.fg, pen.bg, pen.flags};
        applyReverse(pen, opt, cell);
        cell.ch = code;
        out.push_back(cell);
    }
}

void wrap(const std::vector<TermCell>& cells, uint16_t columns, uint8_t indent,
          bool wordWrap, std::vector<RowSpan>& out) {
    out.clear();
    if (columns == 0) return;

    if (cells.empty()) {
        out.push_back({0, 0, 0});
        return;
    }

    const uint16_t total = cells.size();
    uint16_t start = 0;
    bool     first = true;

    while (start < total) {
        const uint8_t  rowIndent = first ? 0 : indent;
        const uint16_t width     = columns > rowIndent ? columns - rowIndent : 1;

        if (static_cast<uint16_t>(total - start) <= width) {
            out.push_back({start, total, rowIndent});
            break;
        }

        uint16_t end = start + width;

        if (wordWrap) {
            // Walk back to the last space that leaves something on this row.
            uint16_t candidate = end;
            while (candidate > start && cells[candidate].ch != ' ') candidate--;
            if (candidate > start) {
                end = candidate;
            }
        }

        out.push_back({start, end, rowIndent});

        // Swallow the space we broke on so rows do not start with one. `end` is
        // always greater than `start` here, so this cannot stall.
        start = end;
        while (start < total && wordWrap && cells[start].ch == ' ') start++;
        first = false;
    }

    if (out.empty()) out.push_back({0, total, 0});
}

String strip(const String& text) {
    FormatOptions opt;
    opt.mircColors = opt.background = opt.ansi = opt.attributes = true;

    std::vector<TermCell> cells;
    toCells(text, opt, cells);

    String out;
    out.reserve(cells.size());
    for (const TermCell& cell : cells) {
        // Keep it byte-for-byte for ASCII; anything else becomes UTF-8 again.
        if (cell.ch < 0x80) {
            out += static_cast<char>(cell.ch);
        } else if (cell.ch < 0x800) {
            out += static_cast<char>(0xC0 | (cell.ch >> 6));
            out += static_cast<char>(0x80 | (cell.ch & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cell.ch >> 12));
            out += static_cast<char>(0x80 | ((cell.ch >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cell.ch & 0x3F));
        }
    }
    return out;
}

} // namespace textfmt
