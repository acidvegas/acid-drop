#include "ui/Ui.h"

#include "apps/AboutApp.h"
#include "apps/ChannelsApp.h"
#include "apps/IrcApp.h"
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
#include "relay/WeechatRelay.h"
#include "net/NetworkList.h"
#include "net/WifiService.h"
#include "ui/StatusBar.h"
#include "ui/Theme.h"

namespace ui {
namespace {

constexpr const char* TAG = "ui";

lv_obj_t* s_root    = nullptr;
lv_obj_t* s_content = nullptr;
lv_obj_t* s_toast   = nullptr;
uint32_t  s_toastUntil = 0;

AppId     s_current  = AppId::Irc;
bool      s_appBuilt = false;   // s_current has actually been created
IrcClient    s_irc;
WeechatRelay s_relay;

struct AppHooks {
    void (*create)(lv_obj_t*);
    void (*destroy)();
    void (*tick)();
};

AppHooks hooksFor(AppId id) {
    switch (id) {
        case AppId::Settings: return {settingsapp::create, settingsapp::destroy, settingsapp::tick};
        case AppId::Channels: return {channelsapp::create, channelsapp::destroy, channelsapp::tick};
        case AppId::Wifi:     return {wifiapp::create,     wifiapp::destroy,     wifiapp::tick};
        case AppId::Syslog:   return {syslogapp::create,   syslogapp::destroy,   syslogapp::tick};
        case AppId::About:    return {aboutapp::create,    aboutapp::destroy,    aboutapp::tick};
        case AppId::Irc:
        default:              return {ircapp::create,      ircapp::destroy,      ircapp::tick};
    }
}

// Chat follows the network, always. It is what the device is for, so waiting
// to be asked was never the right default - and a setting for it was a setting
// for "do you want this firmware to do its job".
void maybeAutoConnect() {
    if (!net::isConnected()) return;

    if (relayMode()) {
        if (s_relay.userQuit()) return;        // they asked to be offline
        if (s_relay.state() != RelayState::Offline) return;
        if (settings::getText("relay_host").isEmpty()) return;

        LOG_I(TAG, "network up, connecting to the relay");
        s_relay.connect();
        return;
    }

    if (s_irc.userQuit()) return;          // they asked to be offline
    if (s_irc.state() != IrcState::Offline) return;
    if (settings::getText("irc_nick").isEmpty()) return;

    // Nothing to connect to yet. Which host that is depends on the mode.
    const bool znc = chatMode() == ChatMode::Znc;
    if (settings::getText(znc ? "znc_host" : "irc_server").isEmpty()) return;

    LOG_I(TAG, "network up, connecting to %s", znc ? "ZNC" : "IRC");
    s_irc.connect();
}

void wireNetworkCallbacks() {
    net::onConnectionChanged = [](bool connected) {
        if (connected) maybeAutoConnect();
    };
}

void wireRelayCallbacks() {
    s_relay.onStateChanged = [](RelayState state) {
        LOG_I(TAG, "relay state: %d", static_cast<int>(state));
        if (state == RelayState::Ready)        audio::alert(Alert::Connected);
        if (state == RelayState::Reconnecting) audio::alert(Alert::Disconnected);
        ircapp::onRelayStateChanged();
    };

    s_relay.onBufferChanged     = [](RelayBuffer&) { ircapp::onWindowContentChanged(); };
    s_relay.onBufferListChanged = []               { ircapp::onBufferListChanged(); };

    s_relay.onHighlight = [](const String& nick, const String& text, RelayBuffer& buffer) {
        LV_UNUSED(text);
        audio::alert(buffer.isChannel() ? Alert::Mention : Alert::PrivateMessage);
        statusbar::setNotification(true);
        power::wake();
        LOG_I(TAG, "relay highlight from %s in %s", nick.c_str(), buffer.shortName.c_str());
    };
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
    LOG_I(TAG, "ui: theme");
    theme::init();

    s_root = lv_screen_active();
    lv_obj_set_scrollable(s_root, false);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_root, 0, 0);

    LOG_I(TAG, "ui: status bar");
    statusbar::create(s_root);

    s_content = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_content);
    // Grows into whatever the status bar leaves, rather than being sized once
    // against it. Hiding the bar used to leave the content 26px short, which
    // showed up as a band of black below the input row.
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_style_bg_color(s_content, theme::background(), 0);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_content, false);

    LOG_I(TAG, "ui: channels");
    input::setHoldHandler([] { home(); });

    channels::begin();
    netlist::begin();
    LOG_I(TAG, "ui: irc");
    s_irc.begin();
    s_relay.begin();
    wireIrcCallbacks();
    wireRelayCallbacks();
    wireNetworkCallbacks();

    LOG_I(TAG, "ui: opening chat");
    openApp(AppId::Irc);
    LOG_I(TAG, "ui: chat open");
}

