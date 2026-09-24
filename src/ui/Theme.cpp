#include "ui/Theme.h"

#include "core/Settings.h"

namespace theme {
namespace {

// The five values the Theme menu owns, cached so a colour lookup during
// drawing is not an NVS-backed settings read.
uint32_t s_accent     = kDefaultAccent;
uint32_t s_background = kDefaultBackground;
uint32_t s_text       = kDefaultText;
uint32_t s_panel      = kDefaultPanel;
uint32_t s_input      = kDefaultInput;

uint8_t luminance(uint32_t rgb) {
    const uint8_t r = (rgb >> 16) & 0xFF;
    const uint8_t g = (rgb >> 8)  & 0xFF;
    const uint8_t b =  rgb        & 0xFF;
    // Rec. 601, near enough for deciding "is this dark".
    return static_cast<uint8_t>((r * 77 + g * 151 + b * 28) >> 8);
}

// Moves a colour away from its own brightness by `amount` 0-255: a dark base
// gets lighter, a light base gets darker. That is what lets a white-background
// theme produce darker rows instead of invisible ones.
uint32_t shade(uint32_t rgb, uint8_t amount) {
    const bool up = luminance(rgb) < 128;

    int r = (rgb >> 16) & 0xFF;
    int g = (rgb >> 8)  & 0xFF;
    int b =  rgb        & 0xFF;

    if (up) { r += amount; g += amount; b += amount; }
    else    { r -= amount; g -= amount; b -= amount; }

    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
            static_cast<uint32_t>(b);
}

// Blends `rgb` towards `towards` by `mix` 0-255.
uint32_t blend(uint32_t rgb, uint32_t towards, uint8_t mix) {
    const int inv = 255 - mix;
    const int r = (((rgb >> 16) & 0xFF) * inv + ((towards >> 16) & 0xFF) * mix) / 255;
    const int g = (((rgb >> 8)  & 0xFF) * inv + ((towards >> 8)  & 0xFF) * mix) / 255;
    const int b = ((rgb & 0xFF) * inv + (towards & 0xFF) * mix) / 255;
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
            static_cast<uint32_t>(b);
}

} // namespace

void reload() {
    s_accent     = static_cast<uint32_t>(settings::getInt("th_accent"));
    s_background = static_cast<uint32_t>(settings::getInt("th_bg"));
    s_text       = static_cast<uint32_t>(settings::getInt("th_text"));
    s_panel      = static_cast<uint32_t>(settings::getInt("th_panel"));
    s_input      = static_cast<uint32_t>(settings::getInt("th_input"));
}

void init() {
    reload();

    lv_theme_t* base = lv_theme_default_init(
        lv_display_get_default(),
        accent(),
        lv_color_hex(kInfo),
        luminance(s_background) < 128,   // dark or light, from the background
        uiFont());
    lv_display_set_theme(lv_display_get_default(), base);

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, background(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

lv_color_t accent()     { return lv_color_hex(s_accent); }
lv_color_t background() { return lv_color_hex(s_background); }
lv_color_t text()       { return lv_color_hex(s_text); }
lv_color_t panel()      { return lv_color_hex(s_panel); }
lv_color_t inputBg()    { return lv_color_hex(s_input); }

// Rows and buttons sit one step off the panel they are on.
lv_color_t surfaceAlt() { return lv_color_hex(shade(s_panel, 14)); }
lv_color_t border()     { return lv_color_hex(shade(s_panel, 32)); }

// The focus fill is the accent pulled most of the way back towards the
// background, so secondary text stays readable on top of it. A saturated
// accent behind dim grey is the exact thing that made this unreadable before.
lv_color_t focus()      { return lv_color_hex(blend(s_accent, s_background, 205)); }
lv_color_t accentDim()  { return lv_color_hex(blend(s_accent, s_background, 150)); }

lv_color_t textDim()    { return lv_color_hex(blend(s_text, s_background, 110)); }
lv_color_t textFaint()  { return lv_color_hex(blend(s_text, s_background, 165)); }

const lv_font_t* uiFont()      { return &lv_font_montserrat_14; }
const lv_font_t* uiFontSmall() { return &lv_font_montserrat_12; }
const lv_font_t* uiFontLarge() { return &lv_font_montserrat_16; }
const lv_font_t* uiFontTiny()  { return &lv_font_montserrat_10; }

const lv_font_t* termFont() {
    // Only the 6x14 face. The 9x20 one fitted about half as much backlog on a
    // 320x240 panel, which is the opposite of what this firmware is for.
    return &acid_mono_10;
}

void stylePanel(lv_obj_t* object) {
    lv_obj_set_style_bg_color(object, panel(), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(object, border(), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_radius(object, 8, 0);
    lv_obj_set_style_pad_all(object, 8, 0);
    lv_obj_set_style_text_color(object, text(), 0);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

void styleRow(lv_obj_t* object) {
    lv_obj_set_style_bg_color(object, surfaceAlt(), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 6, 0);
    lv_obj_set_style_pad_hor(object, 10, 0);
    lv_obj_set_style_pad_ver(object, 7, 0);
    lv_obj_set_style_text_color(object, text(), 0);

    // Focus is what the trackball moves, so it has to be obvious - but it also
    // has to stay readable, which a bright accent fill behind dim text is not.
    lv_obj_set_style_bg_color(object, focus(), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(object, accent(), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(object, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(object, LV_OPA_COVER, LV_STATE_FOCUSED);

    // And the list has to follow it. LVGL only scrolls a newly focused object
    // into view when this is set, and the flag is off by default, so rolling
    // the trackball walked the focus straight off the bottom of the screen and
    // left the list sitting where it was.
    lv_obj_set_scroll_on_focus(object, true);
}

void styleRowCompact(lv_obj_t* object) {
    styleRow(object);
    lv_obj_set_style_pad_ver(object, 3, 0);
}

lv_color_t batteryColor(uint8_t percent) {
    if (percent <= 15) return lv_color_hex(kDanger);
    if (percent <= 35) return lv_color_hex(kWarning);
    return accent();
}

lv_color_t signalColor(uint8_t quality) {
    if (quality == 0)  return textFaint();
    if (quality < 35)  return lv_color_hex(kDanger);
    if (quality < 65)  return lv_color_hex(kWarning);
    return accent();
}

} // namespace theme
