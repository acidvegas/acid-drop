#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <functional>
#include <memory>
#include <vector>

#include "irc/IrcMessage.h"
#include "ui/TermView.h"

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

// How a line should be coloured. Kept out of the text itself so filters like
// "hide joins and parts" stay cheap.
enum LineKind : uint8_t {
    LINE_MESSAGE = 0,
    LINE_ACTION,
    LINE_NOTICE,
    LINE_JOIN,
    LINE_PART,
    LINE_QUIT,
    LINE_KICK,
    LINE_NICK,
    LINE_MODE,
    LINE_TOPIC,
    LINE_SERVER,
    LINE_ERROR,
    LINE_LOCAL,     // our own status text
    LINE_RAW,
};

struct IrcBuffer {
    String     name;          // "#chan", a nick, or "" for the status window
    BufferKind kind = BufferKind::Status;
    TermDoc    doc;

    // Channel state
    bool                joined      = false;
    bool                retryEnabled = true;   // from the saved channel config
    uint32_t            retryAt     = 0;   // millis, 0 when nothing is pending
    uint16_t            retryCount  = 0;
    String              retryReason;       // e.g. "+i", shown while retrying
    String              key;               // channel key, when one is known
    String              topic;
    String              modes;             // from RPL_CHANNELMODEIS
    std::vector<String> nicks;

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
    void handleCtcp(const IrcMessage& message, const String& target, String payload);
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
    String colorForNick(const String& nick) const;
    String formatNick(const String& nick) const;
    uint32_t lineStamp(const IrcMessage& message) const;

    void setState(IrcState next);
    void removeNickEverywhere(const String& nick, const String& reason, uint32_t stamp);

    // --- socket ---
    std::unique_ptr<WiFiClient> m_socket;
    void*    m_job = nullptr;      // in-flight ConnectJob, owned by the task
    bool     m_usingTls        = false;
    bool     m_triedTlsAlready = false;   // drives the plaintext fallback
    String   m_rxBuffer;

    IrcState m_state = IrcState::Offline;
    bool     m_wantConnection = false;

    // --- identity ---
    String m_nick;
    uint8_t m_nickAttempt = 0;

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
    struct {
        uint32_t joinDelayMs;
        uint32_t reconnectDelayS;
        uint32_t reconnectMaxS;
        uint32_t kickDelayS;
        uint32_t lockDelayS;
        uint32_t pingTimeoutS;
        bool     autoReconnect;
        bool     rejoinOnKick;
        bool     retryFailedJoins;
        bool     showJoinPart;
        bool     showModes;
        bool     showRaw;
        bool     allowCtcp;
        uint16_t scrollback;
    } m_cfg;
};
