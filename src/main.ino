// Standard includes
#include <map>
#include <time.h>
#include <vector>

// Aurduino includes
#include <esp_wifi.h> // Needed for Mac spoofing
#include "nvs_flash.h"
#include <Pangodream_18650_CL.h> // Power management
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>

// Local includes
#include "bootScreen.h"
#include "pins.h"
#include "Speaker.h"


// Constants
#define CHAR_HEIGHT 10
#define LINE_SPACING  0
#define STATUS_BAR_HEIGHT 10
#define INPUT_LINE_HEIGHT (CHAR_HEIGHT + LINE_SPACING)
#define MAX_LINES ((SCREEN_HEIGHT - INPUT_LINE_HEIGHT - STATUS_BAR_HEIGHT) / (CHAR_HEIGHT + LINE_SPACING))

/// Struct to hold information about a WiFi network (do we want to include mac addresses)
struct WiFiNetwork {
    int index;
    int channel;
    int rssi;
    String encryption;
    String ssid;
};

// Initialize components and objects
Pangodream_18650_CL BL(BOARD_BAT_ADC, CONV_FACTOR, READS);
Preferences preferences;
TFT_eSPI tft = TFT_eSPI();
WiFiClient* client;

// Memory vectors & maps
std::map<String, uint32_t> nickColors;
std::vector<String> lines; // Possible rename to bufferLines ?
std::vector<bool> mentions;
std::vector<WiFiNetwork> wifiNetworks;

// Global variables to cache preferences and buffers
String irc_nickname;
String irc_username;
String irc_realname;
String irc_server;
int irc_port;
bool irc_tls;
String irc_channel;
String irc_nickserv;
String wifi_ssid;
String wifi_password;
String inputBuffer = "";

// Leftover crack variables (will be removed when preferences are done)
const char* channel = "#comms";

// Timing variables
unsigned long infoScreenStartTime = 0;
unsigned long configScreenStartTime = 0;
unsigned long joinChannelTime = 0;
unsigned long lastStatusUpdateTime = 0;
unsigned long lastActivityTime = 0;

// Timing constants
const unsigned long STATUS_UPDATE_INTERVAL = 15000; // 15 seconds
const unsigned long INACTIVITY_TIMEOUT = 30000;     // 30 seconds

// Dynamic variables
bool infoScreen = false;
bool configScreen = false;
bool readyToJoinChannel = false;
bool screenOn = true;
int selectedNetworkIndex = 0;


