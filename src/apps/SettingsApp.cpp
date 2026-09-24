#include "apps/SettingsApp.h"

#include <vector>

#include "apps/IrcApp.h"
#include "board/Audio.h"
#include "board/Display.h"
#include "board/Power.h"
#include "board/Input.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "irc/IrcClient.h"
#include "relay/WeechatRelay.h"
#include "net/WifiService.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace settingsapp {
namespace {

constexpr const char* TAG = "settings-ui";

lv_obj_t* s_page    = nullptr;
lv_obj_t* s_header  = nullptr;
lv_obj_t* s_list    = nullptr;
lv_obj_t* s_editor  = nullptr;

String      s_group;     // empty at the top level
String      s_section;   // empty while showing a group's section list
const SettingDef* s_editing = nullptr;

// Brightness, volume and the keyboard backlight are set by looking at the
// device, so dragging their slider applies the value straight away instead of
// waiting for Save. If the editor is then cancelled, this puts it back.
bool s_previewing = false;

// True for the settings worth applying while the slider moves.
bool isPreviewable(const char* key) {
    return strcmp(key, "brightness") == 0 ||
           strcmp(key, "snd_volume") == 0 ||
           strcmp(key, "kb_bright")  == 0;
}

void applyPreview(const char* key, int32_t value) {
    if (strcmp(key, "brightness") == 0) {
        display::setBrightness(value);
    } else if (strcmp(key, "snd_volume") == 0) {
        audio::setVolume(static_cast<uint8_t>(value));
    } else if (strcmp(key, "kb_bright") == 0) {
        // Straight to the keyboard, ignoring the on/off setting: you are
        // looking at the keyboard while you drag this, so it has to light up
        // even if the boot setting is off.
        input::setKeyboardBacklight(static_cast<uint8_t>(value));
    }
}

// Puts a previewed setting back to whatever is actually saved. This re-derives
// from the settings rather than replaying the old number, because the keyboard
// backlight also depends on its own on/off switch - putting the brightness
// back on its own would leave the keyboard lit with the feature turned off.
void restorePreview(const char* key) {
    if (strcmp(key, "brightness") == 0) {
        display::setBrightness(settings::getInt(key));
    } else if (strcmp(key, "snd_volume") == 0) {
        audio::setVolume(static_cast<uint8_t>(settings::getInt(key)));
    } else if (strcmp(key, "kb_bright") == 0) {
        input::applyKeyboardBacklight();
    }
}

// The menu is three levels: a short list of groups, the sections inside a
// group, then the values inside a section. Before this it was one flat list of
// eleven unlabelled sections - five of which were IRC settings with nothing
// saying so - plus WiFi appearing three separate times.
void showGroups();
void showGroup(const String& group);
void showSection(const String& section);
void addSettingRows(const String& section);

// Re-draws whichever level is currently showing, after a value changed. It
// cannot just be showSection(s_section): a group that lists its values inline
// has no section open, and that would rebuild the list as empty.
void refreshCurrentList();

// A row that opens another screen rather than editing a value.
void addShortcutRow(const char* icon, const char* label, void (*onClick)());

// How the menu is laid out. Presentation lives here rather than in the
// settings registry, so re-arranging the menu never means touching the values.
struct MenuGroup {
    const char*        title;
    const char*        icon;
    const char* const* sections;   // nullptr-terminated, in display order
    bool               hasExtras;  // shortcut rows appended by showGroup()
};

const char* const kIrcSections[]     = {"Server", "ZNC", "Relay", "Identity",
                                        "Authentication", "Connection", "Chat",
                                        nullptr};
const char* const kWifiSections[]    = {"WiFi", nullptr};
const char* const kThemeSections[]   = {"Theme", nullptr};
const char* const kDisplaySections[] = {"Display", nullptr};
const char* const kSoundSections[]   = {"Sound", nullptr};
const char* const kPowerSections[]   = {"Power", nullptr};
const char* const kDeviceSections[]  = {"Device", nullptr};
const char* const kSystemSections[]  = {"Advanced", nullptr};

const MenuGroup kMenu[] = {
    {"IRC",     LV_SYMBOL_KEYBOARD, kIrcSections,     true},
    {"WiFi",    LV_SYMBOL_WIFI,     kWifiSections,    true},
    {"Theme",   LV_SYMBOL_TINT,     kThemeSections,   false},
    {"Display", LV_SYMBOL_IMAGE,    kDisplaySections, false},
    {"Sound",   LV_SYMBOL_AUDIO,    kSoundSections,   false},
    {"Power",   LV_SYMBOL_CHARGE,   kPowerSections,   false},
    {"Device",  LV_SYMBOL_SETTINGS, kDeviceSections,  false},
    {"System",  LV_SYMBOL_FILE,     kSystemSections,  true},
};

const MenuGroup* findGroup(const String& title) {
    for (const MenuGroup& group : kMenu) {
        if (title == group.title) return &group;
    }
    return nullptr;
}

// True when the group is a single section with nothing else on it, in which
// case its own screen would be one row deep and is skipped entirely.
bool groupIsPassthrough(const MenuGroup& group) {
    return !group.hasExtras && group.sections[0] != nullptr && group.sections[1] == nullptr;
}

// --- applying a change ----------------------------------------------------
// Settings are only useful if changing them does something immediately.
void applyLive(const char* key) {
    const String name(key);

    if (name == "brightness")  { display::setBrightness(settings::getInt(key)); power::wake(); }
    else if (name == "dim_secs" || name == "off_secs" || name == "dim_level" ||
             name == "cpu_mhz") { power::applySettings(); }
    else if (name.startsWith("snd_"))  { audio::applySettings(); }
    else if (name.startsWith("kb_"))   { input::applyKeyboardBacklight(); }
    else if (name.startsWith("ball_")) { input::applySettings(); }
    else if (name == "wifi_enable")    { net::setEnabled(settings::getBool(key)); }
    else if (name == "wifi_ps" || name == "dev_name") { net::applySettings(); }
    // Addressing is applied on the next association, so changing it while
    // connected does nothing until the link comes back. Say so rather than
    // silently doing nothing.
    else if (name.startsWith("net_")) {
        ui::toast(net::isConnected() ? "Applies on the next connection"
                                     : "Saved");
    }
    else if (name == "tz_offset" || name == "dst") { net::applyTimezone(); }
    else if (name == "ntp_server" || name == "ntp_enable") { net::syncClock(true); }
    else if (name == "chat_mode")      { ui::switchChatMode(); }
    else if (name.startsWith("znc_"))  { ui::irc().applySettings(); }
    else if (name.startsWith("relay_")) { ui::relay().applySettings(); }
    else if (name.startsWith("irc_"))  { ui::irc().applySettings(); ircapp::applySettings(); }
    else if (name.startsWith("term_")) { ircapp::applySettings(); }
    // th_* is handled by the colour editor, which calls ui::restyle() - that
    // rebuilds this screen, so there is nothing to apply here.

    LOG_I(TAG, "%s = %s", key, settings::getAsString(key).c_str());
}

// --- editors --------------------------------------------------------------

void closeEditor() {
    // Still previewing when the editor goes away means it was not saved, so
    // the live value has to be put back where it was.
    if (s_previewing && s_editing) {
        restorePreview(s_editing->key);
        s_previewing = false;
    }

    if (s_editor) {
        lv_obj_delete(s_editor);
        s_editor = nullptr;
    }
    s_editing = nullptr;
    if (s_list) lv_group_focus_freeze(input::group(), false);
}

lv_obj_t* makeEditorShell(const SettingDef& def) {
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_clickable(overlay, true);

    lv_obj_t* card = lv_obj_create(overlay);
    theme::stylePanel(card);
    lv_obj_set_width(card, LV_PCT(88));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, def.label);
    lv_obj_set_style_text_font(title, theme::uiFontTiny(), 0);
    lv_obj_set_style_text_color(title, theme::accent(), 0);

