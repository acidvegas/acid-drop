#include "relay/WeechatRelay.h"

#include <WiFi.h>

#include <algorithm>

#include "core/Log.h"
#include "core/Settings.h"

namespace {

constexpr const char* TAG = "relay";

// A frame larger than this is a corrupt length field, not a big answer. Real
// answers do get large: a nicklist costs 70-80 bytes a nick, and a 200 line
// backlog runs to tens of kilobytes. Anything over 4KB is allocated from PSRAM
// (CONFIG_SPIRAM_USE_MALLOC), which has megabytes to spare.
constexpr uint32_t kMaxFrame = 512 * 1024;

// Quiet for this long and the link is probed; quiet for this much longer and
// it is treated as dead.
constexpr uint32_t kPingAfterMs      = 60000;
constexpr uint32_t kSilenceTimeoutMs = 180000;

// Message ids this client sends, so replies can be told apart from the
// unsolicited updates that arrive once sync is on.
constexpr const char* kIdBufferList = "buffers";
// A backlog reply's id carries the buffer it was asked for ("lines-0x..."), so
// a reply with no lines in it still says which buffer to clear.
constexpr const char* kIdLines      = "lines-";
constexpr const char* kIdNicklist   = "nicks";
constexpr const char* kIdPalette    = "palette";
constexpr const char* kIdColors     = "colors";

// Line notify levels (GUI_HOTLIST_* in src/gui/gui-hotlist.h). -1 is a line
// that notifies nothing, such as your own messages.
constexpr int kNotifyPrivate = 2;

// WeeChat's colour options by their \x19NN index: the GUI_COLOR_* enum in
// src/gui/gui-color.h, bound to options in gui_color_init_weechat()
// (src/gui/curses/gui-curses-color.c). Each is a foreground and background
// option name. 17-26 are obsolete nick colours with fixed values, marked by a
// leading '=' on the foreground.
struct OptionColor {
    const char* fg;
    const char* bg;
};
constexpr OptionColor kOptionColors[] = {
    {"separator", "chat_bg"},                    // 0
    {"chat", "chat_bg"},
    {"chat_time", "chat_bg"},
    {"chat_time_delimiters", "chat_bg"},
    {"chat_prefix_error", "chat_bg"},
    {"chat_prefix_network", "chat_bg"},          // 5
    {"chat_prefix_action", "chat_bg"},
    {"chat_prefix_join", "chat_bg"},
    {"chat_prefix_quit", "chat_bg"},
    {"chat_prefix_more", "chat_bg"},
    {"chat_prefix_suffix", "chat_bg"},           // 10
    {"chat_buffer", "chat_bg"},
    {"chat_server", "chat_bg"},
    {"chat_channel", "chat_bg"},
    {"chat_nick", "chat_bg"},
    {"chat_nick_self", "chat_bg"},               // 15
    {"chat_nick_other", "chat_bg"},
    {"=cyan", "chat_bg"},
    {"=magenta", "chat_bg"},
    {"=green", "chat_bg"},
    {"=brown", "chat_bg"},                       // 20
    {"=lightblue", "chat_bg"},
    {"=default", "chat_bg"},
    {"=lightcyan", "chat_bg"},
    {"=lightmagenta", "chat_bg"},
    {"=lightgreen", "chat_bg"},                  // 25
    {"=blue", "chat_bg"},
    {"chat_host", "chat_bg"},
    {"chat_delimiters", "chat_bg"},
    {"chat_highlight", "chat_highlight_bg"},
    {"chat_read_marker", "chat_read_marker_bg"}, // 30
    {"chat_text_found", "chat_text_found_bg"},
    {"chat_value", "chat_bg"},
    {"chat_prefix_buffer", "chat_bg"},
    {"chat_tags", "chat_bg"},
    {"chat_inactive_window", "chat_bg"},         // 35
    {"chat_inactive_buffer", "chat_bg"},
    {"chat_prefix_buffer_inactive_buffer", "chat_bg"},
    {"chat_nick_offline", "chat_bg"},
    {"chat_nick_offline_highlight", "chat_nick_offline_highlight_bg"},
    {"chat_nick_prefix", "chat_bg"},             // 40
    {"chat_nick_suffix", "chat_bg"},
    {"emphasized", "emphasized_bg"},
    {"chat_day_change", "chat_bg"},
    {"chat_value_null", "chat_bg"},
    {"chat_status_disabled", "chat_bg"},         // 45
    {"chat_status_enabled", "chat_bg"},
};

// init takes comma-separated options and unescapes "\," back into a comma
// (string_split_command in src/core/core-string.c), so a comma in the
// password has to be escaped or it splits the option in two.
String escapeInitValue(const String& value) {
    String out;
    out.reserve(value.length() + 4);
    for (unsigned int i = 0; i < value.length(); i++) {
        if (value[i] == ',') out += '\\';
        out += value[i];
    }
    return out;
}

// A connection attempt in flight on its own task, exactly as the IRC client
// does it: a TLS handshake takes seconds and would otherwise freeze the UI.
struct ConnectJob {
    String        host;
    uint16_t      port = 0;
    bool          tls  = false;
    WiFiClient*   socket    = nullptr;
    IPAddress     resolved;              // 0.0.0.0 when the name did not resolve
    volatile bool done      = false;
    volatile bool ok        = false;
    volatile bool abandoned = false;
};

void connectTask(void* arg) {
    ConnectJob* job = static_cast<ConnectJob*>(arg);

    WiFiClient* socket;
    if (job->tls) {
        auto* secure = new WiFiClientSecure();
        // A relay is usually a self-signed certificate on a machine you own,
        // so this does not verify. The alternative is refusing to connect to
        // the overwhelmingly common setup.
        secure->setInsecure();
        secure->setHandshakeTimeout(12);
        secure->setTimeout(12);
        socket = secure;
    } else {
        socket = new WiFiClient();
        socket->setTimeout(8);
    }

    // Resolve first and record it, so a name that does not resolve can be
    // told apart from a port that is closed. Those have completely different
    // fixes and "could not reach" covered both.
    const bool resolved = WiFi.hostByName(job->host.c_str(), job->resolved);

    const bool ok = resolved && socket->connect(job->resolved, job->port);

    job->socket = socket;
    job->ok     = ok;
    job->done   = true;
    vTaskDelete(nullptr);
}

// WeeChat names buffers "plugin.network.channel". The middle part is the
// network for an irc buffer; anything else is grouped under its plugin.
String networkOf(const String& fullName) {
    const int first = fullName.indexOf('.');
    if (first < 0) return fullName;

    const int second = fullName.indexOf('.', first + 1);
    if (second < 0) return fullName.substring(first + 1);   // e.g. "core.weechat"
    return fullName.substring(first + 1, second);
}

} // namespace

