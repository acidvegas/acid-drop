#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Pangodream_18650_CL.h> // Power management
#include <WiFi.h>

// Local includes
#include "utilities.h"

#define LVGL_BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT)

#define TOUCH_MODULES_GT911
#include "TouchLib.h"

TFT_eSPI tft = TFT_eSPI();
TouchLib *touch = NULL;
uint8_t touchAddress = GT911_SLAVE_ADDRESS2;

SemaphoreHandle_t xSemaphore = NULL;

lv_obj_t *batteryLabel;
lv_obj_t *timeLabel;
lv_obj_t *wifiLabel;
lv_obj_t *volumeLabel;
lv_obj_t *bluetoothLabel;
lv_obj_t *usbLabel;
lv_obj_t *infoScreen = NULL;

LV_FONT_DECLARE(lv_font_montserrat_12); // Default font

Pangodream_18650_CL BL(BOARD_BAT_ADC, CONV_FACTOR, READS);

void connectToWiFi() {
    WiFi.begin("change", "me"); // SSID & PASSWORD

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }

    Serial.println("Connected to WiFi");
    updateStatusBar();
}

void createButton(lv_obj_t *parent, const char *symbol, int x, int y, lv_event_cb_t event_cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 70, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, x, y);
    
    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 10);
    lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
    lv_style_set_bg_color(&style_btn, lv_palette_lighten(LV_PALETTE_GREEN, 1));
    lv_style_set_bg_grad_color(&style_btn, lv_palette_darken(LV_PALETTE_GREEN, 3));
    lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_VER);
    lv_style_set_border_color(&style_btn, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_border_width(&style_btn, 2);

    lv_obj_add_style(btn, &style_btn, 0);
    lv_obj_t *btnLabel = lv_label_create(btn);
    
    lv_label_set_text(btnLabel, symbol);
    
    lv_obj_set_style_text_color(btnLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(btnLabel, &lv_font_montserrat_28, 0);
    lv_obj_center(btnLabel);
    lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
}

void createHomeButtons() {
    int screenWidth = 320;
    int buttonSpacing = screenWidth / 4;

    createButton(lv_scr_act(), LV_SYMBOL_HOME,    -buttonSpacing * 1.5, 90, infoButtonEventHandler);
    createButton(lv_scr_act(), LV_SYMBOL_LIST,    -buttonSpacing * 0.5, 90, applicationsButtonEventHandler);
    createButton(lv_scr_act(), LV_SYMBOL_BELL,     buttonSpacing * 0.5, 90, NULL);
    createButton(lv_scr_act(), LV_SYMBOL_SETTINGS, buttonSpacing * 1.5, 90, settingsButtonEventHandler);
}

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        tft.startWrite();
        tft.setAddrWindow(area->x1, area->y1, w, h);
        tft.pushColors((uint16_t *)&color_p->full, w * h, false);
        tft.endWrite();
        lv_disp_flush_ready(disp);
        xSemaphoreGive(xSemaphore);
    }
}

static bool getTouch(int16_t &x, int16_t &y) {
    uint8_t rotation = tft.getRotation();
    if (!touch->read())
        return false;
    TP_Point t = touch->getPoint(0);
    switch (rotation) {
        case 1:
            x = t.y;
            y = tft.height() - t.x;
            break;
        case 2:
            x = tft.width() - t.x;
            y = tft.height() - t.y;
            break;
        case 3:
            x = tft.width() - t.y;
            y = t.x;
            break;
        case 0:
        default:
            x = t.x;
            y = t.y;
    }
    Serial.printf("R:%d X:%d Y:%d\n", rotation, x, y);
    return true;
}

