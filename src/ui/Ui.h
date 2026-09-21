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
    Settings,
    Wifi,
    Syslog,
    About,
};

void begin();
void loop();

void  openApp(AppId id);
void  back();                 // to the launcher, or out of a settings section
AppId currentApp();

lv_obj_t* content();          // the container apps build into
int32_t   contentHeight();

// Shared services the apps reach for.
IrcClient& irc();
void       reconnectIrc();

// Shows a transient message across the bottom of the screen.
void toast(const String& text, uint32_t milliseconds = 2000);

} // namespace ui
