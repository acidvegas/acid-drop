#include "apps/AboutApp.h"

#include <esp_chip_info.h>
#include <esp_mac.h>

#include "board/Gps.h"
#include "board/Input.h"
#include "board/Power.h"
#include "core/Settings.h"
#include "irc/IrcClient.h"
#include "net/WifiService.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace aboutapp {
namespace {

lv_obj_t* s_page = nullptr;
lv_obj_t* s_body = nullptr;

String formatBytes(size_t bytes) {
    if (bytes < 1024)               return String(bytes) + " B";
    if (bytes < 1024 * 1024)        return String(bytes / 1024.0f, 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return String(bytes / 1024.0f / 1024.0f, 1) + " MB";
    return String(bytes / 1024.0f / 1024.0f / 1024.0f, 1) + " GB";
}

String uptime() {
    const uint32_t seconds = millis() / 1000;
    char text[24];
    snprintf(text, sizeof(text), "%lud %02luh %02lum %02lus",
             static_cast<unsigned long>(seconds / 86400),
             static_cast<unsigned long>((seconds % 86400) / 3600),
             static_cast<unsigned long>((seconds % 3600) / 60),
             static_cast<unsigned long>(seconds % 60));
    return text;
}

bool keyHook(uint32_t key) {
    if (key != LV_KEY_ESC) return false;
    ui::back();
    return true;
}

} // namespace

void create(lv_obj_t* parent) {
    s_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_page, 8, 0);
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_page, 6, 0);
    lv_obj_set_scroll_dir(s_page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_page, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* title = lv_label_create(s_page);
    lv_label_set_text(title, "ACID DROP");
    lv_obj_set_style_text_font(title, theme::uiFontLarge(), 0);
    lv_obj_set_style_text_color(title, theme::accent(), 0);

    s_body = lv_label_create(s_page);
    lv_obj_set_width(s_body, LV_PCT(100));
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_body, &acid_mono_10, 0);
    lv_obj_set_style_text_color(s_body, theme::text(), 0);

    input::setKeyHook(keyHook);
    tick();
}

void destroy() {
    input::clearKeyHook();
    s_page = nullptr;
    s_body = nullptr;
}

void tick() {
    if (!s_body) return;

    static uint32_t lastUpdate = 0;
    const uint32_t  now        = millis();
    if (lastUpdate != 0 && now - lastUpdate < 1000) return;
    lastUpdate = now;

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char macText[18];
    snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    String text;
    text += "Board     " + String(ACID_BOARD_TDECK_PLUS ? "T-Deck Plus" : "T-Deck") + "\n";
    text += "MCU       ESP32-S3 rev " + String(chip.revision) + ", " +
            String(chip.cores) + " cores @ " + String(getCpuFrequencyMhz()) + " MHz\n";
    text += "MAC       " + String(macText) + "\n";
    text += "Flash     " + formatBytes(ESP.getFlashChipSize()) + "\n";
    text += "PSRAM     " + formatBytes(ESP.getPsramSize() - ESP.getFreePsram()) + " / " +
            formatBytes(ESP.getPsramSize()) + "\n";
    text += "Heap      " + formatBytes(ESP.getHeapSize() - ESP.getFreeHeap()) + " / " +
            formatBytes(ESP.getHeapSize()) + "\n";
    text += "Sketch    " + formatBytes(ESP.getSketchSize()) + "\n";
    text += "Uptime    " + uptime() + "\n";
    text += "Battery   " + String(power::batteryPercent()) + "% (" +
            String(power::batteryMillivolts() / 1000.0f, 2) + " V)\n";
    text += "\n";
    text += "WiFi      " + (net::isConnected()
                            ? net::ssid() + " / " + net::ipAddress()
                            : String("not connected")) + "\n";
    text += "GPS       " + gps::summary() + "\n";
    text += "\n";
    text += "IRC       " + ui::irc().stateText() + "\n";
    text += "Server    " + settings::getText("irc_server") + ":" +
            String(settings::getInt("irc_port")) +
            (settings::getBool("irc_tls") ? " (TLS)" : "") + "\n";
    text += "Nick      " + ui::irc().nick() + "\n";
    text += "Windows   " + String(ui::irc().bufferCount()) + "\n";

    lv_label_set_text(s_body, text.c_str());
}

} // namespace aboutapp
