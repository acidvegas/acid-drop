#include "apps/SettingsApp.h"

#include <vector>

#include "apps/IrcApp.h"
#include "board/Audio.h"
#include "board/Ble.h"
#include "board/Display.h"
#include "board/Gps.h"
#include "board/Power.h"
#include "board/Radio.h"
#include "board/Input.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "irc/IrcClient.h"
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

String      s_section;                       // empty while showing the section list
const char* s_group   = settings::kGroupSystem;
const SettingDef* s_editing = nullptr;

void showSections();
void showSection(const String& section);

// A row that opens another screen rather than editing a value.
void addShortcutRow(const char* label, const char* help, void (*onClick)());

// --- applying a change ----------------------------------------------------
// Settings are only useful if changing them does something immediately.
void applyLive(const char* key) {
    const String name(key);

    if (name == "brightness")  { display::setBrightness(settings::getInt(key)); power::wake(); }
    else if (name == "dim_secs" || name == "off_secs" || name == "dim_level" ||
             name == "cpu_mhz") { power::applySettings(); }
    else if (name.startsWith("snd_"))  { audio::applySettings(); }
    else if (name == "wifi_enable")    { net::setEnabled(settings::getBool(key)); }
    else if (name == "wifi_ps" || name == "dev_name") { net::applySettings(); }
    else if (name == "ble_enable")     { ble::setEnabled(settings::getBool(key)); }
    else if (name == "ble_name")       { if (ble::enabled()) { ble::setEnabled(false); ble::setEnabled(true); } }
    else if (name == "gps_enable")     { gps::setEnabled(settings::getBool(key)); }
    else if (name.startsWith("lora_")) { radio::applySettings(); }
    else if (name == "tz_offset" || name == "dst") { net::applyTimezone(); }
    else if (name == "ntp_server" || name == "ntp_enable") { net::syncClock(true); }
    else if (name.startsWith("irc_"))  { ui::irc().applySettings(); ircapp::applySettings(); }
    else if (name.startsWith("term_")) { ircapp::applySettings(); }

    LOG_I(TAG, "%s = %s", key, settings::getAsString(key).c_str());
}

// --- editors --------------------------------------------------------------

