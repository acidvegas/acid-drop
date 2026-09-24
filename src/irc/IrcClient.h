#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <functional>
#include <memory>
#include <vector>

#include "irc/IrcMessage.h"
#include "ui/TermView.h"

// How this firmware introduces itself: the quit message, the part message and
// the real name are all this, and none of them is configurable. The username
// is fixed too. These say what the client is, not who is using it.
constexpr const char* kIrcSignature = "ACID-DROP https://github.com/acidvegas/acid-drop";

enum class IrcState : uint8_t {
    Offline,        // not wanted, or WiFi is down
    Connecting,     // TCP/TLS handshake in flight
    Registering,    // CAP/SASL/NICK/USER sent, waiting for 001
    JoinDelay,      // registered, counting down before the first JOIN
    Ready,          // registered, channels joined or joining
    Reconnecting,   // dropped, waiting out the backoff
};

enum class BufferKind : uint8_t {
    Status,   // raw server traffic and anything with no better home
    Channel,
    Query,    // private message window
};

struct IrcBuffer {
    String     name;          // "#chan", a nick, or "" for the status window
    BufferKind kind = BufferKind::Status;
    TermDoc    doc;

    // Channel state
    bool                joined      = false;
    bool                retryEnabled = true;   // from the saved channel config
    bool                requested    = false;  // we asked to be here
    bool                namesLoading = false;  // mid RPL_NAMREPLY batch
    // While this is in the future, the replies to an info request are folded
    // into the window's state without also being printed. Opening the channel
    // details asks the server for MODE, TOPIC and NAMES, and without this all
    // three answers bleed into the backlog the moment the panel is closed.
    uint32_t            infoQuietUntil = 0;

    uint32_t            retryAt     = 0;   // millis, 0 when nothing is pending
    uint16_t            retryCount  = 0;
    String              retryReason;       // e.g. "+i", shown while retrying
    String              key;               // channel key, when one is known
    String              topic;
    String              modes;             // from RPL_CHANNELMODEIS
    std::vector<IrcNick> nicks;

    bool isStatus() const  { return kind == BufferKind::Status; }
    bool isChannel() const { return kind == BufferKind::Channel; }
};

class IrcClient {
public:
    // Fires whenever a buffer gained a line, so the view can redraw.
    std::function<void(IrcBuffer&)>          onBufferChanged;
    // Fires when buffers are created or removed.
    std::function<void()>                    onBufferListChanged;
    std::function<void(IrcState)>            onStateChanged;
    // nick, message, buffer - for the mention sound and notifications.
    std::function<void(const String&, const String&, IrcBuffer&)> onHighlight;

    void begin();
    void loop();

    // Connection control. `connect()` is also what the reconnect timer calls.
    void connect();
    void disconnect(const String& quitMessage, bool stayOffline);
    bool isConnected() const;
    IrcState state() const { return m_state; }
    String   stateText() const;

    // Sends a raw line. Appends CRLF and enforces the 512 byte limit.
    bool sendRaw(const String& line);

    // Sends `text` to `target` as a normal message, echoing it locally.
    void say(const String& target, const String& text);
    void action(const String& target, const String& text);
    void notice(const String& target, const String& text);

    // Asks the server for a channel's modes, topic and members, without the
    // answers being printed into the window.
    void requestChannelInfo(const String& channel);

    void join(const String& channel, const String& key = String());
    void part(const String& channel, const String& reason = String());
    void setNick(const String& nick);

    // Buffers. Index 0 is always the status window.
    size_t     bufferCount() const { return m_buffers.size(); }
    IrcBuffer& buffer(size_t index) { return *m_buffers[index]; }
    IrcBuffer& status() { return *m_buffers[0]; }
    IrcBuffer* findBuffer(const String& name);
    IrcBuffer& ensureBuffer(const String& name, BufferKind kind);
    bool       closeBuffer(size_t index);   // false for the status window

    const String& nick() const { return m_nick; }

    // True when the user deliberately went offline; auto-connect respects it.
    bool userQuit() const { return m_userQuit; }

    // The mIRC palette index this nick is drawn in, so the nick list can use
    // the same colour the messages do.
    uint8_t nickColorIndex(const String& nick) const;

    // Applies anything the settings screen may have changed.
    void applySettings();

private:
    // --- connection ---
    // The socket connect runs on its own task: a TLS handshake takes seconds
    // and the Arduino socket API has no non-blocking connect, so doing it
    // inline freezes the whole UI. startConnect() kicks it off, pollConnect()
    // picks up the result.
    void startConnect();
    void pollConnect();
    void reapOrphanedJobs();
    void handleSocket();
    void onDisconnected(const char* why);
    void scheduleReconnect();

