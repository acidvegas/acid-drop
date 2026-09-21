#include "apps/WifiApp.h"

#include "board/Input.h"
#include "core/Settings.h"
#include "net/WifiService.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace wifiapp {
namespace {

lv_obj_t* s_page      = nullptr;
lv_obj_t* s_status    = nullptr;
lv_obj_t* s_list      = nullptr;
lv_obj_t* s_prompt    = nullptr;
lv_obj_t* s_scanButton = nullptr;

// The connecting dialog. It stays up until the association succeeds, gives up,
// or is cancelled, so there is always something on screen saying what the
// radio is doing.
lv_obj_t* s_connectBox    = nullptr;
lv_obj_t* s_connectTitle  = nullptr;
lv_obj_t* s_connectDetail = nullptr;
lv_obj_t* s_connectAction = nullptr;   // label inside the right-hand button
String    s_connectSsid;
bool      s_connectSettled = false;    // connected or given up

constexpr uint8_t kMaxAttempts = 5;

String s_pendingSsid;
bool   s_listDirty = false;

void rebuildList();
void showConnectDialog(const String& ssid);
void closeConnectDialog();

void closePrompt() {
    if (s_prompt) {
        lv_obj_delete(s_prompt);
        s_prompt = nullptr;
    }
}

void askForPassword(const String& ssid, bool secured) {
    s_pendingSsid = ssid;

    if (!secured) {
        net::connect(ssid, "", true);
        showConnectDialog(ssid);
        return;
    }

    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_clickable(overlay, true);
    s_prompt = overlay;

    lv_obj_t* card = lv_obj_create(overlay);
    theme::stylePanel(card);
    lv_obj_set_width(card, LV_PCT(88));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text_fmt(title, "Password for %s", ssid.c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_color(title, theme::accent(), 0);

    static lv_obj_t* s_field;
    s_field = lv_textarea_create(card);
    lv_textarea_set_one_line(s_field, true);
    lv_textarea_set_password_mode(s_field, true);
    lv_obj_set_width(s_field, LV_PCT(100));
    lv_group_add_obj(input::group(), s_field);
    lv_group_focus_obj(s_field);

    lv_obj_t* row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 34);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_scrollable(row, false);

    lv_obj_t* cancel = lv_button_create(row);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(theme::kSurfaceAlt), 0);
    lv_obj_t* cancelLabel = lv_label_create(cancel);
    lv_label_set_text(cancelLabel, "Cancel");
    lv_obj_center(cancelLabel);
    lv_obj_add_event_cb(cancel, [](lv_event_t*) { closePrompt(); }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(input::group(), cancel);

    lv_obj_t* join = lv_button_create(row);
    lv_obj_set_style_bg_color(join, theme::accent(), 0);
    lv_obj_t* joinLabel = lv_label_create(join);
    lv_label_set_text(joinLabel, "Join");
    lv_obj_set_style_text_color(joinLabel, lv_color_hex(theme::kBackground), 0);
    lv_obj_center(joinLabel);
    lv_group_add_obj(input::group(), join);

    lv_obj_add_event_cb(join, [](lv_event_t*) {
        const String password(lv_textarea_get_text(s_field));
        const String target = s_pendingSsid;
        closePrompt();
        net::connect(target, password, true);
        showConnectDialog(target);
    }, LV_EVENT_CLICKED, nullptr);
}

void closeConnectDialog() {
    if (s_connectBox) {
        lv_obj_delete(s_connectBox);
        s_connectBox = nullptr;
    }
    s_connectTitle = s_connectDetail = s_connectAction = nullptr;
    s_connectSettled = false;
}

void showConnectDialog(const String& ssid) {
    closeConnectDialog();
    s_connectSsid    = ssid;
    s_connectSettled = false;

    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_clickable(overlay, true);
    s_connectBox = overlay;

    lv_obj_t* card = lv_obj_create(overlay);
    theme::stylePanel(card);
    lv_obj_set_width(card, LV_PCT(88));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);

