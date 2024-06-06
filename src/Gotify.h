#pragma once

#include <ArduinoWebsockets.h>

#include "Speaker.h"

using namespace websockets;

extern WebsocketsClient gotifyClient;
extern const char* gotify_server;
extern const char* gotify_token;
extern unsigned long lastAttemptTime;
extern const unsigned long reconnectInterval;

void onMessageCallback(WebsocketsMessage message);
void connectToGotify();
void loopGotifyWebSocket();