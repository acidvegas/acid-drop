#include "apps/IrcApp.h"

#include <algorithm>
#include <vector>

#include "board/Input.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "net/WifiService.h"
#include "irc/IrcCommands.h"
#include "relay/WeechatRelay.h"
#include "ui/StatusBar.h"
#include "ui/TermView.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace ircapp {
namespace {

constexpr const char* TAG = "ircapp";

lv_obj_t* s_page   = nullptr;
lv_obj_t* s_tabs   = nullptr;
lv_obj_t* s_input  = nullptr;
lv_obj_t* s_barToggleLabel  = nullptr;
lv_obj_t* s_infoPanel      = nullptr;
lv_obj_t* s_infoStatus     = nullptr;
lv_obj_t* s_infoModes      = nullptr;
lv_obj_t* s_infoUsers      = nullptr;
lv_obj_t* s_infoTopic      = nullptr;
lv_obj_t* s_nickList       = nullptr;
lv_obj_t* s_offlinePanel   = nullptr;
lv_obj_t* s_pickerPanel    = nullptr;
lv_obj_t* s_offlineTitle   = nullptr;
lv_obj_t* s_offlineDetail  = nullptr;
lv_obj_t* s_offlineButton  = nullptr;
TermView  s_view;

// The nick list is rebuilt from LVGL objects, which is far too expensive to do
// on every one-second refresh. This is a cheap fingerprint of the roster, so it
// is only rebuilt when the roster actually changed.
uint32_t s_nickSignature = 0;

size_t s_activeBuffer = 0;

// In relay mode the active buffer is also held by WeeChat pointer, because
// indices shift whenever WeeChat opens, closes or moves a buffer.
String s_activeRelayPointer;

// The picker's rows by position, as the pointer each one opens.
std::vector<String> s_pickerPointers;
bool   s_alive        = false;

// --- nick completion ------------------------------------------------------
// There is no Tab key on this keyboard - the matrix is letters, space, enter,
// backspace, shift, alt, mic and a single '$'. So completion is offered as
// ghost text: the rest of the best-matching nick is drawn greyed out ahead of
// the cursor as you type, and '$' takes it. Pressing '$' again cycles to the
// next match. With nothing to complete, '$' types a literal '$'.
lv_obj_t* s_ghost          = nullptr;
bool      s_completing     = false;   // '$' has been pressed at least once
uint32_t  s_completeStart  = 0;       // where the partial word began
String    s_completePrefix;           // what was typed before the first '$'
size_t    s_completeIndex  = 0;       // which match is currently inserted

std::vector<lv_obj_t*> s_tabButtons;

// --- windows --------------------------------------------------------------
//
// This screen renders either a direct IRC connection or a WeeChat relay. The
// two have different buffer types, so everything below goes through one small
// set of accessors rather than the screen knowing which it is looking at.

size_t windowCount() {
    return ui::relayMode() ? ui::relay().bufferCount() : ui::irc().bufferCount();
}

TermDoc* windowDoc(size_t index) {
    if (index >= windowCount()) return nullptr;
    return ui::relayMode() ? &ui::relay().buffer(index).doc
                           : &ui::irc().buffer(index).doc;
}

String windowName(size_t index) {
    if (index >= windowCount()) return String();
    if (ui::relayMode()) return ui::relay().buffer(index).shortName;

    IrcBuffer& buffer = ui::irc().buffer(index);
    return buffer.isStatus() ? String("status") : buffer.name;
}

// The network a window belongs to. Empty for direct IRC, which only ever has
// one, and is what the relay picker groups by.
String windowGroup(size_t index) {
    if (!ui::relayMode() || index >= windowCount()) return String();
    return ui::relay().buffer(index).network;
}

bool windowIsChannel(size_t index) {
    if (index >= windowCount()) return false;
    return ui::relayMode() ? ui::relay().buffer(index).isChannel()
                           : ui::irc().buffer(index).isChannel();
}

bool windowIsReady() {
    return ui::relayMode() ? ui::relay().state() == RelayState::Ready
                           : ui::irc().state() == IrcState::Ready;
}

// --- helpers --------------------------------------------------------------

void closeInfoPanel();
void openInfoPanel();
void updateGhost();
void updateOfflinePanel();
void closeBufferPicker();
void selectBuffer(size_t index);

// Only valid in direct IRC mode; the relay has no IrcBuffer.
//
// Deliberately does not write to s_activeBuffer. It used to reset it to 0 when
// the index was out of range, which in relay mode is every call - the IRC
// client has one buffer while the relay has dozens. Completion runs this on
// every keystroke, so typing silently moved the selection to buffer 0 and the
// message went to WeeChat's core buffer instead of the channel on screen.
IrcBuffer& activeBuffer() {
    IrcClient& client = ui::irc();
    const size_t index = s_activeBuffer < client.bufferCount() ? s_activeBuffer : 0;
    return client.buffer(index);
}

FormatOptions currentFormatOptions() {
    // All on, always. mIRC colours, ANSI escapes, background colours for ANSI
    // art and the bold/italic/underline attributes are what makes IRC text
    // look like IRC text; none of them was ever worth a switch.
    FormatOptions options;
    options.mircColors = true;
    options.background = true;
    options.ansi       = true;
    options.attributes = true;
    options.defaultFg  = theme::text();
    options.defaultBg  = theme::background();
    return options;
}

// Nothing to do with a title any more: the status bar stopped repeating the
// window name, so this only governs the reconnect button.
void updateHeaderButtons() {
    // windowIsReady() rather than the IRC client's own state: in relay mode
    // the IRC client is idle by design, and asking it whether we are connected
    // gets "no" forever.
    const bool connected = windowIsReady();

    // The info panel describes an IRC channel this client is in: its modes,
    // its topic and the roster it tracks. A relay buffer has none of that on
    // our side, so the panel is not offered there at all.
    const bool infoUseful = connected && !ui::relayMode() &&
                            windowIsChannel(s_activeBuffer);
    if (s_infoPanel && !infoUseful) closeInfoPanel();

    updateOfflinePanel();
}

// The whole path from "no network" to "on IRC", narrated. It covers the
// device's own startup, so it is shown until the client is actually on a
// server rather than only while idle.
void updateOfflinePanel() {
    if (!s_offlinePanel) return;

    // This panel is opaque and sits over the message grid, so leaving it up
    // does not just look wrong - it hides the chat completely.
    if (windowIsReady()) {
        lv_obj_set_hidden(s_offlinePanel, true);
        return;
    }
    lv_obj_set_hidden(s_offlinePanel, false);

    if (ui::relayMode()) {
        WeechatRelay& relay = ui::relay();

        String relayTitle;
        String relayDetail;
        bool   relayNeedsSetup = false;

        if (settings::getText("wifi_ssid").isEmpty()) {
            relayTitle      = "No WiFi configured";
            relayDetail     = "Add a network in Settings to get online";
            relayNeedsSetup = true;
        } else if (!net::isConnected()) {
            relayTitle  = "Connecting to " + settings::getText("wifi_ssid");
            relayDetail = net::statusText();
        } else if (settings::getText("relay_host").isEmpty()) {
            relayTitle      = "Connected to " + net::ssid();
            relayDetail     = "No relay host configured";
            relayNeedsSetup = true;
        } else {
            relayTitle = "WeeChat relay - " + relay.stateText();
            relayDetail = relay.lastError().isEmpty()
                              ? settings::getText("relay_host") + ":" +
                                    String(settings::getInt("relay_port"))
                              : relay.lastError();
        }

        lv_label_set_text(s_offlineTitle, relayTitle.c_str());
        lv_label_set_text(s_offlineDetail, relayDetail.c_str());
        lv_obj_set_hidden(s_offlineButton, !relayNeedsSetup);
        return;
    }

    IrcClient& client = ui::irc();

    const String ssid = settings::getText("wifi_ssid");
    const String nick = settings::getText("irc_nick");

    // A bouncer is a different host, and naming the wrong one while it fails
    // to connect is worse than saying nothing.
    const bool   znc    = ui::chatMode() == ui::ChatMode::Znc;
    const String server = znc ? settings::getText("znc_host")
                              : settings::getText("irc_server");

    String title;
    String detail;
    bool   needsSetup = false;

    if (ssid.isEmpty()) {
        title      = "No WiFi configured";
        detail     = "Add a network in Settings to get online";
        needsSetup = true;
    } else if (!net::isConnected()) {
        if (net::isScanning()) {
            title  = "Scanning for networks";
            detail = "";
        } else if (!net::lastError().isEmpty()) {
            title  = "Failed to connect to " + ssid;
            detail = net::lastError();
        } else {
            const uint8_t attempts = net::connectAttempts();
            title  = "Connecting to " + ssid;
            detail = attempts > 1 ? "attempt " + String(attempts) : net::statusText();
        }
    } else if (server.isEmpty() || nick.isEmpty()) {
        title      = "Connected to " + net::ssid();
        detail     = znc ? "ZNC is not configured yet - set a bouncer host and "
                           "nick in Settings"
                         : "IRC is not configured yet - set a server and nick "
                           "in Settings";
        needsSetup = true;
    } else {
        switch (client.state()) {
            case IrcState::Connecting:
                title  = "Connecting to " + server;
                detail = net::ssid() + "  -  " + net::ipAddress();
                break;
            case IrcState::Registering:
                title  = "Registering as " + client.nick();
                detail = server;
                break;
            case IrcState::JoinDelay:
                title  = "Joining channels";
                detail = server;
                break;
            case IrcState::Reconnecting:
                title  = "Reconnecting to " + server;
                detail = client.stateText();
                break;
            default:
                title  = "Connected to " + net::ssid();
                detail = "Starting IRC";
                break;
        }
    }

    lv_label_set_text(s_offlineTitle, title.c_str());
    lv_label_set_text(s_offlineDetail, detail.c_str());
    lv_obj_set_hidden(s_offlineButton, !needsSetup);
}

// The status-bar dot means "something wants you somewhere", so it clears only
// once no window is still holding a highlight.
void refreshNotificationDot() {
    for (size_t i = 0; i < windowCount(); i++) {
        const TermDoc* doc = windowDoc(i);
        if (doc && doc->unreadHighlight) {
            statusbar::setNotification(true);
            return;
        }
    }
    statusbar::setNotification(false);
}

void selectBuffer(size_t index) {
    if (index >= windowCount()) return;

    s_activeBuffer = index;

    TermDoc* doc = windowDoc(index);
    if (doc == nullptr) return;

    doc->unread          = 0;
    doc->unreadHighlight = false;

    // A relay buffer's history is only fetched when it is first opened -
    // WeeChat will describe forty buffers and asking for all their backlog up
    // front would not fit in memory.
    if (ui::relayMode()) {
        s_activeRelayPointer = ui::relay().buffer(index).pointer;
        ui::relay().ensureLines(ui::relay().buffer(index));
    }

    refreshNotificationDot();

    // A different window has a different roster, and possibly no roster at all.
    s_completing = false;
    if (s_ghost) lv_obj_set_hidden(s_ghost, true);

    s_view.setDocument(doc);
    onBufferListChanged();
    updateHeaderButtons();
}

void tabEventCb(lv_event_t* event) {
    const size_t index = reinterpret_cast<size_t>(lv_event_get_user_data(event));

    // Tapping the window you are already in opens its details, which is where
    // the modes, topic and nick list live. Tapping any other window just
    // switches to it. This replaced a dedicated info button that was only
    // meaningful on a channel anyway.
    if (index == s_activeBuffer && !ui::relayMode() && windowIsChannel(index)) {
        openInfoPanel();
        return;
    }
    selectBuffer(index);
}

// --- commands -------------------------------------------------------------

// The picker and the info panel sit on lv_layer_top, and LVGL still repaints
// the whole active screen under that layer (refr_area in lv_refr.c). The chat
// grid re-parses every visible line on each paint, so while a full-screen
// overlay is up it is hidden, and scrolling the overlay stays cheap.
void setChatHidden(bool hidden) {
    if (s_view.object()) lv_obj_set_hidden(s_view.object(), hidden);
}

void closeInfoPanel() {
    if (s_infoPanel) {
        lv_obj_delete(s_infoPanel);
        s_infoPanel = nullptr;
        setChatHidden(false);
    }
    s_infoStatus = nullptr;
    s_infoModes  = nullptr;
    s_infoUsers  = nullptr;
    s_infoTopic  = nullptr;
    s_nickList   = nullptr;
}

uint32_t nickSignature(const std::vector<IrcNick>& nicks) {
    uint32_t hash = 2166136261u;
    for (const IrcNick& entry : nicks) {
        hash ^= static_cast<uint8_t>(entry.prefix);
        hash *= 16777619u;
        for (unsigned int i = 0; i < entry.name.length(); i++) {
            hash ^= static_cast<uint8_t>(entry.name[i]);
            hash *= 16777619u;
        }
    }
    return hash;
}

// --- info panel pieces ----------------------------------------------------

lv_obj_t* makeCard(lv_obj_t* parent) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, theme::panel(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_color(card, theme::border(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_scrollable(card, false);
    return card;
}

// A small dim heading, so each card says what it is.
void addCardTitle(lv_obj_t* card, const char* text) {
    lv_obj_t* label = lv_label_create(card);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(label, theme::accent(), 0);
}

// "Key            value", the key dim on the left and the value pushed right.
lv_obj_t* addKeyValue(lv_obj_t* card, const char* key) {
    lv_obj_t* row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t* keyLabel = lv_label_create(row);
    lv_label_set_text(keyLabel, key);
    lv_obj_set_style_text_font(keyLabel, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(keyLabel, theme::textDim(), 0);
    lv_obj_set_flex_grow(keyLabel, 1);

    lv_obj_t* valueLabel = lv_label_create(row);
    lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(valueLabel, LV_PCT(65), 0);
    lv_obj_set_style_text_font(valueLabel, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(valueLabel, theme::text(), 0);
    return valueLabel;
}

// One roster section: a heading, then one nick per line.
void addNickSection(const char* heading, const std::vector<IrcNick>& sorted,
                    uint8_t rank, IrcClient& client, size_t& budget) {
    // Count first, so an empty category prints no heading at all.
    size_t count = 0;
    for (const IrcNick& entry : sorted) {
        if (entry.rank() == rank) count++;
    }
    if (count == 0) return;

    lv_obj_t* title = lv_label_create(s_nickList);
    lv_label_set_text(title, (String(heading) + "  (" + String(count) + ")").c_str());
    lv_obj_set_style_text_font(title, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(title, theme::accent(), 0);
    lv_obj_set_style_pad_top(title, 6, 0);

    for (const IrcNick& entry : sorted) {
        if (entry.rank() != rank) continue;
        if (budget == 0) return;
        budget--;

        lv_obj_t* row = lv_obj_create(s_nickList);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_style_pad_left(row, 4, 0);

        // The prefix sits in its own fixed-width cell so every nick on every
        // line starts at the same column, prefixed or not.
        lv_obj_t* prefix = lv_label_create(row);
        char prefixText[2] = {entry.prefix ? entry.prefix : ' ', 0};
        lv_label_set_text(prefix, prefixText);
        lv_obj_set_style_text_font(prefix, &acid_mono_10, 0);
        lv_obj_set_style_text_color(prefix, theme::textDim(), 0);
        lv_obj_set_width(prefix, 10);

        lv_obj_t* name = lv_label_create(row);
        lv_label_set_text(name, entry.name.c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_font(name, &acid_mono_10, 0);
        lv_obj_set_style_text_color(
            name, textfmt::mircColor(client.nickColorIndex(entry.name), theme::text()), 0);
    }
}

// Rebuilds the roster: one nick per line, grouped by status, each group in
// alphabetical order and each nick in the colour its messages are drawn in.
void rebuildNickList(IrcBuffer& buffer) {
    if (!s_nickList) return;

    lv_obj_clean(s_nickList);

    std::vector<IrcNick> sorted = buffer.nicks;
    std::sort(sorted.begin(), sorted.end(), [](const IrcNick& a, const IrcNick& b) {
        if (a.rank() != b.rank()) return a.rank() < b.rank();

        // Case-insensitive, so "alice" and "Bob" do not sort by capitalisation.
        String left  = a.name; left.toLowerCase();
        String right = b.name; right.toLowerCase();
        return strcmp(left.c_str(), right.c_str()) < 0;
    });

    // Bounded: each nick is several LVGL objects, and a big channel would
    // otherwise build thousands of them.
    size_t budget = 250;

    IrcClient& client = ui::irc();
    addNickSection("OWNERS",     sorted, 0, client, budget);
    addNickSection("ADMINS",     sorted, 1, client, budget);
    addNickSection("OPERATORS",  sorted, 2, client, budget);
    addNickSection("HALF-OPS",   sorted, 3, client, budget);
    addNickSection("VOICE",      sorted, 4, client, budget);
    addNickSection("USERS",      sorted, 5, client, budget);

    if (budget == 0) {
        lv_obj_t* more = lv_label_create(s_nickList);
        lv_label_set_text(more, "list truncated");
        lv_obj_set_style_text_font(more, theme::uiFontSmall(), 0);
        lv_obj_set_style_text_color(more, theme::textDim(), 0);
    }
}

// lv_label_set_text() has no same-text check: it always invalidates the label
// and, for a content-sized one, relays out its parents. This runs every
// second, so only a real change is passed on.
void setLabelText(lv_obj_t* label, const char* text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

void refreshInfoPanel() {
    if (!s_infoPanel) return;

    IrcBuffer& buffer = activeBuffer();
    if (!buffer.isChannel()) { closeInfoPanel(); return; }

    if (s_infoStatus) {
        String status = buffer.joined ? "joined" : "not joined";
        if (!buffer.joined && !buffer.retryReason.isEmpty()) {
            status += " (" + buffer.retryReason + ", retrying)";
        }
        setLabelText(s_infoStatus, status.c_str());
        lv_obj_set_style_text_color(
            s_infoStatus, buffer.joined ? theme::accent() : lv_color_hex(theme::kWarning), 0);
    }
    if (s_infoModes) {
        setLabelText(s_infoModes,
                     buffer.modes.isEmpty() ? "unknown" : buffer.modes.c_str());
    }
    if (s_infoUsers) {
        setLabelText(s_infoUsers, String(buffer.nicks.size()).c_str());
    }
    if (s_infoTopic) {
        setLabelText(s_infoTopic, buffer.topic.isEmpty()
                     ? "(none set)"
                     : textfmt::strip(buffer.topic).c_str());
    }

    const uint32_t signature = nickSignature(buffer.nicks);
    if (signature != s_nickSignature) {
        s_nickSignature = signature;
        rebuildNickList(buffer);
    }
}

void openInfoPanel() {
    closeInfoPanel();

    IrcBuffer& buffer = activeBuffer();
    if (!buffer.isChannel()) return;

    // Ask the server for anything we may be missing or stale on. This marks
    // the window quiet for a few seconds so the answers update it without also
    // printing "topic set by X" and "N users" into the backlog.
    ui::irc().requestChannelInfo(buffer.name);

    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, theme::background(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(overlay, 0, 0);
    s_infoPanel = overlay;
    setChatHidden(true);

    // --- header ---
    lv_obj_t* header = lv_obj_create(overlay);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 30);
    lv_obj_set_style_bg_color(header, theme::panel(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, theme::border(), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_pad_hor(header, 4, 0);
    lv_obj_set_scrollable(header, false);

    lv_obj_t* close = lv_button_create(header);
    lv_obj_set_size(close, 34, 24);
    lv_obj_set_style_bg_color(close, theme::surfaceAlt(), 0);
    lv_obj_set_style_radius(close, 5, 0);
    lv_obj_align(close, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* closeLabel = lv_label_create(close);
    lv_label_set_text(closeLabel, LV_SYMBOL_LEFT);
    lv_obj_center(closeLabel);
    lv_group_add_obj(input::group(), close);
    lv_group_focus_obj(close);
    lv_obj_add_event_cb(close, [](lv_event_t*) { closeInfoPanel(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, buffer.name.c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 150);
    lv_obj_set_style_text_font(title, theme::uiFont(), 0);
    lv_obj_set_style_text_color(title, theme::accent(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 42, 0);

    lv_obj_t* closeWindow = lv_button_create(header);
    lv_obj_set_size(closeWindow, 68, 24);
    lv_obj_set_style_bg_color(closeWindow, lv_color_hex(0x5A1828), 0);
    lv_obj_set_style_radius(closeWindow, 5, 0);
    lv_obj_align(closeWindow, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t* closeWindowLabel = lv_label_create(closeWindow);
    lv_label_set_text(closeWindowLabel, LV_SYMBOL_CLOSE " Part");
    lv_obj_set_style_text_font(closeWindowLabel, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(closeWindowLabel, lv_color_hex(theme::kDanger), 0);
    lv_obj_center(closeWindowLabel);
    lv_group_add_obj(input::group(), closeWindow);

    lv_obj_add_event_cb(closeWindow, [](lv_event_t*) {
        const size_t index = s_activeBuffer;
        closeInfoPanel();

        // Let go of the document before the buffer owning it is destroyed.
        s_view.setDocument(nullptr);

        if (!ui::irc().closeBuffer(index)) ui::toast("Cannot close the status window");
        selectBuffer(0);
    }, LV_EVENT_CLICKED, nullptr);

    // --- body ---
    lv_obj_t* scroll = lv_obj_create(overlay);
    lv_obj_remove_style_all(scroll);
    lv_obj_set_width(scroll, LV_PCT(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_pad_all(scroll, 8, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scroll, 8, 0);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* summary = makeCard(scroll);
    addCardTitle(summary, "CHANNEL");
    s_infoStatus = addKeyValue(summary, "Status");
    s_infoModes  = addKeyValue(summary, "Modes");
    s_infoUsers  = addKeyValue(summary, "Users");

    lv_obj_t* topicCard = makeCard(scroll);
    addCardTitle(topicCard, "TOPIC");
    s_infoTopic = lv_label_create(topicCard);
    lv_label_set_long_mode(s_infoTopic, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_infoTopic, LV_PCT(100));
    lv_obj_set_style_text_font(s_infoTopic, &acid_mono_10, 0);
    lv_obj_set_style_text_color(s_infoTopic, theme::text(), 0);

    lv_obj_t* rosterCard = makeCard(scroll);
    addCardTitle(rosterCard, "NICKS");

    s_nickList = lv_obj_create(rosterCard);
    lv_obj_remove_style_all(s_nickList);
    lv_obj_set_width(s_nickList, LV_PCT(100));
    lv_obj_set_height(s_nickList, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_nickList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_nickList, 2, 0);
    lv_obj_set_scrollable(s_nickList, false);

    // Force a build on open rather than waiting for the roster to change.
    s_nickSignature = 0;
    refreshInfoPanel();
}

// Hiding the status bar reflows the root column, and LVGL's flex layout skips
// hidden children, so the message view simply grows into the space. The window
// list is in that bar now, so this hides it too - which is the point, it is
// another two lines of backlog on a 240px screen.
void applyTopBar() {
    const bool show = settings::getBool("irc_topbar");
    if (statusbar::object()) lv_obj_set_hidden(statusbar::object(), !show);
    if (s_barToggleLabel) {
        lv_label_set_text(s_barToggleLabel, show ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    }
}

void selectRelative(int delta) {
    const size_t count = windowCount();
    if (count == 0) return;

    int next = static_cast<int>(s_activeBuffer) + delta;
    while (next < 0) next += count;
    selectBuffer(next % count);
}

void echoLocal(const String& text) {
    IrcBuffer& buffer = activeBuffer();
    buffer.doc.append(text, static_cast<uint32_t>(time(nullptr)), LINE_LOCAL, false);
    if (s_view.atBottom()) s_view.scrollToBottom();
    lv_obj_invalidate(s_view.object());
}

irccmd::Context commandContext() {
    irccmd::Context context;
    context.client = &ui::irc();
    context.window = &activeBuffer();

    context.echo         = echoLocal;
    context.selectWindow = [](size_t index) { selectBuffer(index); };
    context.nextWindow   = [] { selectRelative(1); };
    context.prevWindow   = [] { selectRelative(-1); };

    context.closeWindow = [] {
        const size_t index = s_activeBuffer;
        s_view.setDocument(nullptr);   // the buffer owns the document
        if (!ui::irc().closeBuffer(index)) ui::toast("Cannot close the status window");
        selectBuffer(0);
    };
    context.clearWindow = [] {
        activeBuffer().doc.clear();
        lv_obj_invalidate(s_view.object());
    };
    context.openSettings = [] { ui::openApp(ui::AppId::Settings); };
    context.openChannels = [] { ui::openApp(ui::AppId::Channels); };

    return context;
}

void submitInput() {
    const char* raw = lv_textarea_get_text(s_input);
    if (raw == nullptr || raw[0] == '\0') return;

    String text(raw);
    lv_textarea_set_text(s_input, "");
    s_completing = false;
    if (s_ghost) lv_obj_set_hidden(s_ghost, true);

    // In relay mode every line goes to WeeChat, slash commands included -
    // running them here would act on an IRC connection that is not the one
    // the user is looking at.
    if (!ui::relayMode()) {
        const irccmd::Context context = commandContext();
        if (irccmd::run(text, context)) return;
    }

    // "//foo" is how you send a line that really does start with a slash.
    // WeeChat unescapes it itself (string_input_for_buffer), so in relay mode
    // it goes over as typed; stripping it here made it a command.
    if (!ui::relayMode() && text.startsWith("//")) text = text.substring(1);

    if (ui::relayMode()) {
        // WeeChat parses this exactly as if typed into that buffer, so its
        // own slash commands work without this firmware knowing any of them.
        if (s_activeBuffer < windowCount()) {
            ui::relay().send(ui::relay().buffer(s_activeBuffer), text);
        }
        return;
    }

    IrcBuffer& buffer = activeBuffer();
    if (buffer.isStatus()) {
        echoLocal("No target in this window - use /join or /query, or /help");
        return;
    }
    ui::irc().say(buffer.name, text);
}

void inputEventCb(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY)         submitInput();
    if (code == LV_EVENT_VALUE_CHANGED) updateGhost();
}

// Keys the app wants before the text area sees them.
void sortNoCase(std::vector<String>& list) {
    std::sort(list.begin(), list.end(), [](const String& a, const String& b) {
        String left = a; left.toLowerCase();
        String right = b; right.toLowerCase();
        return strcmp(left.c_str(), right.c_str()) < 0;
    });
}

// What `prefix` could become. A word starting with '/' at the very start of
// the line is a command; anything else is a nick from the current channel.
std::vector<String> completionsFor(const String& prefix, bool atLineStart) {
    std::vector<String> out;

    if (atLineStart && prefix.startsWith("/")) {
        String wanted = prefix.substring(1);
        wanted.toLowerCase();

        for (const char* const* name = irccmd::commandNames(); *name; name++) {
            String candidate(*name);
            if (candidate.startsWith(wanted)) out.push_back("/" + candidate);
        }
        sortNoCase(out);
        return out;
    }

    String wanted = prefix;
    wanted.toLowerCase();

    if (ui::relayMode()) {
        // The relay keeps its roster as plain names from the nicklist.
        if (s_activeBuffer >= ui::relay().bufferCount()) return out;

        for (const String& name : ui::relay().buffer(s_activeBuffer).nicks) {
            String candidate = name;
            candidate.toLowerCase();
            if (candidate.startsWith(wanted)) out.push_back(name);
        }
        sortNoCase(out);
        return out;
    }

    IrcBuffer& buffer = activeBuffer();
    if (!buffer.isChannel()) return out;

    for (const IrcNick& entry : buffer.nicks) {
        String candidate = entry.name;
        candidate.toLowerCase();
        if (candidate.startsWith(wanted)) out.push_back(entry.name);
    }
    sortNoCase(out);
    return out;
}

// The partial word the cursor sits at the end of. Only offered at the end of
// the line: anywhere else the textarea may have scrolled sideways, and the
// ghost is positioned by counting characters.
bool partialWord(String& textOut, uint32_t& startOut, String& prefixOut) {
    if (!s_input) return false;

    const char* raw = lv_textarea_get_text(s_input);
    if (raw == nullptr) return false;

    textOut = String(raw);
    const uint32_t cursor = lv_textarea_get_cursor_pos(s_input);
    if (cursor != textOut.length()) return false;   // not at the end

    uint32_t start = cursor;
    while (start > 0 && textOut[start - 1] != ' ') start--;
    if (start == cursor) return false;              // nothing typed yet

    startOut  = start;
    prefixOut = textOut.substring(start, cursor);
    return true;
}

// Redraws the greyed-out remainder after the cursor. Called whenever the text
// changes, so it has to be cheap and it has to fail quietly.
void updateGhost() {
    if (!s_ghost || !s_input) return;

    auto hide = [] { lv_obj_set_hidden(s_ghost, true); };

    String text, prefix;
    uint32_t start = 0;
    if (!partialWord(text, start, prefix)) { hide(); return; }

    const std::vector<String> matches = completionsFor(prefix, start == 0);
    if (matches.empty()) { hide(); return; }

    // Nothing to show when what you typed is already the whole nick.
    const String& best = matches[s_completing ? s_completeIndex % matches.size() : 0];
    if (best.length() <= prefix.length()) { hide(); return; }

    const String remainder = best.substring(prefix.length());

    // Monospace, so the offset is exact: characters already typed times the
    // advance, plus the field's own border and padding.
    const lv_font_t* font = &acid_mono_10;
    const int32_t advance = lv_font_get_glyph_width(font, 'M', 0);
    const int32_t inset   = lv_obj_get_style_pad_left(s_input, LV_PART_MAIN) +
                            lv_obj_get_style_border_width(s_input, LV_PART_MAIN);
    const int32_t x       = inset + static_cast<int32_t>(text.length()) * advance;

    // If the suggestion would not fit, the field has probably scrolled and the
    // offset is no longer honest. Say nothing rather than draw it in the wrong
    // place.
    const int32_t needed = x + static_cast<int32_t>(remainder.length()) * advance;
    if (needed > lv_obj_get_width(s_input) - 2) { hide(); return; }

    lv_label_set_text(s_ghost, remainder.c_str());
    lv_obj_set_style_text_font(s_ghost, font, 0);
    lv_obj_align(s_ghost, LV_ALIGN_LEFT_MID, x, 0);
    lv_obj_set_hidden(s_ghost, false);
}

// '$' pressed. Returns false when there is nothing to complete, so the key
// falls through and types a literal '$'.
bool acceptCompletion() {
    String text, prefix;
    uint32_t start = 0;

    if (s_completing) {
        // Already completed once: cycle, reusing the prefix from before.
        prefix = s_completePrefix;
        start  = s_completeStart;
        const char* raw = lv_textarea_get_text(s_input);
        text = raw ? String(raw) : String();
        s_completeIndex++;
    } else {
        if (!partialWord(text, start, prefix)) return false;
        s_completeStart  = start;
        s_completePrefix = prefix;
        s_completeIndex  = 0;
    }

    const std::vector<String> matches = completionsFor(prefix, start == 0);
    if (matches.empty()) { s_completing = false; return false; }

    // One past the last match is not a wrap back to the first: it hands back
    // exactly what was typed, followed by the '$' that was pressed. That is
    // the escape hatch for wanting a literal '$' after a word that happens to
    // match somebody's nick - cycle past everyone and you get your text back.
    if (s_completeIndex >= matches.size()) {
        const String literal = text.substring(0, start) + s_completePrefix + "$";
        lv_textarea_set_text(s_input, literal.c_str());
        lv_textarea_set_cursor_pos(s_input, literal.length());
        s_completing = false;
        updateGhost();
        return true;
    }

    // At the start of a line a nick is an address, so it gets the colon every
    // other client uses. A command is not an address, and neither is a word
    // in the middle of a sentence.
    const bool isCommand = matches[s_completeIndex].startsWith("/");
    String insert = matches[s_completeIndex];
    insert += (start == 0 && !isCommand) ? ": " : " ";

    const String rebuilt = text.substring(0, start) + insert;
    lv_textarea_set_text(s_input, rebuilt.c_str());
    lv_textarea_set_cursor_pos(s_input, rebuilt.length());

    s_completing = true;
    updateGhost();
    return true;
}

bool keyHook(uint32_t key) {
    if (!s_alive) return false;

    // Anything that is not another '$' ends the cycle, so the next one starts
    // from whatever is on the line rather than from a stale prefix.
    if (key != '$') s_completing = false;

    switch (key) {
        case '$':
            // Takes the ghost suggestion, or cycles to the next match. With
            // nothing to complete it falls through and types a literal '$'.
            if (acceptCompletion()) return true;
            return false;

        case LV_KEY_NEXT:
        case LV_KEY_PREV:
            return false;

        case LV_KEY_UP:
            // Once scrolled to the top, hand the key back so focus can move to
            // the header rather than the view swallowing it forever.
            if (s_view.atTop()) return false;
            s_view.scrollRows(1);
            return true;

        case LV_KEY_DOWN:
            if (s_view.atBottom()) return false;
            s_view.scrollRows(-1);
            return true;

        case LV_KEY_LEFT:
        case LV_KEY_RIGHT: {
            // Only steal the arrows when there is no text to move through.
            const char* text = lv_textarea_get_text(s_input);
            if (text && text[0] != '\0') return false;

            selectRelative(key == LV_KEY_LEFT ? -1 : 1);
            return true;
        }

        case LV_KEY_ESC:
            if (s_pickerPanel) { closeBufferPicker(); return true; }
            if (s_infoPanel)   { closeInfoPanel();   return true; }
            ui::back();
            return true;

        default:
            return false;
    }
}

void closeBufferPicker() {
    if (s_pickerPanel) {
        lv_obj_delete(s_pickerPanel);
        s_pickerPanel = nullptr;
        setChatHidden(false);
    }
}

// The relay's window list, grouped by network. A strip of tabs works for one
// IRC connection with a handful of windows; WeeChat has every network you are
// on, which is tens of buffers and no room across 320 pixels.
void openBufferPicker() {
    closeBufferPicker();

    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, theme::background(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
    s_pickerPanel = overlay;
    setChatHidden(true);
    // Small text so more of a long buffer list fits on the screen. Row
    // names inherit it; the rest set it explicitly.
    lv_obj_set_style_text_font(overlay, theme::uiFontTiny(), 0);

    lv_obj_t* header = lv_obj_create(overlay);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 30);
    lv_obj_set_style_bg_color(header, theme::panel(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(header, 4, 0);
    lv_obj_set_scrollable(header, false);

    lv_obj_t* close = lv_button_create(header);
    lv_obj_set_size(close, 34, 24);
    lv_obj_set_style_bg_color(close, theme::surfaceAlt(), 0);
    lv_obj_set_style_radius(close, 5, 0);
    lv_obj_align(close, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* closeLabel = lv_label_create(close);
    lv_label_set_text(closeLabel, LV_SYMBOL_LEFT);
    lv_obj_center(closeLabel);
    lv_group_add_obj(input::group(), close);
    lv_group_focus_obj(close);
    lv_obj_add_event_cb(close, [](lv_event_t*) { closeBufferPicker(); },
                        LV_EVENT_CLICKED, nullptr);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Windows");
    lv_obj_set_style_text_font(title, theme::uiFontTiny(), 0);
    lv_obj_set_style_text_color(title, theme::accent(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 42, 0);

    lv_obj_t* list = lv_obj_create(overlay);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    WeechatRelay& relay = ui::relay();
    s_pickerPointers.clear();

    for (const String& network : relay.networks()) {
        lv_obj_t* heading = lv_label_create(list);
        String headingText = network;
        headingText.toUpperCase();
        lv_label_set_text(heading, headingText.c_str());
        lv_obj_set_style_text_font(heading, theme::uiFontTiny(), 0);
        lv_obj_set_style_text_color(heading, theme::accent(), 0);
        lv_obj_set_style_pad_top(heading, 4, 0);

        for (size_t i = 0; i < relay.bufferCount(); i++) {
            RelayBuffer& buffer = relay.buffer(i);
            if (buffer.network != network) continue;

            lv_obj_t* row = lv_obj_create(list);
            lv_obj_remove_style_all(row);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, LV_SIZE_CONTENT);
            theme::styleRowCompact(row);
            lv_obj_set_clickable(row, true);
            lv_obj_set_scrollable(row, false);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 6, 0);
            lv_group_add_obj(input::group(), row);

            lv_obj_t* name = lv_label_create(row);
            lv_label_set_text(name, buffer.shortName.c_str());
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_obj_set_flex_grow(name, 1);
            lv_obj_set_style_text_color(
                name, i == s_activeBuffer ? theme::accent() : theme::text(), 0);

            if (buffer.doc.unread > 0) {
                lv_obj_t* badge = lv_label_create(row);
                lv_label_set_text(badge, String(buffer.doc.unread).c_str());
                lv_obj_set_style_text_font(badge, theme::uiFontTiny(), 0);
                lv_obj_set_style_text_color(
                    badge,
                    buffer.doc.unreadHighlight ? lv_color_hex(theme::kDanger)
                                               : theme::textDim(), 0);
            }

            // By pointer, not index: the list can change while the picker is
            // open, and an index captured now may name another buffer by then.
            s_pickerPointers.push_back(buffer.pointer);
            lv_obj_add_event_cb(row, [](lv_event_t* event) {
                const size_t position =
                    reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
                const String pointer = position < s_pickerPointers.size()
                                           ? s_pickerPointers[position] : String();
                closeBufferPicker();

                WeechatRelay& client = ui::relay();
                for (size_t index = 0; index < client.bufferCount(); index++) {
                    if (client.buffer(index).pointer == pointer) {
                        selectBuffer(index);
                        break;
                    }
                }
            }, LV_EVENT_CLICKED,
               reinterpret_cast<void*>(static_cast<uintptr_t>(s_pickerPointers.size() - 1)));
        }
    }
}

} // namespace

// --- lifecycle ------------------------------------------------------------

void create(lv_obj_t* parent) {
    s_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_set_style_pad_row(s_page, 0, 0);
    lv_obj_set_scrollable(s_page, false);

    // No header row any more. The window list lives in the status bar and the
    // settings button sits in the input row, which gives the backlog back the
    // 30px that bar used to cost.
    s_tabs = statusbar::centerSlot();

    // The message grid.
    lv_obj_t* viewHost = lv_obj_create(s_page);
    lv_obj_remove_style_all(viewHost);
    lv_obj_set_width(viewHost, LV_PCT(100));
    lv_obj_set_flex_grow(viewHost, 1);
    // Extra room at the top so the first row of text is not flush against the
    // header bar above it.
    lv_obj_set_style_pad_all(viewHost, 2, 0);
    lv_obj_set_style_pad_top(viewHost, 4, 0);
    lv_obj_set_style_pad_bottom(viewHost, 0, 0);
    lv_obj_set_scrollable(viewHost, false);

    // Marked live before applySettings(), which refuses to touch the view
    // unless this screen is up.
    s_alive = true;
    s_view.create(viewHost);
    applySettings();

    // Sits over the message grid while there is nothing to read. Created after
    // the view so it draws on top of it.
    s_offlinePanel = lv_obj_create(viewHost);
    lv_obj_remove_style_all(s_offlinePanel);
    lv_obj_set_size(s_offlinePanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_offlinePanel, theme::background(), 0);
    lv_obj_set_style_bg_opa(s_offlinePanel, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_offlinePanel, false);
    lv_obj_set_flex_flow(s_offlinePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_offlinePanel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_offlinePanel, 10, 0);

    // A live account of what the device is doing, rather than a button that
    // asks whether you would like it to start. It connects on its own.
    s_offlineTitle = lv_label_create(s_offlinePanel);
    lv_label_set_long_mode(s_offlineTitle, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(s_offlineTitle, LV_PCT(90), 0);
    lv_obj_set_style_text_font(s_offlineTitle, theme::uiFont(), 0);
    lv_obj_set_style_text_color(s_offlineTitle, theme::accent(), 0);
    lv_label_set_text(s_offlineTitle, "Starting up");

    s_offlineDetail = lv_label_create(s_offlinePanel);
    lv_label_set_long_mode(s_offlineDetail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_offlineDetail, LV_PCT(80));
    lv_obj_set_style_text_align(s_offlineDetail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_offlineDetail, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(s_offlineDetail, theme::textDim(), 0);
    lv_label_set_text(s_offlineDetail, "");

    // Only shown when there is nothing configured to connect to.
    s_offlineButton = lv_button_create(s_offlinePanel);
    lv_obj_set_size(s_offlineButton, 150, 34);
    lv_obj_set_style_bg_color(s_offlineButton, theme::accent(), 0);
    lv_obj_set_style_radius(s_offlineButton, 6, 0);
    lv_group_add_obj(input::group(), s_offlineButton);

    lv_obj_t* settingsShortcut = lv_label_create(s_offlineButton);
    lv_label_set_text(settingsShortcut, LV_SYMBOL_SETTINGS "  Settings");
    lv_obj_set_style_text_font(settingsShortcut, theme::uiFont(), 0);
    lv_obj_set_style_text_color(settingsShortcut, theme::background(), 0);
    lv_obj_center(settingsShortcut);

    lv_obj_add_event_cb(s_offlineButton, [](lv_event_t*) {
        ui::openApp(ui::AppId::Settings);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_hidden(s_offlineButton, true);

    // Input line.
    // Input row: the bar toggle sits beside the text field so the header can be
    // dropped for two more lines of backlog without leaving the keyboard.
    lv_obj_t* inputRow = lv_obj_create(s_page);
    lv_obj_remove_style_all(inputRow);
    // 22px rather than 30. The text field is given the small mono font and
    // near-zero vertical padding so it still fits whatever the chat font is
    // set to, and the eight pixels go back to the backlog.
    lv_obj_set_size(inputRow, LV_PCT(100), 22);
    lv_obj_set_flex_flow(inputRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(inputRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(inputRow, 2, 0);
    lv_obj_set_scrollable(inputRow, false);

    lv_obj_t* barToggle = lv_obj_create(inputRow);
    lv_obj_remove_style_all(barToggle);
    lv_obj_set_size(barToggle, 24, 20);
    lv_obj_set_clickable(barToggle, true);
    lv_obj_set_scrollable(barToggle, false);
    lv_obj_set_style_bg_color(barToggle, theme::surfaceAlt(), 0);
    lv_obj_set_style_bg_opa(barToggle, LV_OPA_COVER, 0);
    lv_group_add_obj(input::group(), barToggle);

    s_barToggleLabel = lv_label_create(barToggle);
    lv_obj_set_style_text_font(s_barToggleLabel, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(s_barToggleLabel, theme::textDim(), 0);
    lv_obj_center(s_barToggleLabel);

    lv_obj_add_event_cb(barToggle, [](lv_event_t*) {
        settings::setBool("irc_topbar", !settings::getBool("irc_topbar"));
        applyTopBar();
    }, LV_EVENT_CLICKED, nullptr);

    s_input = lv_textarea_create(inputRow);
    lv_obj_set_height(s_input, 22);
    lv_obj_set_flex_grow(s_input, 1);
    lv_textarea_set_one_line(s_input, true);
    lv_textarea_set_placeholder_text(s_input, "message or /command");
    lv_textarea_set_max_length(s_input, 400);
    lv_obj_set_style_bg_color(s_input, theme::inputBg(), 0);
    lv_obj_set_style_border_color(s_input, theme::border(), 0);
    lv_obj_set_style_border_width(s_input, 1, 0);
    lv_obj_set_style_radius(s_input, 0, 0);
    lv_obj_set_style_pad_ver(s_input, 1, 0);
    lv_obj_set_style_pad_hor(s_input, 4, 0);
    // Always the small mono font, not termFont(): the chat font can be set to
    // the 9x20 face, and a 20px line cannot fit a 22px row that also has a
    // border. What you type still lines up with the chat, just smaller.
    lv_obj_set_style_text_font(s_input, &acid_mono_10, 0);
    lv_obj_set_style_text_color(s_input, theme::text(), 0);
    lv_obj_add_event_cb(s_input, inputEventCb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_input, inputEventCb, LV_EVENT_VALUE_CHANGED, nullptr);

    // The completion suggestion, drawn over the field. A child of the textarea
    // so it is clipped by it, and non-clickable so it cannot eat a tap meant
    // for the input underneath.
    s_ghost = lv_label_create(s_input);
    lv_obj_set_style_text_color(s_ghost, theme::textFaint(), 0);
    lv_obj_set_style_text_font(s_ghost, &acid_mono_10, 0);
    lv_obj_set_clickable(s_ghost, false);
    lv_label_set_text(s_ghost, "");
    lv_obj_set_hidden(s_ghost, true);

    lv_group_add_obj(input::group(), s_input);

    lv_obj_t* settingsButton = lv_obj_create(inputRow);
    lv_obj_remove_style_all(settingsButton);
    lv_obj_set_size(settingsButton, 24, 20);
    lv_obj_set_clickable(settingsButton, true);
    lv_obj_set_scrollable(settingsButton, false);
    lv_obj_set_style_bg_color(settingsButton, theme::surfaceAlt(), 0);
    lv_obj_set_style_bg_opa(settingsButton, LV_OPA_COVER, 0);
    lv_group_add_obj(input::group(), settingsButton);

    lv_obj_t* settingsLabel = lv_label_create(settingsButton);
    lv_label_set_text(settingsLabel, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(settingsLabel, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(settingsLabel, theme::textDim(), 0);
    lv_obj_center(settingsLabel);

    lv_obj_add_event_cb(settingsButton, [](lv_event_t*) {
        ui::openApp(ui::AppId::Settings);
    }, LV_EVENT_CLICKED, nullptr);

    lv_group_focus_obj(s_input);

    applyTopBar();

    input::setKeyHook(keyHook);

    selectBuffer(s_activeBuffer);
    LOG_I(TAG, "IRC window ready");
}

void destroy() {
    // Never leave the status bar hidden for the next screen.
    if (statusbar::object()) lv_obj_set_hidden(statusbar::object(), false);

    s_alive = false;
    closeInfoPanel();
    closeBufferPicker();
    input::clearKeyHook();
    s_view.setDocument(nullptr);

    // The window list lives in the shared status bar, so it has to be handed
    // back rather than dying with this screen.
    statusbar::clearCenterSlot();

    s_page  = nullptr;
    s_tabs  = nullptr;
    s_input = nullptr;
    s_offlinePanel    = nullptr;
    s_pickerPanel     = nullptr;
    s_offlineTitle    = nullptr;
    s_offlineDetail   = nullptr;
    s_offlineButton   = nullptr;
    s_barToggleLabel  = nullptr;
    s_ghost           = nullptr;
    s_completing      = false;
    s_tabButtons.clear();
}

void tick() {
    if (!s_alive) return;

    static uint32_t lastTitle = 0;
    const uint32_t  now       = millis();
    if (now - lastTitle > 1000) {
        lastTitle = now;
        updateHeaderButtons();
        refreshInfoPanel();
    }
}

void applySettings() {
    // The settings screen calls this for any irc_* or term_* change, and by
    // then this screen has been torn down and its LVGL objects freed. Touching
    // the view then wrote style into freed memory, which is why toggling a
    // chat setting rebooted the device.
    if (!s_alive) return;

    s_view.setFont(theme::termFont());
    // No extra spacing. The grid is already as tight as the 6x14 face allows,
    // and this was a per-pixel knob on something nobody wanted to tune.
    s_view.setLineSpacing(0);
    // The view still understands three modes; the setting only offers HH:MM
    // or nothing, because HH:MM:SS cost three more columns per line and told
    // nobody anything they needed.
    s_view.setTimestampMode(settings::getBool("irc_ts") ? 1 : 0);
    s_view.setFormatOptions(currentFormatOptions());

    // The input keeps the small mono font whatever the chat font is set to -
    // see the comment where it is created.
}

// --- notifications from the client ---------------------------------------

void onBufferChanged(IrcBuffer& buffer) {
    if (!s_alive) return;

    if (&buffer.doc == s_view.document()) {
        if (s_view.atBottom()) s_view.scrollToBottom();
        lv_obj_invalidate(s_view.object());
        buffer.doc.unread          = 0;
        buffer.doc.unreadHighlight = false;
        refreshNotificationDot();
    } else {
        onBufferListChanged();   // refresh the unread badge
    }
}

void onBufferListChanged() {
    if (!s_alive || !s_tabs) return;

    // The relay rebuilds its whole buffer list on reconnect and erases entries
    // when WeeChat closes a buffer, which frees the TermDoc the view is
    // pointing at. Re-anchor before anything can draw through a stale pointer.
    //
    // The index is found again from the pointer: a buffer closed or moved
    // above the active one shifts it, which used to switch the view (and
    // where typed text went) to a different buffer.
    if (ui::relayMode()) {
        WeechatRelay& relay = ui::relay();
        bool found = false;
        for (size_t i = 0; i < relay.bufferCount(); i++) {
            if (relay.buffer(i).pointer == s_activeRelayPointer) {
                s_activeBuffer = i;
                found = true;
                break;
            }
        }
        if (!found) {
            if (s_activeBuffer >= windowCount()) s_activeBuffer = 0;
            if (s_activeBuffer < windowCount()) {
                s_activeRelayPointer = relay.buffer(s_activeBuffer).pointer;
                relay.ensureLines(relay.buffer(s_activeBuffer));
            }
        }
        TermDoc* doc = windowDoc(s_activeBuffer);
        if (doc != s_view.document()) s_view.setDocument(doc);
    }

    lv_obj_clean(s_tabs);
    s_tabButtons.clear();

    // Nothing to list until there is something to switch between. The live
    // status panel covers everything up to being connected.
    if (!windowIsReady()) return;

    if (ui::relayMode()) {
        // One button naming the current window, which opens the picker. Tens
        // of buffers across several networks cannot be a strip of tabs.
        lv_obj_t* current = lv_obj_create(s_tabs);
        lv_obj_remove_style_all(current);
        lv_obj_set_size(current, LV_SIZE_CONTENT, 18);
        lv_obj_set_style_pad_hor(current, 6, 0);
        lv_obj_set_style_radius(current, 3, 0);
        lv_obj_set_style_bg_color(current, theme::accent(), 0);
        lv_obj_set_style_bg_opa(current, LV_OPA_COVER, 0);
        lv_obj_set_clickable(current, true);
        lv_obj_set_scrollable(current, false);
        lv_group_add_obj(input::group(), current);

        lv_obj_t* label = lv_label_create(current);
        const String group = windowGroup(s_activeBuffer);
        String text = group.isEmpty() ? windowName(s_activeBuffer)
                                      : group + " / " + windowName(s_activeBuffer);
        text += "  " LV_SYMBOL_DOWN;
        lv_label_set_text(label, text.c_str());
        lv_obj_set_style_text_font(label, theme::uiFontSmall(), 0);
        lv_obj_set_style_text_color(label, theme::background(), 0);
        lv_obj_center(label);

        lv_obj_add_event_cb(current, [](lv_event_t*) { openBufferPicker(); },
                            LV_EVENT_CLICKED, nullptr);
        return;
    }

    IrcClient& client = ui::irc();
    for (size_t i = 0; i < client.bufferCount(); i++) {
        IrcBuffer& buffer = client.buffer(i);

        // Sized for the status bar rather than a row of its own: 18px tall
        // with tight padding, so a handful of windows fit across 320px.
        lv_obj_t* tab = lv_obj_create(s_tabs);
        lv_obj_remove_style_all(tab);
        lv_obj_set_size(tab, LV_SIZE_CONTENT, 18);
        lv_obj_set_style_pad_hor(tab, 4, 0);
        lv_obj_set_style_radius(tab, 3, 0);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(tab, false);
        lv_obj_set_clickable(tab, true);

        const bool active = i == s_activeBuffer;
        lv_obj_set_style_bg_color(tab,
                                  active ? theme::accent() : theme::surfaceAlt(), 0);

        lv_obj_t* label = lv_label_create(tab);
        String text = String(i) + ":" + (buffer.isStatus() ? String("status") : buffer.name);
        if (!active && buffer.doc.unread > 0) text += " (" + String(buffer.doc.unread) + ")";

        // With the info button gone, the tab is the way in - so the one you
        // can tap for details says so rather than leaving it to be discovered.
        if (active && buffer.isChannel()) text += "  " LV_SYMBOL_LIST;

        lv_label_set_text(label, text.c_str());
        lv_obj_set_style_text_font(label, theme::uiFontSmall(), 0);

        lv_color_t colour = active ? theme::background() : theme::textDim();
        if (!active && buffer.doc.unreadHighlight) colour = lv_color_hex(theme::kDanger);
        lv_obj_set_style_text_color(label, colour, 0);
        lv_obj_center(label);

        lv_obj_add_event_cb(tab, tabEventCb, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
        s_tabButtons.push_back(tab);
    }

    // Keep the active tab in view when there are more than fit.
    if (s_activeBuffer < s_tabButtons.size()) {
        lv_obj_scroll_to_view(s_tabButtons[s_activeBuffer], LV_ANIM_OFF);
    }
}

void onWindowContentChanged() {
    if (!s_alive) return;

    // The relay hands over a buffer rather than a document, so the cheapest
    // correct thing is to ask whether the one on screen is the one that moved.
    TermDoc* active = windowDoc(s_activeBuffer);
    if (active != nullptr && active == s_view.document()) {
        if (s_view.atBottom()) s_view.scrollToBottom();
        lv_obj_invalidate(s_view.object());
        active->unread          = 0;
        active->unreadHighlight = false;
    }
    refreshNotificationDot();
}

void onRelayStateChanged() {
    if (!s_alive) return;

    closeBufferPicker();      // its indices belong to the old buffer list
    updateHeaderButtons();
    onBufferListChanged();

    // Land somewhere sensible once the buffer list arrives.
    if (windowIsReady() && s_activeBuffer >= windowCount()) selectBuffer(0);
    else if (windowIsReady())                              selectBuffer(s_activeBuffer);
}

void onIrcStateChanged(IrcState state) {
    LV_UNUSED(state);
    if (!s_alive) return;

    updateHeaderButtons();
    // Going offline empties the tab row, and coming back fills it again.
    onBufferListChanged();
}

} // namespace ircapp