    s_connectTitle = lv_label_create(card);
    lv_label_set_text_fmt(s_connectTitle, "Connecting to %s", ssid.c_str());
    lv_label_set_long_mode(s_connectTitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_connectTitle, LV_PCT(100));
    lv_obj_set_style_text_color(s_connectTitle, theme::accent(), 0);
    lv_obj_set_style_text_font(s_connectTitle, theme::uiFont(), 0);

    s_connectDetail = lv_label_create(card);
    lv_label_set_text(s_connectDetail, "starting");
    lv_label_set_long_mode(s_connectDetail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_connectDetail, LV_PCT(100));
    lv_obj_set_style_text_color(s_connectDetail, theme::textDim(), 0);
    lv_obj_set_style_text_font(s_connectDetail, theme::uiFontSmall(), 0);

    lv_obj_t* row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 34);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_scrollable(row, false);

    lv_obj_t* button = lv_button_create(row);
    lv_obj_set_style_bg_color(button, lv_color_hex(theme::kSurfaceAlt), 0);
    s_connectAction = lv_label_create(button);
    lv_label_set_text(s_connectAction, "Stop");
    lv_obj_center(s_connectAction);
    lv_group_add_obj(input::group(), button);
    lv_group_focus_obj(button);

    // One button: it stops the attempt while it is running, and dismisses the
    // dialog once there is a result.
    lv_obj_add_event_cb(button, [](lv_event_t*) {
        if (!s_connectSettled) net::cancelConnect();
        closeConnectDialog();
    }, LV_EVENT_CLICKED, nullptr);
}

void updateConnectDialog() {
    if (!s_connectBox || s_connectSettled) return;

    if (net::isConnected()) {
        s_connectSettled = true;
        lv_label_set_text_fmt(s_connectTitle, "Connected to %s", net::ssid().c_str());
        lv_label_set_text_fmt(s_connectDetail, "%s  -  signal %u%%",
                              net::ipAddress().c_str(), net::quality());
        lv_obj_set_style_text_color(s_connectTitle, theme::accent(), 0);
        lv_label_set_text(s_connectAction, "Done");
        return;
    }

    const uint8_t attempts = net::connectAttempts();

    if (attempts > kMaxAttempts) {
        s_connectSettled = true;
        net::cancelConnect();
        lv_label_set_text_fmt(s_connectTitle, "Could not connect to %s", s_connectSsid.c_str());
        lv_label_set_text_fmt(s_connectDetail, "Gave up after %u attempts: %s",
                              kMaxAttempts, net::statusText().c_str());
        lv_obj_set_style_text_color(s_connectTitle, lv_color_hex(theme::kDanger), 0);
        lv_label_set_text(s_connectAction, "Close");
        return;
    }

    lv_label_set_text_fmt(s_connectDetail, "Attempt %u of %u  -  %s",
                          attempts ? attempts : 1, kMaxAttempts,
                          net::statusText().c_str());
}

void networkEventCb(lv_event_t* event) {
    const size_t index = reinterpret_cast<size_t>(lv_event_get_user_data(event));
    const auto& results = net::scanResults();
    if (index >= results.size()) return;
    askForPassword(results[index].ssid, !results[index].open());
}

