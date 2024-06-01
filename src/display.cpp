// src/display.cpp

#include "display.h"
#include "boot.h"

Pangodream_18650_CL BL(BOARD_BAT_ADC, CONV_FACTOR, READS);

/*TFT_eSPI tft = TFT_eSPI(SCREEN_WIDTH, SCREEN_HEIGHT);*/


void displayLines() {
    tft.fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT - INPUT_LINE_HEIGHT, TFT_BLACK);

    int cursorY = STATUS_BAR_HEIGHT;
    Buffer& currentBuffer = buffers[currentBufferIndex];
    int totalLinesHeight = 0;
    std::vector<int> lineHeights;

    for (const String& line : currentBuffer.lines) {
        int lineHeight = calculateLinesRequired(line) * CHAR_HEIGHT;
        lineHeights.push_back(lineHeight);
        totalLinesHeight += lineHeight;
    }

    while (totalLinesHeight > SCREEN_HEIGHT - STATUS_BAR_HEIGHT - INPUT_LINE_HEIGHT) {
        totalLinesHeight -= lineHeights.front();
        currentBuffer.lines.erase(currentBuffer.lines.begin());
        currentBuffer.mentions.erase(currentBuffer.mentions.begin());
        lineHeights.erase(lineHeights.begin());
    }

    for (size_t i = 0; i < currentBuffer.lines.size(); ++i) {
        const String& line = currentBuffer.lines[i];
        bool mention = currentBuffer.mentions[i];

        tft.setCursor(0, cursorY);

        if (line.startsWith("JOIN ")) {
            tft.setTextColor(TFT_GREEN);
            tft.print("JOIN ");
            int startIndex = 5;
            int endIndex = line.indexOf(" has joined ");
            String senderNick = line.substring(startIndex, endIndex);
            tft.setTextColor(nickColors[senderNick]);
            tft.print(senderNick);
            tft.setTextColor(TFT_WHITE);
            tft.print(" has joined ");
            tft.setTextColor(TFT_CYAN);
            tft.print(currentBuffer.channel);
            cursorY += CHAR_HEIGHT;
        } else if (line.startsWith("PART ")) {
            tft.setTextColor(TFT_RED);
            tft.print("PART ");
            int startIndex = 5;
            int endIndex = line.indexOf(" has EMO-QUIT ");
            String senderNick = line.substring(startIndex, endIndex);
            tft.setTextColor(nickColors[senderNick]);
            tft.print(senderNick);
            tft.setTextColor(TFT_WHITE);
            tft.print(" has EMO-QUIT ");
            tft.setTextColor(TFT_CYAN);
            tft.print(currentBuffer.channel);
            cursorY += CHAR_HEIGHT;
        } else if (line.startsWith("QUIT ")) {
            tft.setTextColor(TFT_RED);
            tft.print("QUIT ");
            String senderNick = line.substring(5);
            tft.setTextColor(nickColors[senderNick]);
            tft.print(senderNick);
            cursorY += CHAR_HEIGHT;
        } else if (line.startsWith("NICK ")) {
            tft.setTextColor(TFT_BLUE);
            tft.print("NICK ");
            int startIndex = 5;
            int endIndex = line.indexOf(" -> ");
            String oldNick = line.substring(startIndex, endIndex);
            String newNick = line.substring(endIndex + 4);
            tft.setTextColor(nickColors[oldNick]);
            tft.print(oldNick);
            tft.setTextColor(TFT_WHITE);
            tft.print(" -> ");
            tft.setTextColor(nickColors[newNick]);
            tft.print(newNick);
            cursorY += CHAR_HEIGHT;
        } else if (line.startsWith("KICK ")) {
            tft.setTextColor(TFT_RED);
            tft.print("KICK ");
            int startIndex = 5;
            int endIndex = line.indexOf(" by ");
            String kickedNick = line.substring(startIndex, endIndex);
            String kicker = line.substring(endIndex + 4);
            tft.setTextColor(nickColors[kickedNick]);
            tft.print(kickedNick);
            tft.setTextColor(TFT_WHITE);
            tft.print(" by ");
            tft.setTextColor(nickColors[kicker]);
            tft.print(kicker);
            cursorY += CHAR_HEIGHT;
        } else if (line.startsWith("MODE ")) {
            tft.setTextColor(TFT_YELLOW);
            tft.print("MODE ");
            String modeChange = line.substring(5);
            tft.setTextColor(TFT_WHITE);
            tft.print(modeChange);
            cursorY += CHAR_HEIGHT;
        } else if (line.startsWith("ERROR ")) {
            tft.setTextColor(TFT_RED);
            tft.print("ERROR ");
            String errorReason = line.substring(6);
            tft.setTextColor(TFT_DARKGREY);
            tft.print(errorReason);
            cursorY += CHAR_HEIGHT;
        } else if (line.startsWith("* ")) {
            tft.setTextColor(TFT_MAGENTA);
            tft.print("* ");
            int startIndex = 2;
            int endIndex = line.indexOf(' ', startIndex);
            String senderNick = line.substring(startIndex, endIndex);
            String actionMessage = line.substring(endIndex + 1);
            tft.setTextColor(nickColors[senderNick]);
            tft.print(senderNick + " ");
            tft.setTextColor(TFT_WHITE);
            tft.print(actionMessage);
            cursorY += CHAR_HEIGHT;
        } else {
            int colonIndex = line.indexOf(':');
            String senderNick = line.substring(0, colonIndex);
            String message = line.substring(colonIndex + 1);

            tft.setTextColor(nickColors[senderNick]);
            tft.print(senderNick);
            tft.setTextColor(TFT_WHITE);
            cursorY = renderFormattedMessage(":" + message, cursorY, CHAR_HEIGHT, mention);
        }
    }

    displayInputLine();
}