void loop() {
    // Only one of the two ever has a connection: relayMode() decides which,
    // and the other sits idle rather than being torn down, so switching back
    // does not lose its settings or its buffers.
    if (relayMode()) s_relay.loop();
    else             s_irc.loop();

    // The association can come up without the callback firing - a reconnect
    // by the supplicant, or a connection that was already live when this
    // screen was built. Cheap enough to just check.
    static uint32_t lastAutoCheck = 0;
    if (millis() - lastAutoCheck > 2000) {
        lastAutoCheck = millis();
        maybeAutoConnect();
    }

    hooksFor(s_current).tick();

    static uint32_t lastStatusTick = 0;
    const uint32_t  now            = millis();
    if (now - lastStatusTick > 500) {
        lastStatusTick = now;
        statusbar::tick();
    }

    if (s_toast && s_toastUntil != 0 && static_cast<int32_t>(now - s_toastUntil) >= 0) {
        lv_obj_delete(s_toast);
        s_toast      = nullptr;
        s_toastUntil = 0;
    }
}

void openApp(AppId id) {
    // Re-opening the current screen is a no-op, but only once it exists: at
    // boot s_current is already Irc and nothing has been built yet.
    if (id == s_current && s_appBuilt) return;

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
    // The screen may own a sub-screen it would rather close first.
    if (s_current == AppId::Settings && settingsapp::handleBack()) return;
    if (s_current == AppId::Channels && channelsapp::handleBack()) return;

    // Everything returns to IRC, because IRC is the only root there is. That
    // includes the settings screen reached from IRC's own gear button, which
    // used to land on a home screen instead of back where it came from.
    if (s_current == AppId::Irc) return;
    openApp(AppId::Irc);
}

lv_obj_t* content() { return s_content; }

lv_obj_t* createAppHeader(lv_obj_t* parent, const char* title) {
    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 26);
    lv_obj_set_scrollable(header, false);

    lv_obj_t* backButton = lv_button_create(header);
    lv_obj_set_size(backButton, 34, 24);
    lv_obj_set_style_bg_color(backButton, theme::surfaceAlt(), 0);
    lv_obj_set_style_radius(backButton, 5, 0);
    lv_obj_align(backButton, LV_ALIGN_LEFT_MID, 0, 0);
    lv_group_add_obj(input::group(), backButton);

    lv_obj_t* backLabel = lv_label_create(backButton);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(backButton, [](lv_event_t*) { back(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* titleLabel = lv_label_create(header);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_font(titleLabel, theme::uiFont(), 0);
    lv_obj_set_style_text_color(titleLabel, theme::accent(), 0);
    lv_obj_align(titleLabel, LV_ALIGN_LEFT_MID, 42, 0);

    return header;
}

void home() { openApp(AppId::Irc); }

void restyle() {
    theme::reload();

    // The screen background and the status bar are built once for the session,
    // so they are re-coloured in place. Everything else belongs to the current
    // screen, and a widget keeps whatever colour it was given at creation -
    // so the only honest way to recolour it is to build it again.
    lv_obj_set_style_bg_color(lv_screen_active(), theme::background(), 0);
    lv_obj_set_style_bg_color(s_content, theme::background(), 0);
    statusbar::applyTheme();

    if (!s_appBuilt) return;

    const AppId current = s_current;
    hooksFor(current).destroy();
    lv_obj_clean(s_content);
    hooksFor(current).create(s_content);
}

IrcClient&    irc()   { return s_irc; }
WeechatRelay& relay() { return s_relay; }

ChatMode chatMode() {
    return static_cast<ChatMode>(settings::getEnum("chat_mode"));
}

bool relayMode() { return chatMode() == ChatMode::Relay; }

void switchChatMode() {
    if (relayMode()) {
        // Empty quit message: the client substitutes the fixed signature.
        s_irc.disconnect(String(), true);
    } else {
        s_relay.disconnect(true);
    }

    // The screen caches an index into whichever list was in use, and the two
    // have nothing to do with each other.
    s_relay.applySettings();
    openApp(AppId::Irc);
    if (s_appBuilt) {
        hooksFor(AppId::Irc).destroy();
        lv_obj_clean(s_content);
        hooksFor(AppId::Irc).create(s_content);
    }
    maybeAutoConnect();
}

void toast(const String& text, uint32_t milliseconds) {
    if (s_toast) lv_obj_delete(s_toast);

    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_toast);
    lv_obj_set_style_bg_color(s_toast, theme::surfaceAlt(), 0);
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
