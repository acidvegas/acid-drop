#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <functional>
#include <vector>

// WiFi association, scanning and the clock sync that depends on it.

namespace net {

struct ScanResult {
    String           ssid;
    int32_t          rssi;
    uint8_t          channel;
    wifi_auth_mode_t auth;

    bool open() const { return auth == WIFI_AUTH_OPEN; }
    // 0-100, from the usual -50/-90 dBm window.
    uint8_t quality() const;
    String  authName() const;
};

void begin();
void loop();

void setEnabled(bool enabled);
bool enabled();

// Starts an association attempt. Saving stores the credentials for next boot.
void connect(const String& ssid, const String& password, bool save);
void disconnect();
bool isConnected();

// Progress of the association attempt, for the connecting dialog.
uint8_t connectAttempts();
void    cancelConnect();
String  statusText();

void    startScan();
bool    isScanning();
const std::vector<ScanResult>& scanResults();

String  ssid();
String  ipAddress();
int32_t rssi();
uint8_t quality();
String  macAddress();

// Kicks off an SNTP sync. Safe to call repeatedly; it will not re-sync more
// often than once an hour.
void syncClock(bool force = false);
bool clockSynced();

// Applies the timezone and DST settings to the C library.
void applyTimezone();

void applySettings();

extern std::function<void(bool connected)> onConnectionChanged;
extern std::function<void()>               onScanFinished;

} // namespace net