// --- lifecycle ------------------------------------------------------------

void WeechatRelay::begin() {
    applySettings();
}

void WeechatRelay::applySettings() {
    m_cfg.host       = settings::getText("relay_host");
    m_cfg.port       = static_cast<uint16_t>(settings::getInt("relay_port"));
    m_cfg.password   = settings::getText("relay_pass");
    m_cfg.tls        = settings::getBool("relay_tls");
    m_cfg.backlog    = static_cast<uint16_t>(settings::getInt("relay_backlog"));
    m_cfg.scrollback = static_cast<uint16_t>(settings::getInt("irc_scrollbk"));

    for (auto& buffer : m_buffers) buffer->doc.setMaxLines(m_cfg.scrollback);
}

void WeechatRelay::setState(RelayState next) {
    if (m_state == next) return;
    m_state = next;
    if (onStateChanged) onStateChanged(next);
}

String WeechatRelay::stateText() const {
    if (WiFi.status() != WL_CONNECTED && m_state != RelayState::Ready) return "No network";
    switch (m_state) {
        case RelayState::Offline:        return "Offline";
        case RelayState::Connecting:     return "Connecting";
        case RelayState::Authenticating: return "Authenticating";
        case RelayState::Syncing:        return "Loading buffers";
        case RelayState::Ready:          return "Connected";
        case RelayState::Reconnecting:   return "Reconnecting";
    }
    return "?";
}

bool WeechatRelay::isConnected() const { return m_socket && m_socket->connected(); }

void WeechatRelay::connect() {
    m_wantConnection = true;
    m_userQuit       = false;
    m_reconnectDelay = 0;
    m_reconnectAt    = 0;

    if (WiFi.status() != WL_CONNECTED) { setState(RelayState::Offline); return; }
    if (m_state == RelayState::Offline || m_state == RelayState::Reconnecting) startConnect();
}

