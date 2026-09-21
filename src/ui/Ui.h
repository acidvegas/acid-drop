#pragma once

#include <Arduino.h>
#include <lvgl.h>

class IrcClient;

// Screen management. One status bar and one content area live for the whole
// session; apps build into the content area and are torn down when they are
// swapped out. The IRC client is deliberately not an app - it keeps running
// whatever is on screen.

namespace ui {

enum class AppId : uint8_t {
    Launcher,
    Irc,
    IrcSettings,   // the IRC app's own settings, scoped to the "irc" group
    Channels,      // saved channel list editor
    Settings,      // everything that is not IRC
    Wifi,
    Gps,
    Syslog,
    About,
};

void begin();
void loop();

void  openApp(AppId id);

// Returns to whatever opened the current app, falling back to the launcher.
// Apps get first refusal so they can close a sub-screen instead.
void  back();
AppId currentApp();

lv_obj_t* content();          // the container apps build into
int32_t   contentHeight();

// The standard header every app puts at the top: a back button wired to
// back(), then the title. The T-Deck keyboard has no escape key, so this
// button is the only way out of an app for most people.
lv_obj_t* createAppHeader(lv_obj_t* parent, const char* title);

// Goes straight to the launcher, whatever the back stack says.
void home();

// Shared services the apps reach for.
IrcClient& irc();
void       reconnectIrc();

// Shows a transient message across the bottom of the screen.
void toast(const String& text, uint32_t milliseconds = 2000);

} // namespace ui
