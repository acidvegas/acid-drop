#include "ui/Ui.h"

#include "apps/AboutApp.h"
#include "apps/ChannelsApp.h"
#include "apps/GpsApp.h"
#include "apps/IrcApp.h"
#include "apps/Launcher.h"
#include "apps/SettingsApp.h"
#include "apps/SyslogApp.h"
#include "apps/WifiApp.h"
#include "board/Audio.h"
#include "board/Input.h"
#include "board/Power.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "irc/ChannelList.h"
#include "irc/IrcClient.h"
#include "ui/Shade.h"
#include "ui/StatusBar.h"
#include "ui/Theme.h"

namespace ui {
namespace {

constexpr const char* TAG = "ui";

lv_obj_t* s_root    = nullptr;
lv_obj_t* s_content = nullptr;
lv_obj_t* s_toast   = nullptr;
uint32_t  s_toastUntil = 0;

AppId              s_current = AppId::Launcher;
bool               s_appBuilt = false;   // s_current has actually been created
std::vector<AppId> s_stack;              // where back() goes, most recent last
IrcClient          s_irc;

struct AppHooks {
    void (*create)(lv_obj_t*);
    void (*destroy)();
    void (*tick)();
};

AppHooks hooksFor(AppId id) {
    switch (id) {
        case AppId::Irc:         return {ircapp::create,       ircapp::destroy,      ircapp::tick};
        case AppId::IrcSettings: return {settingsapp::createIrc, settingsapp::destroy, settingsapp::tick};
        case AppId::Channels:    return {channelsapp::create,  channelsapp::destroy, channelsapp::tick};
        case AppId::Settings:    return {settingsapp::create,  settingsapp::destroy, settingsapp::tick};
        case AppId::Wifi:        return {wifiapp::create,      wifiapp::destroy,     wifiapp::tick};
        case AppId::Gps:         return {gpsapp::create,       gpsapp::destroy,      gpsapp::tick};
        case AppId::Syslog:      return {syslogapp::create,    syslogapp::destroy,   syslogapp::tick};
        case AppId::About:       return {aboutapp::create,     aboutapp::destroy,    aboutapp::tick};
        case AppId::Launcher:
        default:                 return {launcher::create,     launcher::destroy,    launcher::tick};
    }
}

void wireIrcCallbacks() {
    s_irc.onStateChanged = [](IrcState state) {
        LOG_I(TAG, "IRC state: %d", static_cast<int>(state));
        if (state == IrcState::Ready)        audio::alert(Alert::Connected);
        if (state == IrcState::Reconnecting) audio::alert(Alert::Disconnected);
        ircapp::onIrcStateChanged(state);
    };

    s_irc.onBufferChanged = [](IrcBuffer& buffer) {
        ircapp::onBufferChanged(buffer);
    };

    s_irc.onBufferListChanged = [] {
        ircapp::onBufferListChanged();
    };

    s_irc.onHighlight = [](const String& nick, const String& text, IrcBuffer& buffer) {
        LV_UNUSED(text);
        audio::alert(buffer.kind == BufferKind::Query ? Alert::PrivateMessage : Alert::Mention);
        statusbar::setNotification(true);
        power::wake();
        LOG_I(TAG, "highlight from %s in %s", nick.c_str(), buffer.name.c_str());
    };
}

} // namespace

void begin() {
    theme::init();

    s_root = lv_screen_active();
    lv_obj_set_scrollable(s_root, false);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_root, 0, 0);

    statusbar::create(s_root);

    s_content = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_size(s_content, LV_PCT(100), contentHeight());
    lv_obj_set_style_bg_color(s_content, theme::background(), 0);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_content, false);

    shade::create();

    channels::begin();
    s_irc.begin();
    wireIrcCallbacks();

    // Where to land after boot.
    const uint8_t bootApp = settings::getEnum("boot_app");
    openApp(bootApp == 1 ? AppId::Irc : AppId::Launcher);

    if (settings::getBool("irc_autoconn")) s_irc.connect();
}

void loop() {
    s_irc.loop();

    hooksFor(s_current).tick();

    static uint32_t lastStatusTick = 0;
    const uint32_t  now            = millis();
    if (now - lastStatusTick > 500) {
        lastStatusTick = now;
        statusbar::tick();
        if (shade::isOpen()) shade::tick();
    }

    if (s_toast && s_toastUntil != 0 && static_cast<int32_t>(now - s_toastUntil) >= 0) {
        lv_obj_delete(s_toast);
        s_toast      = nullptr;
        s_toastUntil = 0;
    }
}

void openApp(AppId id) {
    // Re-opening the current app is a no-op, but only once it exists: at boot
    // s_current is already Launcher and nothing has been built yet.
    if (id == s_current && s_appBuilt) return;

    // Remember where we came from, but never let the trail grow without bound
    // and never record the launcher, which is the floor of the stack anyway.
    if (s_appBuilt && s_current != AppId::Launcher) {
        s_stack.push_back(s_current);
        if (s_stack.size() > 4) s_stack.erase(s_stack.begin());
    }

    if (s_appBuilt) {
        hooksFor(s_current).destroy();
        lv_obj_clean(s_content);
    }

    s_current  = id;
    s_appBuilt = true;
    hooksFor(id).create(s_content);

    LOG_I(TAG, "opened app %d", static_cast<int>(id));
}

void back() {
    // The app may own a sub-screen it would rather close first.
    if ((s_current == AppId::Settings || s_current == AppId::IrcSettings) &&
        settingsapp::handleBack()) {
        return;
    }
    if (s_current == AppId::Channels && channelsapp::handleBack()) return;
    if (s_current == AppId::Launcher) return;

    AppId destination = AppId::Launcher;
    if (!s_stack.empty()) {
        destination = s_stack.back();
        s_stack.pop_back();
    }

    // openApp would push the app we are leaving straight back on, so step
    // around it: this is a pop, not a new navigation.
    hooksFor(s_current).destroy();
    lv_obj_clean(s_content);
    s_current  = destination;
    s_appBuilt = true;
    hooksFor(destination).create(s_content);
}

AppId currentApp() { return s_current; }

lv_obj_t* content() { return s_content; }

int32_t contentHeight() {
    return lv_display_get_vertical_resolution(lv_display_get_default()) - statusbar::kHeight;
}

IrcClient& irc() { return s_irc; }

void reconnectIrc() {
    s_irc.disconnect("Reconnecting", false);
    s_irc.connect();
    toast("Reconnecting to IRC");
}

void toast(const String& text, uint32_t milliseconds) {
    if (s_toast) lv_obj_delete(s_toast);

    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_toast);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(theme::kSurfaceAlt), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_toast, 6, 0);
    lv_obj_set_style_pad_all(s_toast, 8, 0);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_toast, LV_PCT(90), 0);

    lv_obj_t* label = lv_label_create(s_toast);
    lv_label_set_text(label, text.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(label, theme::text(), 0);

    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -12);
    s_toastUntil = millis() + milliseconds;
}

} // namespace ui
