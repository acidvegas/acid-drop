// src/wifi_mgr.h

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <vector>
#include <String.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

struct WiFiNetwork {
    int index;
    int channel;
    int rssi;
    String encryption;
    String ssid;
};

struct WiFiProfile {
    String ssid;
    String password;
};

enum WiFiState {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED
};

extern std::vector<WiFiNetwork> wifiNetworks;
extern std::vector<WiFiProfile> wifiProfiles;
extern int selectedNetworkIndex;
extern int currentProfileIndex;
extern bool enteringPassword;
extern WiFiState wifiState;

void handleWiFiSelection(char key);
void scanWiFiNetworks();
void displayWiFiNetworks();
void displayWiFiNetwork(int index, int displayIndex);
void handlePasswordInput(char key);
void displayPasswordInputLine();
void updateSelectedNetwork(int delta);
bool connectToWiFi();
void saveWiFiProfile(const String& ssid, const String& password);
void loadWiFiProfiles();

#endif