// Main functions ---------------------------------------------------------------------------------
void displayXBM() {
    tft.fillScreen(TFT_BLACK);

    // Get the center position for the logo
    int x = (SCREEN_WIDTH - logo_width) / 2;
    int y = (SCREEN_HEIGHT - logo_height) / 2;

    tft.drawXBitmap(x, y, logo_bits, logo_width, logo_height, TFT_GREEN);

    // Time handling
    unsigned long startTime = millis();
    bool wipeInitiated = false;

    // Check for 'w' key press during the logo display time
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


void setup() {
    // Initialize serial communication
    Serial.begin(115200);
    Serial.println("Booting device...");

    // Turn on the power to the board
    pinMode(BOARD_POWERON, OUTPUT); 
    digitalWrite(BOARD_POWERON, HIGH);

    // Turn on power to the screen
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // Start the I2C bus for the keyboard
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

    // Initialize the screen
    tft.begin();
    tft.setRotation(1);
    tft.invertDisplay(1);
    Serial.println("TFT initialized");

    // Display the boot screen
    displayXBM();

    // Initialize the preferences
    loadPreferences();

    // Initialize the speaker
    setupI2S(); // Do we want to keep this open or uninstall after each use to keep resources free?
    //const char* rtttl_boot = "ff6_victory:d=4,o=5,b=120:32d6,32p,32d6,32p,32d6,32p,d6,a#,c6,16d6,8p,16c6,2d6"; // This will go in preferences soon
    const char* rtttl_boot = "TakeOnMe:d=4,o=4,b=450:8f#5,8f#5,8f#5,8d5,8p,8b,8p,8e5,8p,8e5,8p,8e5,8g#5,8g#5,8a5,8b5,8a5,8a5,8a5,8e5,8p,8d5,8p,8f#5,8p,8f#5,8p,8f#5,8e5,8e5,8f#5,8e5,8f#5,8f#5,8f#5,8d5,8p,8b,8p,8e5,8p,8e5,8p,8e5,8g#5,8g#5,8a5,8b5,8a5,8a5,8a5,8e5,8p,8d5,8p,8f#5,8p,8f#5,8p,8f#5,8e5,8e5";
    playRTTTL(rtttl_boot);

    // Setup the WiFi
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("acid-drop"); // Turn into a preference
    WiFi.onEvent(WiFiEvent);
    randomizeMacAddress();

    // Connect to WiFi if credentials are stored, otherwise scan for networks
    if (wifi_ssid.length() > 0 && wifi_password.length() > 0) {
        Serial.println("Stored WiFi credentials found");
        connectToWiFi(wifi_ssid, wifi_password);
    } else {
        scanWiFiNetworks();
    }
}


void loop() {
    // Handle the info screen if it is active (from /info command)
    if (infoScreen) {
        if (millis() - infoScreenStartTime > 10000) { // 10 seconds
            infoScreen = false;
            tft.fillScreen(TFT_BLACK);
            displayLines(); // Redraw the previous buffer
        }
    } else if (configScreen) {
        if (millis() - configScreenStartTime > 10000) { // 10 seconds
            configScreen = false;
            tft.fillScreen(TFT_BLACK);
            displayLines(); // Redraw the previous buffer
        }
    } else {
        // Handle keyboard input for WiFi if the SSID is empty still (aka not connected)
        if (wifi_ssid.length() == 0) {
            char incoming = getKeyboardInput();
            if (incoming != 0) {
                handleWiFiSelection(incoming);
                lastActivityTime = millis(); // Reset activity timer
            }
        } else {
            // Handle status bar updating on an interval
            if (millis() - lastStatusUpdateTime > STATUS_UPDATE_INTERVAL) {
                updateStatusBar();
                lastStatusUpdateTime = millis();
            }

            // Handle IRC data
            if (client && client->connected()) {
                handleIRC();
            } else {
                // Connect to IRC if not connected (or reconnect to wifi if disconnected)
                if (WiFi.status() == WL_CONNECTED) {
                    displayCenteredText("CONNECTING TO " + String(irc_server));
                    if (connectToIRC()) {
                        delay(2000); // Delay to allow the connection to establish
                        Serial.println("Connected to IRC!");
                        displayCenteredText("CONNECTED TO " + String(irc_server));
                        Serial.println("Connecting to IRC with " + irc_nickname + " (" + irc_username + ") [" + irc_realname + "]");
                        sendIRC("NICK " + String(irc_nickname));
                        sendIRC("USER " + String(irc_username) + " 0 * :" + String(irc_realname));
                    } else {
                        Serial.println("Failed to connect to IRC");
                        displayCenteredText("CONNECTION FAILED");
                        delay(1000);
                    }
                } else {
                    displayCenteredText("RECONNECTING TO WIFI");
                    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str()); // Need to handle this better
                }
            }

            // Join channel after a delay
            if (readyToJoinChannel && millis() >= joinChannelTime) {
                tft.fillScreen(TFT_BLACK);
                updateStatusBar();
                sendIRC("JOIN " + String(channel));
                readyToJoinChannel = false;
            }

            // Handle keyboard input (for IRC)
            char incoming = getKeyboardInput();
            if (incoming != 0) {
                handleKeyboardInput(incoming);
                lastActivityTime = millis();
            }

            // Handle inactivity timeout
            if (screenOn && millis() - lastActivityTime > INACTIVITY_TIMEOUT) {
                turnOffScreen();
            }
        }
    }
}


