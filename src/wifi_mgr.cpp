#include "wifi_mgr.h"
#include "display.h"
#include "utilities.h"

std::vector<WiFiNetwork> wifiNetworks;
int selectedNetworkIndex = 0;

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
        connectToWiFi();
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
