#include "IRC.h"


unsigned long joinChannelTime    = 0;
bool          readyToJoinChannel = false;

WiFiClient* client;


bool connectToIRC() {
    if (irc_tls) {
        Serial.println("Connecting to IRC with TLS: " + String(irc_server) + ":" + String(irc_port));
        client = new WiFiClientSecure();
        static_cast<WiFiClientSecure*>(client)->setInsecure();
        return static_cast<WiFiClientSecure*>(client)->connect(irc_server.c_str(), irc_port);
    } else {
        Serial.println("Connecting to IRC: " + String(irc_server) + ":" + String(irc_port));
        client = new WiFiClient();
        return client->connect(irc_server.c_str(), irc_port);
    }
}


void handleIRC() {
    while (client->available()) {
        String line = client->readStringUntil('\n');

        if (line.length() > 512) {
            Serial.println("WARNING: IRC line length exceeds 512 characters!");
            line = line.substring(0, 512);
        }

        Serial.println("IRC: " + line);

        int firstSpace = line.indexOf(' ');
        int secondSpace = line.indexOf(' ', firstSpace + 1);

        if (firstSpace != -1 && secondSpace != -1) {
            String prefix = line.substring(0, firstSpace);
            String command = line.substring(firstSpace + 1, secondSpace);

            if (command == "001") {
                joinChannelTime = millis() + 2500;
                readyToJoinChannel = true;
            }
        }

        if (line.startsWith("PING")) {
            String pingResponse = "PONG " + line.substring(line.indexOf(' ') + 1);
            sendIRC(pingResponse);
        } else {
            parseAndDisplay(line);
            lastActivityTime = millis();
        }
    }
}


void sendIRC(String command) {
    if (command.length() > 510) {
        Serial.println("Failed to send: Command too long");
        return;
    }

    if (client->connected()) {
        if (client->println(command)) {
            Serial.println("IRC: >>> " + command);
        } else {
            Serial.println("Failed to send: " + command);
        }
    } else {
        Serial.println("Failed to send: Not connected to IRC");
    }
}