void WeechatRelay::disconnect(bool stayOffline) {
    if (m_job != nullptr) {
        static_cast<ConnectJob*>(m_job)->abandoned = true;
        m_orphanedJobs.push_back(m_job);
        m_job = nullptr;
    }

    if (isConnected()) {
        sendCommand("quit");
        m_socket->flush();
        m_socket->stop();
    }
    m_socket.reset();
    m_rx.clear();

    if (stayOffline) {
        m_wantConnection = false;
        m_userQuit       = true;
    }
    setState(stayOffline ? RelayState::Offline : RelayState::Reconnecting);
}

void WeechatRelay::startConnect() {
    if (m_cfg.host.isEmpty()) {
        m_lastError = "No relay host configured";
        setState(RelayState::Offline);
        return;
    }

    setState(RelayState::Connecting);
    LOG_I(TAG, "connecting to %s:%u tls=%d", m_cfg.host.c_str(), m_cfg.port,
          m_cfg.tls ? 1 : 0);

    m_socket.reset();
    m_rx.clear();

    auto* job = new ConnectJob();
    job->host = m_cfg.host;
    job->port = m_cfg.port;
    job->tls  = m_cfg.tls;

    m_job = job;
    m_connectStartedAt = millis();

    if (xTaskCreate(connectTask, "relay-connect", 12288, job, 1, nullptr) != pdPASS) {
        LOG_E(TAG, "could not start the connect task");
        m_job = nullptr;
        delete job;
        scheduleReconnect();
    }
}

void WeechatRelay::pollConnect() {
    auto* job = static_cast<ConnectJob*>(m_job);
    if (job == nullptr) { scheduleReconnect(); return; }

    if (!job->done) {
        if (millis() - m_connectStartedAt > 30000UL) {
            LOG_W(TAG, "connect task overran, abandoning it");
            job->abandoned = true;
            m_orphanedJobs.push_back(m_job);
            m_job = nullptr;
            scheduleReconnect();
        }
        return;
    }

    const bool      ok       = job->ok;
    WiFiClient*     socket   = job->socket;
    const IPAddress resolved = job->resolved;
    m_job = nullptr;
    delete job;

    if (!ok) {
        if (resolved == IPAddress(0, 0, 0, 0)) {
            m_lastError = "Cannot resolve " + m_cfg.host;
        } else if (m_cfg.tls) {
            // The name resolved and the port was dialled, so this is either a
            // closed port or a relay that is not speaking TLS. Both look the
            // same from here, and both are worth naming.
            m_lastError = "No TLS relay on " + resolved.toString() + ":" +
                          String(m_cfg.port) + " - check it is listening and "
                          "was added as tls.weechat";
        } else {
            m_lastError = "Nothing listening on " + resolved.toString() + ":" +
                          String(m_cfg.port);
        }

        LOG_W(TAG, "%s", m_lastError.c_str());
        if (socket) { socket->stop(); delete socket; }
        scheduleReconnect();
        return;
    }

    LOG_I(TAG, "connected to %s:%u", resolved.toString().c_str(), m_cfg.port);

    m_socket.reset(socket);
    m_lastError     = "";
    m_lastMessageAt = millis();

    // Compression off keeps zlib out of the firmware. WeeChat is happy with
    // it, and the frames this client asks for are small.
    sendCommand("init password=" + escapeInitValue(m_cfg.password) + ",compression=off");
    setState(RelayState::Authenticating);

    // The colour options, so \x19NN codes in lines can be drawn in the colours
    // WeeChat itself uses. The palette comes first: a colour option's value can
    // be one of its aliases.
    sendCommand("(" + String(kIdPalette) + ") infolist option 0 weechat.palette.*");
    sendCommand("(" + String(kIdColors) + ") infolist option 0 weechat.color.*");

    // There is no positive acknowledgement for init: a good password produces
    // silence and a bad one closes the socket. So the buffer list request is
    // what actually proves the connection works.
    requestBufferList();
}

void WeechatRelay::onDisconnected(const char* why) {
    LOG_W(TAG, "disconnected: %s", why);
    m_lastError = why;

    // WiFi can drop while a connect is still in flight. The job owns a socket
    // and has to be handed to the orphan list, as disconnect() does, or the
    // next startConnect() overwrites it and it is never freed.
    if (m_job != nullptr) {
        static_cast<ConnectJob*>(m_job)->abandoned = true;
        m_orphanedJobs.push_back(m_job);
        m_job = nullptr;
    }

    if (m_socket) m_socket->stop();
    m_socket.reset();
    m_rx.clear();
    scheduleReconnect();
}