int renderFormattedMessage(String message, int cursorY, int lineHeight, bool highlightNick = false) {
    uint16_t fgColor = TFT_WHITE;
    uint16_t bgColor = TFT_BLACK;
    bool bold = false;
    bool underline = false;
    bool nickHighlighted = false; // Track if the nick has been highlighted

    for (unsigned int i = 0; i < message.length(); i++) {
        char c = message[i];
        if (c == '\x02') { // Bold
            bold = !bold;
            tft.setTextFont(bold ? 2 : 1);
        } else if (c == '\x1F') { // Underline
            underline = !underline;
        } else if (c == '\x03') { // Color
            fgColor = TFT_WHITE;
            bgColor = TFT_BLACK;

            if (i + 1 < message.length() && (isdigit(message[i + 1]) || message[i + 1] == ',')) {
                int colorCode = -1;
                if (isdigit(message[i + 1])) {
                    colorCode = message[++i] - '0';
                    if (i + 1 < message.length() && isdigit(message[i + 1]))
                        colorCode = colorCode * 10 + (message[++i] - '0');
                }

                if (colorCode != -1)
                    fgColor = getColorFromCode(colorCode);

                if (i + 1 < message.length() && message[i + 1] == ',') {
                    i++;
                    int bgColorCode = -1;
                    if (isdigit(message[i + 1])) {
                        bgColorCode = message[++i] - '0';
                        if (i + 1 < message.length() && isdigit(message[i + 1]))
                            bgColorCode = bgColorCode * 10 + (message[++i] - '0');
                    }

                    if (bgColorCode != -1)
                        bgColor = getColorFromCode(bgColorCode);
                }

                tft.setTextColor(fgColor, bgColor);
            }
        } else if (c == '\x0F') { // Reset
            fgColor = TFT_WHITE;
            bgColor = TFT_BLACK;
            bold = false;
            underline = false;
            tft.setTextColor(fgColor, bgColor);
            tft.setTextFont(1);
        } else {
            if (highlightNick && !nickHighlighted && message.substring(i).startsWith(nick)) {
                tft.setTextColor(TFT_YELLOW, bgColor); // Set both foreground and background color
                for (char nc : nick) {
                    tft.print(nc);
                    i++;
                }
                i--; // Adjust for the loop increment
                tft.setTextColor(TFT_WHITE, bgColor); // Reset to white foreground with current background
                nickHighlighted = true;
            } else {
                if (tft.getCursorX() + tft.textWidth(String(c)) > SCREEN_WIDTH) {
                    cursorY += lineHeight;
                    tft.setCursor(0, cursorY);
                }
                if (c == ' ') {
                    // Draw background for spaces
                    int spaceWidth = tft.textWidth(" ");
                    tft.fillRect(tft.getCursorX(), tft.getCursorY(), spaceWidth, lineHeight, bgColor);
                    tft.setCursor(tft.getCursorX() + spaceWidth, tft.getCursorY());
                } else {
                    // Ensure that background color is applied to characters
                    tft.fillRect(tft.getCursorX(), tft.getCursorY(), tft.textWidth(String(c)), lineHeight, bgColor);
                    tft.setTextColor(fgColor, bgColor);
                    tft.print(c);
                }
            }
        }
    }

    // Ensure trailing spaces are displayed with background color
    if (message.endsWith(" ")) {
        int trailingSpaces = 0;
        for (int i = message.length() - 1; i >= 0 && message[i] == ' '; i--) {
            trailingSpaces++;
        }
        for (int i = 0; i < trailingSpaces; i++) {
            int spaceWidth = tft.textWidth(" ");
            tft.fillRect(tft.getCursorX(), tft.getCursorY(), spaceWidth, lineHeight, bgColor);
            tft.setCursor(tft.getCursorX() + spaceWidth, tft.getCursorY());
        }
    }

    cursorY += lineHeight; // Add line height after printing the message
    return cursorY; // Return the new cursor Y position for the next line
}


