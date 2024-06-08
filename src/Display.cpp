#include "Display.h"
#include "Storage.h"
#include "Utilities.h"
#include "Speaker.h"
#include "pins.h"
#include "bootScreen.h"
#include "IRC.h"

// External variables definitions
bool infoScreen = false;
bool configScreen = false;
bool screenOn = true;
const char* channel = "#comms";
unsigned long infoScreenStartTime = 0;
unsigned long configScreenStartTime = 0;
unsigned long lastStatusUpdateTime = 0;
unsigned long lastActivityTime = 0;
String inputBuffer = "";

std::vector<String> lines;
std::vector<bool> mentions;
std::map<String, uint32_t> nickColors;

TFT_eSPI tft = TFT_eSPI();

void addLine(String senderNick, String message, String type, bool mention, uint16_t errorColor, uint16_t reasonColor) {
    if (type != "error" && nickColors.find(senderNick) == nickColors.end())
        nickColors[senderNick] = generateRandomColor();

    String formattedMessage;
    if (type == "join") {
        formattedMessage = "JOIN " + senderNick + " has joined " + String(channel);
    } else if (type == "part") {
        formattedMessage = "PART " + senderNick + " has EMO-QUIT " + String(channel);
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
    } else if (type == "mode") {
        formattedMessage = "MODE " + message;
        tft.setTextColor(TFT_BLUE);
    } else if (type == "action") {
        formattedMessage = "* " + senderNick + " " + message;
    } else if (type == "error") {
        formattedMessage = "ERROR " + message;
        senderNick = "ERROR";
    } else {
        formattedMessage = senderNick + ": " + message;
    }

    int linesRequired = calculateLinesRequired(formattedMessage);

    while (lines.size() + linesRequired > MAX_LINES) {
        lines.erase(lines.begin());
        mentions.erase(mentions.begin());
    }

    if (type == "error") {
        lines.push_back("ERROR " + message);
        mentions.push_back(false);
    } else {
        lines.push_back(formattedMessage);
        mentions.push_back(mention);
    }

    displayLines();
}

int calculateLinesRequired(String message) {
    int linesRequired = 1;
    int lineWidth = 0;

    for (unsigned int i = 0; i < message.length(); i++) {
        char c = message[i];
        if (c == '\x03') {
            if (i + 1 < message.length() && isdigit(message[i + 1])) {
                i++;
                if (i + 1 < message.length() && isdigit(message[i + 1]))
                    i++;
            }
            if (i + 1 < message.length() && message[i + 1] == ',' && isdigit(message[i + 2])) {
                i += 2;
                if (i + 1 < message.length() && isdigit(message[i + 1]))
                    i++;
            }
        } else if (c != '\x02' && c != '\x0F' && c != '\x1F') {
            lineWidth += tft.textWidth(String(c));
            if (lineWidth > SCREEN_WIDTH) {
                linesRequired++;
                lineWidth = tft.textWidth(String(c));
            }
        }
    }

    return linesRequired;
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

    String displayInput = inputBuffer;
    int displayWidth = tft.textWidth(displayInput);
    int inputWidth = SCREEN_WIDTH - tft.textWidth("> ");
    
    while (displayWidth > inputWidth) {
        displayInput = displayInput.substring(1);
        displayWidth = tft.textWidth(displayInput);
    }

    if (inputBuffer.length() >= 510)
        tft.setTextColor(TFT_RED);

    tft.print("> " + displayInput);
    tft.setTextColor(TFT_WHITE);
}

