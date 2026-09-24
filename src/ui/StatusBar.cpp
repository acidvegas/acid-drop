#include "ui/StatusBar.h"

#include <time.h>

#include "board/Power.h"
#include "core/Settings.h"
#include "net/WifiService.h"
#include "ui/Theme.h"

namespace statusbar {
namespace {

lv_obj_t* s_bar          = nullptr;
lv_obj_t* s_clock        = nullptr;
lv_obj_t* s_notification = nullptr;
lv_obj_t* s_center       = nullptr;
lv_obj_t* s_wifi         = nullptr;

// The battery is drawn rather than written: a glyph plus "100%" ate most of
// the right-hand side, and the bar now has the window list to fit as well.
lv_obj_t* s_battery      = nullptr;   // the outline
lv_obj_t* s_batteryFill  = nullptr;   // the part that grows with charge

// Outline 20x10 with a 1px border and 1px of padding, so the fill has 16px to
// move across. The nub on the right is what makes it read as a battery at
// this size rather than as a progress bar.
constexpr int32_t kBatteryWidth  = 20;
constexpr int32_t kBatteryHeight = 10;
constexpr int32_t kFillWidth     = kBatteryWidth - 4;

lv_obj_t* makeIcon(lv_obj_t* parent, const char* symbol) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(label, theme::textFaint(), 0);
    return label;
}

void buildBattery(lv_obj_t* parent) {
    lv_obj_t* wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, kBatteryWidth + 3, kBatteryHeight);
    lv_obj_set_scrollable(wrap, false);

    s_battery = lv_obj_create(wrap);
    lv_obj_remove_style_all(s_battery);
    lv_obj_set_size(s_battery, kBatteryWidth, kBatteryHeight);
    lv_obj_align(s_battery, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_border_color(s_battery, theme::textDim(), 0);
    lv_obj_set_style_border_width(s_battery, 1, 0);
    lv_obj_set_style_radius(s_battery, 2, 0);
    lv_obj_set_style_pad_all(s_battery, 1, 0);
    lv_obj_set_scrollable(s_battery, false);

    s_batteryFill = lv_obj_create(s_battery);
    lv_obj_remove_style_all(s_batteryFill);
    lv_obj_set_height(s_batteryFill, LV_PCT(100));
    lv_obj_set_width(s_batteryFill, kFillWidth);
    lv_obj_align(s_batteryFill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_batteryFill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_batteryFill, 1, 0);

    // The terminal nub.
    lv_obj_t* nub = lv_obj_create(wrap);
    lv_obj_remove_style_all(nub);
    lv_obj_set_size(nub, 2, 4);
    lv_obj_align(nub, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(nub, theme::textDim(), 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(nub, 1, 0);
}

} // namespace

void create(lv_obj_t* parent) {
    s_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_size(s_bar, LV_PCT(100), kHeight);
    lv_obj_set_style_bg_color(s_bar, theme::panel(), 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_bar, 6, 0);
    lv_obj_set_style_border_side(s_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_bar, theme::border(), 0);
    lv_obj_set_style_border_width(s_bar, 1, 0);
    lv_obj_set_scrollable(s_bar, false);
    // Deliberately not clickable: it used to be the handle for a pull-down
    // panel, and swallowed presses meant for the controls just below it.
    lv_obj_set_clickable(s_bar, false);

    // Three columns, laid out rather than aligned: the middle one has to take
    // whatever the two ends leave, because the window list lives in it.
    lv_obj_set_flex_flow(s_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_bar, 6, 0);

    // Left: clock and the notification dot.
    lv_obj_t* left = lv_obj_create(s_bar);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, kHeight);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 4, 0);
    lv_obj_set_scrollable(left, false);

