#pragma once

#include <vector>

#include <esp_wifi.h>
#include <WireGuard-ESP32.h>

#include "Display.h"
#include "Storage.h"
#include "Utilities.h"
#include "WiFi.h"

struct WiFiNetwork {
    int index;
    int channel;
    int rssi;
    String encryption;
    String ssid;
};

extern std::vector<WiFiNetwork> wifiNetworks;
extern int selectedNetworkIndex;
extern WireGuard wg;

void connectToWiFi(String ssid, String password);
void displayPasswordInputLine();
void displayWiFiNetworks();
void displayWiFiNetwork(int index, int displayIndex);
String getEncryptionType(wifi_auth_mode_t encryptionType);
void handlePasswordInput(char key);
void handleWiFiSelection(char key);
void initializeNetwork();
void randomizeMacAddress();
void scanWiFiNetworks();
void updateSelectedNetwork(int delta);
void wgConnect(const IPAddress& localIp, const char* privateKey, const char* endpointAddress, const char* publicKey, uint16_t endpointPort);
void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
