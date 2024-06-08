#pragma once

#include <WiFiClientSecure.h>

#include "Display.h"
#include "Storage.h"

extern WiFiClient* client;
extern unsigned long joinChannelTime;
extern bool readyToJoinChannel;

bool connectToIRC();
void handleIRC();
void sendIRC(String command);