void WeechatRelay::scheduleReconnect() {
    if (!m_wantConnection) { setState(RelayState::Offline); return; }

    if (m_reconnectDelay == 0) m_reconnectDelay = 5;
    else {
        m_reconnectDelay *= 2;
        if (m_reconnectDelay > 120) m_reconnectDelay = 120;
    }

    m_reconnectAt = millis() + m_reconnectDelay * 1000UL;
    setState(RelayState::Reconnecting);
}

// --- pump -----------------------------------------------------------------

void WeechatRelay::loop() {
    for (size_t i = 0; i < m_orphanedJobs.size();) {
        auto* job = static_cast<ConnectJob*>(m_orphanedJobs[i]);
        if (!job->done) { i++; continue; }
        if (job->socket) { job->socket->stop(); delete job->socket; }
        delete job;
        m_orphanedJobs.erase(m_orphanedJobs.begin() + i);
    }

    if (!m_wantConnection) return;

    if (WiFi.status() != WL_CONNECTED) {
        if (m_state != RelayState::Offline && m_state != RelayState::Reconnecting) {
            onDisconnected("WiFi lost");
        }
        return;
    }

    if (m_state == RelayState::Connecting) { pollConnect(); return; }

    if (m_state == RelayState::Reconnecting) {
        if (static_cast<int32_t>(millis() - m_reconnectAt) >= 0) startConnect();
        return;
    }

    if (m_state == RelayState::Offline) { startConnect(); return; }

    if (!isConnected()) { onDisconnected("connection closed"); return; }

    handleSocket();

    // Reloads asked for while reading are sent once here, so a redraw that
    // replaces fifty lines of a free buffer costs one request, not fifty.
    for (auto& buffer : m_buffers) {
        if (!buffer->reloadPending) continue;
        buffer->reloadPending = false;
        reloadLines(*buffer);
    }

    // A relay that stops answering leaves the socket looking open, which is
    // the usual way a TCP session rots. The protocol has a ping for exactly
    // this; without it the device would sit on a dead connection forever.
    const uint32_t now = millis();
    const int32_t  silence = static_cast<int32_t>(now - m_lastMessageAt);

    if (silence > static_cast<int32_t>(kSilenceTimeoutMs)) {
        onDisconnected("relay stopped responding");
        return;
    }
    if (silence > static_cast<int32_t>(kPingAfterMs) &&
        static_cast<int32_t>(now - m_lastPingAt) > static_cast<int32_t>(kPingAfterMs)) {
        m_lastPingAt = now;
        sendCommand("ping");
    }
}

void WeechatRelay::handleSocket() {
    // Bounded per call so a burst cannot starve the UI, and bounded in total
    // so a frame that never completes cannot grow without limit.
    int budget = 2048;

    while (m_socket->available() && budget-- > 0) {
        const int incoming = m_socket->read();
        if (incoming < 0) break;

        if (m_rx.size() >= kMaxFrame) {
            onDisconnected("oversized frame");
            return;
        }
        m_rx.push_back(static_cast<uint8_t>(incoming));

        // Wait for the length field before deciding anything else.
        if (m_rx.size() < 5) continue;

        const uint32_t frameLength = (static_cast<uint32_t>(m_rx[0]) << 24) |
                                     (static_cast<uint32_t>(m_rx[1]) << 16) |
                                     (static_cast<uint32_t>(m_rx[2]) << 8)  |
                                      static_cast<uint32_t>(m_rx[3]);

        if (frameLength < 5 || frameLength > kMaxFrame) {
            onDisconnected("bad frame length");
            return;
        }
        if (m_rx.size() < frameLength) continue;

        const uint8_t compression = m_rx[4];
        if (compression != 0) {
            // We asked for compression=off, so this should not happen. It is
            // not worth carrying zlib to find out.
            LOG_E(TAG, "relay sent a compressed frame despite compression=off");
            onDisconnected("unexpected compression");
            return;
        }

        weechat::Reader reader(m_rx.data() + 5, frameLength - 5);
        const String id = reader.readString();
        if (reader.ok()) handleMessage(id, reader);
        else             LOG_W(TAG, "malformed frame, ignored");

        m_lastMessageAt = millis();
        m_rx.erase(m_rx.begin(), m_rx.begin() + frameLength);
    }
}