void loadPreferences() {
    preferences.begin("config", false);

    // IRC preferences
    if (!preferences.isKey("irc_nickname"))
        preferences.putString("irc_nickname", "ACID_" + String(random(1000, 10000)));
    irc_nickname = preferences.getString("irc_nickname");

    if (!preferences.isKey("irc_username"))
        preferences.putString("irc_username", "tdeck");
    irc_username = preferences.getString("irc_username");

    if (!preferences.isKey("irc_realname"))
        preferences.putString("irc_realname", "ACID DROP Firmware");
    irc_realname = preferences.getString("irc_realname");

    if (!preferences.isKey("irc_server"))
        preferences.putString("irc_server", "irc.supernets.org");
    irc_server = preferences.getString("irc_server");

    if (!preferences.isKey("irc_port"))
        preferences.putInt("irc_port", 6667);
    irc_port = preferences.getInt("irc_port");

    if (!preferences.isKey("irc_tls"))
        preferences.putBool("irc_tls", false);
    irc_tls = preferences.getBool("irc_tls");

    if (!preferences.isKey("irc_channel"))
        preferences.putString("irc_channel", "#comms");
    irc_channel = preferences.getString("irc_channel");

    if (!preferences.isKey("irc_nickserv"))
        preferences.putString("irc_nickserv", "");
    irc_nickserv = preferences.getString("irc_nickserv");

    // WiFi preferences
    if (!preferences.isKey("wifi_ssid"))
        preferences.putString("wifi_ssid", "");
    wifi_ssid = preferences.getString("wifi_ssid");

    if (!preferences.isKey("wifi_password"))
        preferences.putString("wifi_password", "");
    wifi_password = preferences.getString("wifi_password");

    preferences.end();
}

// ------------------------------------------------------------------------------------------------



// WiFi functions ---------------------------------------------------------------------------------
void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case SYSTEM_EVENT_STA_CONNECTED:
            Serial.println("WiFi connected");
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            Serial.println("WiFi disconnected");
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            Serial.println("WiFi got IP address: " + WiFi.localIP().toString());
            break;
        case SYSTEM_EVENT_STA_LOST_IP:
            Serial.println("WiFi lost IP address");
            break;
        default:
            break;
    }
}


void connectToWiFi(String ssid, String password) {
    Serial.println("Connecting to WiFi network: " + ssid);
    WiFi.begin(ssid.c_str(), password.c_str());

    // Wait for the WiFi connection to complete (or timeout after 10 seconds)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        displayCenteredText("CONNECTING TO " + ssid);
        attempts++;
    }

    // Handle the connection result
    if (WiFi.status() == WL_CONNECTED) {
        displayCenteredText("CONNECTED TO " + ssid);

        // Sync time with NTP server
        updateTimeFromNTP();

        // Store the WiFi credentials upon successful connection
        preferences.begin("config", false);
        preferences.putString("wifi_ssid", ssid);
        preferences.putString("wifi_password", password);
        preferences.end();
        Serial.println("Stored WiFi credentials updated");

        // Cache the WiFi credentials
        wifi_ssid = ssid;
        wifi_password = password;
    } else {
        Serial.println("Failed to connect to WiFi network: " + ssid);
        displayCenteredText("WIFI CONNECTION FAILED");
        
        // Clear stored credentials on failure
        preferences.begin("config", false);
        preferences.putString("wifi_ssid", "");
        preferences.putString("wifi_password", "");
        preferences.end();
        Serial.println("Stored WiFi credentials removed");\

        // Clear the cached WiFi credentials
        wifi_ssid = "";
        wifi_password = "";

        scanWiFiNetworks(); // Rescan for networks
    }
}


void randomizeMacAddress() {
    Serial.println("Current MAC Address: " + WiFi.macAddress());

    uint8_t new_mac[6];

    for (int i = 0; i < 6; ++i)
        new_mac[i] = random(0x00, 0xFF);

    if (esp_wifi_set_mac(WIFI_IF_STA, new_mac) == ESP_OK)
        Serial.println("New MAC Address: " + WiFi.macAddress());
    else
        Serial.print("Failed to set new MAC Address");
}


