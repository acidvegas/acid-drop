#include "board/Ble.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "core/Log.h"
#include "core/Settings.h"

namespace ble {
namespace {

constexpr const char* TAG = "ble";

// Nordic UART Service, so a phone terminal app can talk to the device.
constexpr const char* kServiceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kRxUuid      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kTxUuid      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

bool           s_enabled   = false;
bool           s_connected = false;
BLEServer*     s_server    = nullptr;
BLECharacteristic* s_tx    = nullptr;
String         s_name;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        s_connected = true;
        LOG_I(TAG, "central connected");
    }
    void onDisconnect(BLEServer* server) override {
        s_connected = false;
        LOG_I(TAG, "central disconnected");
        server->startAdvertising();
    }
};

ServerCallbacks s_callbacks;

} // namespace

void begin() {
    setEnabled(settings::getBool("ble_enable"));
}

void setEnabled(bool enabled) {
    if (enabled == s_enabled) return;

    if (enabled) {
        s_name = settings::getText("ble_name");
        if (s_name.isEmpty()) s_name = "acid-drop";

        BLEDevice::init(s_name.c_str());

        s_server = BLEDevice::createServer();
        s_server->setCallbacks(&s_callbacks);

        BLEService* service = s_server->createService(kServiceUuid);

        s_tx = service->createCharacteristic(kTxUuid, BLECharacteristic::PROPERTY_NOTIFY);
        service->createCharacteristic(kRxUuid, BLECharacteristic::PROPERTY_WRITE);

        service->start();

        BLEAdvertising* advertising = BLEDevice::getAdvertising();
        advertising->addServiceUUID(kServiceUuid);
        advertising->setScanResponse(true);
        BLEDevice::startAdvertising();

        s_enabled = true;
        LOG_I(TAG, "advertising as '%s'", s_name.c_str());
    } else {
        BLEDevice::deinit(true);
        s_server    = nullptr;
        s_tx        = nullptr;
        s_connected = false;
        s_enabled   = false;
        LOG_I(TAG, "radio off");
    }
}

bool enabled()   { return s_enabled; }
bool connected() { return s_connected; }

String advertisedName() { return s_name; }

} // namespace ble
