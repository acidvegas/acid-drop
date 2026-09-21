#include "ui/Shade.h"

#include "board/Audio.h"
#include "board/Ble.h"
#include "board/Display.h"
#include "board/Gps.h"
#include "board/Power.h"
#include "board/Radio.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "net/WifiService.h"
#include "ui/StatusBar.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace shade {
namespace {

constexpr int32_t kHeight = 210;
constexpr int32_t kHidden = -(kHeight + 4);

lv_obj_t* s_panel      = nullptr;
lv_obj_t* s_scrim      = nullptr;
lv_obj_t* s_readout    = nullptr;
lv_obj_t* s_brightness = nullptr;
lv_obj_t* s_volume     = nullptr;

struct Toggle {
    lv_obj_t*   button;
    lv_obj_t*   label;
    const char* icon;
    const char* name;
    bool (*get)();
    void (*set)(bool);
};

Toggle s_toggles[4];

bool s_open       = false;
bool s_dragging   = false;
int32_t s_dragBase = 0;

// --- toggle plumbing ------------------------------------------------------
bool wifiGet()  { return net::enabled(); }
void wifiSet(bool on) { settings::setBool("wifi_enable", on); net::setEnabled(on); }

bool bleGet()   { return ble::enabled(); }
void bleSet(bool on)  { settings::setBool("ble_enable", on); ble::setEnabled(on); }

bool gpsGet()   { return gps::enabled(); }
void gpsSet(bool on)  { settings::setBool("gps_enable", on); gps::setEnabled(on); }

bool loraGet()  { return radio::enabled(); }
void loraSet(bool on) { settings::setBool("lora_enable", on); radio::setEnabled(on); }

void setPanelY(int32_t y) {
    lv_obj_set_y(s_panel, y);

    // The scrim fades in with the panel so the screen behind recedes.
    const int32_t travel = kHeight;
    const int32_t shown  = y - kHidden;
    lv_opa_t opa = static_cast<lv_opa_t>((shown * 140) / (travel > 0 ? travel : 1));
    if (opa > 140) opa = 140;
    lv_obj_set_style_bg_opa(s_scrim, opa, 0);

    if (opa == 0) lv_obj_set_hidden(s_scrim, true);
    else          lv_obj_set_hidden(s_scrim, false);
}

void animateTo(int32_t target) {
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_panel);
    lv_anim_set_values(&anim, lv_obj_get_y(s_panel), target);
    lv_anim_set_duration(&anim, 180);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, [](void* object, int32_t value) {
        LV_UNUSED(object);
        setPanelY(value);
    });
    lv_anim_start(&anim);
}

void refreshToggle(const Toggle& toggle) {
    const bool on = toggle.get();
    lv_obj_set_style_bg_color(toggle.button,
                              on ? theme::accent() : lv_color_hex(theme::kSurfaceAlt), 0);
    lv_obj_set_style_text_color(toggle.label,
                                on ? lv_color_hex(theme::kBackground) : theme::textDim(), 0);
}

void toggleEventCb(lv_event_t* event) {
    Toggle* toggle = static_cast<Toggle*>(lv_event_get_user_data(event));
    toggle->set(!toggle->get());
    refreshToggle(*toggle);
    tick();
}

void brightnessEventCb(lv_event_t* event) {
    const int32_t value = lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(event)));
    settings::setInt("brightness", value);
    display::setBrightness(value);
    power::wake();
}

void volumeEventCb(lv_event_t* event) {
    const int32_t value = lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(event)));
    settings::setInt("snd_volume", value);
    audio::setVolume(value);
}

void scrimEventCb(lv_event_t*) {
    close();
}

// Dragging the grab bar upwards pulls the shade closed, mirroring the pull-down
// gesture on the status bar.
void handleEventCb(lv_event_t* event) {
    static int32_t startY  = 0;
    static bool    decided = false;

    lv_indev_t* indev = lv_indev_active();
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    switch (lv_event_get_code(event)) {
        case LV_EVENT_PRESSED:
            startY  = point.y;
            decided = false;
            break;

        case LV_EVENT_PRESSING:
            if (!decided && abs(point.y - startY) > 6) {
                decided = true;
                beginDrag();
            }
            if (decided) dragTo(point.y - startY);
            break;

        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:
            if (decided) endDrag();
            else         close();
            decided = false;
            break;

        default:
            break;
    }
}

