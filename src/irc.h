#ifndef IRC_MANAGER_H
#define IRC_MANAGER_H

#include <WiFiClientSecure.h>
#include "buffer.h"
#include "utilities.h"
#include "display.h"
#include "config.h"

extern WiFiClientSecure client;
extern String nick;

bool connectToIRC();
void sendIRC(String command);
void handleIRC();
void parseAndDisplay(String line);
void handleCommand(String command);
void sendRawCommand(String command);
void joinChannels();

#endif
