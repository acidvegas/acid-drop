#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Preferences.h>
#include "boot.h"
#include "pins.h"
#include "buffer.h"
#include "display.h"
#include "irc.h"
#include "utilities.h"
#include "wifi_mgr.h"
#include "config.h"

TFT_eSPI tft = TFT_eSPI();



void setup() {
    // Initialize I2C to read keyboard input
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    
    // Check if 'd' key is pressed during boot
    char keyPressed = getKeyboardInput();
    if (keyPressed == 'd') {
        debugEnabled = true;
        Serial.begin(115200);
        Serial.println("Booting device...");
    }

    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.begin();
    tft.setRotation(1);
    tft.invertDisplay(1);

    debugPrint("TFT initialized");

    displayXBM();
    delay(3000);
    displayCenteredText("SCANNING WIFI");
    delay(1000);

    preferences.begin("wifi", false);  // Start preferences with namespace "wifi"
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");

    if (ssid != "" && password != "") {
        connectToWiFi();
    } else {
        scanWiFiNetworks();
        displayWiFiNetworks();
    }

    randomSeed(analogRead(0));
    int randomNum = random(1000, 10000);
    nick = "ACID_" + String(randomNum);

    Buffer defaultBuffer;
    defaultBuffer.channel = "#comms";
    buffers.push_back(defaultBuffer);

    defaultBuffer.channel = "#superbowl";
    buffers.push_back(defaultBuffer);
}

void loop() {
    unsigned long currentMillis = millis();
    
    // Update the status bar if enough time has passed
    if (currentMillis - lastStatusUpdateTime >= STATUS_UPDATE_INTERVAL) {
        updateStatusBar();
        lastStatusUpdateTime = currentMillis;
    }

    if (ssid.isEmpty() && !enteringPassword) {
        char incoming = getKeyboardInput();
        if (incoming != 0) {
            handleWiFiSelection(incoming);
            lastActivityTime = millis();
        }
    } else if (enteringPassword) {
        char incoming = getKeyboardInput();
        if (incoming != 0) {
            handlePasswordInput(incoming);
            lastActivityTime = millis();
        }
    } else {
        if (displayingTopic && currentMillis - lastTopicDisplayTime > 5000) { // Display topic for 5 seconds
            displayingTopic = false;
            lastStatusUpdateTime = currentMillis;
            updateStatusBar();
        } else if (!displayingTopic && currentMillis - lastStatusUpdateTime > 10000) { // Show topic every 10 seconds
            displayingTopic = true;
            lastTopicDisplayTime = currentMillis;
            updateTopicOnStatusBar();
        }

        if (client.connected()) {
            handleIRC();
        } else {
            if (WiFi.status() == WL_CONNECTED) {
                displayCenteredText("CONNECTING TO " + String(server));
                if (connectToIRC()) {
                    displayCenteredText("CONNECTED TO " + String(server));
                    sendIRC("NICK " + String(nick));
                    sendIRC("USER " + String(user) + " 0 * :" + String(realname));
                } else {
                    displayCenteredText("CONNECTION FAILED");
                    delay(1000);
                }
            } else {
                displayCenteredText("RECONNECTING TO WIFI");
                WiFi.begin(ssid.c_str(), password.c_str());
            }
        }

        if (readyToJoinChannel && currentMillis >= joinChannelTime) {
            joinChannels();
            tft.fillScreen(TFT_BLACK);
            updateStatusBar();
            readyToJoinChannel = false;
        }

        char incoming = getKeyboardInput();
        if (incoming != 0) {
            handleKeyboardInput(incoming);
            lastActivityTime = millis(); // Reset activity timer
        }

        // Check for inactivity
        if (screenOn && millis() - lastActivityTime > INACTIVITY_TIMEOUT) {
            turnOffScreen(); // Turn off screen and backlight
        }
    }
}