void WeechatRelay::sendCommand(const String& command) {
    if (!m_socket || !m_socket->connected()) return;
    const String out = command + "\n";
    m_socket->print(out);
    LOG_D(TAG, ">> %s", command.c_str());
}

void WeechatRelay::requestBufferList() {
    setState(RelayState::Syncing);
    sendCommand("(" + String(kIdBufferList) + ") hdata buffer:gui_buffers(*) "
                "number,full_name,short_name,title,type,local_variables");
    sendCommand("sync");
}

// --- messages -------------------------------------------------------------

void WeechatRelay::handleMessage(const String& id, weechat::Reader& reader) {
    LOG_D(TAG, "<< %s", id.c_str());

    if (id == kIdBufferList)              { handleBufferList(reader); return; }
    if (id == kIdPalette)                 { handlePalette(reader);    return; }
    if (id == kIdColors)                  { handleColors(reader);     return; }
    if (id.startsWith(kIdLines)) {
        handleLines(id.substring(strlen(kIdLines)), reader);
        return;
    }
    if (id == kIdNicklist ||
        id == "_nicklist")                { handleNicklist(reader);   return; }
    if (id == "_nicklist_diff")           { handleNicklistDiff(reader); return; }
    if (id == "_buffer_line_added")       { handleLineAdded(reader);  return; }
    if (id == "_buffer_opened")           { handleBufferOpened(reader); return; }
    if (id == "_buffer_closing")          { handleBufferClosing(reader); return; }
    if (id == "_buffer_renamed" ||
        id == "_buffer_title_changed" ||
        id == "_buffer_type_changed" ||
        id == "_buffer_localvar_added" ||
        id == "_buffer_localvar_changed" ||
        id == "_buffer_localvar_removed") { handleBufferRenamed(reader); return; }
    if (id == "_buffer_moved" ||
        id == "_buffer_merged" ||
        id == "_buffer_unmerged")         { handleBufferMoved(reader); return; }
    if (id == "_buffer_cleared")          { handleBufferCleared(reader); return; }
    if (id == "_buffer_line_data_changed") { handleLineDataChanged(reader); return; }

    // After /upgrade every buffer and line has a new pointer, so everything
    // held from before is stale. Fetch the list again, as on a fresh connect.
    if (id == "_upgrade_ended")           { requestBufferList(); return; }

    // Everything else is deliberately ignored rather than half-handled.
}

RelayBuffer& WeechatRelay::ensureBuffer(const String& pointer) {
    if (RelayBuffer* existing = findByPointer(pointer)) return *existing;

    auto created = std::unique_ptr<RelayBuffer>(new RelayBuffer());
    created->pointer = pointer;
    created->doc.setMaxLines(m_cfg.scrollback);
    m_buffers.push_back(std::move(created));
    return *m_buffers.back();
}

RelayBuffer* WeechatRelay::findByPointer(const String& pointer) {
    for (auto& buffer : m_buffers) {
        if (buffer->pointer == pointer) return buffer.get();
    }
    return nullptr;
}

std::vector<String> WeechatRelay::networks() const {
    std::vector<String> out;
    for (const auto& buffer : m_buffers) {
        bool seen = false;
        for (const String& name : out) {
            if (name == buffer->network) { seen = true; break; }
        }
        if (!seen) out.push_back(buffer->network);
    }
    return out;
}

void WeechatRelay::applyBufferItem(RelayBuffer& buffer, const weechat::HDataItem& item) {
    if (item.has("full_name"))  buffer.fullName  = item.value("full_name");
    if (item.has("short_name")) buffer.shortName = item.value("short_name");
    if (item.has("title"))      buffer.title     = item.value("title");   // null clears it
    if (item.has("number"))     buffer.number    = item.value("number").toInt();
    if (item.has("type"))       buffer.freeContent = item.value("type").toInt() == 1;

    // Every event that can rename a buffer also carries its local variables,
    // so the network is only worked out again when they are present. IRC sets
    // "server" on server, channel and private buffers alike; a server buffer's
    // name is "irc.server.<name>", so the name alone would group it as
    // "server".
    if (item.has("local_variables")) {
        const String vars   = item.value("local_variables");
        const String server = weechat::tableValue(vars, "server");
        buffer.kind    = weechat::tableValue(vars, "type");
        buffer.network = server.isEmpty() ? networkOf(buffer.fullName) : server;
    }

    if (buffer.shortName.isEmpty()) buffer.shortName = buffer.fullName;
}

