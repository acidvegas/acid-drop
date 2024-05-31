#include "buffer.h"
#include "display.h"
#include "utilities.h"

std::vector<Buffer> buffers;
int currentBufferIndex = 0;

void addLineToBuffer(Buffer& buffer, String senderNick, String message, String type, bool mention) {
    if (nickColors.find(senderNick) == nickColors.end())
        nickColors[senderNick] = generateRandomColor();

    String formattedMessage;
    if (type == "join") {
        formattedMessage = "JOIN " + senderNick + " has joined " + buffer.channel;
    } else if (type == "part") {
        formattedMessage = "PART " + senderNick + " has EMO-QUIT " + buffer.channel;
    } else if (type == "quit") {
        formattedMessage = "QUIT " + senderNick;
    } else if (type == "nick") {
        int arrowPos = message.indexOf(" -> ");
        String oldNick = senderNick;
        String newNick = message.substring(arrowPos + 4);
        if (nickColors.find(newNick) == nickColors.end()) {
            nickColors[newNick] = generateRandomColor();
        }
        formattedMessage = "NICK " + oldNick + " -> " + newNick;
    } else if (type == "kick") {
        formattedMessage = "KICK " + senderNick + message;
    } else if (type == "action") {
        formattedMessage = "* " + senderNick + " " + message;
    } else if (type == "mode") {
        formattedMessage = "MODE " + message;
    } else {
        formattedMessage = senderNick + ": " + message;
    }

    int linesRequired = calculateLinesRequired(formattedMessage);

    while (buffer.lines.size() + linesRequired > MAX_LINES) {
        buffer.lines.erase(buffer.lines.begin());
        buffer.mentions.erase(buffer.mentions.begin());
    }

    buffer.lines.push_back(formattedMessage);
    buffer.mentions.push_back(mention);
}

void addLine(String senderNick, String message, String type, bool mention) {
    Buffer& currentBuffer = buffers[currentBufferIndex];
    addLineToBuffer(currentBuffer, senderNick, message, type, mention);
    displayLines();
}

Buffer& getBufferByChannel(String channel) {
    for (auto& buffer : buffers) {
        if (buffer.channel == channel) {
            return buffer;
        }
    }
    Buffer newBuffer;
    newBuffer.channel = channel;
    buffers.push_back(newBuffer);
    return buffers.back();
}