lv_obj_t* makeToggleButton(lv_obj_t* parent, Toggle& toggle) {
    lv_obj_t* button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 68, 46);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_clickable(button, true);
    lv_obj_set_scrollable(button, false);

    lv_obj_t* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, theme::uiFontSmall(), 0);
    lv_label_set_text_fmt(label, "%s\n%s", toggle.icon, toggle.name);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    toggle.button = button;
    toggle.label  = label;

    lv_obj_add_event_cb(button, toggleEventCb, LV_EVENT_CLICKED, &toggle);
    return button;
}

lv_obj_t* makeSlider(lv_obj_t* parent, const char* icon, int32_t min, int32_t max,
                     lv_event_cb_t callback) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 26);
    lv_obj_set_scrollable(row, false);

    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_color(label, theme::textDim(), 0);
    lv_obj_set_style_text_font(label, theme::uiFontSmall(), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* slider = lv_slider_create(row);
    lv_slider_set_range(slider, min, max);
    lv_obj_set_width(slider, 240);
    lv_obj_set_height(slider, 8);
    lv_obj_align(slider, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(slider, lv_color_hex(theme::kSurfaceAlt), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, theme::accent(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, theme::accent(), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, nullptr);

    return slider;
}

} // namespace

void create() {
    lv_obj_t* top = lv_layer_top();

    // Tap-anywhere-else-to-close layer, behind the panel.
    s_scrim = lv_obj_create(top);
    lv_obj_remove_style_all(s_scrim);
    lv_obj_set_size(s_scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(s_scrim, true);
    lv_obj_set_hidden(s_scrim, true);
    lv_obj_add_event_cb(s_scrim, scrimEventCb, LV_EVENT_CLICKED, nullptr);

    s_panel = lv_obj_create(top);
    lv_obj_set_size(s_panel, LV_PCT(100), kHeight);
    lv_obj_set_style_radius(s_panel, 0, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(theme::kSurface), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_panel, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(theme::kBorder), 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_pad_all(s_panel, 8, 0);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_panel, 7, 0);
    lv_obj_set_scrollable(s_panel, false);
    lv_obj_set_x(s_panel, 0);

    // Row of radio toggles.
    lv_obj_t* row = lv_obj_create(s_panel);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 48);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);

    s_toggles[0] = {nullptr, nullptr, LV_SYMBOL_WIFI,      "WiFi", wifiGet, wifiSet};
    s_toggles[1] = {nullptr, nullptr, LV_SYMBOL_BLUETOOTH, "BLE",  bleGet,  bleSet};
    s_toggles[2] = {nullptr, nullptr, LV_SYMBOL_GPS,       "GPS",  gpsGet,  gpsSet};
    s_toggles[3] = {nullptr, nullptr, LV_SYMBOL_SHUFFLE,   "LoRa", loraGet, loraSet};
    for (Toggle& toggle : s_toggles) makeToggleButton(row, toggle);

    s_brightness = makeSlider(s_panel, LV_SYMBOL_EYE_OPEN, 5, 255, brightnessEventCb);
    s_volume     = makeSlider(s_panel, LV_SYMBOL_VOLUME_MAX, 0, 21, volumeEventCb);

    // Status readout.
    s_readout = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_readout, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(s_readout, theme::textDim(), 0);
    lv_label_set_long_mode(s_readout, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_readout, LV_PCT(100));
    lv_label_set_text(s_readout, "");

    // Shortcuts.
    lv_obj_t* actions = lv_obj_create(s_panel);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), 32);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(actions, false);

    struct Action {
        const char* label;
        void (*run)();
    };
    static const Action kActions[] = {
        {LV_SYMBOL_HOME,     [] { close(); ui::home(); }},
        {LV_SYMBOL_SETTINGS, [] { close(); ui::openApp(ui::AppId::Settings); }},
        {LV_SYMBOL_REFRESH,  [] { close(); ui::reconnectIrc(); }},
        {LV_SYMBOL_POWER,    [] { close(); display::sleep(); }},
    };

    for (const Action& action : kActions) {
        lv_obj_t* button = lv_button_create(actions);
        lv_obj_set_size(button, 66, 28);
        lv_obj_set_style_bg_color(button, lv_color_hex(theme::kSurfaceAlt), 0);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_t* label = lv_label_create(button);
        lv_label_set_text(label, action.label);
        lv_obj_set_style_text_font(label, theme::uiFontSmall(), 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(button, [](lv_event_t* event) {
            reinterpret_cast<void(*)()>(lv_event_get_user_data(event))();
        }, LV_EVENT_CLICKED, reinterpret_cast<void*>(action.run));
    }

    // A grab bar along the bottom edge, so the shade can be dismissed without
    // reaching for the scrim behind it.
    lv_obj_t* handle = lv_obj_create(s_panel);
    lv_obj_remove_style_all(handle);
    lv_obj_set_size(handle, LV_PCT(100), 14);
    lv_obj_set_clickable(handle, true);
    lv_obj_set_scrollable(handle, false);

    lv_obj_t* grip = lv_obj_create(handle);
    lv_obj_remove_style_all(grip);
    lv_obj_set_size(grip, 44, 4);
    lv_obj_set_style_radius(grip, 2, 0);
    lv_obj_set_style_bg_color(grip, lv_color_hex(theme::kBorder), 0);
    lv_obj_set_style_bg_opa(grip, LV_OPA_COVER, 0);
    lv_obj_center(grip);

    lv_obj_add_event_cb(handle, handleEventCb, LV_EVENT_PRESSED,    nullptr);
    lv_obj_add_event_cb(handle, handleEventCb, LV_EVENT_PRESSING,   nullptr);
    lv_obj_add_event_cb(handle, handleEventCb, LV_EVENT_RELEASED,   nullptr);
    lv_obj_add_event_cb(handle, handleEventCb, LV_EVENT_PRESS_LOST, nullptr);

    setPanelY(kHidden);
}

