#include "utilities.h"
#include "buffer.h"
#include "display.h"


Preferences preferences;
String ssid = "";
String password = "";
String nick = "";
bool debugEnabled = false;
bool screenOn = true;
bool enteringPassword = false;
unsigned long joinChannelTime = 0;
bool readyToJoinChannel = false;
unsigned long lastStatusUpdateTime = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 15000;
unsigned long lastTopicUpdateTime = 0;
unsigned long lastTopicDisplayTime = 0;
const unsigned long TOPIC_SCROLL_INTERVAL = 100;
const unsigned long TOPIC_DISPLAY_INTERVAL = 10000;
bool topicUpdated = false;
bool displayingTopic = false;
unsigned long lastActivityTime = 0;
const unsigned long INACTIVITY_TIMEOUT = 30000;
WiFiClientSecure client;
String currentTopic = "";
int topicOffset = 0;
std::map<String, uint32_t> nickColors;
String inputBuffer = "";

void debugPrint(String message) {
    if (debugEnabled) {
        Serial.println(message);
    }
}

uint16_t getColorFromPercentage(int rssi) {
    if (rssi > -50) return TFT_GREEN;
    else if (rssi > -60) return TFT_YELLOW;
    else if (rssi > -70) return TFT_ORANGE;
    else return TFT_RED;
}

int calculateLinesRequired(String message) {
    int linesRequired = 1;
    int lineWidth = 0;

    for (unsigned int i = 0; i < message.length(); i++) {
        char c = message[i];
        if (c == '\x03') {
            while (i < message.length() && (isdigit(message[i + 1]) || message[i + 1] == ',')) {
                i++;
                if (isdigit(message[i + 1])) {
                    i++;
                }
                if (message[i] == ',' && isdigit(message[i + 1])) {
                    i++;
                }
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

uint32_t generateRandomColor() {
    return tft.color565(random(0, 255), random(0, 255), random(0, 255));
}

void saveWiFiCredentials() {
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    debugPrint("WiFi credentials saved.");
}

void connectToWiFi() {
    WiFi.begin(ssid.c_str(), password.c_str());
    debugPrint("Connecting to WiFi...");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
        delay(500);
        displayCenteredText("CONNECTING TO " + ssid);
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        debugPrint("Connected to WiFi network: " + ssid);
        displayCenteredText("CONNECTED TO " + ssid);
        updateTimeFromNTP();
        saveWiFiCredentials();
    } else {
        displayCenteredText("WIFI CONNECTION FAILED");
        debugPrint("Failed to connect to WiFi.");
    }
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

void handleKeyboardInput(char key) {
    if (key == '\n' || key == '\r') { // Enter
        if (inputBuffer.startsWith("/")) {
            handleCommand(inputBuffer);
        } else {
            sendIRC("PRIVMSG " + buffers[currentBufferIndex].channel + " :" + inputBuffer);
            addLine(nick, inputBuffer, "message");
        }
        inputBuffer = "";
        displayInputLine();
        lastActivityTime = millis();
        if (!screenOn) {
            turnOnScreen();
        }
    } else if (key == '\b') { // Backspace
        if (inputBuffer.length() > 0) {
            inputBuffer.remove(inputBuffer.length() - 1);
            displayInputLine();
            lastActivityTime = millis();
            if (!screenOn) {
                turnOnScreen();
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

void printDeviceInfo() {
    // Get MAC Address
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    String macAddress = String(mac[0], HEX) + ":" + String(mac[1], HEX) + ":" + String(mac[2], HEX) + ":" + String(mac[3], HEX) + ":" + String(mac[4], HEX) + ":" + String(mac[5], HEX);

    // Get Chip Info
    uint32_t chipId = ESP.getEfuseMac(); // Unique ID
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
