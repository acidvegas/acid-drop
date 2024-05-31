#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <TFT_eSPI.h>
#include "buffer.h"
#include "wifi_mgr.h"
#include "utilities.h"
#include "pins.h"

#include <map>

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define CHAR_HEIGHT   10
#define LINE_SPACING  0

#define INPUT_LINE_HEIGHT (CHAR_HEIGHT + LINE_SPACING)
#define STATUS_BAR_HEIGHT 10
#define MAX_LINES ((SCREEN_HEIGHT - INPUT_LINE_HEIGHT - STATUS_BAR_HEIGHT) / (CHAR_HEIGHT + LINE_SPACING))

#include "pins.h"
#include <Pangodream_18650_CL.h>
#define BOARD_BAT_ADC 4
#define CONV_FACTOR 1.8
#define READS 20

extern TFT_eSPI tft;
extern std::map<String, uint32_t> nickColors;

void displayLines();
void displayCenteredText(String text);
void displayInputLine();
void displayXBM();
void updateStatusBar();
void updateTimeOnStatusBar();
void updateWiFiOnStatusBar();
void updateBatteryOnStatusBar();
void updateTopicOnStatusBar();
void turnOffScreen();
void turnOnScreen();
void displayDeviceInfo();
int renderFormattedMessage(String message, int x, int y, bool mention);

#endif
