#include "Network.h"


std::vector<WiFiNetwork> wifiNetworks;
int selectedNetworkIndex = 0;

WireGuard wg;


void connectToWiFi(String ssid, String password) {
    wifiNetworks.clear();
    Serial.println("Connecting to WiFi network: " + ssid);
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        displayCenteredText("CONNECTING TO " + ssid);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        displayCenteredText("CONNECTED TO " + ssid);

        updateTimeFromNTP();

        preferences.begin("config", false);
        preferences.putString("wifi_ssid", ssid);
        preferences.putString("wifi_password", password);
        preferences.end();
        Serial.println("Stored WiFi credentials updated");

        wifi_ssid = ssid;
        wifi_password = password;
    } else {
        Serial.println("Failed to connect to WiFi network: " + ssid);
        displayCenteredText("WIFI CONNECTION FAILED");
        
        preferences.begin("config", false);
        preferences.putString("wifi_ssid", "");
        preferences.putString("wifi_password", "");
        preferences.end();
        Serial.println("Stored WiFi credentials removed");

        wifi_ssid = "";
        wifi_password = "";

        scanWiFiNetworks();
    }
}


void displayPasswordInputLine() {
    tft.fillRect(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT, SCREEN_WIDTH, INPUT_LINE_HEIGHT, TFT_BLACK);
    tft.setCursor(0, SCREEN_HEIGHT - INPUT_LINE_HEIGHT);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.print("> " + inputBuffer);
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

    if (index == selectedNetworkIndex)
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
    else
        tft.setTextColor(TFT_WHITE, TFT_BLACK);

    WiFiNetwork net = wifiNetworks[index];

    uint16_t rssiColor = getColorFromPercentage((int32_t)net.rssi);
    tft.printf("%-2d %-3d ", net.index, net.channel);

    tft.setTextColor(rssiColor, TFT_BLACK);
    tft.printf("%-5d ", net.rssi);

    tft.setTextColor(index == selectedNetworkIndex ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    tft.printf("%-8s %s", net.encryption.c_str(), net.ssid.c_str());
}


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


void handlePasswordInput(char key) {
    if (key == '\n' || key == '\r') {
        wifi_password = inputBuffer;
        inputBuffer = "";
        connectToWiFi(wifi_ssid, wifi_password);
    } else if (key == '\b') {
        if (inputBuffer.length() > 0) {
            inputBuffer.remove(inputBuffer.length() - 1);
            displayPasswordInputLine();
        }
    } else if (inputBuffer.length() < 63) {
        inputBuffer += key;
        displayPasswordInputLine();
    }
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

            while (true) {
                char incoming = getKeyboardInput();
                if (incoming != 0) {
                    handlePasswordInput(incoming);
                    if (incoming == '\n' || incoming == '\r')
                        break;
                }
            }
        } else {
            wifi_password = "";
            connectToWiFi(wifi_ssid, wifi_password);
        }
    }
}


void initializeNetwork() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("acid-drop"); // Turn into a preference
    WiFi.onEvent(WiFiEvent);
    randomizeMacAddress();
}


void randomizeMacAddress() {
    Serial.println("Current MAC Address: " + WiFi.macAddress());

    uint8_t new_mac[6];

    for (int i = 0; i < 6; ++i)
        new_mac[i] = random(0x00, 0xFF);

    if (esp_wifi_set_mac(WIFI_IF_STA, new_mac) == ESP_OK)
        Serial.println("New MAC Address: " + WiFi.macAddress());
    else
        Serial.println("Failed to set new MAC Address");
}


void scanWiFiNetworks() {
    Serial.println("Scanning for WiFi networks...");
    displayCenteredText("SCANNING WIFI");
    delay(1000);

    wifiNetworks.clear();

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

    for (int i = 0; i < n && i < 100; i++) {
        WiFiNetwork net;
        net.index = i + 1;
        net.channel = WiFi.channel(i);
        net.rssi = WiFi.RSSI(i);
        net.encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured";
        String ssid = WiFi.SSID(i).substring(0, 32);
        net.ssid = ssid;
        wifiNetworks.push_back(net);
    }

    displayWiFiNetworks();
}


void updateSelectedNetwork(int delta) {
    int newIndex = selectedNetworkIndex + delta;

    if (newIndex >= 0 && newIndex < wifiNetworks.size()) {
        selectedNetworkIndex = newIndex;
        displayWiFiNetworks();
    }
}


void wgConnect(const IPAddress& localIp, const char* privateKey, const char* endpointAddress, const char* publicKey, uint16_t endpointPort) {
    wg.begin(localIp, privateKey, endpointAddress, publicKey, endpointPort);
}


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