void displayLines() {
    tft.fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT - INPUT_LINE_HEIGHT, TFT_BLACK);

    int cursorY = STATUS_BAR_HEIGHT;
    int totalLinesHeight = 0;
    std::vector<int> lineHeights;

    for (const String& line : lines) {
        int lineHeight = calculateLinesRequired(line) * CHAR_HEIGHT;
        lineHeights.push_back(lineHeight);
        totalLinesHeight += lineHeight;
    }

    while (totalLinesHeight > SCREEN_HEIGHT - STATUS_BAR_HEIGHT - INPUT_LINE_HEIGHT) {
        totalLinesHeight -= lineHeights.front();
        lines.erase(lines.begin());
        mentions.erase(mentions.begin());
        lineHeights.erase(lineHeights.begin());
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        const String& line = lines[i];
        bool mention = mentions[i];

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
            tft.print(channel);
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
            tft.print(channel);
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
            tft.setTextColor(TFT_BLUE);
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
            tft.print(senderNick);
            tft.setTextColor(TFT_MAGENTA);
            tft.print(" " + actionMessage);
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

void displayXBM() {
    tft.fillScreen(TFT_BLACK);

    int x = (SCREEN_WIDTH - logo_width) / 2;
    int y = (SCREEN_HEIGHT - logo_height) / 2;

    tft.drawXBitmap(x, y, logo_bits, logo_width, logo_height, TFT_GREEN);

    unsigned long startTime = millis();
    bool wipeInitiated = false;

    while (millis() - startTime < 3000) {
        if (getKeyboardInput() == 'w') {
            wipeNVS();
            tft.fillScreen(TFT_BLACK);
            displayCenteredText("NVS WIPED");
            delay(2000);
            wipeInitiated = true;
            break;
        }
    }
}

uint32_t generateRandomColor() {
    return tft.color565(random(0, 255), random(0, 255), random(0, 255));
}

uint16_t getColorFromCode(int colorCode) {
    switch (colorCode) {
        case 0: return TFT_WHITE;
        case 1: return TFT_BLACK;
        case 2: return tft.color565(0, 0, 128); // Dark Blue (Navy)
        case 3: return TFT_GREEN;
        case 4: return TFT_RED;
        case 5: return tft.color565(128, 0, 0); // Brown (Maroon)
        case 6: return tft.color565(128, 0, 128); // Purple
        case 7: return tft.color565(255, 165, 0); // Orange
        case 8: return TFT_YELLOW;
        case 9: return tft.color565(144, 238, 144); // Light Green
        case 10: return tft.color565(0, 255, 255); // Cyan (Light Blue)
        case 11: return tft.color565(224, 255, 255); // Light Cyan (Aqua)
        case 12: return TFT_BLUE;
        case 13: return tft.color565(255, 192, 203); // Pink (Light Purple)
        case 14: return tft.color565(128, 128, 128); // Grey
        case 15: return tft.color565(211, 211, 211); // Light Grey
        case 16: return 0x4000;
        case 17: return 0x4100;
        case 18: return 0x4220;
        case 19: return 0x3220;
        case 20: return 0x0220;
        case 21: return 0x0225;
        case 22: return 0x0228;
        case 23: return 0x0128;
        case 24: return 0x0008;
        case 25: return 0x2808;
        case 26: return 0x4008;
        case 27: return 0x4005;
        case 28: return 0x7000;
        case 29: return 0x71C0;
        case 30: return 0x73A0;
        case 31: return 0x53A0;
        case 32: return 0x03A0;
        case 33: return 0x03A9;
        case 34: return 0x03AE;
        case 35: return 0x020E;
        case 36: return 0x000E;
        case 37: return 0x480E;
        case 38: return 0x700E;
        case 39: return 0x7008;
        case 40: return 0xB000;
        case 41: return 0xB300;
        case 42: return 0xB5A0;
        case 43: return 0x7DA0;
        case 44: return 0x05A0;
        case 45: return 0x05AE;
        case 46: return 0x05B6;
        case 47: return 0x0316;
        case 48: return 0x0016;
        case 49: return 0x7016;
        case 50: return 0xB016;
        case 51: return 0xB00D;
        case 52: return 0xF800;
        case 53: return 0xFC60;
        case 54: return 0xFFE0;
        case 55: return 0xB7E0;
        case 56: return 0x07E0;
        case 57: return 0x07F4;
        case 58: return 0x07FF;
        case 59: return 0x047F;
        case 60: return 0x001F;
        case 61: return 0xA01F;
        case 62: return 0xF81F;
        case 63: return 0xF813;
        case 64: return 0xFACB;
        case 65: return 0xFDAB;
        case 66: return 0xFFEE;
        case 67: return 0xCFEC;
        case 68: return 0x6FED;
        case 69: return 0x67F9;
        case 70: return 0x6FFF;
        case 71: return 0x5DBF;
        case 72: return 0x5ADF;
        case 73: return 0xC2DF;
        case 74: return 0xFB3F;
        case 75: return 0xFAD7;
        case 76: return 0xFCF3;
        case 77: return 0xFE93;
        case 78: return 0xFFF3;
        case 79: return 0xE7F3;
        case 80: return 0x9FF3;
        case 81: return 0x9FFB;
        case 82: return 0x9FFF;
        case 83: return 0x9E9F;
        case 84: return 0x9CFF;
        case 85: return 0xDCFF;
        case 86: return 0xFCFF;
        case 87: return 0xFCBA;
        case 88: return 0x0000;
        case 89: return 0x1082;
        case 90: return 0x2945;
        case 91: return 0x31A6;
        case 92: return 0x4A69;
        case 93: return 0x632C;
        case 94: return 0x8410;
        case 95: return 0x9CF3;
        case 96: return 0xBDF7;
        case 97: return 0xE71C;
        case 98: return 0xFFFF;
        default: return TFT_WHITE;
    }
}

uint16_t getColorFromPercentage(int percentage) {
    if      (percentage > 75) return TFT_GREEN;
    else if (percentage > 50) return TFT_YELLOW;
    else if (percentage > 25) return TFT_ORANGE;
    else                      return TFT_RED;
}

void handleKeyboardInput(char key) {
    lastActivityTime = millis();

    if (!screenOn) {
        turnOnScreen();
        screenOn = true;
        return;
    }

    static bool altPressed = false;

    if (key == '\n' || key == '\r') {
        if (inputBuffer.startsWith("/nick ")) {
            String newNick = inputBuffer.substring(6);
            sendIRC("NICK " + newNick);
            inputBuffer = "";
        } else if (inputBuffer.startsWith("/config")) {
            configScreen = true;
            configScreenStartTime = millis();
            inputBuffer = "";
        } else if (inputBuffer.startsWith("/info")) {
            infoScreen = true;
            infoScreenStartTime = millis();
            tft.fillScreen(TFT_BLACK);
            printDeviceInfo();
            inputBuffer = "";
        } else if (inputBuffer.startsWith("/raw ")) {
            String rawCommand = inputBuffer.substring(5);
            sendIRC(rawCommand);
        } else if (inputBuffer.startsWith("/me ")) {
            String actionMessage = inputBuffer.substring(4);
            sendIRC("PRIVMSG " + String(channel) + " :\001ACTION " + actionMessage + "\001");
            addLine(irc_nickname, actionMessage, "action");
            inputBuffer = "";
        } else {
            sendIRC("PRIVMSG " + String(channel) + " :" + inputBuffer);
            addLine(irc_nickname, inputBuffer, "message");
        }
        inputBuffer = "";
        displayInputLine();
    } else if (key == '\b') {
        if (inputBuffer.length() > 0) {
            inputBuffer.remove(inputBuffer.length() - 1);
            displayInputLine();
        }
    } else {
        inputBuffer += key;
        displayInputLine();
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
            if (target == String(channel)) {
                int colonPos = line.indexOf(':', thirdSpace);
                String message = line.substring(colonPos + 1);
                String senderNick = line.substring(1, line.indexOf('!'));
                bool mention = message.indexOf(irc_nickname) != -1;

                if (mention)
                    playNotificationSound();

                if (message.startsWith(String("\x01") + "ACTION ") && message.endsWith("\x01")) {
                    String actionMessage = message.substring(8, message.length() - 1);
                    addLine(senderNick, actionMessage, "action");
                } else {
                    addLine(senderNick, message, "message", mention);
                }
            }
        } else if (command == "JOIN" && line.indexOf(channel) != -1) {
            String senderNick = line.substring(1, line.indexOf('!'));
            addLine(senderNick, " has joined " + String(channel), "join");
        } else if (command == "PART" && line.indexOf(channel) != -1) {
            String senderNick = line.substring(1, line.indexOf('!'));
            addLine(senderNick, " has EMO-QUIT " + String(channel), "part");
        } else if (command == "QUIT") {
            String senderNick = line.substring(1, line.indexOf('!'));
            addLine(senderNick, "", "quit");
        } else if (command == "NICK") {
            String prefix = line.startsWith(":") ? line.substring(1, firstSpace) : "";
            String newNick = line.substring(line.lastIndexOf(':') + 1);

            if (prefix.indexOf('!') == -1) {
                addLine(irc_nickname, " -> " + newNick, "nick");
                irc_nickname = newNick;
            } else {
                String oldNick = prefix.substring(0, prefix.indexOf('!'));
                addLine(oldNick, " -> " + newNick, "nick");
                if (oldNick == irc_nickname) {
                    irc_nickname = newNick;
                }
            }
        } else if (command == "KICK") {
            int thirdSpace = line.indexOf(' ', secondSpace + 1);
            int fourthSpace = line.indexOf(' ', thirdSpace + 1);
            String kicker = line.substring(1, line.indexOf('!'));
            String kicked = line.substring(thirdSpace + 1, fourthSpace);
            addLine(kicked, " by " + kicker, "kick");
        } else if (command == "MODE") {
            String modeChange = line.substring(secondSpace + 1);
            addLine("", modeChange, "mode");
        } else if (command == "432") {
            addLine("ERROR", "ERR_ERRONEUSNICKNAME", "error", TFT_RED, TFT_DARKGREY);
        } else if (command == "433") {
            addLine("ERROR", "ERR_NICKNAMEINUSE", "error", TFT_RED, TFT_DARKGREY);
            irc_nickname = "ACID_" + String(random(1000, 9999));
            sendIRC("NICK " + irc_nickname);
        }
    }
}

