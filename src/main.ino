#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Pangodream_18650_CL.h>
#include <Preferences.h>
#include <map>
#include <vector>
#include <time.h>
#include "boot_screen.h"
#include "pins.h"

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define CHAR_HEIGHT   10
#define LINE_SPACING  0

#define INPUT_LINE_HEIGHT (CHAR_HEIGHT + LINE_SPACING)
#define STATUS_BAR_HEIGHT 10
#define MAX_LINES ((SCREEN_HEIGHT - INPUT_LINE_HEIGHT - STATUS_BAR_HEIGHT) / (CHAR_HEIGHT + LINE_SPACING))

#define BOARD_BAT_ADC 4
#define CONV_FACTOR 1.8
#define READS 20
Pangodream_18650_CL BL(BOARD_BAT_ADC, CONV_FACTOR, READS);

TFT_eSPI tft = TFT_eSPI();
WiFiClientSecure client;

std::map<String, uint32_t> nickColors;
String inputBuffer = "";

// WiFi credentials
String ssid = "";
String password = "";
String nick = "";

// Preferences for saving WiFi credentials
Preferences preferences;

// IRC connection
const char* server = "irc.supernets.org";
const int port = 6697;
bool useSSL = true;

// IRC identity
const char* user = "tdisck";
const char* realname = "ACID DROP Firmware 1.0.0";

unsigned long joinChannelTime = 0;
bool readyToJoinChannel = false;

unsigned long lastStatusUpdateTime = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 15000;
const unsigned long TOPIC_SCROLL_INTERVAL = 100;
const unsigned long TOPIC_DISPLAY_INTERVAL = 10000;

unsigned long lastTopicUpdateTime = 0;
unsigned long lastTopicDisplayTime = 0;
bool topicUpdated = false;
bool displayingTopic = false;

unsigned long lastActivityTime = 0;
const unsigned long INACTIVITY_TIMEOUT = 30000;
bool screenOn = true;

bool enteringPassword = false;
bool debugEnabled = false;

String currentTopic = ""; // Placeholder for the IRC topic
int topicOffset = 0; // For scrolling the topic text

// Buffer structure and initialization
struct Buffer {
    std::vector<String> lines;
    std::vector<bool> mentions;
    String channel;
    String topic;
};

struct WiFiNetwork {
    int index;
    int channel;
    int rssi;
    String encryption;
    String ssid;
};

std::vector<WiFiNetwork> wifiNetworks;
int selectedNetworkIndex = 0;

std::vector<Buffer> buffers;
int currentBufferIndex = 0;

void debugPrint(String message) {
    if (debugEnabled) {
        Serial.println(message);
    }
}

