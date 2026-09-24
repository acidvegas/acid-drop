#pragma once

#include <lvgl.h>

LV_FONT_DECLARE(acid_mono_10);

// Chained from the mono font via lv_font_t.fallback: Latin, Greek, Cyrillic,
// punctuation, arrows, symbols, the Mathematical Alphanumeric block that IRC
// uses for styled text, and monochrome emoji. LVGL resolves the chain itself.
LV_FONT_DECLARE(acid_fallback_10);

// One place for the palette and the shared styles, so screens do not each
// invent their own greys.
//
// Five colours are the theme, and everything else is derived from them. That
// keeps the Theme menu to five choices instead of fifteen, and means a custom
// theme cannot end up with a border that has drifted away from the panel it
// borders. The derived shades move away from their base's own brightness, so
// a light background produces darker rows rather than lighter ones.

namespace theme {

// Defaults, and the factory values the Theme menu resets to.
constexpr uint32_t kDefaultAccent     = 0x35E08A;   // acid green
constexpr uint32_t kDefaultBackground = 0x07090C;
constexpr uint32_t kDefaultText       = 0xE6EAF0;
constexpr uint32_t kDefaultPanel      = 0x11151B;   // status bar
constexpr uint32_t kDefaultInput      = 0x11151B;   // message box

// Fixed meanings, not part of the theme: red is bad wherever you are.
constexpr uint32_t kWarning    = 0xFFB454;
constexpr uint32_t kDanger     = 0xFF3B6E;
constexpr uint32_t kInfo       = 0x58B4FF;

void init();

// Re-reads the five colours from settings. The caller is responsible for
// rebuilding anything already on screen - widgets keep the colour they were
// given at creation.
void reload();

// --- the five ---
lv_color_t accent();
lv_color_t background();
lv_color_t text();
lv_color_t panel();      // status bar
lv_color_t inputBg();    // the message box

// --- derived ---
lv_color_t surfaceAlt(); // list rows, buttons
lv_color_t border();
lv_color_t accentDim();  // shortcut rows
lv_color_t focus();      // the focused row's fill
lv_color_t textDim();
lv_color_t textFaint();

const lv_font_t* uiFont();
const lv_font_t* uiFontSmall();
const lv_font_t* uiFontLarge();
// For long lists - the settings menu and the relay buffer list - where fitting
// more rows on a 240px screen matters more than size.
const lv_font_t* uiFontTiny();

// The monospace font the chat grid uses. One face, deliberately.
const lv_font_t* termFont();

// Applies the house style to a plain container: dark surface, thin border,
// rounded corners, no scrollbar.
void stylePanel(lv_obj_t* object);

// A list row that highlights on focus, for the settings and launcher screens.
void styleRow(lv_obj_t* object);
// styleRow with less vertical padding, to go with uiFontTiny.
void styleRowCompact(lv_obj_t* object);

// Colour for a battery level, green through amber to red.
lv_color_t batteryColor(uint8_t percent);

// Colour for a signal quality percentage.
lv_color_t signalColor(uint8_t quality);

} // namespace theme