void infoButtonEventHandler(lv_event_t *e) {
    lv_obj_clean(lv_scr_act());
    
    setupStatusBar();

    infoScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(infoScreen, 320, 220);
    lv_obj_align(infoScreen, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *closeBtn = lv_btn_create(infoScreen);
    lv_obj_set_size(closeBtn, 50, 30);
    lv_obj_align(closeBtn, LV_ALIGN_TOP_LEFT, 10, 10);

    lv_obj_t *closeLabel = lv_label_create(closeBtn);
    lv_label_set_text(closeLabel, LV_SYMBOL_CLOSE);
    lv_obj_set_style_bg_color(closeBtn, lv_palette_main(LV_PALETTE_RED), 0);
    //lv_obj_set_style_text_color(closeLabel, lv_color_white, 0);
    lv_obj_center(closeLabel);

    lv_obj_add_event_cb(closeBtn, [](lv_event_t *e) {
        lv_obj_clean(lv_scr_act());
        setupStatusBar();
        createHomeButtons();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *infoTitle = lv_label_create(infoScreen);
    lv_label_set_text(infoTitle, "INFORMATION");
    lv_obj_set_style_text_font(infoTitle, &lv_font_montserrat_16, 0);
    lv_obj_align(infoTitle, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *infoLabel = lv_label_create(infoScreen);
    lv_label_set_text_fmt(infoLabel,
        "Device Info:\n"
        "Firmware Version: %s\n"
        "Battery Level: %d%%\n"
        "WiFi Signal: %s\n",
        "1.0", BL.getBatteryChargeLevel(), WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    lv_obj_align(infoLabel, LV_ALIGN_TOP_LEFT, 10, 50);
}


void applicationsButtonEventHandler(lv_event_t *e) {
    lv_obj_clean(lv_scr_act());
    
    setupStatusBar();

    lv_obj_t *applicationsScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(applicationsScreen, 320, 220);
    lv_obj_align(applicationsScreen, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(applicationsScreen, LV_OBJ_FLAG_SCROLLABLE);

    // Apply a small padding of 5 pixels to the screen container
    lv_obj_set_style_pad_left(applicationsScreen, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_right(applicationsScreen, 5, LV_PART_MAIN);

    lv_obj_t *closeBtn = lv_btn_create(applicationsScreen);
    lv_obj_set_size(closeBtn, 30, 30);
    lv_obj_align(closeBtn, LV_ALIGN_TOP_LEFT, 0, 5);  // Slightly adjusted to align vertically

    lv_obj_t *closeLabel = lv_label_create(closeBtn);
    lv_label_set_text(closeLabel, LV_SYMBOL_LEFT);
    lv_obj_set_style_bg_color(closeBtn, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_center(closeLabel);

    lv_obj_add_event_cb(closeBtn, [](lv_event_t *e) {
        lv_obj_clean(lv_scr_act());
        setupStatusBar();
        createHomeButtons();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *titleLabel = lv_label_create(applicationsScreen);
    lv_label_set_text(titleLabel, "APPLICATIONS");
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
    lv_obj_align_to(titleLabel, closeBtn, LV_ALIGN_OUT_RIGHT_MID, 10, 0);  // Align vertically with the close button

    const char *buttonLabels[] = {
        "CHATGPT", "IRC", "GOTIFY",
        "SSH", "WARDRIVE", "EVIL PORTAL",
        "BLE ATTACK", "PORT SCAN", "MESHTASTIC"
    };

    int button_width = 100;  // Adjust width to account for new padding
    int button_height = 50;
    int start_x = 0;  // Start with small padding from the edge
    int start_y = 40;  // Start below the title/close button

    for (int i = 0; i < 9; ++i) {
        int row = i / 3;
        int col = i % 3;
        lv_obj_t *btn = lv_btn_create(applicationsScreen);
        lv_obj_set_size(btn, button_width, button_height);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, start_x + col * (button_width + 5), start_y + row * (button_height + 5));

        static lv_style_t style_btn;
        lv_style_init(&style_btn);
        lv_style_set_radius(&style_btn, 10);
        lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
        lv_style_set_bg_color(&style_btn, lv_palette_lighten(LV_PALETTE_GREEN, 1));
        lv_style_set_bg_grad_color(&style_btn, lv_palette_darken(LV_PALETTE_GREEN, 3));
        lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_VER);
        lv_style_set_border_color(&style_btn, lv_palette_darken(LV_PALETTE_GREY, 4));
        lv_style_set_border_width(&style_btn, 2);

        lv_obj_add_style(btn, &style_btn, 0);

        lv_obj_t *btnLabel = lv_label_create(btn);
        lv_label_set_text(btnLabel, buttonLabels[i]);

        lv_obj_set_style_text_font(btnLabel, &lv_font_montserrat_10, 0);
        lv_obj_center(btnLabel);
    }
}

void settingsButtonEventHandler(lv_event_t *e) {
    lv_obj_clean(lv_scr_act());

    setupStatusBar();

    lv_obj_t *settingsScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(settingsScreen, 320, 220);
    lv_obj_align(settingsScreen, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(settingsScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *closeBtn = lv_btn_create(settingsScreen);
    lv_obj_set_size(closeBtn, 30, 30);
    lv_obj_align(closeBtn, LV_ALIGN_TOP_LEFT, 0, 5);

    lv_obj_t *closeLabel = lv_label_create(closeBtn);
    lv_label_set_text(closeLabel, LV_SYMBOL_LEFT);
    lv_obj_set_style_bg_color(closeBtn, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_center(closeLabel);

    lv_obj_add_event_cb(closeBtn, [](lv_event_t *e) {
        lv_obj_clean(lv_scr_act());
        setupStatusBar();
        createHomeButtons();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *titleLabel = lv_label_create(settingsScreen);
    lv_label_set_text(titleLabel, "SETTINGS");
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
    lv_obj_align_to(titleLabel, closeBtn, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    lv_obj_t *list = lv_list_create(settingsScreen);
    lv_obj_set_size(list, 320, 160); // Adjust size to fit below title and close button
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 50); // Move list down to avoid overlap

    static lv_style_t style_list;
    lv_style_init(&style_list);
    lv_style_set_bg_opa(&style_list, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_list, 0);
    lv_style_set_pad_left(&style_list, 10);
    lv_style_set_pad_right(&style_list, 10);

    lv_obj_add_style(list, &style_list, LV_PART_MAIN);

    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_bg_opa(&style_btn, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_btn, 0);
    lv_style_set_pad_all(&style_btn, 0);

    static lv_style_t style_btn_pressed;
    lv_style_init(&style_btn_pressed);
    lv_style_set_text_color(&style_btn_pressed, lv_palette_main(LV_PALETTE_GREEN));

    const char *settingsLabels[] = {
        "POWER", "DISPLAY", "SOUND", "BLUETOOTH", "LORA", "WIFI", "PREFERENCES"
    };

    for (int i = 0; i < sizeof(settingsLabels) / sizeof(settingsLabels[0]); ++i) {
        lv_obj_t *btn = lv_btn_create(list);
        lv_obj_add_style(btn, &style_btn, 0);
        lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_height(btn, 40);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, settingsLabels[i]);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_center(label);

        lv_obj_t *separator = lv_obj_create(list);
        lv_obj_set_size(separator, 300, 1);
        lv_obj_set_style_bg_color(separator, lv_color_hex(0x444444), 0);
        lv_obj_set_style_pad_ver(separator, 5, 0);  // Padding to space out the separator
    }
}


void loop() {
    lv_task_handler();
    delay(100);
}

void scanDevices(TwoWire *w) {
    uint8_t err, addr;
    int nDevices = 0;
    uint32_t start = 0;

    for (addr = 1; addr < 127; addr++) {
        start = millis();
        w->beginTransmission(addr); delay(2);
        err = w->endTransmission();

        if (err == 0) {
            nDevices++;
            Serial.print("I2C device found at address 0x");
            if (addr < 16)
                Serial.print("0");
            Serial.print(addr, HEX);
            Serial.println(" !");

            if (addr == GT911_SLAVE_ADDRESS2) {
                touchAddress = GT911_SLAVE_ADDRESS2;
                Serial.println("Find GT911 Drv Slave address: 0x14");
            } else if (addr == GT911_SLAVE_ADDRESS1) {
                touchAddress = GT911_SLAVE_ADDRESS1;
                Serial.println("Find GT911 Drv Slave address: 0x5D");
            }
        } else if (err == 4) {
            Serial.print("Unknown error at address 0x");
            if (addr < 16)
                Serial.print("0");
            Serial.println(addr, HEX);
        }
    }

    if (nDevices == 0)
        Serial.println("No I2C devices found\n");
}

void setup() {
    Serial.begin(115200);
    Serial.println("T-DECK factory");

    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);

    // Set CS on all SPI buses to high level during initialization
    pinMode(BOARD_TFT_CS, OUTPUT);
    digitalWrite(BOARD_TFT_CS, HIGH);

    pinMode(BOARD_SPI_MISO, INPUT_PULLUP);
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    pinMode(BOARD_TOUCH_INT, OUTPUT);
    digitalWrite(BOARD_TOUCH_INT, HIGH);

    pinMode(BOARD_BL_PIN, OUTPUT);
    digitalWrite(BOARD_BL_PIN, HIGH);

    xSemaphore = xSemaphoreCreateBinary();
    assert(xSemaphore);
    xSemaphoreGive(xSemaphore);

    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

    pinMode(BOARD_TOUCH_INT, INPUT); 
    delay(20);

    // Scan for touch devices
    scanDevices(&Wire);
    touch = new TouchLib(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, touchAddress);
    touch->init();

    setupLvgl();
    setupStatusBar();

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);

    xTaskCreatePinnedToCore(statusBarUpdateTask, "Status Bar Task", 4096, NULL, 1, NULL, 1);

    connectToWiFi();

    createHomeButtons();
}

void setupLvgl() {
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t *buf = (lv_color_t *)ps_malloc(LVGL_BUFFER_SIZE * sizeof(lv_color_t));
    if (!buf) {
        Serial.println("Memory allocation failed!");
        delay(5000);
        assert(buf);
    }

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LVGL_BUFFER_SIZE);

    // Initialize the display
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Initialize the input device
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

void setupStatusBar() {
    lv_obj_t *statusBar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(statusBar, 320, 20);

    static lv_style_t style_bg;
    lv_style_init(&style_bg);
    lv_style_set_bg_color(&style_bg, lv_color_hex(0x333333));
    lv_style_set_bg_grad_color(&style_bg, lv_color_hex(0x0A0A0A));
    lv_style_set_bg_grad_dir(&style_bg, LV_GRAD_DIR_VER);
    lv_style_set_radius(&style_bg, 0);
    lv_style_set_border_side(&style_bg, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_color(&style_bg, lv_color_hex(0x111111));
    lv_style_set_border_width(&style_bg, 1);
    lv_style_set_pad_all(&style_bg, 0);

    lv_obj_add_style(statusBar, &style_bg, 0);
    lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(statusBar, LV_ALIGN_TOP_MID, 0, 0);

    timeLabel = lv_label_create(statusBar);
    lv_label_set_text(timeLabel, "3:50");
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(timeLabel, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *bellLabel = lv_label_create(statusBar);
    lv_label_set_text(bellLabel, LV_SYMBOL_BELL);
    lv_obj_set_style_text_font(bellLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(bellLabel, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_align_to(bellLabel, timeLabel, LV_ALIGN_OUT_RIGHT_MID, 5, 0);

    lv_obj_t *rightContainer = lv_obj_create(statusBar);
    lv_obj_set_size(rightContainer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rightContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(rightContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightContainer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rightContainer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rightContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(rightContainer, LV_ALIGN_RIGHT_MID, 0, 0);

    wifiLabel = lv_label_create(rightContainer);
    lv_label_set_text(wifiLabel, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifiLabel, &lv_font_montserrat_12, 0);

    bluetoothLabel = lv_label_create(rightContainer);
    lv_label_set_text(bluetoothLabel, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(bluetoothLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(bluetoothLabel, lv_palette_main(LV_PALETTE_BLUE), 0);

    usbLabel = lv_label_create(rightContainer);
    lv_label_set_text(usbLabel, LV_SYMBOL_USB);
    lv_obj_set_style_text_font(usbLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(usbLabel, lv_color_hex(0x9B30FF), 0);

    lv_obj_t *sdCardLabel = lv_label_create(rightContainer);
    lv_label_set_text(sdCardLabel, LV_SYMBOL_SD_CARD);
    lv_obj_set_style_text_font(sdCardLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sdCardLabel, lv_color_hex(0xC71585), 0);

    lv_obj_t *gpsLabel = lv_label_create(rightContainer);
    lv_label_set_text(gpsLabel, LV_SYMBOL_GPS);
    lv_obj_set_style_text_font(gpsLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(gpsLabel, lv_palette_main(LV_PALETTE_TEAL), 0);

    volumeLabel = lv_label_create(rightContainer);
    lv_label_set_text(volumeLabel, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(volumeLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(volumeLabel, lv_color_white(), 0);

    batteryLabel = lv_label_create(rightContainer);
    lv_obj_set_style_text_font(batteryLabel, &lv_font_montserrat_12, 0);

    updateStatusBar();
}

void statusBarUpdateTask(void *pvParameters) {
    while (true) {
        updateStatusBar();
        vTaskDelay(pdMS_TO_TICKS(15000)); // 15 seconds
    }
}

static void touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    int16_t x, y;
    bool touched = getTouch(x, y);
    if (touched) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
        Serial.printf("Touched at X: %d, Y: %d\n", x, y);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void updateStatusBar() {
    float volts = BL.getBatteryVolts();
    int level = BL.getBatteryChargeLevel();
    const char *batterySymbol;
    lv_color_t batteryColor;

    if (volts >= MIN_USB_VOL) {
        batterySymbol = LV_SYMBOL_CHARGE;
        batteryColor = lv_palette_main(LV_PALETTE_BLUE);
    } else if (level > 80) {
        batterySymbol = LV_SYMBOL_BATTERY_FULL;
        batteryColor = lv_palette_main(LV_PALETTE_GREEN);
    } else if (level > 60) {
        batterySymbol = LV_SYMBOL_BATTERY_3;
        batteryColor = lv_palette_main(LV_PALETTE_LIGHT_GREEN);
    } else if (level > 40) {
        batterySymbol = LV_SYMBOL_BATTERY_2;
        batteryColor = lv_palette_main(LV_PALETTE_YELLOW);
    } else if (level > 20) {
        batterySymbol = LV_SYMBOL_BATTERY_1;
        batteryColor = lv_palette_main(LV_PALETTE_ORANGE);
    } else {
        batterySymbol = LV_SYMBOL_BATTERY_EMPTY;
        batteryColor = lv_palette_main(LV_PALETTE_RED);
    }

    lv_label_set_text_fmt(batteryLabel, "%s %d%%", batterySymbol, level);
    lv_obj_set_style_text_color(batteryLabel, batteryColor, 0);
    Serial.printf("Battery: %s %d%% (%.2fV)\n", batterySymbol, level, volts);

    if (WiFi.status() == WL_CONNECTED) {
        int32_t rssi = WiFi.RSSI();
        lv_color_t wifiColor;
        if (rssi > -50)
            wifiColor = lv_palette_main(LV_PALETTE_GREEN);
        else if (rssi > -60)
            wifiColor = lv_palette_main(LV_PALETTE_LIGHT_GREEN);
        else if (rssi > -70)
            wifiColor = lv_palette_main(LV_PALETTE_YELLOW);
        else
            wifiColor = lv_palette_main(LV_PALETTE_ORANGE);
        lv_label_set_text(wifiLabel, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(wifiLabel, wifiColor, 0);
        Serial.printf("WiFi: %s %d dBm\n", LV_SYMBOL_WIFI, rssi);
    } else {
        lv_label_set_text(wifiLabel, "");
        Serial.println("WiFi: Disconnected");
    }

    // There is a better way to detect USB power, but this is a simple way
    if (volts >= MIN_USB_VOL)
        lv_obj_clear_flag(usbLabel, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(usbLabel, LV_OBJ_FLAG_HIDDEN);
}