// WeeChat orders buffers by its own number and people arrange those
// deliberately, so the picker follows that rather than arrival order.
void WeechatRelay::sortBuffers() {
    std::stable_sort(m_buffers.begin(), m_buffers.end(),
                     [](const std::unique_ptr<RelayBuffer>& a,
                        const std::unique_ptr<RelayBuffer>& b) {
                         return a->number < b->number;
                     });
}

void WeechatRelay::handleBufferList(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::HData) return;

    weechat::HData hdata;
    if (!reader.readHData(hdata)) { LOG_W(TAG, "bad buffer list"); return; }

    m_buffers.clear();

    for (const auto& item : hdata.items) {
        applyBufferItem(ensureBuffer(item.pointer()), item);
    }
    sortBuffers();

    LOG_I(TAG, "%u buffers across %u networks",
          (unsigned)m_buffers.size(), (unsigned)networks().size());

    setState(RelayState::Ready);
    if (onBufferListChanged) onBufferListChanged();
}

void WeechatRelay::handleBufferOpened(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::HData) return;

    weechat::HData hdata;
    if (!reader.readHData(hdata)) return;

    for (const auto& item : hdata.items) {
        applyBufferItem(ensureBuffer(item.pointer()), item);
    }
    sortBuffers();
    if (onBufferListChanged) onBufferListChanged();
}

// Moved, merged and unmerged all carry the buffer's new number.
void WeechatRelay::handleBufferMoved(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::HData) return;

    weechat::HData hdata;
    if (!reader.readHData(hdata)) return;

    for (const auto& item : hdata.items) {
        if (RelayBuffer* target = findByPointer(item.pointer())) applyBufferItem(*target, item);
    }
    sortBuffers();
    if (onBufferListChanged) onBufferListChanged();
}

void WeechatRelay::handleBufferCleared(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::HData) return;

    weechat::HData hdata;
    if (!reader.readHData(hdata)) return;

    for (const auto& item : hdata.items) {
        RelayBuffer* target = findByPointer(item.pointer());
        if (target == nullptr) continue;
        target->doc.clear();
        target->doc.unread          = 0;
        target->doc.unreadHighlight = false;
        if (onBufferChanged) onBufferChanged(*target);
    }
    if (onBufferListChanged) onBufferListChanged();
}

void WeechatRelay::handleBufferClosing(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::HData) return;

    weechat::HData hdata;
    if (!reader.readHData(hdata)) return;

    for (const auto& item : hdata.items) {
        const String pointer = item.pointer();
        for (size_t i = 0; i < m_buffers.size(); i++) {
            if (m_buffers[i]->pointer == pointer) {
                m_buffers.erase(m_buffers.begin() + i);
                break;
            }
        }
    }
    if (onBufferListChanged) onBufferListChanged();
}

void WeechatRelay::appendLine(RelayBuffer& target, const String& prefix,
                              const String& message, uint32_t stamp, bool highlight) {
    // WeeChat's colour encoding is its own, not mIRC. The message is
    // translated to ANSI so its colours and backgrounds survive. A nick
    // prefix is re-coloured with this firmware's own hash, which is the same
    // one the direct IRC client uses, so a person looks the same either way
    // round; an event prefix keeps WeeChat's colours.
    const String plainPrefix  = weechat::stripColors(prefix);
    const String formattedMessage = weechat::colorsToAnsi(message, &m_optionColors);

    String line;
    if (!plainPrefix.isEmpty()) {
        // The prefix carries the status character for a person ("@nick"), and
        // is a marker like "-->" or "--" for events. Only colour it when it
        // looks like somebody.
        String nick = plainPrefix;
        while (nick.length() > 0 && strchr("~&@%+", nick[0]) != nullptr) {
            nick = nick.substring(1);
        }

        const bool looksLikeNick =
            nick.length() > 0 && isalnum(static_cast<unsigned char>(nick[0]));

        if (looksLikeNick) {
            char color[8];
            snprintf(color, sizeof(color), "\x03%02u", textfmt::nickColorIndex(nick));
            line += String(color) + plainPrefix + "\x0F";
        } else {
            line += weechat::colorsToAnsi(prefix, &m_optionColors) + "\x1B[0m";
        }
        line += " ";
    }
    line += formattedMessage;

    target.doc.append(line, stamp, LINE_MESSAGE, highlight);
}