void setup() {
    // Initialize I2C to read keyboard input
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    
    // Check if 'd' key is pressed during boot
    char keyPressed = getKeyboardInput();
    if (keyPressed == 'd') {
        debugEnabled = true;
        Serial.begin(115200);
        Serial.println("Booting device...");
    }

    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.begin();
    tft.setRotation(1);
    tft.invertDisplay(1);

    debugPrint("TFT initialized");

    displayXBM();
    delay(3000);
    displayCenteredText("SCANNING WIFI");
    delay(1000);

    preferences.begin("wifi", false);  // Start preferences with namespace "wifi"
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");

    if (ssid != "" && password != "") {
        connectToWiFi();
    } else {
        scanWiFiNetworks();
        displayWiFiNetworks();
    }

    randomSeed(analogRead(0));
    nick = "s4d";

    Buffer defaultBuffer;
    defaultBuffer.channel = "#comms";
    buffers.push_back(defaultBuffer);

    defaultBuffer.channel = "#superbowl";
    buffers.push_back(defaultBuffer);
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

void turnOffScreen() {
    debugPrint("Screen turned off");
    tft.writecommand(TFT_DISPOFF); // Turn off display
    tft.writecommand(TFT_SLPIN);   // Put display into sleep mode
    digitalWrite(TFT_BL, LOW);     // Turn off the backlight (Assuming TFT_BL is the backlight pin)
    screenOn = false;
}

void turnOnScreen() {
    debugPrint("Screen turned on");
    digitalWrite(TFT_BL, HIGH);    // Turn on the backlight (Assuming TFT_BL is the backlight pin)
    tft.writecommand(TFT_SLPOUT);  // Wake up display from sleep mode
    tft.writecommand(TFT_DISPON);  // Turn on display
    screenOn = true;
}

void displayLines() {
    tft.fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT - INPUT_LINE_HEIGHT, TFT_BLACK);

    int cursorY = STATUS_BAR_HEIGHT;
    Buffer& currentBuffer = buffers[currentBufferIndex];

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
            tft.setTextColor(TFT_BLUE);
            tft.print("MODE ");
            String modeChange = line.substring(5);
            tft.setTextColor(TFT_WHITE);
            tft.print(modeChange);
            cursorY += CHAR_HEIGHT;
        } else if (line.startsWith("* ")) { // Check for action message
            int spacePos = line.indexOf(' ', 2);
            String senderNick = line.substring(2, spacePos);
            String actionMessage = line.substring(spacePos + 1);

            tft.setTextColor(TFT_WHITE);
            tft.print("* ");
            tft.setTextColor(nickColors[senderNick]);
            tft.print(senderNick + " ");
            tft.setTextColor(TFT_WHITE);
            tft.print(actionMessage);
            cursorY += CHAR_HEIGHT;
        } else {
            int colonPos = line.indexOf(':');
            String senderNick = line.substring(0, colonPos);
            String message = line.substring(colonPos + 2);

            tft.setTextColor(nickColors[senderNick]);
            tft.print(senderNick + ": ");
            tft.setTextColor(TFT_WHITE);

            // Check if the message contains the nick and highlight it
            int nickPos = message.indexOf(nick);
            if (mention && nickPos != -1) {
                cursorY = renderFormattedMessage(message, cursorY, CHAR_HEIGHT, true);
            } else {
                cursorY = renderFormattedMessage(message, cursorY, CHAR_HEIGHT, false);
            }
        }
    }

    displayInputLine();
}

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

void addLine(String senderNick, String message, String type, bool mention = false) {
    Buffer& currentBuffer = buffers[currentBufferIndex];
    addLineToBuffer(currentBuffer, senderNick, message, type, mention);

    displayLines();
}

void loop() {
    if (ssid.isEmpty() && !enteringPassword) {
        char incoming = getKeyboardInput();
        if (incoming != 0) {
            handleWiFiSelection(incoming);
            lastActivityTime = millis();
        }
    } else if (enteringPassword) {
        char incoming = getKeyboardInput();
        if (incoming != 0) {
            handlePasswordInput(incoming);
            lastActivityTime = millis();
        }
    } else {
        unsigned long currentMillis = millis();

        if (displayingTopic && currentMillis - lastTopicDisplayTime > 5000) { // Display topic for 5 seconds
            displayingTopic = false;
            lastStatusUpdateTime = currentMillis;
            updateStatusBar();
        } else if (!displayingTopic && currentMillis - lastStatusUpdateTime > 10000) { // Show topic every 10 seconds
            displayingTopic = true;
            lastTopicDisplayTime = currentMillis;
            updateTopicOnStatusBar();
        }

        if (client.connected()) {
            handleIRC();
        } else {
            if (WiFi.status() == WL_CONNECTED) {
                displayCenteredText("CONNECTING TO " + String(server));
                if (connectToIRC()) {
                    displayCenteredText("CONNECTED TO " + String(server));
                    sendIRC("NICK " + String(nick));
                    sendIRC("USER " + String(user) + " 0 * :" + String(realname));
                } else {
                    displayCenteredText("CONNECTION FAILED");
                    delay(1000);
                }
            } else {
                displayCenteredText("RECONNECTING TO WIFI");
                WiFi.begin(ssid.c_str(), password.c_str());
            }
        }

        if (readyToJoinChannel && currentMillis >= joinChannelTime) {
            joinChannels();
            tft.fillScreen(TFT_BLACK);
            updateStatusBar();
            readyToJoinChannel = false;
        }

        char incoming = getKeyboardInput();
        if (incoming != 0) {
            handleKeyboardInput(incoming);
            lastActivityTime = millis(); // Reset activity timer
        }

        // Check for inactivity
        if (screenOn && millis() - lastActivityTime > INACTIVITY_TIMEOUT) {
            turnOffScreen(); // Turn off screen and backlight
        }
    }
}

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

