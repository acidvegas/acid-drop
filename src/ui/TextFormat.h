#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <vector>

// Turns a raw IRC line into character cells carrying their own colours, which
// is what ASCII/ANSI art needs: a lot of it is drawn with coloured spaces, so
// the background colour of every cell has to survive into rendering.

enum CellFlags : uint8_t {
    CELL_BOLD      = 1 << 0,
    CELL_UNDERLINE = 1 << 1,
    CELL_ITALIC    = 1 << 2,
    CELL_HAS_BG    = 1 << 3,  // bg is meaningful; otherwise the view's default
    CELL_STRIKE    = 1 << 4,
};

struct TermCell {
    uint32_t   ch;     // Unicode code point
    lv_color_t fg;
    lv_color_t bg;
    uint8_t    flags;
};

struct FormatOptions {
    bool       mircColors  = true;   // ^C / ^K colour codes
    bool       background  = true;   // honour the background half of ^C
    bool       ansi        = true;   // ESC[ ... m SGR sequences
    bool       attributes  = true;   // ^B bold, ^] italic, ^_ underline, ^V reverse
    lv_color_t defaultFg   = lv_color_white();
    lv_color_t defaultBg   = lv_color_black();
};

namespace textfmt {

// mIRC palette, indices 0-98. 99 means "default", handled by the caller.
lv_color_t mircColor(uint8_t index, lv_color_t fallback);

// xterm 256-colour palette, used by ANSI 38;5;n / 48;5;n.
lv_color_t xterm256(uint8_t index);

// Maps a code page 437 byte to the Unicode code point that draws the same
// glyph. Used when a line is not valid UTF-8, which is how BBS-era .ans art
// and a fair amount of IRC art arrives.
uint32_t cp437ToUnicode(uint8_t byte);

// True when the whole string decodes as UTF-8.
bool isValidUtf8(const char* data, size_t length);

// Expands control codes and decodes text into cells. Never wraps; one cell per
// visible character. Control codes themselves produce no cells.
void toCells(const String& text, const FormatOptions& options, std::vector<TermCell>& out);

// Splits `cells` into rows at most `columns` wide, breaking on spaces where it
// can and hard-breaking where it cannot. `indent` spaces are prepended to every
// row after the first, so wrapped chat lines stay readable.
// Returns row start/end offsets as half-open ranges into `cells`.
struct RowSpan {
    uint16_t start;
    uint16_t end;
    uint8_t  indent;
};
void wrap(const std::vector<TermCell>& cells, uint16_t columns, uint8_t indent,
          bool wordWrap, std::vector<RowSpan>& out);

// Strips every control code, for logging and for nick matching.
String strip(const String& text);

} // namespace textfmt
