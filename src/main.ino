#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>

#include <map>
#include <vector>

#include "boot_screen.h"
#include "pins.h"

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define CHAR_HEIGHT   10
#define LINE_SPACING  0

#define INPUT_LINE_HEIGHT (CHAR_HEIGHT + LINE_SPACING)
#define MAX_LINES ((SCREEN_HEIGHT - INPUT_LINE_HEIGHT) / (CHAR_HEIGHT + LINE_SPACING))


TFT_eSPI tft = TFT_eSPI();
WiFiClientSecure client;

std::map<String, uint32_t> nickColors;
std::vector<String> lines;
String inputBuffer = "";


// WiFi credentials
const char* ssid = "CHANGEME";
const char* password = "CHANGEME";

// IRC connection
const char* server = "irc.supernets.org";
const int port = 6697;
bool useSSL = true;
const char* channel = "#comms";

// IRC identity
const char* nick = "ACID_DROP";
const char* user = "tdeck";
const char* realname = "ACID DROP Firmware 1.0.0"; // Need to eventually set this up to use a varaible

unsigned long joinChannelTime = 0;
bool readyToJoinChannel = false;


void setup() {
    Serial.begin(115200);
    Serial.println("Booting device...");

    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    tft.begin();
    tft.setRotation(1);
    tft.invertDisplay(1);

    displayXBM();
    delay(3000);
    displayCenteredText("CONNECTING TO WIFI");
    delay(2000);
    connectToWiFi();
    displayCenteredText("CONNECTED TO WIFI");
    delay(2000);
    displayCenteredText("CONNECTING TO " + String(server));
    delay(2000);
    if (connectToIRC()) {
        displayCenteredText("CONNECTED TO " + String(server));
        sendIRC("NICK " + String(nick));
        sendIRC("USER " + String(user) + " 0 * :" + String(realname));
    } else {
        displayCenteredText("CONNECTION FAILED"); // Dead point, need to build a reconnect loop possibly
    }
}


void loop() {
    if (client.connected()) {
        handleIRC();
    } else {
        displayCenteredText("RECONNECTING");
        if (!connectToIRC()) {
            displayCenteredText("RECONNECT FAILED");
        } else {
            displayCenteredText("RECONNECTED");
            sendIRC("NICK " + String(nick));
            sendIRC("USER " + String(user) + " 0 * :" + String(realname));
        }
        delay(1000);
    }

    if (readyToJoinChannel && millis() >= joinChannelTime) {
        tft.fillScreen(TFT_BLACK);
        sendIRC("JOIN " + String(channel));
        readyToJoinChannel = false;
    }

    char incoming = getKeyboardInput();

    if (incoming != 0)
        handleKeyboardInput(incoming);
}


bool connectToIRC() {
    if (useSSL) {
        client.setInsecure();
        return client.connect(server, port);
    } else {
        WiFiClient nonSecureClient;
        return nonSecureClient.connect(server, port);
    }
}


void connectToWiFi() {
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
        delay(500);

    //tft.println("\nWiFi Connected!");
    //tft.printf("IP: %s\n", WiFi.localIP().toString().c_str());
}


void sendIRC(String command) {
    if (client.println(command))
        Serial.println(">>> " + command);
    else
        Serial.println("Failed to send: " + command);
}


void handleIRC() {
    while (client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        Serial.println(line);

        int firstSpace = line.indexOf(' ');
        int secondSpace = line.indexOf(' ', firstSpace + 1);

        // Ensure the spaces are found and prevent substring from going out of bounds
        if (firstSpace != -1 && secondSpace != -1) {
            String prefix = line.substring(0, firstSpace);
            String command = line.substring(firstSpace + 1, secondSpace);

            // RPL_WELCOME
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
            if (target == String(channel)) {
                int colonPos = line.indexOf(':', thirdSpace);
                String message = line.substring(colonPos + 1);
                String nick = line.substring(1, line.indexOf('!'));
                addLine(nick, message, "message");
            }
        } else if (command == "JOIN" && line.indexOf(channel) != -1) {
            String nick = line.substring(1, line.indexOf('!'));
            addLine(nick, " has joined " + String(channel), "join");
        } else if (command == "PART" && line.indexOf(channel) != -1) {
            String nick = line.substring(1, line.indexOf('!'));
            addLine(nick, " has EMO-QUIT " + String(channel), "part");
        } else if (command == "QUIT" && line.indexOf(channel) != -1) {
            String nick = line.substring(1, line.indexOf('!'));
            addLine(nick, "", "quit");
        } else if (command == "NICK") {
            String oldNick = line.substring(1, line.indexOf('!'));
            String newNick = line.substring(line.lastIndexOf(':') + 1);
            addLine(oldNick, " -> " + newNick, "nick");
        } else if (command == "KICK") {
            int thirdSpace = line.indexOf(' ', secondSpace + 1);
            int fourthSpace = line.indexOf(' ', thirdSpace + 1);
            String kicker = line.substring(1, line.indexOf('!'));
            String kicked = line.substring(thirdSpace + 1, fourthSpace);
            addLine(kicked, " by " + kicker, "kick");
        } else if (command == "MODE") {
            String modeChange = line.substring(secondSpace + 1);
            addLine("", modeChange, "mode");
        }
    }
}