    s_clock = lv_label_create(left);
    lv_obj_set_style_text_font(s_clock, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(s_clock, theme::text(), 0);
    lv_label_set_text(s_clock, "--:--");

    s_notification = lv_obj_create(left);
    lv_obj_remove_style_all(s_notification);
    lv_obj_set_size(s_notification, 6, 6);
    lv_obj_set_style_radius(s_notification, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_notification, lv_color_hex(theme::kDanger), 0);
    lv_obj_set_style_bg_opa(s_notification, LV_OPA_COVER, 0);
    lv_obj_set_hidden(s_notification, true);

    // Middle: whatever the active screen puts here. Scrolls sideways, because
    // the IRC window list can be longer than the space available.
    s_center = lv_obj_create(s_bar);
    lv_obj_remove_style_all(s_center);
    lv_obj_set_height(s_center, kHeight - 4);
    lv_obj_set_flex_grow(s_center, 1);
    lv_obj_set_flex_flow(s_center, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_center, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_center, 3, 0);
    lv_obj_set_scroll_dir(s_center, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_center, LV_SCROLLBAR_MODE_OFF);

    // Right: the radio and the battery.
    lv_obj_t* icons = lv_obj_create(s_bar);
    lv_obj_remove_style_all(icons);
    lv_obj_set_size(icons, LV_SIZE_CONTENT, kHeight);
    lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(icons, 5, 0);
    lv_obj_set_scrollable(icons, false);

    s_wifi = makeIcon(icons, LV_SYMBOL_WIFI);
    buildBattery(icons);
}

void tick() {
    if (!s_bar) return;

    // Clock
    const time_t now = time(nullptr);
    struct tm parts;
    localtime_r(&now, &parts);

    char text[16];
    const bool use24   = settings::getEnum("clock_fmt") == 1;
    const bool seconds = settings::getBool("sb_seconds");

    if (use24) {
        if (seconds) snprintf(text, sizeof(text), "%02d:%02d:%02d", parts.tm_hour, parts.tm_min, parts.tm_sec);
        else         snprintf(text, sizeof(text), "%02d:%02d", parts.tm_hour, parts.tm_min);
    } else {
        // No AM/PM suffix. It cost four columns of a bar that also has to fit
        // the window list, and nobody looks at a clock to find out whether it
        // is the morning.
        int hour = parts.tm_hour % 12;
        if (hour == 0) hour = 12;
        if (seconds) snprintf(text, sizeof(text), "%d:%02d:%02d", hour, parts.tm_min, parts.tm_sec);
        else         snprintf(text, sizeof(text), "%d:%02d", hour, parts.tm_min);
    }
    lv_label_set_text(s_clock, text);

    lv_obj_set_style_text_color(s_wifi,
                                net::isConnected() ? theme::signalColor(net::quality())
                                                   : theme::textFaint(), 0);

    // Battery: the fill is the charge, the colour is the warning.
    const uint8_t percent = power::batteryPercent();
    int32_t width = (kFillWidth * percent) / 100;
    // Never let a live battery read as completely empty.
    if (width < 1 && percent > 0) width = 1;
    lv_obj_set_width(s_batteryFill, width);

    // Charging is shown by colour rather than a second glyph, because the
    // whole point of drawing the battery was to get the right-hand side of the
    // bar back for the window list.
    lv_obj_set_style_bg_color(s_batteryFill,
                              power::probablyCharging() ? theme::accent()
                                                        : theme::batteryColor(percent), 0);
}

void applyTheme() {
    if (!s_bar) return;

    // Re-colours in place rather than rebuilding: this bar is created once for
    // the whole session and outlives every screen, so it cannot just be thrown
    // away and remade when the theme changes.
    lv_obj_set_style_bg_color(s_bar, theme::panel(), 0);
    lv_obj_set_style_border_color(s_bar, theme::border(), 0);
    lv_obj_set_style_text_color(s_clock, theme::text(), 0);
    lv_obj_set_style_bg_color(s_notification, lv_color_hex(theme::kDanger), 0);
    lv_obj_set_style_border_color(s_battery, theme::textDim(), 0);
    lv_obj_invalidate(s_bar);
}

void setNotification(bool pending) {
    if (!s_notification) return;
    lv_obj_set_hidden(s_notification, !pending);
}

lv_obj_t* centerSlot() { return s_center; }

void clearCenterSlot() {
    if (s_center) lv_obj_clean(s_center);
}

lv_obj_t* object() { return s_bar; }

} // namespace statusbar
