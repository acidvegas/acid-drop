#ifndef UTILITIES_H
#define UTILITIES_H

#include <Preferences.h>
#include <map>
#include "wifi_mgr.h"
#include "irc.h"
#include <Wire.h>


extern Preferences preferences;
extern String ssid;
extern String password;
extern String nick;
extern bool debugEnabled;
extern bool screenOn;
extern bool enteringPassword;
extern unsigned long joinChannelTime;
extern bool readyToJoinChannel;
extern unsigned long lastStatusUpdateTime;
extern const unsigned long STATUS_UPDATE_INTERVAL;
extern unsigned long lastTopicUpdateTime;
extern unsigned long lastTopicDisplayTime;
extern const unsigned long TOPIC_SCROLL_INTERVAL;
extern const unsigned long TOPIC_DISPLAY_INTERVAL;
extern bool topicUpdated;
extern bool displayingTopic;
extern unsigned long lastActivityTime;
extern const unsigned long INACTIVITY_TIMEOUT;
extern WiFiClientSecure client;
extern String currentTopic;
extern int topicOffset;
extern std::map<String, uint32_t> nickColors;
extern String inputBuffer;

uint16_t getColorFromPercentage(int rssi);
int calculateLinesRequired(String message);
void debugPrint(String message);
void updateTimeFromNTP();
char getKeyboardInput();
uint32_t generateRandomColor();
void saveWiFiCredentials();
void connectToWiFi();
void printDeviceInfo();
uint16_t getColorFromCode(int code);
void handleKeyboardInput(char key);



#endif
