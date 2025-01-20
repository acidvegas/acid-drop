#include "Storage.h"


Preferences preferences;

// Config variables
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
        preferences.putString("irc_channel", "#superbowl");
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


bool mountSD() {
    if (SD.begin(BOARD_SDCARD_CS, SPI, 800000U)) {
        uint8_t cardType = SD.cardType();

        if (cardType == CARD_NONE) {
            Serial.println("No SD card attached");
            return false;
        } else {
            Serial.print("SD Card Type: ");
            if (cardType == CARD_MMC)
                Serial.println("MMC");
            else if (cardType == CARD_SD)
                Serial.println("SDSC");
            else if (cardType == CARD_SDHC)
                Serial.println("SDHC");
            else
                Serial.println("UNKNOWN");

            uint32_t cardSize  = SD.cardSize()   / (1024 * 1024);
            uint32_t cardTotal = SD.totalBytes() / (1024 * 1024);
            uint32_t cardUsed  = SD.usedBytes()  / (1024 * 1024);
            Serial.printf("SD Card Size: %lu MB\n", cardSize);
            Serial.printf("Total space: %lu MB\n",  cardTotal);
            Serial.printf("Used space: %lu MB\n",   cardUsed);

            return true;
        }
    }

    return false;
}


void setupSD() {
    pinMode(BOARD_SDCARD_CS, OUTPUT);
    digitalWrite(BOARD_SDCARD_CS, HIGH);
    pinMode(BOARD_SPI_MISO, INPUT_PULLUP);
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);
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
