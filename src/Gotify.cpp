#include "Gotify.h"


WebsocketsClient gotifyClient;

const char* gotify_server = "ws://your.gotify.server.com/stream"; // Use ws:// or wss:// based on your server configuration
const char* gotify_token = "your_gotify_app_token";

unsigned long lastAttemptTime = 0;
const unsigned long reconnectInterval = 60000; // 1 minute


void onMessageCallback(WebsocketsMessage message) {
    Serial.println("Gotify Notification:");
    Serial.println(message.data());
    playNotificationSound();
}


void connectToGotify() {
    String url = String(gotify_server) + "?token=" + gotify_token;
    gotifyClient.onMessage(onMessageCallback);
    gotifyClient.connect(url);

    if (gotifyClient.available())
        Serial.println("Connected to Gotify WebSocket");
    else
        Serial.println("Failed to connect to Gotify WebSocket");
}


void loopGotifyWebSocket() {
    while (true) {
        if (!gotifyClient.available()) {
            unsigned long currentTime = millis();
            if (currentTime - lastAttemptTime > reconnectInterval) {
                Serial.println("Attempting to reconnect to Gotify WebSocket...");
                lastAttemptTime = currentTime;
                connectToGotify();
            }
        } else {
            gotifyClient.poll();
        }

        delay(10);
    }
}
