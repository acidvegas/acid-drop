#include "net/WifiService.h"

#include <esp_wifi.h>
#include <time.h>

#include "board/Gps.h"
#include "core/Log.h"
#include "core/Settings.h"

namespace net {

std::function<void(bool)> onConnectionChanged;
std::function<void()>     onScanFinished;

namespace {

constexpr const char* TAG = "wifi";

bool     s_enabled      = false;
uint32_t s_attemptAt    = 0;    // millis of the last WiFi.begin()
uint8_t  s_attempts     = 0;    // association attempts for the current target
String   s_lastError;           // why the last attempt failed, for the UI
bool     s_connected    = false;
bool     s_scanning     = false;
bool     s_clockSynced  = false;
uint32_t s_lastSyncAt   = 0;
uint32_t s_nextRetryAt  = 0;
String   s_pendingSsid;
String   s_pendingPassword;

std::vector<ScanResult> s_results;

void randomizeMac() {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    for (int i = 3; i < 6; i++) mac[i] = random(0, 256);
    mac[0] = (mac[0] & 0xFE) | 0x02;   // locally administered, unicast
    esp_wifi_set_mac(WIFI_IF_STA, mac);
    LOG_I(TAG, "MAC randomized to %02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// The supplicant's reason code is the only thing that distinguishes "wrong
// password" from "AP out of range" from "AP pushed us off", and Arduino only
// prints it at a debug level we do not ship.
const char* disconnectReason(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:         return "auth expired";
        case WIFI_REASON_AUTH_LEAVE:          return "auth leave";
        case WIFI_REASON_ASSOC_EXPIRE:        return "association expired";
        case WIFI_REASON_ASSOC_TOOMANY:       return "AP has too many clients";
        case WIFI_REASON_NOT_AUTHED:          return "not authenticated";
        case WIFI_REASON_NOT_ASSOCED:         return "not associated";
        case WIFI_REASON_ASSOC_LEAVE:         return "AP disconnected us";
        case WIFI_REASON_MIC_FAILURE:         return "MIC failure";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "handshake timeout - wrong password?";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "group key timeout";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS:  return "key mismatch - wrong password?";
        case WIFI_REASON_BEACON_TIMEOUT:      return "beacon timeout - out of range?";
        case WIFI_REASON_NO_AP_FOUND:         return "no AP with that name found";
        case WIFI_REASON_AUTH_FAIL:           return "authentication rejected";
        case WIFI_REASON_ASSOC_FAIL:          return "association rejected";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:   return "handshake timeout";
        case WIFI_REASON_CONNECTION_FAIL:     return "connection failed";
        default:                              return "unknown";
    }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            s_connected = true;
            s_attempts  = 0;
            LOG_I(TAG, "connected to %s as %s",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            syncClock(true);
            if (onConnectionChanged) onConnectionChanged(true);
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            const uint8_t reason = info.wifi_sta_disconnected.reason;
            LOG_W(TAG, "disconnected: reason %u (%s)", reason, disconnectReason(reason));

            // An authentication rejection will not fix itself. Retrying it
            // forever keeps the radio busy and stops scanning working, so give
            // up after a few and leave the reason on screen.
            const bool authProblem = reason == WIFI_REASON_AUTH_EXPIRE ||
                                     reason == WIFI_REASON_AUTH_FAIL ||
                                     reason == WIFI_REASON_MIC_FAILURE ||
                                     reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                                     reason == WIFI_REASON_IE_IN_4WAY_DIFFERS;
            if (authProblem && s_attempts >= 4) {
                LOG_E(TAG, "giving up on %s: the AP keeps rejecting us (%s)",
                      s_pendingSsid.isEmpty() ? settings::getText("wifi_ssid").c_str()
                                              : s_pendingSsid.c_str(),
                      disconnectReason(reason));
                s_lastError   = disconnectReason(reason);
                s_nextRetryAt = 0;          // stop the retry loop
                WiFi.disconnect(false, false);
                break;
            }

            if (s_connected) {
                s_connected = false;
                if (onConnectionChanged) onConnectionChanged(false);
            }
            // Retry on the configured cadence rather than immediately.
            s_nextRetryAt = millis() + settings::getInt("wifi_retry") * 1000UL;
            break;
        }

        case ARDUINO_EVENT_WIFI_SCAN_DONE:
            break;

        default:
            break;
    }
}

void collectScanResults() {
    const int16_t count = WiFi.scanComplete();
    if (count < 0) return;   // still running, or nothing was started

    s_results.clear();
    s_results.reserve(count);

    for (int16_t i = 0; i < count; i++) {
        ScanResult entry;
        entry.ssid    = WiFi.SSID(i);
        entry.rssi    = WiFi.RSSI(i);
        entry.channel = WiFi.channel(i);
        entry.auth    = WiFi.encryptionType(i);

        // A hidden network cannot be joined by name from this list, so listing
        // it is only clutter.
        if (entry.ssid.isEmpty()) continue;

        s_results.push_back(entry);
    }

    WiFi.scanDelete();
    s_scanning = false;

    LOG_I(TAG, "scan found %u networks", (unsigned)s_results.size());
    if (onScanFinished) onScanFinished();
}

} // namespace

uint8_t ScanResult::quality() const {
    if (rssi <= -90) return 0;
    if (rssi >= -50) return 100;
    return static_cast<uint8_t>((rssi + 90) * 100 / 40);
}

String ScanResult::authName() const {
    switch (auth) {
        case WIFI_AUTH_OPEN:            return "open";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
        default:                        return "?";
    }
}

void begin() {
    WiFi.onEvent(onWiFiEvent);
    applyTimezone();
    applySettings();

    if (!s_enabled) return;

    if (settings::getBool("wifi_auto")) {
        const String ssid = settings::getText("wifi_ssid");
        const String pass = settings::getText("wifi_pass");
        if (!ssid.isEmpty()) connect(ssid, pass, false);
    }
}

void applySettings() {
    setEnabled(settings::getBool("wifi_enable"));
    if (s_enabled) {
        WiFi.setSleep(settings::getBool("wifi_ps"));
        const String host = settings::getText("dev_name");
        if (!host.isEmpty()) WiFi.setHostname(host.c_str());
    }
}

void setEnabled(bool enabled) {
    if (enabled == s_enabled) return;
    s_enabled = enabled;

    if (enabled) {
        WiFi.mode(WIFI_STA);
        if (settings::getBool("wifi_macrnd")) randomizeMac();
        LOG_I(TAG, "radio on");
    } else {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        if (s_connected) {
            s_connected = false;
            if (onConnectionChanged) onConnectionChanged(false);
        }
        LOG_I(TAG, "radio off");
    }
}

bool enabled()     { return s_enabled; }
bool isConnected() { return s_connected; }

void connect(const String& ssid, const String& password, bool save) {
    if (!s_enabled) setEnabled(true);

    s_pendingSsid     = ssid;
    s_pendingPassword = password;

    if (save) {
        settings::setText("wifi_ssid", ssid);
        settings::setText("wifi_pass", password);
    }

    LOG_I(TAG, "associating with %s", ssid.c_str());
    s_attempts  = 1;
    s_lastError = "";
    s_attemptAt = millis();
    WiFi.begin(ssid.c_str(), password.isEmpty() ? nullptr : password.c_str());
    s_nextRetryAt = millis() + 20000;
}

void disconnect() {
    WiFi.disconnect();
    s_connected = false;
    if (onConnectionChanged) onConnectionChanged(false);
}

void startScan() {
    if (!s_enabled) setEnabled(true);
    if (s_scanning) return;

    s_scanning = true;
    s_results.clear();

    // Stop any association attempt first. The supplicant retries roughly once
    // a second on a rejected password, and a scan cannot get the radio while
    // that is going on - which looks exactly like "scanning finds nothing".
    if (!s_connected) WiFi.disconnect(false, false);

    // Hold off the reconnect timer for the duration; whichever network the user
    // picks from the results supersedes it anyway.
    s_nextRetryAt = millis() + 30000;

    WiFi.scanNetworks(true /* async */, false /* skip hidden */);
    LOG_I(TAG, "scanning");
}

bool isScanning() { return s_scanning; }

const std::vector<ScanResult>& scanResults() { return s_results; }

void loop() {
    // A scan and an association attempt cannot both have the radio. Reassociating
    // mid-scan aborts the scan and stalls the UI, so the retry timer stands down
    // until the scan has finished.
    if (s_scanning) {
        collectScanResults();
        return;
    }

    if (!s_enabled || s_connected) return;
    if (s_nextRetryAt == 0) return;
    if (static_cast<int32_t>(millis() - s_nextRetryAt) < 0) return;

    // WiFi.begin() is expensive and blocks for a noticeable slice of a frame.
    // Calling it again while the supplicant is still working on the previous
    // attempt just restarts it, so the association never converges and the UI
    // hitches every few seconds. Give each attempt time to actually fail.
    const uint32_t minimumGap = 12000;
    if (s_attemptAt != 0 && millis() - s_attemptAt < minimumGap) {
        s_nextRetryAt = s_attemptAt + minimumGap;
        return;
    }

    const String ssid = s_pendingSsid.isEmpty() ? settings::getText("wifi_ssid")
                                                : s_pendingSsid;
    if (ssid.isEmpty()) return;

    const String pass = s_pendingPassword.isEmpty() ? settings::getText("wifi_pass")
                                                    : s_pendingPassword;

    const uint32_t retryMs = settings::getInt("wifi_retry") * 1000UL;
    s_attemptAt   = millis();
    s_nextRetryAt = s_attemptAt + (retryMs > minimumGap ? retryMs : minimumGap);

    if (s_attempts < 255) s_attempts++;
    LOG_I(TAG, "retrying %s (attempt %u)", ssid.c_str(), s_attempts);
    WiFi.begin(ssid.c_str(), pass.isEmpty() ? nullptr : pass.c_str());
}

uint8_t connectAttempts() { return s_attempts; }

String lastError() { return s_lastError; }

void cancelConnect() {
    LOG_I(TAG, "association cancelled");
    s_pendingSsid     = "";
    s_pendingPassword = "";
    s_nextRetryAt     = 0;
    s_attempts        = 0;
    s_lastError       = "";
    WiFi.disconnect();
}

String statusText() {
    switch (WiFi.status()) {
        case WL_CONNECTED:       return "connected";
        case WL_NO_SSID_AVAIL:   return "network not found";
        case WL_CONNECT_FAILED:  return "rejected - check the password";
        case WL_CONNECTION_LOST: return "connection lost";
        case WL_IDLE_STATUS:     return "starting";
        case WL_DISCONNECTED:    return "associating";
        default:                 return "working";
    }
}

String  ssid()       { return WiFi.SSID(); }
String  ipAddress()  { return s_connected ? WiFi.localIP().toString() : String("0.0.0.0"); }
int32_t rssi()       { return s_connected ? WiFi.RSSI() : 0; }
String  macAddress() { return WiFi.macAddress(); }

uint8_t quality() {
    if (!s_connected) return 0;
    const int32_t value = WiFi.RSSI();
    if (value <= -90) return 0;
    if (value >= -50) return 100;
    return static_cast<uint8_t>((value + 90) * 100 / 40);
}

void applyTimezone() {
    // The C library wants a POSIX TZ string, where the offset sign is inverted
    // relative to how everyone says it out loud: UTC-5 is written "UTC5".
    const int32_t offsetMinutes = settings::getInt("tz_offset");
    const bool    dst           = settings::getBool("dst");

    const int32_t inverted = -offsetMinutes;
    const int32_t hours    = inverted / 60;
    const int32_t minutes  = abs(inverted % 60);

    char tz[48];
    if (dst) {
        // Generic US-style rule; good enough for the northern hemisphere and
        // harmless where DST is not observed, because NTP supplies the truth.
        snprintf(tz, sizeof(tz), "UTC%+ld:%02ldUTD,M3.2.0,M11.1.0",
                 static_cast<long>(hours), static_cast<long>(minutes));
    } else {
        snprintf(tz, sizeof(tz), "UTC%+ld:%02ld",
                 static_cast<long>(hours), static_cast<long>(minutes));
    }

    setenv("TZ", tz, 1);
    tzset();
    LOG_I(TAG, "timezone set to %s", tz);
}

void syncClock(bool force) {
    if (!s_connected) {
        // No network, but a GNSS fix carries the time too.
        const uint32_t fromGps = gps::unixTime();
        if (fromGps > 0) {
            timeval now{static_cast<time_t>(fromGps), 0};
            settimeofday(&now, nullptr);
            s_clockSynced = true;
            LOG_I(TAG, "clock set from GNSS");
        }
        return;
    }

    if (!settings::getBool("ntp_enable")) return;
    if (!force && s_lastSyncAt != 0 && millis() - s_lastSyncAt < 3600000UL) return;

    s_lastSyncAt = millis();
    const String server = settings::getText("ntp_server");
    configTzTime(getenv("TZ"), server.c_str(), "pool.ntp.org", "time.nist.gov");
    LOG_I(TAG, "NTP sync requested from %s", server.c_str());

    // sntp updates the clock asynchronously; note success on the next check.
    s_clockSynced = false;
}

bool clockSynced() {
    if (s_clockSynced) return true;
    // Anything past 2021 means something actually set the clock.
    s_clockSynced = time(nullptr) > 1609459200;
    return s_clockSynced;
}

} // namespace net
