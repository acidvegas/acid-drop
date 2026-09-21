#include "ui/StatusBar.h"

#include <time.h>

#include "board/Audio.h"
#include "board/Ble.h"
#include "board/Gps.h"
#include "board/Power.h"
#include "core/Settings.h"
#include "net/WifiService.h"
#include "ui/Shade.h"
#include "ui/Theme.h"

namespace statusbar {
namespace {

lv_obj_t* s_bar          = nullptr;
lv_obj_t* s_clock        = nullptr;
lv_obj_t* s_title        = nullptr;
lv_obj_t* s_notification = nullptr;
lv_obj_t* s_gps          = nullptr;
lv_obj_t* s_sound        = nullptr;
lv_obj_t* s_ble          = nullptr;
lv_obj_t* s_wifi         = nullptr;
lv_obj_t* s_battery      = nullptr;
lv_obj_t* s_batteryText  = nullptr;

// Drag state for the pull-down gesture.
int32_t s_dragStartY   = 0;
bool    s_dragging     = false;
bool    s_dragDecided  = false;

const char* batterySymbol(uint8_t percent) {
    if (percent >= 85) return LV_SYMBOL_BATTERY_FULL;
    if (percent >= 60) return LV_SYMBOL_BATTERY_3;
    if (percent >= 35) return LV_SYMBOL_BATTERY_2;
    if (percent >= 12) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

lv_obj_t* makeIcon(lv_obj_t* parent, const char* symbol) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(theme::kTextFaint), 0);
    return label;
}

void setIconState(lv_obj_t* icon, bool active, lv_color_t activeColor) {
    lv_obj_set_style_text_color(icon,
                                active ? activeColor : lv_color_hex(theme::kTextFaint), 0);
}

void barEventCb(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t* indev = lv_indev_active();
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
        if (code == LV_EVENT_CLICKED) shade::toggle();
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    switch (code) {
        case LV_EVENT_PRESSED:
            s_dragStartY  = point.y;
            s_dragging    = false;
            s_dragDecided = false;
            break;

        case LV_EVENT_PRESSING: {
            const int32_t delta = point.y - s_dragStartY;
            // A few pixels of slop so a tap is not read as a drag.
            if (!s_dragDecided && delta > 6) {
                s_dragDecided = true;
                s_dragging    = true;
                shade::beginDrag();
            }
            if (s_dragging) shade::dragTo(delta);
            break;
        }

        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:
            if (s_dragging) {
                shade::endDrag();
                s_dragging = false;
            } else if (!s_dragDecided) {
                shade::toggle();
            }
            break;

        default:
            break;
    }
}

} // namespace

void create(lv_obj_t* parent) {
    s_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_size(s_bar, LV_PCT(100), kHeight);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(theme::kSurface), 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_bar, 8, 0);
    lv_obj_set_style_border_side(s_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_bar, lv_color_hex(theme::kBorder), 0);
    lv_obj_set_style_border_width(s_bar, 1, 0);
    lv_obj_set_scrollable(s_bar, false);
    lv_obj_set_clickable(s_bar, true);

    lv_obj_add_event_cb(s_bar, barEventCb, LV_EVENT_PRESSED,    nullptr);
    lv_obj_add_event_cb(s_bar, barEventCb, LV_EVENT_PRESSING,   nullptr);
    lv_obj_add_event_cb(s_bar, barEventCb, LV_EVENT_RELEASED,   nullptr);
    lv_obj_add_event_cb(s_bar, barEventCb, LV_EVENT_PRESS_LOST, nullptr);
    lv_obj_add_event_cb(s_bar, barEventCb, LV_EVENT_CLICKED,    nullptr);

    // Left: clock and the notification dot.
    s_clock = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_clock, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(s_clock, theme::text(), 0);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_LEFT_MID, 0, 0);

    s_notification = lv_obj_create(s_bar);
    lv_obj_remove_style_all(s_notification);
    lv_obj_set_size(s_notification, 6, 6);
    lv_obj_set_style_radius(s_notification, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_notification, lv_color_hex(theme::kDanger), 0);
    lv_obj_set_style_bg_opa(s_notification, LV_OPA_COVER, 0);
    lv_obj_align(s_notification, LV_ALIGN_LEFT_MID, 44, 0);
    lv_obj_set_hidden(s_notification, true);

    // Middle: whatever the active app wants to say.
    s_title = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_title, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(s_title, theme::textDim(), 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_title, 120);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_title, "");
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    // Right: a row of indicators, laid out right to left.
    lv_obj_t* icons = lv_obj_create(s_bar);
    lv_obj_remove_style_all(icons);
    lv_obj_set_size(icons, LV_SIZE_CONTENT, kHeight);
    lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(icons, 5, 0);
    lv_obj_set_scrollable(icons, false);
    lv_obj_align(icons, LV_ALIGN_RIGHT_MID, 0, 0);

    s_sound       = makeIcon(icons, LV_SYMBOL_VOLUME_MAX);
    s_gps         = makeIcon(icons, LV_SYMBOL_GPS);
    s_ble         = makeIcon(icons, LV_SYMBOL_BLUETOOTH);
    s_wifi        = makeIcon(icons, LV_SYMBOL_WIFI);
    s_batteryText = lv_label_create(icons);
    lv_obj_set_style_text_font(s_batteryText, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(s_batteryText, theme::textDim(), 0);
    lv_label_set_text(s_batteryText, "");
    s_battery     = makeIcon(icons, LV_SYMBOL_BATTERY_FULL);
}

