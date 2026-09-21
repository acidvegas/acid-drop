#include "apps/Launcher.h"

#include <vector>

#include "board/Gps.h"
#include "board/Input.h"
#include "core/Settings.h"
#include "irc/IrcClient.h"
#include "net/WifiService.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace launcher {
namespace {

// A grid of app tiles. Each tile carries a status line so the home screen is
// worth looking at rather than just a menu to get past.
struct Entry {
    const char* icon;
    const char* name;
    ui::AppId   app;
    String    (*status)();
    bool      (*active)();     // drives the accent dot, may be null
};

lv_obj_t* s_grid = nullptr;

struct Tile {
    lv_obj_t* root;
    lv_obj_t* status;
    lv_obj_t* dot;
};
std::vector<Tile> s_tiles;

constexpr int kColumns = 3;
int s_focus = 0;

// LVGL's keypad navigation is a flat NEXT/PREV walk, which on a grid means the
// trackball crawls along rows instead of moving the way the ball does. The
// launcher owns its own navigation so up/down/left/right mean what they look
// like, and so focus can never leave the grid.
void focusTile(int index) {
    if (s_tiles.empty()) return;

    const int count = static_cast<int>(s_tiles.size());
    if (index < 0)      index += count;
    if (index >= count) index -= count;

    s_focus = index;
    lv_group_focus_obj(s_tiles[index].root);
    lv_obj_scroll_to_view(s_tiles[index].root, LV_ANIM_OFF);
}

String ircStatus() {
    IrcClient& client = ui::irc();

    uint16_t unread    = 0;
    bool     highlight = false;
    for (size_t i = 0; i < client.bufferCount(); i++) {
        unread    += client.buffer(i).doc.unread;
        highlight |= client.buffer(i).doc.unreadHighlight;
    }

    if (client.state() == IrcState::Ready) {
        // Prefer showing where you are over saying "connected".
        for (size_t i = 0; i < client.bufferCount(); i++) {
            IrcBuffer& buffer = client.buffer(i);
            if (buffer.isChannel() && buffer.joined) {
                String text = buffer.name;
                if (unread > 0) text += "  " + String(unread);
                if (highlight)  text += "!";
                return text;
            }
        }
    }
    return client.stateText();
}

String wifiStatus() {
    if (!net::enabled())     return "off";
    if (net::isScanning())   return "scanning";
    if (!net::isConnected()) return "not connected";
    return net::ssid();
}

String settingsStatus() { return ""; }

String gpsStatus() {
    if (!gps::enabled()) return "off";
    return gps::hasFix() ? String(gps::satellites()) + " sats" : "searching";
}

String syslogStatus() { return "device log"; }
String aboutStatus()  { return "system info"; }

bool ircActive()  { return ui::irc().state() == IrcState::Ready; }
bool wifiActive() { return net::isConnected(); }
bool gpsActive()  { return gps::hasFix(); }

const Entry kEntries[] = {
    {LV_SYMBOL_KEYBOARD, "IRC",      ui::AppId::Irc,      ircStatus,      ircActive},
    {LV_SYMBOL_WIFI,     "WiFi",     ui::AppId::Wifi,     wifiStatus,     wifiActive},
    {LV_SYMBOL_SETTINGS, "Settings", ui::AppId::Settings, settingsStatus, nullptr},
    {LV_SYMBOL_GPS,      "GPS",      ui::AppId::Gps,      gpsStatus,      gpsActive},
    {LV_SYMBOL_LIST,     "Syslog",   ui::AppId::Syslog,   syslogStatus,   nullptr},
    {LV_SYMBOL_FILE,     "About",    ui::AppId::About,    aboutStatus,    nullptr},
};

bool keyHook(uint32_t key) {
    if (s_tiles.empty()) return false;

    const int count = static_cast<int>(s_tiles.size());
    const int rows  = (count + kColumns - 1) / kColumns;

    switch (key) {
        case LV_KEY_LEFT:
            focusTile(s_focus - 1);
            return true;
        case LV_KEY_RIGHT:
            focusTile(s_focus + 1);
            return true;

        case LV_KEY_UP: {
            int next = s_focus - kColumns;
            if (next < 0) next = s_focus + (rows - 1) * kColumns;   // wrap to the bottom
            while (next >= count) next -= kColumns;
            focusTile(next);
            return true;
        }
        case LV_KEY_DOWN: {
            int next = s_focus + kColumns;
            if (next >= count) next = s_focus % kColumns;            // wrap to the top
            focusTile(next);
            return true;
        }

        case LV_KEY_ENTER:
            // The trackball click is the select button here.
            if (s_focus >= 0 && s_focus < count) ui::openApp(kEntries[s_focus].app);
            return true;

        default:
            return false;
    }
}

void tileEventCb(lv_event_t* event) {
    const Entry* entry = static_cast<const Entry*>(lv_event_get_user_data(event));
    ui::openApp(entry->app);
}

} // namespace

void create(lv_obj_t* parent) {
    s_tiles.clear();

    lv_obj_t* page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(page, 7, 0);
    lv_obj_set_scrollable(page, false);

    s_grid = lv_obj_create(page);
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_size(s_grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_obj_set_style_pad_row(s_grid, 5, 0);
    lv_obj_set_style_pad_column(s_grid, 5, 0);
    lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_OFF);

    for (const Entry& entry : kEntries) {
        lv_obj_t* tile = lv_obj_create(s_grid);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, 97, 92);
        theme::styleRow(tile);
        lv_obj_set_style_pad_all(tile, 4, 0);
        lv_obj_set_clickable(tile, true);
        lv_obj_set_scrollable(tile, false);
        lv_group_add_obj(input::group(), tile);

        lv_obj_t* icon = lv_label_create(tile);
        lv_label_set_text(icon, entry.icon);
        lv_obj_set_style_text_font(icon, theme::uiFontLarge(), 0);
        lv_obj_set_style_text_color(icon, theme::text(), 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t* name = lv_label_create(tile);
        lv_label_set_text(name, entry.name);
        lv_obj_set_style_text_font(name, theme::uiFont(), 0);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 36);

        lv_obj_t* status = lv_label_create(tile);
        lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
        lv_obj_set_width(status, 86);
        lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(status, theme::uiFontSmall(), 0);
        lv_obj_set_style_text_color(status, theme::textDim(), 0);
        lv_label_set_text(status, "");
        lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -4);

        lv_obj_t* dot = lv_obj_create(tile);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, theme::accent(), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -3, 3);
        lv_obj_set_hidden(dot, true);

        lv_obj_add_event_cb(tile, tileEventCb, LV_EVENT_CLICKED, const_cast<Entry*>(&entry));
        s_tiles.push_back({tile, status, dot});
    }

    input::setKeyHook(keyHook);
    focusTile(0);
    tick();
}

void destroy() {
    input::clearKeyHook();
    s_grid = nullptr;
    s_tiles.clear();
    s_focus = 0;
}

void tick() {
    if (s_tiles.empty()) return;

    static uint32_t lastUpdate = 0;
    const uint32_t  now        = millis();
    if (lastUpdate != 0 && now - lastUpdate < 1000) return;
    lastUpdate = now;

    const size_t count = sizeof(kEntries) / sizeof(kEntries[0]);
    for (size_t i = 0; i < count && i < s_tiles.size(); i++) {
        lv_label_set_text(s_tiles[i].status, kEntries[i].status().c_str());
        const bool active = kEntries[i].active && kEntries[i].active();
        lv_obj_set_hidden(s_tiles[i].dot, !active);
    }
}

} // namespace launcher
