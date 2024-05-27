#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Pangodream_18650_CL.h> // POWER MANAGEMENT
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

#define BOARD_BAT_ADC 4 // Define the ADC pin used for battery reading
#define CONV_FACTOR 1.8  // Conversion factor for the ADC to voltage conversion
#define READS 20         // Number of readings for averaging
Pangodream_18650_CL BL(BOARD_BAT_ADC, CONV_FACTOR, READS);

TFT_eSPI tft = TFT_eSPI();
WiFiClientSecure client;

std::map<String, uint32_t> nickColors;
std::vector<String> lines;
std::vector<bool> mentions;
String inputBuffer = "";

// WiFi credentials
String ssid = "";
String password = "";
String nick = "";

bool debugMode = false;
unsigned long debugStartTime = 0;


// IRC connection
const char* server = "irc.supernets.org";
const int port = 6697;
bool useSSL = true;
const char* channel = "#comms";

// IRC identity
const char* user = "tdeck";
const char* realname = "ACID DROP Firmware 1.0.0"; // Need to eventually set this up to use a variable

unsigned long joinChannelTime = 0;
bool readyToJoinChannel = false;

unsigned long lastStatusUpdateTime = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 15000;

unsigned long lastActivityTime = 0;
const unsigned long INACTIVITY_TIMEOUT = 30000; // 30 seconds
bool screenOn = true;


struct WiFiNetwork {
    int index;
    int channel;
    int rssi;
    String encryption;
    String ssid;
};

std::vector<WiFiNetwork> wifiNetworks;
int selectedNetworkIndex = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("Booting device...");

    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH); // Turn on the backlight initially

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

    tft.begin();
    tft.setRotation(1);
    tft.invertDisplay(1);

    Serial.println("TFT initialized");

    displayXBM();
    delay(3000);
    displayCenteredText("SCANNING WIFI");
    delay(1000);
    scanWiFiNetworks();
    displayWiFiNetworks();

    randomSeed(analogRead(0));
    int randomNum = random(1000, 10000);
    nick = "ACID_" + String(randomNum);
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
    Serial.println("Screen turned off");
    tft.writecommand(TFT_DISPOFF); // Turn off display
    tft.writecommand(TFT_SLPIN);   // Put display into sleep mode
    digitalWrite(TFT_BL, LOW);     // Turn off the backlight (Assuming TFT_BL is the backlight pin)
    screenOn = false;
}

void turnOnScreen() {
    Serial.println("Screen turned on");
    digitalWrite(TFT_BL, HIGH);    // Turn on the backlight (Assuming TFT_BL is the backlight pin)
    tft.writecommand(TFT_SLPOUT);  // Wake up display from sleep mode
    tft.writecommand(TFT_DISPON);  // Turn on display
    screenOn = true;
}

void displayLines() {
    tft.fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT - INPUT_LINE_HEIGHT, TFT_BLACK);

    int cursorY = STATUS_BAR_HEIGHT;
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

void addLine(String senderNick, String message, String type, bool mention = false) {
    if (nickColors.find(senderNick) == nickColors.end())
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
    } else {
        formattedMessage = senderNick + ": " + message;
    }

    int linesRequired = calculateLinesRequired(formattedMessage);

    while (lines.size() + linesRequired > MAX_LINES) {
        lines.erase(lines.begin());
        mentions.erase(mentions.begin());
    }

    lines.push_back(formattedMessage);
    mentions.push_back(mention);

    displayLines();
}

void displayDeviceInfo() {
    tft.fillScreen(TFT_BLACK);
    printDeviceInfo();
}