int renderFormattedMessage(String message, int cursorY, int lineHeight, bool highlightNick) {
    uint16_t fgColor = TFT_WHITE;
    uint16_t bgColor = TFT_BLACK;
    bool bold = false;
    bool underline = false;
    bool nickHighlighted = false;

    int nickPos = -1;
    if (highlightNick) {
        nickPos = message.indexOf(irc_nickname);
    }

    for (unsigned int i = 0; i < message.length(); i++) {
        char c = message[i];
        if (c == '\x02') {
            bold = !bold;
        } else if (c == '\x1F') {
            underline = !underline;
        } else if (c == '\x03') {
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
        } else if (c == '\x0F') {
            fgColor = TFT_WHITE;
            bgColor = TFT_BLACK;
            bold = false;
            underline = false;
            tft.setTextColor(fgColor, bgColor);
            tft.setTextFont(1);
        } else {
            if (highlightNick && !nickHighlighted && nickPos != -1 && i == nickPos) {
                tft.setTextColor(TFT_YELLOW, bgColor);
                for (char nc : irc_nickname) {
                    tft.print(nc);
                    i++;
                }
                i--;
                tft.setTextColor(TFT_WHITE, bgColor);
                nickHighlighted = true;
            } else {
                if (tft.getCursorX() + tft.textWidth(String(c)) > SCREEN_WIDTH) {
                    cursorY += lineHeight;
                    tft.setCursor(0, cursorY);
                }
                if (c == ' ') {
                    int spaceWidth = tft.textWidth(" ");
                    tft.fillRect(tft.getCursorX(), tft.getCursorY(), spaceWidth, lineHeight, bgColor);
                    tft.setCursor(tft.getCursorX() + spaceWidth, tft.getCursorY());
                } else {
                    tft.fillRect(tft.getCursorX(), tft.getCursorY(), tft.textWidth(String(c)), lineHeight, bgColor);
                    tft.setTextColor(fgColor, bgColor);
                    tft.print(c);
                }
            }
        }
    }

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

    cursorY += lineHeight;
    return cursorY;
}

