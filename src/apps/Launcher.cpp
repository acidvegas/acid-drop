#include "apps/Launcher.h"

#include "board/Input.h"
#include "irc/IrcClient.h"
#include "net/WifiService.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace launcher {
namespace {

struct Entry {
    const char* icon;
    const char* name;
    ui::AppId   app;
};

const Entry kEntries[] = {
    {LV_SYMBOL_KEYBOARD, "IRC",      ui::AppId::Irc},
    {LV_SYMBOL_WIFI,     "WiFi",     ui::AppId::Wifi},
    {LV_SYMBOL_SETTINGS, "Settings", ui::AppId::Settings},
    {LV_SYMBOL_LIST,     "Syslog",   ui::AppId::Syslog},
    {LV_SYMBOL_FILE,     "About",    ui::AppId::About},
};

lv_obj_t* s_subtitle = nullptr;

void tileEventCb(lv_event_t* event) {
    const Entry* entry = static_cast<const Entry*>(lv_event_get_user_data(event));
    ui::openApp(entry->app);
}

} // namespace

void create(lv_obj_t* parent) {
    lv_obj_t* page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(page, 10, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 8, 0);
    lv_obj_set_scrollable(page, false);

    lv_obj_t* title = lv_label_create(page);
    lv_label_set_text(title, "ACID DROP");
    lv_obj_set_style_text_font(title, theme::uiFontLarge(), 0);
    lv_obj_set_style_text_color(title, theme::accent(), 0);

    s_subtitle = lv_label_create(page);
    lv_obj_set_style_text_font(s_subtitle, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(s_subtitle, theme::textDim(), 0);
    lv_label_set_text(s_subtitle, "");

    lv_obj_t* grid = lv_obj_create(page);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);

    for (const Entry& entry : kEntries) {
        lv_obj_t* tile = lv_obj_create(grid);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, 92, 62);
        theme::styleRow(tile);
        lv_obj_set_clickable(tile, true);
        lv_obj_set_scrollable(tile, false);
        lv_group_add_obj(input::group(), tile);

        lv_obj_t* label = lv_label_create(tile);
        lv_label_set_text_fmt(label, "%s\n%s", entry.icon, entry.name);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);

        lv_obj_add_event_cb(tile, tileEventCb, LV_EVENT_CLICKED,
                            const_cast<Entry*>(&entry));
    }

    tick();
}

void destroy() {
    s_subtitle = nullptr;
}

void tick() {
    if (!s_subtitle) return;

    static uint32_t lastUpdate = 0;
    const uint32_t  now        = millis();
    if (now - lastUpdate < 1000) return;
    lastUpdate = now;

    String text = net::isConnected() ? net::ssid() : String("no network");
    text += "  " LV_SYMBOL_BULLET "  IRC ";
    text += ui::irc().stateText();
    lv_label_set_text(s_subtitle, text.c_str());
}

} // namespace launcher
