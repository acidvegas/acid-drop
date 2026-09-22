#pragma once

#include <lvgl.h>

LV_FONT_DECLARE(acid_mono_10);
LV_FONT_DECLARE(acid_mono_15);

// Chained from the mono fonts via lv_font_t.fallback: Latin, Greek, Cyrillic,
// punctuation, arrows, symbols, the Mathematical Alphanumeric block that IRC
// uses for styled text, and monochrome emoji. LVGL resolves the chain itself.
LV_FONT_DECLARE(acid_fallback_10);
LV_FONT_DECLARE(acid_fallback_15);

// One place for the palette and the shared styles, so screens do not each
// invent their own greys.

namespace theme {

// Surfaces, darkest to lightest.
constexpr uint32_t kBackground = 0x07090C;
constexpr uint32_t kSurface    = 0x11151B;
constexpr uint32_t kSurfaceAlt = 0x1A2028;
constexpr uint32_t kBorder     = 0x2A323D;

// Text.
constexpr uint32_t kText       = 0xE6EAF0;
constexpr uint32_t kTextDim    = 0x8A94A3;
constexpr uint32_t kTextFaint  = 0x5A6068;

// Signal colours.
constexpr uint32_t kAccent     = 0x35E08A;   // acid green
constexpr uint32_t kAccentDim  = 0x1B7A4A;
constexpr uint32_t kWarning    = 0xFFB454;
constexpr uint32_t kDanger     = 0xFF3B6E;
constexpr uint32_t kInfo       = 0x58B4FF;

void init();

lv_color_t background();
lv_color_t surface();
lv_color_t accent();
lv_color_t text();
lv_color_t textDim();

const lv_font_t* uiFont();
const lv_font_t* uiFontSmall();
const lv_font_t* uiFontLarge();

// The monospace font the chat grid uses, chosen by the term_font setting.
const lv_font_t* termFont();

// Applies the house style to a plain container: dark surface, thin border,
// rounded corners, no scrollbar.
void stylePanel(lv_obj_t* object);

// A list row that highlights on focus, for the settings and launcher screens.
void styleRow(lv_obj_t* object);

// Colour for a battery level, green through amber to red.
lv_color_t batteryColor(uint8_t percent);

// Colour for a signal quality percentage.
lv_color_t signalColor(uint8_t quality);

} // namespace theme