void rebuildList() {
    if (!s_list) return;
    lv_obj_clean(s_list);

    const auto& results = net::scanResults();
    for (size_t i = 0; i < results.size(); i++) {
        const auto& network = results[i];

        lv_obj_t* row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        theme::styleRow(row);
        lv_obj_set_clickable(row, true);
        lv_obj_set_scrollable(row, false);
        lv_group_add_obj(input::group(), row);

        lv_obj_t* name = lv_label_create(row);
        lv_label_set_text(name, network.ssid.c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 200);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* detail = lv_label_create(row);
        lv_label_set_text_fmt(detail, "%s %u%%",
                              network.open() ? "open" : network.authName().c_str(),
                              network.quality());
        lv_obj_set_style_text_font(detail, theme::uiFontSmall(), 0);
        lv_obj_set_style_text_color(detail, theme::signalColor(network.quality()), 0);
        lv_obj_align(detail, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_add_event_cb(row, networkEventCb, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
    }

    if (results.empty()) {
        lv_obj_t* empty = lv_label_create(s_list);
        lv_label_set_text(empty, net::isScanning()
                                 ? LV_SYMBOL_REFRESH "  Scanning for networks..."
                                 : "No networks found. Tap Scan to look again.");
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_color(empty,
                                    net::isScanning() ? theme::accent() : theme::textDim(), 0);
    }
}

bool keyHook(uint32_t key) {
    if (key != LV_KEY_ESC) return false;
    if (s_connectBox) { closeConnectDialog(); return true; }
    if (s_prompt)     { closePrompt(); return true; }
    ui::back();
    return true;
}

} // namespace

void create(lv_obj_t* parent) {
    s_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_page, 6, 0);
    lv_obj_set_style_pad_row(s_page, 6, 0);
    lv_obj_set_scrollable(s_page, false);

    lv_obj_t* header = ui::createAppHeader(s_page, "WiFi");

    s_status = lv_label_create(header);
    lv_obj_set_style_text_font(s_status, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(s_status, theme::textDim(), 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_status, 120);
    lv_obj_align(s_status, LV_ALIGN_LEFT_MID, 96, 0);

    lv_obj_t* scan = lv_button_create(header);
    lv_obj_set_size(scan, 70, 24);
    lv_obj_set_style_bg_color(scan, theme::accent(), 0);
    lv_obj_align(scan, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t* scanLabel = lv_label_create(scan);
    lv_label_set_text(scanLabel, LV_SYMBOL_REFRESH " Scan");
    lv_obj_set_style_text_color(scanLabel, lv_color_hex(theme::kBackground), 0);
    lv_obj_set_style_text_font(scanLabel, theme::uiFontSmall(), 0);
    lv_obj_center(scanLabel);
    lv_obj_add_event_cb(scan, [](lv_event_t*) {
        net::startScan();
        // Say so now rather than on the next tick, so the tap feels answered.
        if (s_status) lv_label_set_text(s_status, LV_SYMBOL_REFRESH " Scanning...");
        rebuildList();
    }, LV_EVENT_CLICKED, nullptr);
    s_scanButton = scan;
    lv_group_add_obj(input::group(), scan);

    s_list = lv_obj_create(s_page);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 5, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    net::onScanFinished = [] { s_listDirty = true; };

    input::setKeyHook(keyHook);

    rebuildList();
    if (net::scanResults().empty()) net::startScan();
}

void destroy() {
    closeConnectDialog();
    closePrompt();
    input::clearKeyHook();
    net::onScanFinished = nullptr;
    s_page       = nullptr;
    s_status     = nullptr;
    s_list       = nullptr;
    s_scanButton = nullptr;
}

void tick() {
    updateConnectDialog();

    if (!s_status) return;

    if (s_listDirty) {
        s_listDirty = false;
        rebuildList();
    }

    static uint32_t lastUpdate = 0;
    const uint32_t  now        = millis();
    if (now - lastUpdate < 1000) return;
    lastUpdate = now;

    // Grey the button out while a scan is in flight, so it is obvious that
    // something is happening and a second tap will not queue another one.
    if (s_scanButton) {
        if (net::isScanning()) lv_obj_add_state(s_scanButton, LV_STATE_DISABLED);
        else                   lv_obj_remove_state(s_scanButton, LV_STATE_DISABLED);
    }

    String text;
    if (net::isScanning()) {
        text = LV_SYMBOL_REFRESH " Scanning...";
    } else if (net::isConnected()) {
        text = net::ssid() + "  " + net::ipAddress();
    } else {
        text = net::enabled() ? "Not connected" : "WiFi is off";
    }
    lv_label_set_text(s_status, text.c_str());
}

} // namespace wifiapp