void loop() {
    if (debugMode) {
        if (millis() - debugStartTime > 10000) { // 10 seconds
            debugMode = false;
            // Clear the screen and return to the IRC interface
            tft.fillScreen(TFT_BLACK);
            displayLines();
        }
    } else {
        if (ssid.isEmpty()) {
            char incoming = getKeyboardInput();
            if (incoming != 0) {
                handleWiFiSelection(incoming);
                lastActivityTime = millis(); // Reset activity timer
            }
        } else {
            if (millis() - lastStatusUpdateTime > STATUS_UPDATE_INTERVAL) {
                updateStatusBar();
                lastStatusUpdateTime = millis();
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

            if (readyToJoinChannel && millis() >= joinChannelTime) {
                tft.fillScreen(TFT_BLACK);
                updateStatusBar();
                sendIRC("JOIN " + String(channel));
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
}

bool connectToIRC() {
    Serial.println("Connecting to IRC...");
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
    Serial.println("Connecting to WiFi...");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) { // Try to connect for up to 10 seconds
        delay(500);
        displayCenteredText("CONNECTING TO " + ssid);
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to WiFi network: " + ssid);
        displayCenteredText("CONNECTED TO " + ssid);
        delay(1000);
        updateTimeFromNTP();
    } else {
        displayCenteredText("WIFI CONNECTION FAILED");
        Serial.println("Failed to connect to WiFi.");
    }
}

void sendIRC(String command) {
    if (client.println(command))
        Serial.println("IRC: >>> " + command);
    else
        Serial.println("Failed to send: " + command);
}

void handleIRC() {
    while (client.available()) {
        String line = client.readStringUntil('\n');
        Serial.println("IRC: " + line);

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
            if (target == String(channel)) {
                int colonPos = line.indexOf(':', thirdSpace);
                String message = line.substring(colonPos + 1);
                String senderNick = line.substring(1, line.indexOf('!'));
                bool mention = message.indexOf(nick) != -1;
                addLine(senderNick, message, "message", mention);
            }
        } else if (command == "JOIN" && line.indexOf(channel) != -1) {
            String senderNick = line.substring(1, line.indexOf('!'));
            addLine(senderNick, " has joined " + String(channel), "join");
        } else if (command == "PART" && line.indexOf(channel) != -1) {
            String senderNick = line.substring(1, line.indexOf('!'));
            addLine(senderNick, " has EMO-QUIT " + String(channel), "part");
        } else if (command == "QUIT" && line.indexOf(channel) != -1) {
            String senderNick = line.substring(1, line.indexOf('!'));
            addLine(senderNick, "", "quit");
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
        if (inputBuffer.startsWith("/debug")) {
            debugMode = true;
            debugStartTime = millis();
            displayDeviceInfo();
            inputBuffer = "";
        } else if (inputBuffer.startsWith("/raw ")) {
            String rawCommand = inputBuffer.substring(5);
            sendRawCommand(rawCommand);
        } else {
            sendIRC("PRIVMSG " + String(channel) + " :" + inputBuffer);
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
        lastActivityTime = millis(); // Reset activity timer
        if (!screenOn) {
            turnOnScreen(); // Turn on screen and backlight
        }
    }
}


void sendRawCommand(String command) {
    if (client.connected()) {
        sendIRC(command);
        Serial.println("Sent raw command: " + command);
    } else {
        Serial.println("Failed to send raw command: Not connected to IRC");
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

uint32_t generateRandomColor() {
    return tft.color565(random(0, 255), random(0, 255), random(0, 255));
}

void handlePasswordInput(char key) {
    if (key == '\n' || key == '\r') { // Enter
        password = inputBuffer;
        inputBuffer = "";
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
    Serial.println("Scanning for WiFi networks...");
    int n = WiFi.scanNetworks();
    Serial.print("Total number of networks found: ");
    Serial.println(n);
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
            tft.fillScreen(TFT_BLACK);
            tft.setTextSize(1);
            tft.setTextColor(TFT_WHITE);
            tft.setCursor(0, STATUS_BAR_HEIGHT);
            tft.println("ENTER PASSWORD:");
            displayPasswordInputLine();
            // Switch to password input mode
            while (true) {
                char incoming = getKeyboardInput();
                if (incoming != 0) {
                    handlePasswordInput(incoming);
                    if (incoming == '\n' || incoming == '\r') {
                        break;
                    }
                }
            }
        } else {
            connectToWiFi();
        }
    }
}

void updateStatusBar() {
    Serial.println("Updating status bar...");
    uint16_t darkerGrey = tft.color565(25, 25, 25);
    tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, darkerGrey);

    // Display time
    struct tm timeinfo;
    char timeStr[9];
    if (!getLocalTime(&timeinfo)) {
        // Default time if NTP sync fails
        sprintf(timeStr, "12:00 AM");
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

    int batteryLevel = BL.getBatteryChargeLevel();
    char batteryStr[15];
    sprintf(batteryStr, "Batt: %d%%", batteryLevel);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_CYAN, darkerGrey);
    tft.drawString("Batt:", SCREEN_WIDTH - 40, STATUS_BAR_HEIGHT / 2);
    tft.setTextColor(getColorFromPercentage(batteryLevel), darkerGrey);
    tft.drawString(batteryStr + 5, SCREEN_WIDTH - 5, STATUS_BAR_HEIGHT / 2);
}

uint16_t getColorFromPercentage(int rssi) {
    if (rssi > -50) return TFT_GREEN;
    else if (rssi > -60) return TFT_YELLOW;
    else if (rssi > -70) return TFT_ORANGE;
    else return TFT_RED;
}

void updateTimeFromNTP() {
    Serial.println("Syncing time with NTP server...");
    configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    for (int i = 0; i < 10; ++i) { // Try up to 10 times
        delay(2000);
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            Serial.println(&timeinfo, "Time synchronized: %A, %B %d %Y %H:%M:%S");
            return;
        } else {
            Serial.println("Failed to synchronize time, retrying...");
        }
    }

    Serial.println("Failed to synchronize time after multiple attempts.");
}

String formatBytes(size_t bytes) {
    if (bytes < 1024) {
        return String(bytes) + " B";
    } else if (bytes < (1024 * 1024)) {
        return String(bytes / 1024.0, 2) + " KB";
    } else if (bytes < (1024 * 1024 * 1024)) {
        return String(bytes / 1024.0 / 1024.0, 2) + " MB";
    } else {
        return String(bytes / 1024.0 / 1024.0 / 1024.0, 2) + " GB";
    }
}

void printDeviceInfo() {
    // Get MAC Address
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    String macAddress = String(mac[0], HEX) + ":" + String(mac[1], HEX) + ":" +
                        String(mac[2], HEX) + ":" + String(mac[3], HEX) + ":" +
                        String(mac[4], HEX) + ":" + String(mac[5], HEX);

    // Get Chip Info
    uint32_t chipId = ESP.getEfuseMac(); // Unique ID
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    String chipInfo = String(chip_info.model) + " Rev " + String(chip_info.revision) +
                      ", " + String(chip_info.cores) + " cores, " +
                      String(ESP.getCpuFreqMHz()) + " MHz";

    // Get Flash Info
    size_t flashSize = spi_flash_get_chip_size();
    size_t flashUsed = ESP.getFlashChipSize() - ESP.getFreeSketchSpace();
    String flashInfo = formatBytes(flashUsed) + " / " + formatBytes(flashSize);
    
    // Get PSRAM Info
    size_t total_psram = ESP.getPsramSize();
    size_t free_psram = ESP.getFreePsram();
    String psramInfo = formatBytes(total_psram - free_psram) + " / " + formatBytes(total_psram);

    // Get Heap Info
    size_t total_heap = ESP.getHeapSize();
    size_t free_heap = ESP.getFreeHeap();
    String heapInfo = formatBytes(total_heap - free_heap) + " / " + formatBytes(total_heap);

    // Get WiFi Info
    String wifiInfo = "Not connected";
    String wifiSSID = "";
    String wifiChannel = "";
    String wifiSignal = "";
    String wifiLocalIP = "";
    String wifiGatewayIP = "";
    if (WiFi.status() == WL_CONNECTED) {
        wifiSSID = WiFi.SSID();
        wifiChannel = String(WiFi.channel());
        wifiSignal = String(WiFi.RSSI()) + " dBm";
        wifiLocalIP = WiFi.localIP().toString();
        wifiGatewayIP = WiFi.gatewayIP().toString();
    }

    // Print to Serial Monitor
    Serial.println("Chip ID: " + String(chipId, HEX));
    Serial.println("MAC Address: " + macAddress);
    Serial.println("Chip Info: " + chipInfo);
    Serial.println("Memory:");
    Serial.println("  Flash: " + flashInfo);
    Serial.println("  PSRAM: " + psramInfo);
    Serial.println("  Heap: " + heapInfo);
    Serial.println("WiFi Info: " + wifiInfo);
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("  SSID: " + wifiSSID);
        Serial.println("  Channel: " + wifiChannel);
        Serial.println("  Signal: " + wifiSignal);
        Serial.println("  Local IP: " + wifiLocalIP);
        Serial.println("  Gateway IP: " + wifiGatewayIP);
    }

    // Display on TFT
    tft.fillScreen(TFT_BLACK);
    int line = 0;
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("Chip ID:       "); tft.setTextColor(TFT_WHITE); tft.println(String(chipId, HEX)); line++;
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("MAC Address:   "); tft.setTextColor(TFT_WHITE); tft.println(macAddress); line++;
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("Chip Info:     "); tft.setTextColor(TFT_WHITE); tft.println(chipInfo); line++;
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_CYAN); tft.print("Memory:        "); tft.setTextColor(TFT_WHITE); tft.println(""); line++;
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  Flash:       "); tft.setTextColor(TFT_WHITE); tft.println(flashInfo); line++;
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  PSRAM:       "); tft.setTextColor(TFT_WHITE); tft.println(psramInfo); line++;
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  Heap:        "); tft.setTextColor(TFT_WHITE); tft.println(heapInfo); line++;
    if (WiFi.status() == WL_CONNECTED) {
        tft.setCursor(0, line * 16); tft.setTextColor(TFT_CYAN); tft.print("WiFi Info:     "); tft.setTextColor(TFT_WHITE); tft.println(""); line++;
        tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  SSID:        "); tft.setTextColor(TFT_WHITE); tft.println(wifiSSID); line++;
        tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  Channel:     "); tft.setTextColor(TFT_WHITE); tft.println(wifiChannel); line++;
        tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  Signal:      "); tft.setTextColor(TFT_WHITE); tft.println(wifiSignal); line++;
        tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  Local IP:    "); tft.setTextColor(TFT_WHITE); tft.println(wifiLocalIP); line++;
        tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  Gateway IP:  "); tft.setTextColor(TFT_WHITE); tft.println(wifiGatewayIP); line++;
    } else {
        tft.setCursor(0, line * 16); tft.setTextColor(TFT_CYAN); tft.print("WiFi Info:     "); tft.setTextColor(TFT_WHITE); tft.println("Not connected"); line++;
    }
}