void connectToWiFi() {
    WiFi.begin(ssid.c_str(), password.c_str());
    debugPrint("Connecting to WiFi...");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) { // Try to connect for up to 10 seconds
        delay(500);
        displayCenteredText("CONNECTING TO " + ssid);
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        debugPrint("Connected to WiFi network: " + ssid);
        displayCenteredText("CONNECTED TO " + ssid);
        updateTimeFromNTP();
        saveWiFiCredentials();  // Save credentials after successful connection
    } else {
        displayCenteredText("WIFI CONNECTION FAILED");
        debugPrint("Failed to connect to WiFi.");
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

        // Ensure the spaces are found and prevent substring from going out of bounds
        if (firstSpace != -1 && secondSpace != -1) {
            String prefix = line.substring(0, firstSpace);
            String command = line.substring(firstSpace + 1, secondSpace);

            // RPL_WELCOME
            if (command == "001") {
                joinChannelTime = millis() + 2500;
                readyToJoinChannel = true;
            }
            // Handle topic change
            if (command == "332") { // RPL_TOPIC
                int topicStart = line.indexOf(':', secondSpace + 1) + 1;
                buffers[currentBufferIndex].topic = line.substring(topicStart);
                topicOffset = 0; // Reset scrolling
                topicUpdated = true;
            }
        }

        if (line.startsWith("PING")) {
            String pingResponse = "PONG " + line.substring(line.indexOf(' ') + 1);
            sendIRC(pingResponse);
        } else {
            parseAndDisplay(line);
            lastActivityTime = millis(); // Reset activity timer
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
                String actionMessage = message.substring(8, message.length() - 1); // Substring might need adjustment
                if (target == buffers[currentBufferIndex].channel) {
                    addLine(senderNick, actionMessage, "action");
                } else {
                    addLineToBuffer(getBufferByChannel(target), senderNick, actionMessage, "action", mention);
                }
            } else {
                if (target == buffers[currentBufferIndex].channel) {
                    addLine(senderNick, message, "message", mention);
                } else {
                    addLineToBuffer(getBufferByChannel(target), senderNick, message, "message", mention);
                }
            }
        } else if (command == "JOIN") {
            String senderNick = line.substring(1, line.indexOf('!'));
            String targetChannel = line.substring(secondSpace + 1);
            if (targetChannel == buffers[currentBufferIndex].channel) {
                addLine(senderNick, " has joined " + targetChannel, "join", false);
            } else {
                addLineToBuffer(getBufferByChannel(targetChannel), senderNick, " has joined " + targetChannel, "join", false);
            }
        } else if (command == "PART") {
            String senderNick = line.substring(1, line.indexOf('!'));
            String targetChannel = line.substring(secondSpace + 1);
            if (targetChannel == buffers[currentBufferIndex].channel) {
                addLine(senderNick, " has EMO-QUIT " + targetChannel, "part", false);
            } else {
                addLineToBuffer(getBufferByChannel(targetChannel), senderNick, " has EMO-QUIT " + targetChannel, "part", false);
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
            String oldNick = line.substring(1, line.indexOf('!'));
            String newNick = line.substring(line.lastIndexOf(':') + 1);
            for (auto& buffer : buffers) {
                if (buffer.channel == buffers[currentBufferIndex].channel) {
                    addLine(oldNick, " -> " + newNick, "nick", false);
                } else {
                    addLineToBuffer(buffer, oldNick, " -> " + newNick, "nick", false);
                }
            }
        } else if (command == "KICK") {
            int thirdSpace = line.indexOf(' ', secondSpace + 1);
            int fourthSpace = line.indexOf(' ', thirdSpace + 1);
            String kicker = line.substring(1, line.indexOf('!'));
            String kicked = line.substring(thirdSpace + 1, fourthSpace);
            String targetChannel = line.substring(secondSpace + 1, thirdSpace);
            if (targetChannel == buffers[currentBufferIndex].channel) {
                addLine(kicked, " by " + kicker, "kick", false);
            } else {
                addLineToBuffer(getBufferByChannel(targetChannel), kicked, " by " + kicker, "kick", false);
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

Buffer& getBufferByChannel(String channel) {
    for (auto& buffer : buffers) {
        if (buffer.channel == channel) {
            return buffer;
        }
    }
    // If no buffer found, create a new one (optional, depending on use case)
    Buffer newBuffer;
    newBuffer.channel = channel;
    buffers.push_back(newBuffer);
    return buffers.back();
}

void handleKeyboardInput(char key) {
    if (key == '\n' || key == '\r') {
        if (inputBuffer.startsWith("/")) {
            handleCommand(inputBuffer);
        } else {
            sendIRC("PRIVMSG " + buffers[currentBufferIndex].channel + " :" + inputBuffer);
            addLine(nick, inputBuffer, "message");
        }
        inputBuffer = "";
        displayInputLine();
        lastActivityTime = millis(); // Reset activity timer
        if (!screenOn) {
            turnOnScreen(); // Turn on screen and backlight
        }
    } else if (key == '\b') { // Backspace
        if (inputBuffer.length() > 0) {
            inputBuffer.remove(inputBuffer.length() - 1);
            displayInputLine();
            lastActivityTime = millis(); // Reset activity timer
            if (!screenOn) {
                turnOnScreen(); // Turn on screen and backlight
            }
        }
    } else {
        inputBuffer += key;
        displayInputLine();
        lastActivityTime = millis();
        if (!screenOn) {
            turnOnScreen();
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

char getKeyboardInput() {
    char incoming = 0;
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    if (Wire.available()) {
        incoming = Wire.read();
        if (incoming != (char)0x00) {
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
    tft.fillRect(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT, SCREEN_WIDTH, INPUT_LINE_HEIGHT, TFT_BLACK);
    tft.setCursor(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.print("> " + inputBuffer);
}

void displayCenteredText(String text) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(text, SCREEN_WIDTH / 2, (SCREEN_HEIGHT + STATUS_BAR_HEIGHT) / 2);
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

/* shoutz to e for the color support */
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

uint32_t generateRandomColor() {
    return tft.color565(random(0, 255), random(0, 255), random(0, 255));
}

void handlePasswordInput(char key) {
    if (key == '\n' || key == '\r') { // Enter
        password = inputBuffer;
        inputBuffer = "";
        enteringPassword = false;
        connectToWiFi();
    } else if (key == '\b') { // Backspace
        if (inputBuffer.length() > 0) {
            inputBuffer.remove(inputBuffer.length() - 1);
            displayPasswordInputLine();
        }
    } else {
        inputBuffer += key;
        displayPasswordInputLine();
    }
}

void displayPasswordInputLine() {
    tft.fillRect(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT, SCREEN_WIDTH, INPUT_LINE_HEIGHT, TFT_BLACK);
    tft.setCursor(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.print("> " + inputBuffer);
}

void updateSelectedNetwork(int delta) {
    int newIndex = selectedNetworkIndex + delta;
    if (newIndex >= 0 && newIndex < wifiNetworks.size()) {
        selectedNetworkIndex = newIndex;
        displayWiFiNetworks();
    }
}

void scanWiFiNetworks() {
    debugPrint("Scanning for WiFi networks...");
    int n = WiFi.scanNetworks();
    debugPrint("Total number of networks found: " + String(n));
    for (int i = 0; i < n && i < 100; i++) {
        WiFiNetwork net;
        net.index = i + 1;
        net.channel = WiFi.channel(i);
        net.rssi = WiFi.RSSI(i);
        net.encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured";
        net.ssid = WiFi.SSID(i);
        wifiNetworks.push_back(net);
    }
}

void displayWiFiNetworks() {
    tft.fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN);

    tft.setCursor(0, STATUS_BAR_HEIGHT);
    tft.printf("#  CH  RSSI  ENC      SSID\n");
    tft.setTextColor(TFT_WHITE);

    int maxDisplayedLines = (SCREEN_HEIGHT - STATUS_BAR_HEIGHT - CHAR_HEIGHT) / (CHAR_HEIGHT + LINE_SPACING);
    int startIdx = selectedNetworkIndex >= maxDisplayedLines ? selectedNetworkIndex - maxDisplayedLines + 1 : 0;

    for (int i = startIdx; i < wifiNetworks.size() && i < startIdx + maxDisplayedLines; ++i) {
        displayWiFiNetwork(i, i - startIdx + 1); // +1 to account for the header
    }
}

void displayWiFiNetwork(int index, int displayIndex) {
    int y = STATUS_BAR_HEIGHT + displayIndex * (CHAR_HEIGHT + LINE_SPACING);
    tft.setCursor(0, y);

    if (index == selectedNetworkIndex) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
    } else {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
    }

    WiFiNetwork net = wifiNetworks[index];
    uint16_t rssiColor = getColorFromPercentage((int32_t)net.rssi);
    tft.printf("%-2d %-3d ", net.index, net.channel);

    tft.setTextColor(rssiColor, TFT_BLACK); // Set RSSI color
    tft.printf("%-5d ", net.rssi);

    tft.setTextColor(index == selectedNetworkIndex ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    tft.printf("%-8s %s", net.encryption.c_str(), net.ssid.c_str());
}

void handleWiFiSelection(char key) {
    if (key == 'u') {
        updateSelectedNetwork(-1);
    } else if (key == 'd') {
        updateSelectedNetwork(1);
    } else if (key == '\n' || key == '\r') {
        ssid = wifiNetworks[selectedNetworkIndex].ssid;
        if (wifiNetworks[selectedNetworkIndex].encryption == "Secured") {
            inputBuffer = "";
            enteringPassword = true;
            tft.fillScreen(TFT_BLACK);
            tft.setTextSize(1);
            tft.setTextColor(TFT_WHITE);
            tft.setCursor(0, STATUS_BAR_HEIGHT);
            tft.println("ENTER PASSWORD:");
            displayPasswordInputLine();
        } else {
            connectToWiFi();
        }
    }
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

    // Display time and channel name
    struct tm timeinfo;
    char timeStr[20];
    if (!getLocalTime(&timeinfo)) {
        // Default time if NTP sync fails
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

    tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, TFT_BLACK);  // Clear the status bar area
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buffers[currentBufferIndex].topic, SCREEN_WIDTH / 2, STATUS_BAR_HEIGHT / 2);
}

uint16_t getColorFromPercentage(int rssi) {
    if (rssi > -50) return TFT_GREEN;
    else if (rssi > -60) return TFT_YELLOW;
    else if (rssi > -70) return TFT_ORANGE;
    else return TFT_RED;
}

void updateTimeFromNTP() {
    configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    for (int i = 0; i < 10; ++i) {
        delay(2000);
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            return;
        }
    }
}

void joinChannels() {
    for (auto& buffer : buffers) {
        sendIRC("JOIN " + buffer.channel);
    }
}

void saveWiFiCredentials() {
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    debugPrint("WiFi credentials saved.");
}