// Need to utilize this function still
String getEncryptionType(wifi_auth_mode_t encryptionType) {
    switch (encryptionType) {
        case (WIFI_AUTH_OPEN):
            return "Open";
        case (WIFI_AUTH_WEP):
            return "WEP";
        case (WIFI_AUTH_WPA_PSK):
            return "WPA_PSK";
        case (WIFI_AUTH_WPA2_PSK):
            return "WPA2_PSK";
        case (WIFI_AUTH_WPA_WPA2_PSK):
            return "WPA_WPA2_PSK";
        case (WIFI_AUTH_WPA2_ENTERPRISE):
            return "WPA2_ENTERPRISE";
        default:
            return "Unknown";
    }
}


void scanWiFiNetworks() {
    Serial.println("Scanning for WiFi networks...");
    displayCenteredText("SCANNING WIFI");
    delay(1000); // Do we need this delay?

    int n = WiFi.scanNetworks();

    if (n == 0) {
        Serial.println("No WiFi networks found");
        displayCenteredText("NO NETWORKS FOUND");
        delay(2000);
        scanWiFiNetworks();
        return;
    }
    
    Serial.print("Total number of networks found: ");
    Serial.println(n);

    // Loop through the networks and store them in the wifiNetworks vector
    for (int i = 0; i < n && i < 100; i++) {
        WiFiNetwork net;
        net.index = i + 1;
        net.channel = WiFi.channel(i);
        net.rssi = WiFi.RSSI(i);
        net.encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured";
        String ssid = WiFi.SSID(i).substring(0, 32); // WiFi SSIDs are limited to 32 characters
        net.ssid = ssid;
        wifiNetworks.push_back(net);
    }

    displayWiFiNetworks(); // Display the scanned networks
}


void handlePasswordInput(char key) {
    if (key == '\n' || key == '\r') { // Enter
        wifi_password = inputBuffer;
        inputBuffer = "";
        connectToWiFi(wifi_ssid, wifi_password);
    } else if (key == '\b') { // Backspace
        if (inputBuffer.length() > 0) {
            inputBuffer.remove(inputBuffer.length() - 1);
            displayPasswordInputLine();
        }
    } else if (inputBuffer.length() < 63) { // WiFi passwords are limited to 63 characters
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


void displayWiFiNetworks() {
    tft.fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK); // Clear the screen (except the status bar area)

    // Set the header for the WiFi networks
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(0, STATUS_BAR_HEIGHT);
    tft.printf("#  CH  RSSI  ENC      SSID\n");
    tft.setTextColor(TFT_WHITE);

    // Calculate the starting index and max displayed lines
    int maxDisplayedLines = (SCREEN_HEIGHT - STATUS_BAR_HEIGHT - CHAR_HEIGHT) / (CHAR_HEIGHT + LINE_SPACING);
    int startIdx = selectedNetworkIndex >= maxDisplayedLines ? selectedNetworkIndex - maxDisplayedLines + 1 : 0;

    // Display the WiFi networks on the screen
    for (int i = startIdx; i < wifiNetworks.size() && i < startIdx + maxDisplayedLines; ++i) {
        displayWiFiNetwork(i, i - startIdx + 1); // +1 to account for the header
    }
}


void displayWiFiNetwork(int index, int displayIndex) {
    int y = STATUS_BAR_HEIGHT + displayIndex * (CHAR_HEIGHT + LINE_SPACING);
    tft.setCursor(0, y);

    // Set the text color based on the selected network
    if (index == selectedNetworkIndex)
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
    else
        tft.setTextColor(TFT_WHITE, TFT_BLACK);

    WiFiNetwork net = wifiNetworks[index];

    // Index and channel number
    uint16_t rssiColor = getColorFromPercentage((int32_t)net.rssi);
    tft.printf("%-2d %-3d ", net.index, net.channel);

    // RSSI
    tft.setTextColor(rssiColor, TFT_BLACK);
    tft.printf("%-5d ", net.rssi);

    // Encryption and SSID
    tft.setTextColor(index == selectedNetworkIndex ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    tft.printf("%-8s %s", net.encryption.c_str(), net.ssid.c_str());
}


void handleWiFiSelection(char key) {
    if (key == 'u') {
        updateSelectedNetwork(-1);
    } else if (key == 'd') {
        updateSelectedNetwork(1);
    } else if (key == '\n' || key == '\r') {
        wifi_ssid = wifiNetworks[selectedNetworkIndex].ssid;
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
            wifi_password = "";
            connectToWiFi(wifi_ssid, wifi_password);
        }
    }
}

// ------------------------------------------------------------------------------------------------



// IRC functions ----------------------------------------------------------------------------------
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


void handleIRC() {
    while (client->available()) {
        String line = client->readStringUntil('\n');

        // This is an anomaly, but it can happen and I wanted debug output for if it does
        if (line.length() > 512) {
            Serial.println("WARNING: IRC line length exceeds 512 characters!");
            line = line.substring(0, 512); // Truncate the line to 512 characters anyways
        }

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
            lastActivityTime = millis();
        }
    }
}

