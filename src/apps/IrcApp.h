#pragma once

#include <lvgl.h>

#include "irc/IrcClient.h"

namespace ircapp {

void create(lv_obj_t* parent);
void destroy();
void tick();

// Called by the UI layer whether or not the app is on screen, so unread counts
// and the status bar stay right while the user is somewhere else.
void onBufferChanged(IrcBuffer& buffer);
void onBufferListChanged();
void onIrcStateChanged(IrcState state);

// The relay equivalents. This screen renders either source, so both funnel
// into the same redraw rather than each having their own path through it.
void onWindowContentChanged();
void onRelayStateChanged();

// Re-reads fonts, colours and timestamp settings.
void applySettings();

} // namespace ircapp
