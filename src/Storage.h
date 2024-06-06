#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "nvs_flash.h"
#include <SD.h>

#include "pins.h"

extern Preferences preferences;

extern String irc_nickname;
extern String irc_username;
extern String irc_realname;
extern String irc_server;
extern int irc_port;
extern bool irc_tls;
extern String irc_channel;
extern String irc_nickserv;
extern String wifi_ssid;
extern String wifi_password;

void loadPreferences();
bool mountSD();
void setupSD();
void wipeNVS();