    if (def.help) {
        lv_obj_t* help = lv_label_create(card);
        lv_label_set_text(help, def.help);
        lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(help, LV_PCT(100));
        lv_obj_set_style_text_font(help, theme::uiFontTiny(), 0);
        lv_obj_set_style_text_color(help, theme::textDim(), 0);
    }

    s_editor = overlay;
    return card;
}

void addEditorButtons(lv_obj_t* card, lv_event_cb_t onSave) {
    lv_obj_t* row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 34);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_scrollable(row, false);

    lv_obj_t* cancel = lv_button_create(row);
    lv_obj_set_style_bg_color(cancel, theme::surfaceAlt(), 0);
    lv_obj_t* cancelLabel = lv_label_create(cancel);
    lv_label_set_text(cancelLabel, "Cancel");
    lv_obj_center(cancelLabel);
    lv_obj_add_event_cb(cancel, [](lv_event_t*) { closeEditor(); }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(input::group(), cancel);

    lv_obj_t* save = lv_button_create(row);
    lv_obj_set_style_bg_color(save, theme::accent(), 0);
    lv_obj_t* saveLabel = lv_label_create(save);
    lv_label_set_text(saveLabel, "Save");
    lv_obj_set_style_text_color(saveLabel, theme::background(), 0);
    lv_obj_center(saveLabel);
    lv_obj_add_event_cb(save, onSave, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(input::group(), save);
    lv_group_focus_obj(save);
}

void openTextEditor(const SettingDef& def) {
    lv_obj_t* card = makeEditorShell(def);

    lv_obj_t* field = lv_textarea_create(card);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_text(field, settings::getText(def.key).c_str());
    lv_textarea_set_password_mode(field, def.secret);
    lv_obj_set_width(field, LV_PCT(100));
    lv_obj_set_style_text_font(field, theme::uiFontTiny(), 0);
    lv_group_add_obj(input::group(), field);
    lv_group_focus_obj(field);

    static lv_obj_t* s_field;
    s_field = field;

    addEditorButtons(card, [](lv_event_t*) {
        if (s_editing) {
            settings::setText(s_editing->key, String(lv_textarea_get_text(s_field)));
            const char* key = s_editing->key;
            closeEditor();
            applyLive(key);
            refreshCurrentList();
        }
    });
}

void openNumberEditor(const SettingDef& def) {
    lv_obj_t* card = makeEditorShell(def);

    const int32_t steps = def.step > 0 ? (def.max - def.min) / def.step : 0;
    const bool    useSlider = steps > 0 && steps <= 64;

    static lv_obj_t* s_control;
    static lv_obj_t* s_valueLabel;
    static bool      s_isSlider;

    s_isSlider = useSlider;

    s_previewing = isPreviewable(def.key);

    if (useSlider) {
        s_valueLabel = lv_label_create(card);
        lv_obj_set_style_text_font(s_valueLabel, theme::uiFontTiny(), 0);
        lv_obj_set_style_text_color(s_valueLabel, theme::text(), 0);

        lv_obj_t* slider = lv_slider_create(card);
        lv_slider_set_range(slider, def.min, def.max);
        lv_slider_set_value(slider, settings::getInt(def.key), LV_ANIM_OFF);
        lv_obj_set_width(slider, LV_PCT(100));
        lv_obj_set_style_bg_color(slider, theme::accent(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, theme::accent(), LV_PART_KNOB);
        lv_group_add_obj(input::group(), slider);

        lv_obj_add_event_cb(slider, [](lv_event_t* event) {
            lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
            const int32_t value = lv_slider_get_value(target);
            String text = String(value);
            if (s_editing && s_editing->unit) text += " " + String(s_editing->unit);
            lv_label_set_text(s_valueLabel, text.c_str());

            // Apply as it moves, without persisting: Save writes it, Cancel
            // restores it.
            if (s_previewing && s_editing) applyPreview(s_editing->key, value);
        }, LV_EVENT_VALUE_CHANGED, nullptr);

        s_control = slider;

        String text = String(settings::getInt(def.key));
        if (def.unit) text += " " + String(def.unit);
        lv_label_set_text(s_valueLabel, text.c_str());
    } else {
        lv_obj_t* field = lv_textarea_create(card);
        lv_textarea_set_one_line(field, true);
        lv_textarea_set_accepted_chars(field, "-0123456789");
        lv_textarea_set_text(field, String(settings::getInt(def.key)).c_str());
        lv_obj_set_width(field, LV_PCT(100));
        lv_group_add_obj(input::group(), field);
        lv_group_focus_obj(field);
        s_control = field;

        lv_obj_t* range = lv_label_create(card);
        String text = "Range " + String(def.min) + " to " + String(def.max);
        if (def.unit) text += " " + String(def.unit);
        lv_label_set_text(range, text.c_str());
        lv_obj_set_style_text_font(range, theme::uiFontTiny(), 0);
        lv_obj_set_style_text_color(range, theme::textDim(), 0);
    }

    addEditorButtons(card, [](lv_event_t*) {
        if (!s_editing) return;
        const int32_t value = s_isSlider
            ? lv_slider_get_value(s_control)
            : String(lv_textarea_get_text(s_control)).toInt();

        const char* key = s_editing->key;
        settings::setInt(key, value);

        // The previewed value is the one being kept, so stand the restore down
        // before the editor closes.
        s_previewing = false;

        closeEditor();
        applyLive(key);
        refreshCurrentList();
    });
}

void openEnumEditor(const SettingDef& def) {
    lv_obj_t* card = makeEditorShell(def);

    String options;
    for (int i = 0; def.options[i]; i++) {
        if (i) options += "\n";
        options += def.options[i];
    }

    static lv_obj_t* s_roller;
    s_roller = lv_roller_create(card);
    lv_roller_set_options(s_roller, options.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(s_roller, settings::getEnum(def.key), LV_ANIM_OFF);
    lv_roller_set_visible_row_count(s_roller, 3);
    lv_obj_set_width(s_roller, LV_PCT(100));
    lv_obj_set_style_bg_color(s_roller, theme::accent(), LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_roller, theme::background(), LV_PART_SELECTED);
    lv_group_add_obj(input::group(), s_roller);
    lv_group_focus_obj(s_roller);

    addEditorButtons(card, [](lv_event_t*) {
        if (!s_editing) return;
        const char* key = s_editing->key;
        settings::setEnum(key, lv_roller_get_selected(s_roller));
        closeEditor();
        applyLive(key);
        refreshCurrentList();
    });
}

// Picking a colour is a grid of swatches, not a colour wheel: a wheel needs
// pixel-accurate pointing, and this device is driven with a trackball. Two
// rows of greys and a spread of hues covers what anyone actually wants.
const uint32_t kSwatches[] = {
    0x000000, 0x07090C, 0x11151B, 0x1A2028, 0x2A323D, 0x454F5C,
    0x6B7684, 0x8A94A3, 0xB8C0CC, 0xE6EAF0, 0xFFFFFF, 0xF5E6C8,
    0x35E08A, 0x00FF9C, 0x1B7A4A, 0x58B4FF, 0x0077FF, 0x004E9A,
    0xFF3B6E, 0xD00040, 0xFFB454, 0xFF7A00, 0xFFE800, 0xC8FF00,
    0xB967FF, 0x7B2FFF, 0xFF00E6, 0x00E5FF, 0x00FFD5, 0xFF5555,
};

void openColorEditor(const SettingDef& def) {
    lv_obj_t* card = makeEditorShell(def);

    lv_obj_t* grid = lv_obj_create(card);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, 5, 0);
    lv_obj_set_style_pad_column(grid, 5, 0);
    lv_obj_set_scrollable(grid, false);

    const uint32_t current = static_cast<uint32_t>(settings::getInt(def.key)) & 0xFFFFFF;

    for (const uint32_t colour : kSwatches) {
        lv_obj_t* swatch = lv_obj_create(grid);
        lv_obj_remove_style_all(swatch);
        lv_obj_set_size(swatch, 26, 22);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(colour), 0);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(swatch, 4, 0);
        lv_obj_set_clickable(swatch, true);
        lv_obj_set_scrollable(swatch, false);

        // The one in use, and whatever the trackball is on, both need to be
        // findable against a grid of colours - so both get an outline.
        lv_obj_set_style_border_color(swatch, theme::text(), 0);
        lv_obj_set_style_border_width(swatch, colour == current ? 2 : 0, 0);
        lv_obj_set_style_outline_color(swatch, theme::text(), LV_STATE_FOCUSED);
        lv_obj_set_style_outline_width(swatch, 2, LV_STATE_FOCUSED);
        lv_obj_set_style_outline_opa(swatch, LV_OPA_COVER, LV_STATE_FOCUSED);
        lv_obj_set_scroll_on_focus(swatch, true);
        lv_group_add_obj(input::group(), swatch);

        lv_obj_add_event_cb(swatch, [](lv_event_t* event) {
            if (!s_editing) return;
            const uint32_t chosen =
                reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)) & 0xFFFFFF;
            const char* key = s_editing->key;

            settings::setInt(key, static_cast<int32_t>(chosen));
            closeEditor();

            // Rebuilds the whole screen, this one included, which is why
            // nothing may touch the editor after this call.
            ui::restyle();
        }, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(colour)));
    }

    // No Save button: tapping a colour is the decision. A Cancel is still
    // worth having, because backing out of a grid with no obvious exit is not.
    lv_obj_t* row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 30);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_scrollable(row, false);

    lv_obj_t* reset = lv_button_create(row);
    lv_obj_set_style_bg_color(reset, theme::surfaceAlt(), 0);
    lv_obj_t* resetLabel = lv_label_create(reset);
    lv_label_set_text(resetLabel, "Default");
    lv_obj_center(resetLabel);
    lv_group_add_obj(input::group(), reset);
    lv_obj_add_event_cb(reset, [](lv_event_t*) {
        if (!s_editing) return;
        const char* key = s_editing->key;
        settings::setInt(key, s_editing->defNum);
        closeEditor();
        ui::restyle();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel = lv_button_create(row);
    lv_obj_set_style_bg_color(cancel, theme::surfaceAlt(), 0);
    lv_obj_t* cancelLabel = lv_label_create(cancel);
    lv_label_set_text(cancelLabel, "Cancel");
    lv_obj_center(cancelLabel);
    lv_group_add_obj(input::group(), cancel);
    lv_group_focus_obj(cancel);
    lv_obj_add_event_cb(cancel, [](lv_event_t*) { closeEditor(); }, LV_EVENT_CLICKED, nullptr);
}

void openEditor(const SettingDef& def) {
    s_editing = &def;
    switch (def.type) {
        case SettingType::Text:  openTextEditor(def);   break;
        case SettingType::Enum:  openEnumEditor(def);   break;
        case SettingType::Int:
        case SettingType::Float: openNumberEditor(def); break;
        case SettingType::Color: openColorEditor(def);  break;
        case SettingType::Bool:  break;   // handled inline
    }
}

// --- rows -----------------------------------------------------------------

void rowEventCb(lv_event_t* event) {
    const SettingDef* def = static_cast<const SettingDef*>(lv_event_get_user_data(event));
    if (!def) return;

    if (def->type == SettingType::Bool) {
        settings::setBool(def->key, !settings::getBool(def->key));
        applyLive(def->key);
        refreshCurrentList();
        return;
    }
    openEditor(*def);
}

void addShortcutRow(const char* icon, const char* label, void (*onClick)()) {
    lv_obj_t* row = lv_obj_create(s_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    theme::styleRowCompact(row);
    // No standing tint. These used to be filled with the same green the focus
    // highlight uses, so System log and About looked permanently selected.
    lv_obj_set_clickable(row, true);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, 2, 0);
    lv_group_add_obj(input::group(), row);

    lv_obj_t* title = lv_label_create(row);
    lv_label_set_text(title, (String(icon) + "   " + label).c_str());
    lv_obj_set_style_text_font(title, theme::uiFontTiny(), 0);

    lv_obj_add_event_cb(row, [](lv_event_t* event) {
        reinterpret_cast<void(*)()>(lv_event_get_user_data(event))();
    }, LV_EVENT_CLICKED, reinterpret_cast<void*>(onClick));
}

// One navigation row: icon, title, optional second line, chevron. Used for
// both the group list and the section list so the two levels look alike.
lv_obj_t* addNavRow(const char* icon, const char* title, const char* blurb) {
    lv_obj_t* row = lv_obj_create(s_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    theme::styleRowCompact(row);
    lv_obj_set_clickable(row, true);
    lv_obj_set_scrollable(row, false);
    lv_group_add_obj(input::group(), row);

    // A flex row of two: the text stack, which grows, and the chevron, which
    // gets pushed to the right edge by that growth. The chevron cannot simply
    // be aligned right - flex positions its children itself and would stack it
    // underneath the text instead.
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);

    lv_obj_t* stack = lv_obj_create(row);
    lv_obj_remove_style_all(stack);
    lv_obj_set_height(stack, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(stack, 1);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(stack, 2, 0);
    lv_obj_set_scrollable(stack, false);

    lv_obj_t* titleLabel = lv_label_create(stack);
    lv_label_set_text(titleLabel,
                      icon ? (String(icon) + "   " + title).c_str() : title);
    lv_obj_set_style_text_font(titleLabel, theme::uiFontTiny(), 0);
    lv_obj_set_style_text_color(titleLabel, theme::text(), 0);

    if (blurb) {
        lv_obj_t* blurbLabel = lv_label_create(stack);
        lv_label_set_text(blurbLabel, blurb);
        lv_label_set_long_mode(blurbLabel, LV_LABEL_LONG_DOT);
        lv_obj_set_width(blurbLabel, LV_PCT(100));
        lv_obj_set_style_text_font(blurbLabel, theme::uiFontTiny(), 0);
        lv_obj_set_style_text_color(blurbLabel, theme::textDim(), 0);
    }

    lv_obj_t* chevron = lv_label_create(row);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(chevron, theme::uiFontTiny(), 0);
    lv_obj_set_style_text_color(chevron, theme::textDim(), 0);

    return row;
}

// The back button does something different at each level, so it is handed the
// action rather than guessing from state. There is always one, because a
// screen whose only way out is an undiscoverable trackball hold is how people
// get stranded in here.
void makeHeader(const String& title, void (*onBack)()) {
    lv_obj_clean(s_header);

    lv_obj_t* back = lv_button_create(s_header);
    lv_obj_set_size(back, 36, 26);
    lv_obj_set_style_bg_color(back, theme::surfaceAlt(), 0);
    lv_obj_set_style_radius(back, 5, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* label = lv_label_create(back);
    lv_label_set_text(label, LV_SYMBOL_LEFT);
    lv_obj_center(label);
    lv_group_add_obj(input::group(), back);

    lv_obj_add_event_cb(back, [](lv_event_t* event) {
        reinterpret_cast<void(*)()>(lv_event_get_user_data(event))();
    }, LV_EVENT_CLICKED, reinterpret_cast<void*>(onBack));

    lv_obj_t* titleLabel = lv_label_create(s_header);
    lv_label_set_text(titleLabel, title.c_str());
    lv_obj_set_style_text_font(titleLabel, theme::uiFontTiny(), 0);
    lv_obj_set_style_text_color(titleLabel, theme::accent(), 0);
    lv_obj_align(titleLabel, LV_ALIGN_LEFT_MID, 44, 0);
}

void groupEventCb(lv_event_t* event) {
    showGroup(String(static_cast<const char*>(lv_event_get_user_data(event))));
}

void sectionEventCb(lv_event_t* event) {
    showSection(String(static_cast<const char*>(lv_event_get_user_data(event))));
}

void showGroups() {
    s_group   = "";
    s_section = "";
    makeHeader("Settings", [] { ui::back(); });

    lv_obj_clean(s_list);

    for (const MenuGroup& group : kMenu) {
        lv_obj_t* row = addNavRow(group.icon, group.title, nullptr);
        lv_obj_add_event_cb(row, groupEventCb, LV_EVENT_CLICKED,
                            const_cast<char*>(group.title));
    }
}

void showGroup(const String& groupTitle) {
    const MenuGroup* group = findGroup(groupTitle);
    if (group == nullptr) { showGroups(); return; }

    // Nothing to show but a single section: go straight to the values rather
    // than making someone tap through a screen with one row on it.
    if (groupIsPassthrough(*group)) {
        s_group = groupTitle;
        showSection(String(group->sections[0]));
        return;
    }

    s_group   = groupTitle;
    s_section = "";
    makeHeader(groupTitle, [] { showGroups(); });

    lv_obj_clean(s_list);

    // One section plus a few shortcuts is not worth a sub-menu: put the values
    // straight on this screen. Burying three settings behind an "Advanced" row
    // inside "System" was a level of nesting that earned nothing.
    const bool single = group->sections[0] != nullptr && group->sections[1] == nullptr;

    if (single) {
        addSettingRows(String(group->sections[0]));
    } else {
        for (const char* const* name = group->sections; *name; name++) {
            lv_obj_t* row = addNavRow(nullptr, *name, nullptr);
            lv_obj_add_event_cb(row, sectionEventCb, LV_EVENT_CLICKED,
                                const_cast<char*>(*name));
        }
    }

    if (groupTitle == "IRC") {
        addShortcutRow(LV_SYMBOL_LIST, "Channels",
                       [] { ui::openApp(ui::AppId::Channels); });
    } else if (groupTitle == "WiFi") {
        // The one and only way into the network screen. It used to be reachable
        // from three separate rows in this menu.
        addShortcutRow(LV_SYMBOL_WIFI, "Networks",
                       [] { ui::openApp(ui::AppId::Wifi); });
    } else if (groupTitle == "System") {
        addShortcutRow(LV_SYMBOL_FILE, "System log",
                       [] { ui::openApp(ui::AppId::Syslog); });
        addShortcutRow(LV_SYMBOL_EYE_OPEN, "About",
                       [] { ui::openApp(ui::AppId::About); });

        // No factory reset here. Wiping every setting is a big hammer to leave
        // one mis-tap away in a menu, and the escape hatch still exists: hold
        // W while the device boots - see checkRecoveryKey() in main.cpp.
    }
}

// Back out of a section: to the group's own screen when it has one, otherwise
// straight to the top, since a pass-through group has no screen to return to.
void leaveSection() {
    const MenuGroup* group = findGroup(s_group);
    if (group != nullptr && !groupIsPassthrough(*group)) showGroup(s_group);
    else                                                 showGroups();
}

// Appends every visible value in `section` to the current list. Shared by the
// section screen and by group screens that show their values inline.
void addSettingRows(const String& section) {
    for (const SettingDef& def : settings::defs()) {
        if (section != def.section) continue;
        if (def.hidden) continue;

        lv_obj_t* row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        theme::styleRowCompact(row);
        lv_obj_set_clickable(row, true);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(row, 1, 0);
        lv_group_add_obj(input::group(), row);

        lv_obj_t* top = lv_obj_create(row);
        lv_obj_remove_style_all(top);
        lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_scrollable(top, false);

        lv_obj_t* label = lv_label_create(top);
        lv_label_set_text(label, def.label);
        lv_obj_set_style_text_font(label, theme::uiFontTiny(), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* value = lv_label_create(top);
        lv_obj_set_style_text_font(value, theme::uiFontTiny(), 0);
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);

        if (def.type == SettingType::Color) {
            // A hex string tells you nothing. Show the colour.
            lv_label_set_text(value, "");
            lv_obj_t* chip = lv_obj_create(top);
            lv_obj_remove_style_all(chip);
            lv_obj_set_size(chip, 30, 16);
            lv_obj_set_style_bg_color(
                chip, lv_color_hex(static_cast<uint32_t>(settings::getInt(def.key))), 0);
            lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(chip, 4, 0);
            lv_obj_set_style_border_color(chip, theme::border(), 0);
            lv_obj_set_style_border_width(chip, 1, 0);
            lv_obj_set_scrollable(chip, false);
            lv_obj_align(chip, LV_ALIGN_RIGHT_MID, 0, 0);
        } else if (def.type == SettingType::Bool) {
            const bool on = settings::getBool(def.key);
            lv_label_set_text(value, on ? LV_SYMBOL_OK "  on" : "off");
            lv_obj_set_style_text_color(value, on ? theme::accent() : theme::textDim(), 0);
        } else {
            String text = settings::getAsString(def.key);
            if (def.secret && !text.isEmpty()) text = "********";
            if (text.isEmpty()) text = "-";
            if (def.unit && def.type != SettingType::Enum) text += " " + String(def.unit);
            lv_label_set_text(value, text.c_str());
            lv_obj_set_style_text_color(value, theme::textDim(), 0);
        }

        // No help line under the row. The explanation still appears in the
        // editor, where you are actually deciding something; here it just made
        // every list three times taller than it needed to be.

        lv_obj_add_event_cb(row, rowEventCb, LV_EVENT_CLICKED,
                            const_cast<SettingDef*>(&def));
    }
}

void showSection(const String& section) {
    s_section = section;
    makeHeader(section, [] { leaveSection(); });

    lv_obj_clean(s_list);
    addSettingRows(section);
}

void refreshCurrentList() {
    if (!s_section.isEmpty())    showSection(s_section);
    else if (!s_group.isEmpty()) showGroup(s_group);
    else                         showGroups();
}

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
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_page, 6, 0);
    lv_obj_set_style_pad_row(s_page, 6, 0);
    lv_obj_set_scrollable(s_page, false);
    // Inherited by everything on the page that does not set its own font:
    // buttons, text fields and the back arrow.
    lv_obj_set_style_text_font(s_page, theme::uiFontTiny(), 0);

    s_header = lv_obj_create(s_page);
    lv_obj_remove_style_all(s_header);
    lv_obj_set_size(s_header, LV_PCT(100), 28);
    lv_obj_set_scrollable(s_header, false);

    s_list = lv_obj_create(s_page);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 5, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    input::setKeyHook(keyHook);
    showGroups();
}

void destroy() {
    closeEditor();
    input::clearKeyHook();
    s_page    = nullptr;
    s_header  = nullptr;
    s_list    = nullptr;
    s_group   = "";
    s_section = "";
}

void tick() {}

bool handleBack() {
    // Unwinds one level per press: editor, then section, then group, and only
    // then does it hand back to ui::back() to leave settings altogether.
    if (s_editor) { closeEditor(); return true; }
    if (!s_section.isEmpty()) { leaveSection(); return true; }
    if (!s_group.isEmpty())   { showGroups();   return true; }
    return false;
}

} // namespace settingsapp
