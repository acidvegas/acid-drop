#pragma once

#include <TFT_eSPI.h>
#include <map>
#include <vector>
#include <WiFi.h>
#include <time.h>

// Constants
#define CHAR_HEIGHT       10
#define LINE_SPACING      0
#define STATUS_BAR_HEIGHT 10
#define INPUT_LINE_HEIGHT (CHAR_HEIGHT + LINE_SPACING)
#define MAX_LINES         ((SCREEN_HEIGHT - INPUT_LINE_HEIGHT - STATUS_BAR_HEIGHT) / (CHAR_HEIGHT + LINE_SPACING))

// External variables
extern bool infoScreen;
extern bool configScreen;
extern bool screenOn;
extern const char* channel;
extern unsigned long infoScreenStartTime;
extern unsigned long configScreenStartTime;
extern unsigned long lastStatusUpdateTime;
extern unsigned long lastActivityTime;
extern String inputBuffer;

extern std::vector<String> lines;
extern std::vector<bool> mentions;
extern std::map<String, uint32_t> nickColors;

extern TFT_eSPI tft;

// Function declarations
void addLine(String senderNick, String message, String type, bool mention = false, uint16_t errorColor = TFT_WHITE, uint16_t reasonColor = TFT_WHITE);
int calculateLinesRequired(String message);
void displayCenteredText(String text);
void displayInputLine();
void displayLines();
void displayXBM();
uint32_t generateRandomColor();
uint16_t getColorFromCode(int colorCode);
uint16_t getColorFromPercentage(int percentage);
void handleKeyboardInput(char key);
void parseAndDisplay(String line);
int renderFormattedMessage(String message, int cursorY, int lineHeight, bool highlightNick = false);
void turnOffScreen();
void turnOnScreen();
void updateStatusBar();