void closeEditor() {
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
    lv_obj_set_style_text_font(title, theme::uiFont(), 0);
    lv_obj_set_style_text_color(title, theme::accent(), 0);

    if (def.help) {
        lv_obj_t* help = lv_label_create(card);
        lv_label_set_text(help, def.help);
        lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(help, LV_PCT(100));
        lv_obj_set_style_text_font(help, theme::uiFontSmall(), 0);
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
    lv_obj_set_style_bg_color(cancel, lv_color_hex(theme::kSurfaceAlt), 0);
    lv_obj_t* cancelLabel = lv_label_create(cancel);
    lv_label_set_text(cancelLabel, "Cancel");
    lv_obj_center(cancelLabel);
    lv_obj_add_event_cb(cancel, [](lv_event_t*) { closeEditor(); }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(input::group(), cancel);

    lv_obj_t* save = lv_button_create(row);
    lv_obj_set_style_bg_color(save, theme::accent(), 0);
    lv_obj_t* saveLabel = lv_label_create(save);
    lv_label_set_text(saveLabel, "Save");
    lv_obj_set_style_text_color(saveLabel, lv_color_hex(theme::kBackground), 0);
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
    lv_obj_set_style_text_font(field, theme::uiFont(), 0);
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
            showSection(s_section);
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

    if (useSlider) {
        s_valueLabel = lv_label_create(card);
        lv_obj_set_style_text_font(s_valueLabel, theme::uiFontLarge(), 0);
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
        lv_obj_set_style_text_font(range, theme::uiFontSmall(), 0);
        lv_obj_set_style_text_color(range, theme::textDim(), 0);
    }

    addEditorButtons(card, [](lv_event_t*) {
        if (!s_editing) return;
        const int32_t value = s_isSlider
            ? lv_slider_get_value(s_control)
            : String(lv_textarea_get_text(s_control)).toInt();

        const char* key = s_editing->key;
        settings::setInt(key, value);
        closeEditor();
        applyLive(key);
        showSection(s_section);
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
    lv_obj_set_style_text_color(s_roller, lv_color_hex(theme::kBackground), LV_PART_SELECTED);
    lv_group_add_obj(input::group(), s_roller);
    lv_group_focus_obj(s_roller);

    addEditorButtons(card, [](lv_event_t*) {
        if (!s_editing) return;
        const char* key = s_editing->key;
        settings::setEnum(key, lv_roller_get_selected(s_roller));
        closeEditor();
        applyLive(key);
        showSection(s_section);
    });
}

void openEditor(const SettingDef& def) {
    s_editing = &def;
    switch (def.type) {
        case SettingType::Text:  openTextEditor(def);   break;
        case SettingType::Enum:  openEnumEditor(def);   break;
        case SettingType::Int:
        case SettingType::Float: openNumberEditor(def); break;
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
        showSection(s_section);
        return;
    }
    openEditor(*def);
}

void sectionEventCb(lv_event_t* event) {
    const char* section = static_cast<const char*>(lv_event_get_user_data(event));
    showSection(String(section));
}

void addShortcutRow(const char* label, const char* help, void (*onClick)()) {
    lv_obj_t* row = lv_obj_create(s_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    theme::styleRow(row);
    lv_obj_set_style_bg_color(row, lv_color_hex(theme::kAccentDim), 0);
    lv_obj_set_clickable(row, true);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, 1, 0);
    lv_group_add_obj(input::group(), row);

    lv_obj_t* title = lv_label_create(row);
    lv_label_set_text(title, label);
    lv_obj_set_style_text_font(title, theme::uiFont(), 0);

    if (help) {
        lv_obj_t* hint = lv_label_create(row);
        lv_label_set_text(hint, help);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_text_font(hint, theme::uiFontSmall(), 0);
        lv_obj_set_style_text_color(hint, theme::textDim(), 0);
    }

    lv_obj_add_event_cb(row, [](lv_event_t* event) {
        reinterpret_cast<void(*)()>(lv_event_get_user_data(event))();
    }, LV_EVENT_CLICKED, reinterpret_cast<void*>(onClick));
}

void makeHeader(const String& title, bool withBack) {
    lv_obj_clean(s_header);

    if (withBack) {
        lv_obj_t* back = lv_button_create(s_header);
        lv_obj_set_size(back, 34, 24);
        lv_obj_set_style_bg_color(back, lv_color_hex(theme::kSurfaceAlt), 0);
        lv_obj_set_style_radius(back, 5, 0);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t* label = lv_label_create(back);
        lv_label_set_text(label, LV_SYMBOL_LEFT);
        lv_obj_center(label);
        lv_obj_add_event_cb(back, [](lv_event_t*) { showSections(); }, LV_EVENT_CLICKED, nullptr);
        lv_group_add_obj(input::group(), back);
    }

    lv_obj_t* title_label = lv_label_create(s_header);
    lv_label_set_text(title_label, title.c_str());
    lv_obj_set_style_text_font(title_label, theme::uiFont(), 0);
    lv_obj_set_style_text_color(title_label, theme::accent(), 0);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, withBack ? 42 : 0, 0);
}

void showSections() {
    s_section = "";
    const bool irc = strcmp(s_group, settings::kGroupIrc) == 0;
    makeHeader(irc ? "IRC settings" : "Settings", false);

    lv_obj_clean(s_list);

    // A couple of things are not single values, so they get their own screens
    // rather than a row in the generic list.
    if (irc) {
        addShortcutRow(LV_SYMBOL_LIST "  Channels",
                       "Auto-join list, keys and per-channel retry",
                       [] { ui::openApp(ui::AppId::Channels); });
    }

    for (const char* section : settings::sections(s_group)) {
        lv_obj_t* row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        theme::styleRow(row);
        lv_obj_set_clickable(row, true);
        lv_obj_set_scrollable(row, false);
        lv_group_add_obj(input::group(), row);

        lv_obj_t* label = lv_label_create(row);
        lv_label_set_text(label, section);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* chevron = lv_label_create(row);
        lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron, theme::textDim(), 0);
        lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_add_event_cb(row, sectionEventCb, LV_EVENT_CLICKED,
                            const_cast<char*>(section));
    }

    // A reset row at the bottom, because a settings screen this large needs one.
    if (irc) return;

    lv_obj_t* reset = lv_obj_create(s_list);
    lv_obj_remove_style_all(reset);
    lv_obj_set_size(reset, LV_PCT(100), LV_SIZE_CONTENT);
    theme::styleRow(reset);
    lv_obj_set_style_bg_color(reset, lv_color_hex(0x3A1520), 0);
    lv_obj_set_clickable(reset, true);
    lv_group_add_obj(input::group(), reset);

    lv_obj_t* resetLabel = lv_label_create(reset);
    lv_label_set_text(resetLabel, LV_SYMBOL_TRASH "  Factory reset");
    lv_obj_set_style_text_color(resetLabel, lv_color_hex(theme::kDanger), 0);
    lv_obj_align(resetLabel, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_add_event_cb(reset, [](lv_event_t*) {
        static const char* buttons[] = {"Cancel", "Erase", nullptr};
        lv_obj_t* box = lv_msgbox_create(nullptr);
        lv_msgbox_add_title(box, "Factory reset");
        lv_msgbox_add_text(box, "Erase every setting and reboot?");
        lv_msgbox_add_close_button(box);
        LV_UNUSED(buttons);

        lv_obj_t* erase = lv_msgbox_add_footer_button(box, "Erase");
        lv_obj_add_event_cb(erase, [](lv_event_t*) {
            settings::factoryReset();
            delay(200);
            ESP.restart();
        }, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* cancel = lv_msgbox_add_footer_button(box, "Cancel");
        lv_obj_add_event_cb(cancel, [](lv_event_t* event) {
            lv_msgbox_close(static_cast<lv_obj_t*>(lv_event_get_user_data(event)));
        }, LV_EVENT_CLICKED, box);
    }, LV_EVENT_CLICKED, nullptr);
}

void showSection(const String& section) {
    s_section = section;
    makeHeader(section, true);

    lv_obj_clean(s_list);

    // Typing an SSID by hand is miserable, so offer the scanner right here.
    if (section == "WiFi") {
        addShortcutRow(LV_SYMBOL_REFRESH "  Scan for networks",
                       "Pick a network instead of typing its name",
                       [] { ui::openApp(ui::AppId::Wifi); });
    }

    for (const SettingDef& def : settings::defs()) {
        if (section != def.section) continue;

        lv_obj_t* row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        theme::styleRow(row);
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
        lv_obj_set_style_text_font(label, theme::uiFont(), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* value = lv_label_create(top);
        lv_obj_set_style_text_font(value, theme::uiFontSmall(), 0);
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);

        if (def.type == SettingType::Bool) {
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

        if (def.help) {
            lv_obj_t* help = lv_label_create(row);
            lv_label_set_text(help, def.help);
            lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(help, LV_PCT(100));
            lv_obj_set_style_text_font(help, theme::uiFontSmall(), 0);
            lv_obj_set_style_text_color(help, lv_color_hex(theme::kTextFaint), 0);
        }

        lv_obj_add_event_cb(row, rowEventCb, LV_EVENT_CLICKED,
                            const_cast<SettingDef*>(&def));
    }
}

bool keyHook(uint32_t key) {
    if (key != LV_KEY_ESC) return false;
    ui::back();
    return true;
}

} // namespace

void createIrc(lv_obj_t* parent) {
    s_group = settings::kGroupIrc;
    create(parent);
}

void create(lv_obj_t* parent) {
    s_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_page, 6, 0);
    lv_obj_set_style_pad_row(s_page, 6, 0);
    lv_obj_set_scrollable(s_page, false);

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
    showSections();
}

void destroy() {
    closeEditor();
    input::clearKeyHook();
    s_page    = nullptr;
    s_header  = nullptr;
    s_list    = nullptr;
    s_section = "";
    s_group   = settings::kGroupSystem;   // the IRC entry point re-arms it
}

void tick() {}

bool handleBack() {
    if (s_editor) { closeEditor(); return true; }
    if (!s_section.isEmpty()) { showSections(); return true; }
    return false;
}

} // namespace settingsapp
