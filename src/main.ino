// Standard includes
#include <time.h>

// Local includes
#include "bootScreen.h"
#include "Lora.h"
#include "pins.h"
#include "Storage.h"
#include "Speaker.h"
#include "Utilities.h"
#include "Display.h"
#include "Network.h"
#include "IRC.h"

// Timing constants
const unsigned long STATUS_UPDATE_INTERVAL = 15000; // 15 seconds
const unsigned long INACTIVITY_TIMEOUT = 30000;     // 30 seconds


// Main functions ---------------------------------------------------------------------------------
void setup() {
    // Initialize serial communication
    Serial.begin(115200);

    // Wait for the serial monitor to open
    //while (!Serial);

    Serial.println("Booting device...");

    // Give power to the board peripherals
    pinMode(BOARD_POWERON, OUTPUT); 
    digitalWrite(BOARD_POWERON, HIGH);

    // Give power to the screen
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    setBrightness(8); // Set the screen brightness to 50%)

    // Give power to the SD card
    //setupSD();
    //mountSD();
    
    // Turn on power to the radio
    //setupRadio();
    
    // Start the I2C bus for the keyboard
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

    // Initialize the screen
    tft.begin();
    tft.setRotation(1);
    tft.invertDisplay(1);
    Serial.println("TFT initialized");

    // Display the boot screen
    displayXBM();
    
    // Initialize the preferences
    loadPreferences();

    // Initialize the speaker
    setupI2S(); // Do we want to keep this open or uninstall after each use to keep resources free?
    const char* rtttl_boot = "TakeOnMe:d=4,o=4,b=500:8f#5,8f#5,8f#5,8d5,8p,8b,8p,8e5,8p,8e5,8p,8e5,8g#5,8g#5,8a5,8b5,8a5,8a5,8a5,8e5,8p,8d5,8p,8f#5,8p,8f#5,8p,8f#5,8e5,8e5,8f#5,8e5,8f#5,8f#5,8f#5,8d5,8p,8b,8p,8e5,8p,8e5,8p,8e5,8g#5,8g#5,8a5,8b5,8a5,8a5,8a5,8e5,8p,8d5,8p,8f#5,8p,8f#5,8p,8f#5,8e5,8e5";
    playRTTTL(rtttl_boot);

    // Setup the WiFi
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("acid-drop"); // Turn into a preference
    WiFi.onEvent(WiFiEvent);
    randomizeMacAddress();

    // Connect to WiFi if credentials are stored, otherwise scan for networks
    if (wifi_ssid.length() > 0 && wifi_password.length() > 0) {
        Serial.println("Stored WiFi credentials found");
        connectToWiFi(wifi_ssid, wifi_password);
    } else {
        scanWiFiNetworks();
    }
}


void loop() {
    // Handle the info screen if it is active (from /info command)
    if (infoScreen) {
        if (millis() - infoScreenStartTime > 10000) { // 10 seconds
            infoScreen = false;
            tft.fillScreen(TFT_BLACK);
            displayLines(); // Redraw the previous buffer
        }
    } else if (configScreen) {
        if (millis() - configScreenStartTime > 10000) { // 10 seconds
            configScreen = false;
            tft.fillScreen(TFT_BLACK);
            displayLines(); // Redraw the previous buffer
        }
    } else {
        // Handle keyboard input for WiFi if the SSID is empty still (aka not connected)
        if (wifi_ssid.length() == 0) {
            char incoming = getKeyboardInput();
            if (incoming != 0) {
                handleWiFiSelection(incoming);
                lastActivityTime = millis(); // Reset activity timer
            }
        } else {
            // Handle status bar updating on an interval
            if (millis() - lastStatusUpdateTime > STATUS_UPDATE_INTERVAL) {
                updateStatusBar();
                lastStatusUpdateTime = millis();
            }

            // Handle IRC data
            if (client && client->connected()) {
                handleIRC();
            } else {
                // Connect to IRC if not connected (or reconnect to wifi if disconnected)
                if (WiFi.status() == WL_CONNECTED) {
                    displayCenteredText("CONNECTING TO " + String(irc_server));
                    if (connectToIRC()) {
                        delay(2000); // Delay to allow the connection to establish
                        Serial.println("Connected to IRC!");
                        displayCenteredText("CONNECTED TO " + String(irc_server));
                        Serial.println("Connecting to IRC with " + irc_nickname + " (" + irc_username + ") [" + irc_realname + "]");
                        sendIRC("NICK " + String(irc_nickname));
                        sendIRC("USER " + String(irc_username) + " 0 * :" + String(irc_realname));
                    } else {
                        Serial.println("Failed to connect to IRC");
                        displayCenteredText("CONNECTION FAILED");
                        delay(1000);
                    }
                } else {
                    displayCenteredText("RECONNECTING TO WIFI");
                    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str()); // Need to handle this better
                }
            }

            // Join channel after a delay
            if (readyToJoinChannel && millis() >= joinChannelTime) {
                tft.fillScreen(TFT_BLACK);
                updateStatusBar();
                sendIRC("JOIN " + String(irc_channel));
                readyToJoinChannel = false;
            }

            // Handle keyboard input (for IRC)
            char incoming = getKeyboardInput();
            if (incoming != 0) {
                handleKeyboardInput(incoming);
                lastActivityTime = millis();
            }

            // Handle inactivity timeout
            if (screenOn && millis() - lastActivityTime > INACTIVITY_TIMEOUT)
                turnOffScreen();
        }
    }
}