// ------------------------------------------------------------------------------------------------



// Indepentent functions (does not rely on any other functions) -----------------------------------
int calculateLinesRequired(String message) {
    int linesRequired = 1;
    int lineWidth = 0;

    for (unsigned int i = 0; i < message.length(); i++) {
        char c = message[i];
        if (c == '\x03') {
            // Check for foreground color
            if (i + 1 < message.length() && isdigit(message[i + 1])) {
                i++;
                if (i + 1 < message.length() && isdigit(message[i + 1])) {
                    i++;
                }
            }
            // Check for background color
            if (i + 1 < message.length() && message[i + 1] == ',' && isdigit(message[i + 2])) {
                i += 2; // Skip the comma
                if (i + 1 < message.length() && isdigit(message[i + 1])) {
                    i++;
                }
            }
        } else if (c != '\x02' && c != '\x0F' && c != '\x1F') { // Ignore other formatting codes as they are not as prevalent
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


String formatBytes(size_t bytes) {
    if (bytes < 1024)
        return String(bytes) + " B";
    else if (bytes < (1024 * 1024))
        return String(bytes / 1024.0, 2) + " KB";
    else if (bytes < (1024 * 1024 * 1024))
        return String(bytes / 1024.0 / 1024.0, 2) + " MB";
    else
        return String(bytes / 1024.0 / 1024.0 / 1024.0, 2) + " GB";
}


uint32_t generateRandomColor() {
    return tft.color565(random(0, 255), random(0, 255), random(0, 255));
}


uint16_t getColorFromPercentage(int percentage) {
    if (percentage > 75) return TFT_GREEN;
    else if (percentage > 50) return TFT_YELLOW;
    else if (percentage > 25) return TFT_ORANGE;
    else return TFT_RED;
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


// Cheers to e for hand typing these color codes
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

    Serial.println("Failed to synchronize time after multiple attempts."); // What do we do if we can't sync with the NTP server?
}

// ------------------------------------------------------------------------------------------------



// Logic and Interface functions ------------------------------------------------------------------
int renderFormattedMessage(String message, int cursorY, int lineHeight, bool highlightNick = false) {
    uint16_t fgColor = TFT_WHITE;
    uint16_t bgColor = TFT_BLACK;
    bool bold = false;
    bool underline = false;
    bool nickHighlighted = false; // Track if the nick has been highlighted

    int nickPos = -1;
    if (highlightNick) {
        nickPos = message.indexOf(irc_nickname);
    }

    for (unsigned int i = 0; i < message.length(); i++) {
        char c = message[i];
        if (c == '\x02') { // Bold
            bold = !bold;

            // Bold text is not supported by the current font...it makes the entire screen bold
            //tft.setTextFont(bold ? 2 : 1);
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
            if (highlightNick && !nickHighlighted && nickPos != -1 && i == nickPos) {
                tft.setTextColor(TFT_YELLOW, bgColor); // Set both foreground and background color
                for (char nc : irc_nickname) {
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
                if (c == ' ') { // Handle spaces separately to ensure background color is applied (do we need this anyhmore since .trim() was removed?)
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

    // Ensure trailing spaces are displayed with background color (do we need this anymore since .trim() was removed?)
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



void displayLines() {
    tft.fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT - INPUT_LINE_HEIGHT, TFT_BLACK);

    int cursorY = STATUS_BAR_HEIGHT;
    int totalLinesHeight = 0;
    std::vector<int> lineHeights;

    // Calculate total height needed for all lines
    for (const String& line : lines) {
        int lineHeight = calculateLinesRequired(line) * CHAR_HEIGHT;
        lineHeights.push_back(lineHeight);
        totalLinesHeight += lineHeight;
    }

    // Remove lines from the top if they exceed the screen height
    while (totalLinesHeight > SCREEN_HEIGHT - STATUS_BAR_HEIGHT - INPUT_LINE_HEIGHT) {
        totalLinesHeight -= lineHeights.front();
        lines.erase(lines.begin());
        mentions.erase(mentions.begin());
        lineHeights.erase(lineHeights.begin());
    }

    // Render each line
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


void addLine(String senderNick, String message, String type, bool mention = false, uint16_t errorColor = TFT_WHITE, uint16_t reasonColor = TFT_WHITE) {
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
        senderNick = "ERROR"; // Probably a better way to handle this than emulating a NICK
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
                    playNotificationSound(); // Will need a check here in the future when we have the ability to turn on/off notification sounds...

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
        } else if (command == "QUIT" && line.indexOf(channel) != -1) {
            String senderNick = line.substring(1, line.indexOf('!'));
            addLine(senderNick, "", "quit");
        } else if (command == "NICK") {
            String prefix = line.startsWith(":") ? line.substring(1, firstSpace) : "";
            String newNick = line.substring(line.lastIndexOf(':') + 1);

            if (prefix.indexOf('!') == -1) { // Our own NICK changes
                addLine(irc_nickname, " -> " + newNick, "nick");
                irc_nickname = newNick; // Update global nick when we get confirmation
            } else { // Other peoples NICK change
                String oldNick = prefix.substring(0, prefix.indexOf('!'));
                addLine(oldNick, " -> " + newNick, "nick");
                if (oldNick == irc_nickname) {
                    irc_nickname = newNick; // Update global nick when we get confirmation
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
        } else if (command == "432") { // ERR_ERRONEUSNICKNAME
            addLine("ERROR", "ERR_ERRONEUSNICKNAME", "error", TFT_RED, TFT_DARKGREY);
        } else if (command == "433") { // ERR_NICKNAMEINUSE
            addLine("ERROR", "ERR_NICKNAMEINUSE", "error", TFT_RED, TFT_DARKGREY);
            irc_nickname = "ACID_" + String(random(1000, 9999)); // Generate a random nickname
            sendIRC("NICK " + irc_nickname);                     // Attempt to change to the random nickname
        }
    }
}


void handleKeyboardInput(char key) {
    lastActivityTime = millis(); // Update last activity time to reset the inactivity timer

    if (!screenOn) {
        turnOnScreen();
        return;
    }

    if (key == '\n' || key == '\r') { // Enter
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
    } else if (key == '\b') { // Backspace
        if (inputBuffer.length() > 0) {
            inputBuffer.remove(inputBuffer.length() - 1);
            displayInputLine();
        }
    } else if (inputBuffer.length() <= 510) { // Ensure we do not exceed 512 characters (IRC limit)
        inputBuffer += key;
        displayInputLine();
    }
}


void displayInputLine() {
    tft.fillRect(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT, SCREEN_WIDTH, INPUT_LINE_HEIGHT, TFT_BLACK);
    tft.setCursor(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);

    String displayInput = inputBuffer;
    int displayWidth = tft.textWidth(displayInput);
    int inputWidth = SCREEN_WIDTH - tft.textWidth("> ");
    
    // Allow the input to scroll if it exceeds the screen width
    while (displayWidth > inputWidth) {
        displayInput = displayInput.substring(1);
        displayWidth = tft.textWidth(displayInput);
    }

    // Add a visual indicator if the max input length is reached
    if (inputBuffer.length() >= 510)
        tft.setTextColor(TFT_RED);

    tft.print("> " + displayInput);
    tft.setTextColor(TFT_WHITE); // Reset text color
}


void updateStatusBar() {
    Serial.println("Updating status bar...");
    uint16_t darkerGrey = tft.color565(25, 25, 25);
    tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, darkerGrey);


    // Display the time
    struct tm timeinfo;
    char timeStr[9];
    if (!getLocalTime(&timeinfo)) {
        sprintf(timeStr, "12:00 AM"); // Default time if NTP sync fails
    } else {
        int hour = timeinfo.tm_hour;
        char ampm[] = "AM";
        if (hour == 0) {
            hour = 12;
        } else if (hour >= 12) {
            if (hour > 12)
                hour -= 12;
            strcpy(ampm, "PM"); // DREADED STRCPY X_X
        }
        sprintf(timeStr, "%02d:%02d %s", hour, timeinfo.tm_min, ampm);
    }
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, darkerGrey);
    tft.drawString(timeStr, 0, STATUS_BAR_HEIGHT / 2);

    // Display the WiFi signal strength
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

    // Display the battery level
    int batteryLevel = BL.getBatteryChargeLevel();
    char batteryStr[15];
    sprintf(batteryStr, "Batt: %d%%", batteryLevel);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_CYAN, darkerGrey);
    tft.drawString("Batt:", SCREEN_WIDTH - 40, STATUS_BAR_HEIGHT / 2);
    tft.setTextColor(getColorFromPercentage(batteryLevel), darkerGrey);
    tft.drawString(batteryStr + 5, SCREEN_WIDTH - 5, STATUS_BAR_HEIGHT / 2);
}


void printDeviceInfo() {
    Serial.println("Gathering device info...");
    tft.fillScreen(TFT_BLACK);

    // Get MAC Address
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    String macAddress = String(mac[0], HEX) + ":" + String(mac[1], HEX) + ":" + String(mac[2], HEX) + ":" + String(mac[3], HEX) + ":" + String(mac[4], HEX) + ":" + String(mac[5], HEX);

    // Get Chip Info
    uint32_t chipId = ESP.getEfuseMac();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    String chipInfo = String(chip_info.model) + " Rev " + String(chip_info.revision) + ", " + String(chip_info.cores) + " cores, " +  String(ESP.getCpuFreqMHz()) + " MHz";

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
    Serial.println("MCU: ESP32-S3FN16R8");
    Serial.println("LoRa Tranciever: Semtech SX1262 (915 MHz)"); // Need to set the frequency in pins.h
    Serial.println("LCD: ST7789 SPI"); // Update this
    Serial.println("Chip ID: " + String(chipId, HEX));
    Serial.println("MAC Address: " + macAddress);
    Serial.println("Chip Info: " + chipInfo);
    Serial.println("Battery:");
    Serial.println("  Pin Value: " + String(analogRead(34)));
    Serial.println("  Average Pin Value: " + String(BL.pinRead()));
    Serial.println("  Volts: " + String(BL.getBatteryVolts()));
    Serial.println("  Charge Level: " + String(BL.getBatteryChargeLevel()) + "%");
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
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_CYAN); tft.print("Battery:       "); tft.setTextColor(TFT_WHITE); tft.println(""); line++;
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  Pin Value:   "); tft.setTextColor(TFT_WHITE); tft.println(analogRead(34)); line++;
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  Avg Pin Val: "); tft.setTextColor(TFT_WHITE); tft.println(BL.pinRead()); line++;
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  Volts:       "); tft.setTextColor(TFT_WHITE); tft.println(BL.getBatteryVolts()); line++;
    tft.setCursor(0, line * 16); tft.setTextColor(TFT_YELLOW); tft.print("  Charge Level:"); tft.setTextColor(TFT_WHITE); tft.println(BL.getBatteryChargeLevel() + "%"); line++;
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


void wipeNVS() {
    esp_err_t err = nvs_flash_erase();
    if (err == ESP_OK)
        Serial.println("NVS flash erase successful.");
    else
        Serial.println("Error erasing NVS flash!");

    err = nvs_flash_init();
    if (err == ESP_OK)
        Serial.println("NVS flash init successful.");
    else
        Serial.println("Error initializing NVS flash!");
}