void WeechatRelay::handleLineAdded(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::HData) return;

    weechat::HData hdata;
    if (!reader.readHData(hdata)) return;

    for (const auto& item : hdata.items) {
        // For a line, the buffer is a key rather than a parent pointer.
        const String bufferPointer = item.value("buffer");
        RelayBuffer* target = findByPointer(bufferPointer);
        if (target == nullptr) continue;

        // A free buffer's lines are addressed by row, and WeeChat sends this
        // same event when it replaces one (gui_line_add_y), so appending would
        // pile up every redraw. Fetch the content again if it is on screen.
        if (target->freeContent) {
            if (target->linesLoaded) target->reloadPending = true;
            continue;
        }

        // Hidden by a /filter in WeeChat, so hidden here as well.
        if (item.value("displayed").toInt() == 0) continue;

        const bool highlight   = item.value("highlight").toInt() != 0;
        const int  notifyLevel = item.value("notify_level").toInt();
        appendLine(*target, item.value("prefix"), item.value("message"),
                   static_cast<uint32_t>(item.value("date").toInt()), highlight);

        // Counted the way WeeChat's hotlist counts: lines that notify nothing,
        // such as your own, are not unread.
        if (notifyLevel >= 0) target->doc.unread++;
        if (highlight) target->doc.unreadHighlight = true;

        // A private message has notify level 2 and no highlight unless it
        // also matches a highlight word (gui_line_set_notify_level), so it
        // alerts on its own.
        if ((highlight || notifyLevel == kNotifyPrivate) && onHighlight) {
            onHighlight(item.value("prefix"), item.value("message"), *target);
        }
        if (onBufferChanged) onBufferChanged(*target);
    }
}

// A line edited in place. The line cannot be found by id here, so an open
// buffer is fetched again; one that has not been opened fetches on opening.
void WeechatRelay::handleLineDataChanged(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::HData) return;

    weechat::HData hdata;
    if (!reader.readHData(hdata)) return;

    for (const auto& item : hdata.items) {
        RelayBuffer* target = findByPointer(item.value("buffer"));
        if (target != nullptr && target->linesLoaded) target->reloadPending = true;
    }
}

void WeechatRelay::handleLines(const String& bufferPointer, weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::HData) return;

    weechat::HData hdata;
    if (!reader.readHData(hdata)) return;

    RelayBuffer* target = findByPointer(bufferPointer);
    if (target == nullptr) return;

    // The backlog replaces whatever the buffer showed. Lines that arrived
    // live before it are already part of it: WeeChat answers in order, so the
    // backlog covers everything up to the reply and anything later arrives
    // after it. Appending instead printed those lines twice, and printed the
    // older history below them.
    target->doc.clear();

    // Backlog arrives NEWEST first. The request uses last_line(-N), and the
    // protocol defines a negative count as "iterate using previous element",
    // so the first item is the most recent line. Appending in arrival order
    // would print the history upside down.
    for (size_t i = hdata.items.size(); i-- > 0;) {
        const auto& item = hdata.items[i];
        if (item.value("displayed").toInt() == 0) continue;   // filtered in WeeChat

        appendLine(*target, item.value("prefix"), item.value("message"),
                   static_cast<uint32_t>(item.value("date").toInt()),
                   item.value("highlight").toInt() != 0);
    }

    if (onBufferChanged) onBufferChanged(*target);
}

void WeechatRelay::handleNicklistDiff(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::HData) return;

    weechat::HData hdata;
    if (!reader.readHData(hdata)) return;

    // Applying the diff means replaying adds, removes and changes against the
    // group tree, and the whole list is only a few hundred bytes. Asking for
    // it again is less code and cannot drift out of step with the server.
    for (const auto& item : hdata.items) {
        if (item.pointers.empty()) continue;

        // Only for a buffer that has been opened: sync covers every buffer,
        // so refreshing all of them fetched whole nicklists for channels
        // nobody was looking at on every join and quit.
        RelayBuffer* target = findByPointer(item.pointers[0]);
        if (target == nullptr || !target->linesLoaded) continue;

        sendCommand("(" + String(kIdNicklist) + ") nicklist " + target->pointer);
        break;   // one refresh covers every diff in this message
    }
}