void turnOffScreen() {
    Serial.println("Screen turned off");
    tft.writecommand(TFT_DISPOFF);
    tft.writecommand(TFT_SLPIN);
    digitalWrite(TFT_BL, LOW);
    screenOn = false;
}

void turnOnScreen() {
    Serial.println("Screen turned on");
    digitalWrite(TFT_BL, HIGH);
    tft.writecommand(TFT_SLPOUT);
    tft.writecommand(TFT_DISPON);
    screenOn = true;
}

void updateStatusBar() {
    Serial.println("Updating status bar...");
    uint16_t darkerGrey = tft.color565(25, 25, 25);
    tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, darkerGrey);

    struct tm timeinfo;
    char timeStr[9];
    if (!getLocalTime(&timeinfo)) {
        sprintf(timeStr, "12:00 AM");
    } else {
        int hour = timeinfo.tm_hour;
        char ampm[] = "AM";
        if (hour == 0) {
            hour = 12;
        } else if (hour >= 12) {
            if (hour > 12)
                hour -= 12;
            strcpy(ampm, "PM");
        }
        sprintf(timeStr, "%02d:%02d %s", hour, timeinfo.tm_min, ampm);
    }
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, darkerGrey);
    tft.drawString(timeStr, 0, STATUS_BAR_HEIGHT / 2);

    char wifiStr[15];
    int wifiSignal = 0;
    if (WiFi.status() != WL_CONNECTED) {
        sprintf(wifiStr, "WiFi: N/A");
        tft.setTextColor(TFT_PINK, darkerGrey);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(wifiStr, SCREEN_WIDTH - 100, STATUS_BAR_HEIGHT / 2);
    } else {
        int32_t rssi = WiFi.RSSI();
        if      (rssi > -50) wifiSignal = 100;
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

    int batteryLevel = BL.getBatteryChargeLevel();
    char batteryStr[15];
    sprintf(batteryStr, "Batt: %d%%", batteryLevel);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_CYAN, darkerGrey);
    tft.drawString("Batt:", SCREEN_WIDTH - 40, STATUS_BAR_HEIGHT / 2);
    tft.setTextColor(getColorFromPercentage(batteryLevel), darkerGrey);
    tft.drawString(batteryStr + 5, SCREEN_WIDTH - 5, STATUS_BAR_HEIGHT / 2);
}
