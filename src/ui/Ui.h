#pragma once

#include <Arduino.h>
#include <lvgl.h>

class IrcClient;
class WeechatRelay;

// Screen management. One status bar and one content area live for the whole
// session; screens build into the content area and are torn down when they are
// swapped out. The IRC client is deliberately not a screen - it keeps running
// whatever is on top of it.
//
// IRC is the root. There is no launcher: this is IRC firmware, so the chat
// window is what the device is, and everything else is something you open on
// top of it and come back from.

namespace ui {

enum class AppId : uint8_t {
    Irc,           // the root screen
    Settings,      // one screen for everything, IRC sections first
    Channels,      // saved channel list editor
    Wifi,
    Syslog,
    About,
};

void begin();
void loop();

void openApp(AppId id);

// Returns to IRC. Screens get first refusal so they can close a sub-screen
// instead of leaving entirely.
void back();

lv_obj_t* content();          // the container screens build into

// The standard header every screen puts at the top: a back button wired to
// back(), then the title. The T-Deck keyboard has no escape key, so this
// button is the only way out of a screen for most people.
lv_obj_t* createAppHeader(lv_obj_t* parent, const char* title);

// Goes straight to IRC, whatever is open. Bound to the trackball hold.
void home();

// Re-reads the theme and rebuilds the current screen with it. Widgets keep the
// colours they were created with, so a live theme change means building them
// again - there is no way to repaint LVGL objects wholesale.
void restyle();

// Shared services the screens reach for.
IrcClient& irc();
WeechatRelay& relay();

// True when the device is a window onto a WeeChat relay rather than an IRC
// client in its own right. The two are mutually exclusive: WeeChat is already
// on the networks, so connecting directly as well would just be a second
// client fighting for the same nick.
// What this device is connected to. Exactly one at a time.
enum class ChatMode : uint8_t { DirectIrc = 0, Znc = 1, Relay = 2 };

ChatMode chatMode();

// True only for the WeeChat relay. ZNC is plain IRC over a bouncer, so it runs
// through the IRC client and answers false here.
bool relayMode();

// Called when the relay toggle changes. Drops whichever connection is no
// longer wanted - leaving the other one open would keep a socket and a nick
// held by a mode nobody is looking at.
void switchChatMode();

// Shows a transient message across the bottom of the screen.
void toast(const String& text, uint32_t milliseconds = 2000);

} // namespace ui
