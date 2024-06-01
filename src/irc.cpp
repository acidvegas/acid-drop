// src/irc.cpp

#include "irc.h"

bool connectToIRC() {
    debugPrint("Connecting to IRC...");
    if (useSSL) {
        client.setInsecure();
        return client.connect(server, port);
    } else {
        WiFiClient nonSecureClient;
        return nonSecureClient.connect(server, port);
    }
}

void sendIRC(String command) {
    if (client.println(command))
        debugPrint("IRC: >>> " + command);
    else
        debugPrint("Failed to send: " + command);
}

void handleIRC() {
    while (client.available()) {
        String line = client.readStringUntil('\n');
        debugPrint("IRC: " + line);

        int firstSpace = line.indexOf(' ');
        int secondSpace = line.indexOf(' ', firstSpace + 1);

        if (firstSpace != -1 && secondSpace != -1) {
            String prefix = line.substring(0, firstSpace);
            String command = line.substring(firstSpace + 1, secondSpace);

            if (command == "001") {
                joinChannelTime = millis() + 2500;
                readyToJoinChannel = true;
            }
            if (command == "332") {
                int topicStart = line.indexOf(':', secondSpace + 1) + 1;
                buffers[currentBufferIndex].topic = line.substring(topicStart);
                topicOffset = 0;
                topicUpdated = true;
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

void parseAndDisplay(String line) {
    int firstSpace = line.indexOf(' ');
    int secondSpace = line.indexOf(' ', firstSpace + 1);

    if (firstSpace != -1 && secondSpace != -1) {
        String command = line.substring(firstSpace + 1, secondSpace);

        if (command == "PRIVMSG") {
            int thirdSpace = line.indexOf(' ', secondSpace + 1);
            String target = line.substring(secondSpace + 1, thirdSpace);
            int colonPos = line.indexOf(':', thirdSpace);
            String message = line.substring(colonPos + 1);
            String senderNick = line.substring(1, line.indexOf('!'));
            bool mention = message.indexOf(nick) != -1;

            if (message.startsWith("\x01" "ACTION ") && message.endsWith("\x01")) {
                String actionMessage = message.substring(8, message.length() - 1);
                if (target != "#blackhole" && target != "#5000") {
                    if (target == buffers[currentBufferIndex].channel) {
                        addLine(senderNick, actionMessage, "action");
                    } else {
                        addLineToBuffer(getBufferByChannel(target), senderNick, actionMessage, "action", mention);
                    }
                }
            } else {
                if (target != "#blackhole" && target != "#5000") {
                    if (target == buffers[currentBufferIndex].channel) {
                        addLine(senderNick, message, "message", mention);
                    } else {
                        addLineToBuffer(getBufferByChannel(target), senderNick, message, "message", mention);
                    }
                }
            }
        } else if (command == "JOIN") {
            String senderNick = line.substring(1, line.indexOf('!'));
            String targetChannel = line.substring(secondSpace + 1);
            if (targetChannel != "#blackhole" && targetChannel != "#5000") {
                if (targetChannel == buffers[currentBufferIndex].channel) {
                    addLine(senderNick, " has joined " + targetChannel, "join", false);
                } else {
                    addLineToBuffer(getBufferByChannel(targetChannel), senderNick, " has joined " + targetChannel, "join", false);
                }
            } else {
                sendIRC("PART " + targetChannel);
            }
        } else if (command == "PART") {
            String senderNick = line.substring(1, line.indexOf('!'));
            String targetChannel = line.substring(secondSpace + 1);
            if (targetChannel != "#blackhole" && targetChannel != "#5000") {
                if (targetChannel == buffers[currentBufferIndex].channel) {
                    addLine(senderNick, " has EMO-QUIT " + targetChannel, "part", false);
                } else {
                    addLineToBuffer(getBufferByChannel(targetChannel), senderNick, " has EMO-QUIT " + targetChannel, "part", false);
                }
            }
        } else if (command == "QUIT") {
            String senderNick = line.substring(1, line.indexOf('!'));
            for (auto& buffer : buffers) {
                if (buffer.channel == buffers[currentBufferIndex].channel) {
                    addLine(senderNick, "", "quit", false);
                } else {
                    addLineToBuffer(buffer, senderNick, "", "quit", false);
                }
            }
        } else if (command == "NICK") {
            String prefix = line.startsWith(":") ? line.substring(1, firstSpace) : "";
            String newNick = line.substring(line.lastIndexOf(':') + 1);
            for (auto& buffer : buffers) {
                if (buffer.channel == buffers[currentBufferIndex].channel) {
                    addLine(prefix, " -> " + newNick, "nick", false);
                } else {
                    addLineToBuffer(buffer, prefix, " -> " + newNick, "nick", false);
                }
            }
        } else if (command == "KICK") {
            int thirdSpace = line.indexOf(' ', secondSpace + 1);
            int fourthSpace = line.indexOf(' ', thirdSpace + 1);
            String kicker = line.substring(1, line.indexOf('!'));
            String kicked = line.substring(thirdSpace + 1, fourthSpace);
            String targetChannel = line.substring(secondSpace + 1, thirdSpace);
            if (targetChannel != "#blackhole" && targetChannel != "#5000") {
                if (targetChannel == buffers[currentBufferIndex].channel) {
                    addLine(kicked, " by " + kicker, "kick", false);
                } else {
                    addLineToBuffer(getBufferByChannel(targetChannel), kicked, " by " + kicker, "kick", false);
                }
            }
        } else if (command == "MODE") {
            String modeChange = line.substring(secondSpace + 1);
            for (auto& buffer : buffers) {
                if (buffer.channel == buffers[currentBufferIndex].channel) {
                    addLine("", modeChange, "mode", false);
                } else {
                    addLineToBuffer(buffer, "", modeChange, "mode", false);
                }
            }
        }
    }
}

void handleCommand(String command) {
    if (command.startsWith("/join ")) {
        String newChannel = command.substring(6);
        bool alreadyInChannel = false;
        for (auto & buffer : buffers) {
            if (buffer.channel == newChannel) {
                alreadyInChannel = true;
                break;
            }
        }
        if (!alreadyInChannel) {
            Buffer newBuffer;
            newBuffer.channel = newChannel;
            buffers.push_back(newBuffer);
            sendIRC("JOIN " + newChannel);
            currentBufferIndex = buffers.size() - 1;
            displayLines();
        }
    } else if (command.startsWith("/part ")) {
        String partChannel = command.substring(6);
        for (auto it = buffers.begin(); it != buffers.end(); ++it) {
            if (it->channel == partChannel) {
                sendIRC("PART " + partChannel);
                buffers.erase(it);
                if (currentBufferIndex >= buffers.size()) {
                    currentBufferIndex = 0;
                }
                displayLines();
                break;
            }
        }
    } else if (command.startsWith("/msg ")) {
        int firstSpace = command.indexOf(' ', 5);
        String target = command.substring(5, firstSpace);
        String message = command.substring(firstSpace + 1);
        sendIRC("PRIVMSG " + target + " :" + message);
        addLine(nick, message, "message");
    } else if (command.startsWith("/nick ")) {
        String newNick = command.substring(6);
        sendIRC("NICK " + newNick);
        nick = newNick;
    } else if (command.startsWith("/info")) {
        displayDeviceInfo();
    } else if (command.startsWith("/")) {
        if (command == "/debug") {
            debugEnabled = true;
            Serial.begin(115200);
            debugPrint("Debugging enabled");
        } else {
            int bufferIndex = command.substring(1).toInt() - 1;
            if (bufferIndex >= 0 && bufferIndex < buffers.size()) {
                currentBufferIndex = bufferIndex;
                displayLines();
            }
        }
    } else if (inputBuffer.startsWith("/me ")) {
        String actionMessage = inputBuffer.substring(4);
        sendIRC("PRIVMSG " + String(currentBufferIndex) + " :\001ACTION " + actionMessage + "\001");
        addLine(nick, actionMessage, "action");
        inputBuffer = "";
    } else if (command.startsWith("/raw ")) {
        String rawCommand = command.substring(5);
        sendRawCommand(rawCommand);
    }
}

void sendRawCommand(String command) {
    if (client.connected()) {
        sendIRC(command);
        debugPrint("Sent raw command: " + command);
    } else {
        debugPrint("Failed to send raw command: Not connected to IRC");
    }
}

void joinChannels() {
    for (auto& buffer : buffers) {
        if (buffer.channel != "#blackhole" && buffer.channel != "#5000") {
            sendIRC("JOIN " + buffer.channel);
        }
    }
}