void tick() {
    if (!s_bar) return;

    // Clock
    const time_t now = time(nullptr);
    struct tm parts;
    localtime_r(&now, &parts);

    char text[16];
    const bool use24  = settings::getEnum("clock_fmt") == 1;
    const bool seconds = settings::getBool("sb_seconds");

    if (use24) {
        if (seconds) snprintf(text, sizeof(text), "%02d:%02d:%02d", parts.tm_hour, parts.tm_min, parts.tm_sec);
        else         snprintf(text, sizeof(text), "%02d:%02d", parts.tm_hour, parts.tm_min);
    } else {
        int hour = parts.tm_hour % 12;
        if (hour == 0) hour = 12;
        const char* suffix = parts.tm_hour >= 12 ? "PM" : "AM";
        if (seconds) snprintf(text, sizeof(text), "%d:%02d:%02d %s", hour, parts.tm_min, parts.tm_sec, suffix);
        else         snprintf(text, sizeof(text), "%d:%02d %s", hour, parts.tm_min, suffix);
    }
    lv_label_set_text(s_clock, text);

    // Radios
    setIconState(s_wifi, net::isConnected(), theme::signalColor(net::quality()));
    setIconState(s_ble,  ble::enabled(),
                 ble::connected() ? theme::accent() : lv_color_hex(theme::kInfo));
    setIconState(s_gps,  gps::enabled(),
                 gps::hasFix() ? theme::accent() : lv_color_hex(theme::kWarning));

    const bool sound = audio::enabled() && settings::getInt("snd_volume") > 0;
    lv_label_set_text(s_sound, sound ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_MUTE);
    setIconState(s_sound, sound, theme::text());

    // Battery
    const uint8_t percent = power::batteryPercent();
    lv_label_set_text(s_battery,
                      power::probablyCharging() ? LV_SYMBOL_CHARGE : batterySymbol(percent));
    lv_obj_set_style_text_color(s_battery, theme::batteryColor(percent), 0);

    if (settings::getBool("sb_battpct")) {
        char percentText[8];
        snprintf(percentText, sizeof(percentText), "%u%%", percent);
        lv_label_set_text(s_batteryText, percentText);
        lv_obj_set_hidden(s_batteryText, false);
    } else {
        lv_obj_set_hidden(s_batteryText, true);
    }
}

void setTitle(const String& text) {
    if (s_title) lv_label_set_text(s_title, text.c_str());
}

void setNotification(bool pending) {
    if (!s_notification) return;
    if (pending) lv_obj_set_hidden(s_notification, false);
    else         lv_obj_set_hidden(s_notification, true);
}

lv_obj_t* object() { return s_bar; }

} // namespace statusbar
