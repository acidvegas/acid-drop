// src/buffer.h

#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H

#include <vector>
#include <String.h>
#include "utilities.h"


struct Buffer {
    std::vector<String> lines;
    std::vector<bool> mentions;
    String channel;
    String topic;
};

extern std::vector<Buffer> buffers;
extern int currentBufferIndex;

void addLineToBuffer(Buffer& buffer, String senderNick, String message, String type, bool mention);
void addLine(String senderNick, String message, String type, bool mention = false);
Buffer& getBufferByChannel(String channel);

#endif