void handleKeyboardInput(char key) {
    if (key == '\n' || key == '\r') { // Enter
        sendIRC("PRIVMSG " + String(channel) + " :" + inputBuffer);
        addLine(nick, inputBuffer, "message");
        inputBuffer = "";
        displayInputLine();
    } else if (key == '\b') { // Backspace
        if (inputBuffer.length() > 0) {
            inputBuffer.remove(inputBuffer.length() - 1);
            displayInputLine();
        }
    } else {
        inputBuffer += key;
        displayInputLine();
    }
}


char getKeyboardInput() {
    char incoming = 0;
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    if (Wire.available()) {
        incoming = Wire.read();
        if (incoming != (char)0x00) {
            Serial.print("Key: ");
            Serial.println(incoming);
            return incoming;
        }
    }
    return 0;
}


void displayXBM() {
    tft.fillScreen(TFT_BLACK);
    int x = (SCREEN_WIDTH - logo_width) / 2;
    int y = (SCREEN_HEIGHT - logo_height) / 2;
    tft.drawXBitmap(x, y, logo_bits, logo_width, logo_height, TFT_GREEN);
}


void displayInputLine() {
    tft.fillRect(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT, SCREEN_WIDTH, INPUT_LINE_HEIGHT, TFT_BLACK); // Clear the input line area
    tft.setCursor(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT);
    tft.print("> ");
    tft.print(inputBuffer);
}


uint32_t generateRandomColor() {
    return tft.color565(random(0, 255), random(0, 255), random(0, 255));
}


void addLine(String nick, String message, String type) {
    if (nickColors.find(nick) == nickColors.end())
        nickColors[nick] = generateRandomColor();

    String formattedMessage;
    if (type == "join") {
        formattedMessage = "JOIN " + nick + " has joined " + String(channel);
    } else if (type == "part") {
        formattedMessage = "PART " + nick + " has EMO-QUIT " + String(channel);
    } else if (type == "quit") {
        formattedMessage = "QUIT " + nick;
    } else if (type == "nick") {
        int arrowPos = message.indexOf(" -> ");
        String oldNick = nick;
        String newNick = message.substring(arrowPos + 4);
        if (nickColors.find(newNick) == nickColors.end()) {
            nickColors[newNick] = generateRandomColor();
        }
        formattedMessage = "NICK " + oldNick + " -> " + newNick;
    } else if (type == "kick") {
        formattedMessage = "KICK " + nick + message;
    } else if (type == "mode") {
        formattedMessage = "MODE " + message;
    } else {
        formattedMessage = nick + ": " + message;
    }

    int linesRequired = calculateLinesRequired(formattedMessage);

    while (lines.size() + linesRequired > MAX_LINES)
        lines.erase(lines.begin());

    lines.push_back(formattedMessage);

    displayLines();
}


void displayLines() {
    tft.fillScreen(TFT_BLACK);
    int cursorY = 0;
    for (const String& line : lines) {
        tft.setCursor(0, cursorY);

        if (line.startsWith("JOIN ")) {
            tft.setTextColor(TFT_GREEN);
            tft.print("JOIN ");
            int startIndex = 5;
            int endIndex = line.indexOf(" has joined ");
            String nick = line.substring(startIndex, endIndex);
            tft.setTextColor(nickColors[nick]);
            tft.print(nick);
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
            String nick = line.substring(startIndex, endIndex);
            tft.setTextColor(nickColors[nick]);
            tft.print(nick);
            tft.setTextColor(TFT_WHITE);
            tft.print(" has EMO-QUIT ");
            tft.setTextColor(TFT_CYAN);
            tft.print(channel);
            cursorY += CHAR_HEIGHT;
        } else if (line.startsWith("QUIT ")) {
            tft.setTextColor(TFT_RED);
            tft.print("QUIT ");
            String nick = line.substring(5);
            tft.setTextColor(nickColors[nick]);
            tft.print(nick);
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
        } else {
            int colonPos = line.indexOf(':');
            String nick = line.substring(0, colonPos);
            String message = line.substring(colonPos + 2);
            tft.setTextColor(nickColors[nick]);
            tft.print(nick + ": ");
            tft.setTextColor(TFT_WHITE);

            cursorY = renderFormattedMessage(message, cursorY, CHAR_HEIGHT);
        }
    }

    displayInputLine();
}


int renderFormattedMessage(String message, int cursorY, int lineHeight) {
    uint16_t fgColor = TFT_WHITE;
    uint16_t bgColor = TFT_BLACK;
    bool bold = false;
    bool underline = false;

    for (unsigned int i = 0; i < message.length(); i++) {
        char c = message[i];
        if (c == '\x02') { // Bold
            bold = !bold;
            tft.setTextFont(bold ? 2 : 1);
        } else if (c == '\x1F') { // Underline
            underline = !underline;
            // need to add this still
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
            if (tft.getCursorX() + tft.textWidth(String(c)) > SCREEN_WIDTH) {
                cursorY += lineHeight;
                tft.setCursor(0, cursorY);
            }
            tft.print(c);
        }
    }

    cursorY += lineHeight; // Add line height after printing the message
    return cursorY; // Return the new cursor Y position for the next line
}


int calculateLinesRequired(String message) {
    int linesRequired = 1;
    int lineWidth = 0;

    for (char c : message) {
        lineWidth += tft.textWidth(String(c));
        if (lineWidth > SCREEN_WIDTH) {
            linesRequired++;
            lineWidth = tft.textWidth(String(c));
        }
    }

    return linesRequired;
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
        default: return TFT_WHITE;
    }
}


void displayCenteredText(String text) {
    tft.fillScreen(TFT_BLACK);
    int16_t x1, y1;
    uint16_t w, h;
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(text, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
}