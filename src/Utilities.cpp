#include "Utilities.h"


Pangodream_18650_CL BL(BOARD_BAT_ADC, CONV_FACTOR, READS);


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


void setBrightness(uint8_t value) {
    static uint8_t level = 16;
    static uint8_t steps = 16;
    value = constrain(value, 0, steps); // Ensure the brightness value is within the valid range

    if (value == 0) {
        digitalWrite(BOARD_BL_PIN, 0);
        delay(3);
        level = 0;
        return;
    }

    if (level == 0) {
        digitalWrite(BOARD_BL_PIN, 1);
        level = steps;
        delayMicroseconds(30);
    }

    int from = steps - level;
    int to = steps - value;
    int num = (steps + to - from) % steps;
    for (int i = 0; i < num; i++) {
        digitalWrite(BOARD_BL_PIN, 0);
        digitalWrite(BOARD_BL_PIN, 1);
    }

    level = value;
}


void updateTimeFromNTP() {
    Serial.println("Syncing time with NTP server...");
    configTime(-5 * 3600, 3600, "pool.ntp.org", "north-america.pool.ntp.org", "time.nist.gov");

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


void printDeviceInfo() {
    Serial.println("Gathering device info...");

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

    Serial.println("MCU: ESP32-S3FN16R8");
    Serial.println("LoRa Tranciever: Semtech SX1262 (915 MHz)"); // Need to set the frequency in pins.h
    Serial.println("LCD: ST7789 SPI"); // Update this
    Serial.println("Chip ID: " + String(chipId, HEX));
    Serial.println("MAC Address: " + macAddress);
    Serial.println("Chip Info: " + chipInfo);
    Serial.println("Battery:");
    Serial.println("  Pin Value: " + String(analogRead(34)));
    //Serial.println("  Average Pin Value: " + String(BL.pinRead()));
    //Serial.println("  Volts: " + String(BL.getBatteryVolts()));
    //Serial.println("  Charge Level: " + String(BL.getBatteryChargeLevel()) + "%");
    Serial.println("Memory:");
    Serial.println("  Flash: " + flashInfo);
    Serial.println("  PSRAM: " + psramInfo);
    Serial.println("  Heap: " + heapInfo);
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi Info: " + wifiInfo);
        Serial.println("  SSID: " + wifiSSID);
        Serial.println("  Channel: " + wifiChannel);
        Serial.println("  Signal: " + wifiSignal);
        Serial.println("  Local IP: " + wifiLocalIP);
        Serial.println("  Gateway IP: " + wifiGatewayIP);
    }
}
