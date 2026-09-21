#include "ui/Theme.h"

#include "core/Settings.h"

namespace theme {

void init() {
    lv_theme_t* base = lv_theme_default_init(
        lv_display_get_default(),
        lv_color_hex(kAccent),
        lv_color_hex(kInfo),
        true,                 // dark
        uiFont());
    lv_display_set_theme(lv_display_get_default(), base);

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, background(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

lv_color_t background() { return lv_color_hex(kBackground); }
lv_color_t surface()    { return lv_color_hex(kSurface); }
lv_color_t accent()     { return lv_color_hex(kAccent); }
lv_color_t text()       { return lv_color_hex(kText); }
lv_color_t textDim()    { return lv_color_hex(kTextDim); }

const lv_font_t* uiFont()      { return &lv_font_montserrat_14; }
const lv_font_t* uiFontSmall() { return &lv_font_montserrat_12; }
const lv_font_t* uiFontLarge() { return &lv_font_montserrat_16; }

const lv_font_t* termFont() {
    return settings::getEnum("term_font") == 1 ? &acid_mono_15 : &acid_mono_10;
}

void stylePanel(lv_obj_t* object) {
    lv_obj_set_style_bg_color(object, surface(), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(kBorder), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_radius(object, 8, 0);
    lv_obj_set_style_pad_all(object, 8, 0);
    lv_obj_set_style_text_color(object, text(), 0);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

void styleRow(lv_obj_t* object) {
    lv_obj_set_style_bg_color(object, lv_color_hex(kSurfaceAlt), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 6, 0);
    lv_obj_set_style_pad_hor(object, 10, 0);
    lv_obj_set_style_pad_ver(object, 7, 0);
    lv_obj_set_style_text_color(object, text(), 0);

    // Focus is what the trackball moves, so it has to be obvious.
    lv_obj_set_style_bg_color(object, lv_color_hex(kAccentDim), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(object, accent(), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(object, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(object, LV_OPA_COVER, LV_STATE_FOCUSED);
}

lv_color_t batteryColor(uint8_t percent) {
    if (percent <= 15) return lv_color_hex(kDanger);
    if (percent <= 35) return lv_color_hex(kWarning);
    return lv_color_hex(kAccent);
}

lv_color_t signalColor(uint8_t quality) {
    if (quality == 0)  return lv_color_hex(kTextFaint);
    if (quality < 35)  return lv_color_hex(kDanger);
    if (quality < 65)  return lv_color_hex(kWarning);
    return lv_color_hex(kAccent);
}

} // namespace theme