void displayCenteredText(String text) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(text, SCREEN_WIDTH / 2, (SCREEN_HEIGHT + STATUS_BAR_HEIGHT) / 2);
}

void displayInputLine() {
    tft.fillRect(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT, SCREEN_WIDTH, INPUT_LINE_HEIGHT, TFT_BLACK);
    tft.setCursor(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);

    int inputWidth = SCREEN_WIDTH - tft.textWidth("> ");
    String displayInput = inputBuffer;
    int displayWidth = tft.textWidth(displayInput);

    while (displayWidth > inputWidth) {
        displayInput = displayInput.substring(1);
        displayWidth = tft.textWidth(displayInput);
    }

    tft.print("> " + displayInput);
}

void displayXBM() {
    tft.fillScreen(TFT_BLACK);
    int x = (SCREEN_WIDTH - logo_width) / 2;
    int y = (SCREEN_HEIGHT - logo_height) / 2;
    tft.drawXBitmap(x, y, logo_bits, logo_width, logo_height, TFT_GREEN);
}

void updateStatusBar() {
    debugPrint("Updating status bar...");
    updateTimeOnStatusBar();
    updateWiFiOnStatusBar();
    updateBatteryOnStatusBar();
}

void updateTimeOnStatusBar() {
    uint16_t darkerGrey = tft.color565(25, 25, 25);
    tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, darkerGrey);

    struct tm timeinfo;
    char timeStr[20];
    if (!getLocalTime(&timeinfo)) {
        sprintf(timeStr, "12:00 AM %s", buffers[currentBufferIndex].channel.c_str());
    } else {
        int hour = timeinfo.tm_hour;
        char ampm[] = "AM";
        if (hour == 0) {
            hour = 12;
        } else if (hour >= 12) {
            if (hour > 12) {
                hour -= 12;
            }
            strcpy(ampm, "PM");
        }
        sprintf(timeStr, "%02d:%02d %s %s", hour, timeinfo.tm_min, ampm, buffers[currentBufferIndex].channel.c_str());
    }
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, darkerGrey);
    tft.drawString(timeStr, 0, STATUS_BAR_HEIGHT / 2);
}

void updateWiFiOnStatusBar() {
    uint16_t darkerGrey = tft.color565(25, 25, 25);
    char wifiStr[15];
    int wifiSignal = 0;
    if (WiFi.status() != WL_CONNECTED) {
        sprintf(wifiStr, "WiFi: N/A");
        tft.setTextColor(TFT_PINK, darkerGrey);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(wifiStr, SCREEN_WIDTH - 100, STATUS_BAR_HEIGHT / 2);
    } else {
        int32_t rssi = WiFi.RSSI();
        if (rssi > -50) wifiSignal = 100;
        else if (rssi > -60) wifiSignal = 80;
        else if (rssi > -70) wifiSignal = 60;
        else if (rssi > -80) wifiSignal = 40;
        else if (rssi > -90) wifiSignal = 20;
        else wifiSignal = 0;

        sprintf(wifiStr, "WiFi: %d%%", wifiSignal);
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(TFT_PINK, darkerGrey);
        tft.drawString("WiFi:", SCREEN_WIDTH - 120, STATUS_BAR_HEIGHT / 2);
        tft.setTextColor(getColorFromPercentage(wifiSignal), darkerGrey);
        tft.drawString(wifiStr + 6, SCREEN_WIDTH - 100, STATUS_BAR_HEIGHT / 2);
    }
}

void updateBatteryOnStatusBar() {
    uint16_t darkerGrey = tft.color565(25, 25, 25);
    int batteryLevel = BL.getBatteryChargeLevel();
    char batteryStr[15];
    sprintf(batteryStr, "Batt: %d%%", batteryLevel);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_CYAN, darkerGrey);
    tft.drawString("Batt:", SCREEN_WIDTH - 40, STATUS_BAR_HEIGHT / 2);
    tft.setTextColor(getColorFromPercentage(batteryLevel), darkerGrey);
    tft.drawString(batteryStr + 5, SCREEN_WIDTH - 5, STATUS_BAR_HEIGHT / 2);
}

void updateTopicOnStatusBar() {
    if (buffers.empty() || buffers[currentBufferIndex].topic.isEmpty()) return;

    tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buffers[currentBufferIndex].topic, SCREEN_WIDTH / 2, STATUS_BAR_HEIGHT / 2);
}

void turnOffScreen() {
    debugPrint("Screen turned off");
    tft.writecommand(TFT_DISPOFF);
    tft.writecommand(TFT_SLPIN);
    digitalWrite(TFT_BL, LOW);
    screenOn = false;
}

void turnOnScreen() {
    debugPrint("Screen turned on");
    digitalWrite(TFT_BL, HIGH);
    tft.writecommand(TFT_SLPOUT);
    tft.writecommand(TFT_DISPON);
    screenOn = true;
}

void displayDeviceInfo() {
    tft.fillScreen(TFT_BLACK);
    printDeviceInfo();
}