    // --- protocol ---
    void handleLine(const String& raw);
    void dispatch(const IrcMessage& message);
    void registerConnection();
    void sendCapRequest();
    void finishCapNegotiation();
    void startSasl();

    void handleNumeric(const IrcMessage& message);
    void handlePrivmsg(const IrcMessage& message, bool isNotice);
    void handleCtcp(const IrcMessage& message, const String& target, String payload,
                    bool isNotice);
    void handleJoin(const IrcMessage& message);
    void handlePart(const IrcMessage& message);
    void handleKick(const IrcMessage& message);
    void handleQuit(const IrcMessage& message);
    void handleNickChange(const IrcMessage& message);
    void handleMode(const IrcMessage& message);
    void handleTopic(const IrcMessage& message);

    // --- joining ---
    void queueConfiguredChannels();
    void processJoinQueue();
    void scheduleJoinRetry(IrcBuffer& target, const String& reason);

    // --- output helpers ---
    void addLine(IrcBuffer& target, const String& text, uint8_t kind, bool highlight = false);
    void addStatus(const String& text, uint8_t kind = LINE_SERVER);
    bool isHighlight(const String& text) const;
    // True when this sender is on the ignore list, so nothing they send is
    // shown and no window is opened for them.
    bool isIgnored(const IrcMessage& message) const;
    String colorForNick(const String& nick) const;
    String formatNick(const String& nick) const;
    uint32_t lineStamp(const IrcMessage& message) const;

    void setState(IrcState next);
    void removeNickEverywhere(const String& nick, const String& reason, uint32_t stamp);

    // --- socket ---
    std::unique_ptr<WiFiClient> m_socket;
    void*    m_job = nullptr;      // in-flight ConnectJob
    // Attempts we stopped waiting for. The task never frees anything, so
    // ownership never crosses threads; these are reaped here once they finish.
    std::vector<void*> m_orphanedJobs;
    bool     m_usingTls        = false;
    bool     m_triedTlsAlready = false;   // drives the plaintext fallback
    bool     m_verifying       = false;   // this attempt checks the certificate
    bool     m_waitingForClock = false;   // said so once already
    String   m_rxBuffer;

    IrcState m_state = IrcState::Offline;
    bool     m_wantConnection = false;
    // Set by an explicit /quit or /disconnect, cleared by an explicit connect.
    // Auto-connect honours it, so "disconnect" does not mean "disconnect for
    // two seconds".
    bool     m_userQuit        = false;

    // --- identity ---
    // Last address the server name resolved to. DNS on a flaky link fails
    // intermittently, and re-resolving from scratch every attempt turns a
    // momentary lookup failure into a failed connection.
    uint32_t m_lastGoodAddress = 0;

    String m_nick;
    uint8_t m_nickAttempt = 0;

    // Parsed once in applySettings() rather than split on every line.
    std::vector<String> m_ignores;

    // --- timers (all millis) ---
    uint32_t m_connectStartedAt = 0;
    uint32_t m_reconnectAt      = 0;
    uint32_t m_reconnectDelay   = 0;
    uint32_t m_joinAt           = 0;
    uint32_t m_lastServerLine   = 0;
    uint32_t m_lastPingSent     = 0;

    // --- capabilities ---
    bool m_capNegotiating = false;
    bool m_saslRequested  = false;
    bool m_saslDone       = false;
    std::vector<String> m_capsEnabled;

    std::vector<std::unique_ptr<IrcBuffer>> m_buffers;

    // Cached settings, refreshed by applySettings().
    // Every field initialised: these are only safe uninitialised because the
    // client happens to be a global today, and a timer read before
    // applySettings() would otherwise be indeterminate.
    struct {
        uint32_t joinDelayMs      = 6000;
        uint32_t reconnectDelayS  = 5;
        uint32_t reconnectMaxS    = 120;
        uint32_t kickDelayS       = 3;
        uint32_t lockDelayS       = 5;
        uint32_t pingTimeoutS     = 260;
        bool     autoReconnect    = true;
        bool     rejoinOnKick     = true;
        bool     retryFailedJoins = true;
        bool     showJoinPart     = true;
        // Hides everything that is not someone talking - joins, parts, quits,
        // modes, topics, nick changes and other people's kicks. Things that
        // happened to you are never hidden.
        bool     filterMode        = false;
        bool     showModes        = true;
        bool     showRaw          = false;
        bool     allowCtcp        = true;
        uint16_t scrollback       = 400;
    } m_cfg;
};