// Names and titles change under us: a query buffer is renamed when the other
// side changes nick, and a channel's title is its topic.
void WeechatRelay::handleBufferRenamed(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::HData) return;

    weechat::HData hdata;
    if (!reader.readHData(hdata)) return;

    for (const auto& item : hdata.items) {
        if (RelayBuffer* target = findByPointer(item.pointer())) applyBufferItem(*target, item);
    }

    if (onBufferListChanged) onBufferListChanged();
}

void WeechatRelay::handleNicklist(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::HData) return;

    weechat::HData hdata;
    if (!reader.readHData(hdata)) return;

    RelayBuffer* target = nullptr;
    for (const auto& item : hdata.items) {
        if (!item.pointers.empty()) {
            RelayBuffer* candidate = findByPointer(item.pointers[0]);
            if (candidate != nullptr && candidate != target) {
                target = candidate;
                target->nicks.clear();
            }
        }
        if (target == nullptr) continue;

        // Groups are structural; only leaves are people.
        if (item.value("group").toInt() != 0) continue;

        const String name = item.value("name");
        if (!name.isEmpty()) target->nicks.push_back(name);
    }
}

// --- actions --------------------------------------------------------------

void WeechatRelay::ensureLines(RelayBuffer& target) {
    if (target.linesLoaded || !isConnected()) return;
    target.linesLoaded = true;

    reloadLines(target);
    sendCommand("(" + String(kIdNicklist) + ") nicklist " + target.pointer);
}

void WeechatRelay::reloadLines(RelayBuffer& target) {
    sendCommand("(" + String(kIdLines) + target.pointer + ") hdata buffer:" + target.pointer +
                "/own_lines/last_line(-" + String(m_cfg.backlog) +
                ")/data date,prefix,message,buffer,highlight,displayed");
}

// Palette entries are "weechat.palette.<number>", valued "alias;fg,bg;r/g/b"
// in any order. The item without '/' or ',' is the alias
// (gui_color_palette_add in src/gui/curses/gui-curses-color.c).
void WeechatRelay::handlePalette(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::InfoList) return;

    std::vector<weechat::InfoListItem> items;
    if (!reader.readInfoList(items)) return;

    m_paletteAliases.clear();
    for (const auto& item : items) {
        const String number = item.value("option_name");
        const String value  = item.value("value");

        int start = 0;
        while (start <= static_cast<int>(value.length())) {
            int end = value.indexOf(';', start);
            if (end < 0) end = value.length();
            String part = value.substring(start, end);
            part.trim();
            if (!part.isEmpty() && part.indexOf('/') < 0 && part.indexOf(',') < 0) {
                m_paletteAliases.emplace_back(part, number);
            }
            start = end + 1;
        }
    }
}

void WeechatRelay::handleColors(weechat::Reader& reader) {
    if (reader.readType() != weechat::Type::InfoList) return;

    std::vector<weechat::InfoListItem> items;
    if (!reader.readInfoList(items)) return;

    // An option's value, with its attribute characters kept and a palette
    // alias turned back into the number it names.
    auto valueOf = [&](const char* name) -> String {
        String value;
        if (name[0] == '=') {
            value = name + 1;
        } else {
            for (const auto& item : items) {
                if (item.value("option_name") == name) { value = item.value("value"); break; }
            }
        }

        unsigned int attrs = 0;
        while (attrs < value.length() && strchr("%.*!/_|", value[attrs]) != nullptr) attrs++;
        const String color = value.substring(attrs);
        for (const auto& alias : m_paletteAliases) {
            if (alias.first == color) return value.substring(0, attrs) + alias.second;
        }
        return value;
    };

    m_optionColors.clear();
    for (const OptionColor& option : kOptionColors) {
        m_optionColors.push_back(weechat::optionColorToAnsi(valueOf(option.fg), valueOf(option.bg)));
    }
}

void WeechatRelay::send(RelayBuffer& target, const String& text) {
    if (text.isEmpty()) return;
    // WeeChat parses this exactly as if it had been typed into that buffer, so
    // slash commands are its commands and need no handling here.
    sendCommand("input " + target.pointer + " " + text);
}
