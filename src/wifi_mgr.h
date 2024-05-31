#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <vector>
#include <String.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

struct WiFiNetwork {
    int index;
    int channel;
    int rssi;
    String encryption;
    String ssid;
};

extern std::vector<WiFiNetwork> wifiNetworks;
extern int selectedNetworkIndex;

void handleWiFiSelection(char key);
void scanWiFiNetworks();
void displayWiFiNetworks();
void displayWiFiNetwork(int index, int displayIndex);
void handlePasswordInput(char key);
void displayPasswordInputLine();
void updateSelectedNetwork(int delta);

#endif
