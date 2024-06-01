// src/wifi_mgr.cpp

#include "wifi_mgr.h"
#include "display.h"
#include "utilities.h"

std::vector<WiFiNetwork> wifiNetworks;
std::vector<WiFiProfile> wifiProfiles;
int selectedNetworkIndex = 0;
int currentProfileIndex = 0;
bool enteringPassword = false;
WiFiState wifiState = WIFI_STATE_DISCONNECTED;

void handleWiFiSelection(char key) {
    if (enteringPassword) {
        handlePasswordInput(key);
        return;
    }

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
            if (!connectToWiFi()) {
                // If connection fails, go back to the network selection
                tft.fillScreen(TFT_BLACK);
                tft.setTextSize(1);
                tft.setTextColor(TFT_WHITE);
                tft.setCursor(0, STATUS_BAR_HEIGHT);
                tft.println("Failed to connect. Select a network:");
                displayWiFiNetworks();
            }
        }
    }
}

void scanWiFiNetworks() {
    debugPrint("Initializing WiFi for scanning...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); // Disconnect from any previous connection

    debugPrint("Starting WiFi scan...");
    wifiState = WIFI_STATE_SCANNING;
    int n = WiFi.scanNetworks(true, true); // Start the scan
    debugPrint("Initial scan result: " + String(n));

    // Handle initial scan result
    if (n == WIFI_SCAN_FAILED) {
        debugPrint("WiFi scan failed");
        wifiState = WIFI_STATE_DISCONNECTED;
        return;
    } else if (n == WIFI_SCAN_RUNNING) {
        debugPrint("WiFi scan is running, waiting for completion...");
    }

    while (n == WIFI_SCAN_RUNNING) {
        delay(500);
        n = WiFi.scanNetworks();
        debugPrint("WiFi scan running, current result: " + String(n));
    }

    if (n < 0) {
        debugPrint("Error scanning WiFi networks: " + String(n));
        wifiState = WIFI_STATE_DISCONNECTED;
        return;
    }

    if (n == 0) {
        debugPrint("No networks found.");
    } else {
        debugPrint("Total number of networks found: " + String(n));
        wifiNetworks.clear();
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

    wifiState = WIFI_STATE_DISCONNECTED; // Reset state to allow connection attempts
}

void displayWiFiNetworks() {
    debugPrint("Displaying WiFi networks...");
    tft.fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN);

    tft.setCursor(0, STATUS_BAR_HEIGHT);
    tft.printf("#  CH  RSSI  ENC      SSID\n");
    tft.setTextColor(TFT_WHITE);

    int maxDisplayedLines = (SCREEN_HEIGHT - STATUS_BAR_HEIGHT - CHAR_HEIGHT) / (CHAR_HEIGHT + LINE_SPACING);
    int startIdx = selectedNetworkIndex >= maxDisplayedLines ? selectedNetworkIndex - maxDisplayedLines + 1 : 0;

    for (int i = startIdx; i < wifiNetworks.size() && i < startIdx + maxDisplayedLines; ++i) {
        displayWiFiNetwork(i, i - startIdx + 1);
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

    tft.setTextColor(rssiColor, TFT_BLACK);
    tft.printf("%-5d ", net.rssi);

    tft.setTextColor(index == selectedNetworkIndex ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    tft.printf("%-8s %s", net.encryption.c_str(), net.ssid.c_str());
}

void handlePasswordInput(char key) {
    if (key == '\n' || key == '\r') {
        password = inputBuffer;
        inputBuffer = "";
        enteringPassword = false;
        if (!connectToWiFi()) {
            // If connection fails, go back to the network selection
            tft.fillScreen(TFT_BLACK);
            tft.setTextSize(1);
            tft.setTextColor(TFT_WHITE);
            tft.setCursor(0, STATUS_BAR_HEIGHT);
            tft.println("Failed to connect. Select a network:");
            displayWiFiNetworks();
        } else {
            saveWiFiProfile(ssid, password);
        }
    } else if (key == '\b') {
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

bool connectToWiFi() {
    wifiState = WIFI_STATE_CONNECTING;
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
        wifiState = WIFI_STATE_CONNECTED;
        return true;
    } else {
        displayCenteredText("WIFI CONNECTION FAILED");
        debugPrint("Failed to connect to WiFi.");
        wifiState = WIFI_STATE_DISCONNECTED;
        ssid = "";
        password = "";
        return false;
    }
}

void saveWiFiProfile(const String& ssid, const String& password) {
    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();
}

void loadWiFiProfiles() {
    preferences.begin("wifi", false);
    String storedSSID = preferences.getString("ssid", "");
    String storedPassword = preferences.getString("password", "");
    preferences.end();

    if (!storedSSID.isEmpty() && !storedPassword.isEmpty()) {
        WiFiProfile profile;
        profile.ssid = storedSSID;
        profile.password = storedPassword;
        wifiProfiles.push_back(profile);
    }
}