void tick() {
    if (!s_panel) return;

    for (const Toggle& toggle : s_toggles) {
        if (toggle.button) refreshToggle(toggle);
    }

    if (s_brightness) lv_slider_set_value(s_brightness, settings::getInt("brightness"), LV_ANIM_OFF);
    if (s_volume)     lv_slider_set_value(s_volume, settings::getInt("snd_volume"), LV_ANIM_OFF);

    if (!s_readout) return;

    String text;
    text += "Battery  " + String(power::batteryPercent()) + "%  (" +
            String(power::batteryMillivolts() / 1000.0f, 2) + "V)\n";

    if (net::isConnected()) {
        text += "WiFi  " + net::ssid() + "  " + net::ipAddress() +
                "  " + String(net::quality()) + "%\n";
    } else {
        text += "WiFi  not connected\n";
    }

    text += "GPS  " + gps::summary();

    lv_label_set_text(s_readout, text.c_str());
}

void open() {
    if (!s_panel) return;
    s_open = true;
    tick();
    animateTo(0);
}

void close() {
    if (!s_panel) return;
    s_open = false;
    animateTo(kHidden);
}

void toggle() { s_open ? close() : open(); }

bool isOpen() { return s_open; }

void beginDrag() {
    if (!s_panel) return;
    s_dragging = true;
    s_dragBase = lv_obj_get_y(s_panel);
    tick();
}

void dragTo(int32_t offsetY) {
    if (!s_dragging) return;

    int32_t target = s_dragBase + offsetY;
    if (target > 0)       target = 0;
    if (target < kHidden) target = kHidden;
    setPanelY(target);
}

void endDrag() {
    if (!s_dragging) return;
    s_dragging = false;

    // Past forty percent of the travel commits to the gesture.
    const int32_t y       = lv_obj_get_y(s_panel);
    const int32_t shown   = y - kHidden;
    const bool    commit  = shown > (kHeight * 4) / 10;

    s_open = commit;
    animateTo(commit ? 0 : kHidden);
}

} // namespace shade
