#include "apps/GpsApp.h"

#include "board/Gps.h"
#include "board/Input.h"
#include "core/Settings.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace gpsapp {
namespace {

lv_obj_t* s_page = nullptr;
lv_obj_t* s_body = nullptr;

bool keyHook(uint32_t key) {
    if (key != LV_KEY_ESC) return false;
    ui::back();
    return true;
}

} // namespace

void create(lv_obj_t* parent) {
    s_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_page, 8, 0);
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_page, 8, 0);
    lv_obj_set_scrollable(s_page, false);

    lv_obj_t* title = lv_label_create(s_page);
    lv_label_set_text(title, "GPS");
    lv_obj_set_style_text_font(title, theme::uiFontLarge(), 0);
    lv_obj_set_style_text_color(title, theme::accent(), 0);

    s_body = lv_label_create(s_page);
    lv_obj_set_width(s_body, LV_PCT(100));
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_body, &acid_mono_10, 0);
    lv_obj_set_style_text_color(s_body, theme::text(), 0);

    lv_obj_t* toggle = lv_button_create(s_page);
    lv_obj_set_size(toggle, 120, 30);
    lv_obj_set_style_bg_color(toggle, theme::accent(), 0);
    lv_obj_t* toggleLabel = lv_label_create(toggle);
    lv_label_set_text(toggleLabel, gps::enabled() ? "Turn off" : "Turn on");
    lv_obj_set_style_text_color(toggleLabel, lv_color_hex(theme::kBackground), 0);
    lv_obj_center(toggleLabel);
    lv_group_add_obj(input::group(), toggle);

    lv_obj_add_event_cb(toggle, [](lv_event_t* event) {
        const bool wanted = !gps::enabled();
        settings::setBool("gps_enable", wanted);
        gps::setEnabled(wanted);

        lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
        lv_label_set_text(lv_obj_get_child(button, 0), wanted ? "Turn off" : "Turn on");
    }, LV_EVENT_CLICKED, nullptr);

    input::setKeyHook(keyHook);
    tick();
}

void destroy() {
    input::clearKeyHook();
    s_page = nullptr;
    s_body = nullptr;
}

void tick() {
    if (!s_body) return;

    static uint32_t lastUpdate = 0;
    const uint32_t  now        = millis();
    if (lastUpdate != 0 && now - lastUpdate < 1000) return;
    lastUpdate = now;

    String text;
    if (!gps::enabled()) {
        text = "Receiver is off.\n\nThe T-Deck Plus has an L76K module;\nthe original T-Deck does not.";
    } else if (!gps::hasFix()) {
        text  = "Status     " + gps::summary() + "\n";
        text += "Satellites " + String(gps::satellites()) + "\n\n";
        text += "A cold start outdoors usually takes\n30-60 seconds to get a first fix.";
    } else {
        text += "Latitude   " + String(gps::latitude(), 6) + "\n";
        text += "Longitude  " + String(gps::longitude(), 6) + "\n";
        text += "Altitude   " + String(gps::altitudeMeters(), 1) + " m\n";
        text += "Speed      " + String(gps::speedKnots(), 1) + " kn\n";
        text += "Satellites " + String(gps::satellites()) + "\n";
        text += "Fix age    " + String(gps::fixAgeMs() / 1000) + " s\n";
    }

    lv_label_set_text(s_body, text.c_str());
}

} // namespace gpsapp
