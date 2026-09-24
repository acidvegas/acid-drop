#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <functional>
#include <memory>
#include <vector>

#include "relay/WeechatProtocol.h"
#include "ui/TermView.h"

// A client for WeeChat's relay, speaking its native binary protocol.
//
// The point of this mode is that WeeChat is already on every network you care
// about, with your history and your nick, and it stays connected when this
// device does not. So the device stops being an IRC client and becomes a
// window onto one.
//
// Compression is negotiated off. WeeChat supports that, and it removes zlib
// from a firmware that would otherwise carry it for this one feature.

enum class RelayState : uint8_t {
    Offline,
    Connecting,     // TCP/TLS handshake in flight
    Authenticating, // init sent, waiting to see whether it was accepted
    Syncing,        // asking for the buffer list
    Ready,
    Reconnecting,
};

// One WeeChat buffer. `fullName` is WeeChat's own identifier and looks like
// "irc.libera.#channel" or "core.weechat". The network is what the picker
// groups by: the "server" local variable where there is one, otherwise the
// middle part of the name.
struct RelayBuffer {
    String  pointer;      // "0x..." - how WeeChat identifies it back to us
    String  fullName;
    String  shortName;
    String  network;
    String  kind;         // the "type" local variable: channel, private, server
    String  title;        // channel topic, for channels
    int32_t number = 0;   // WeeChat's own ordering
    bool    freeContent = false;   // a free buffer, whose lines are replaced by y

    TermDoc doc;
    bool    linesLoaded = false;   // backlog has been fetched at least once
    bool    reloadPending = false; // backlog to be fetched again on the next loop

    std::vector<String> nicks;

    bool isChannel() const { return kind == "channel"; }
    // A network's own server buffer, rather than a channel on it.
    bool isServer() const  { return !isChannel() && network.length() > 0; }
};

class WeechatRelay {
public:
    std::function<void(RelayBuffer&)> onBufferChanged;
    std::function<void()>             onBufferListChanged;
    std::function<void(RelayState)>   onStateChanged;
    std::function<void(const String&, const String&, RelayBuffer&)> onHighlight;

    void begin();
    void loop();

    void connect();
    void disconnect(bool stayOffline);
    bool isConnected() const;

    RelayState state() const { return m_state; }
    // True when the user deliberately went offline; auto-connect respects it.
    bool       userQuit() const { return m_userQuit; }
    String     stateText() const;
    // Why the last attempt failed, empty when it has not.
    const String& lastError() const { return m_lastError; }

    size_t       bufferCount() const { return m_buffers.size(); }
    RelayBuffer& buffer(size_t index) { return *m_buffers[index]; }
    RelayBuffer* findByPointer(const String& pointer);

    // Every distinct network, in the order the buffers appear, for the picker.
    std::vector<String> networks() const;

    // Fetches the backlog for a buffer the first time it is opened. WeeChat
    // will happily describe forty buffers; asking for all their history up
    // front would not fit.
    void ensureLines(RelayBuffer& target);

    // Sends a line as if typed into that buffer in WeeChat. Commands work too,
    // because WeeChat parses them on its own side.
    void send(RelayBuffer& target, const String& text);

    void applySettings();

private:
    void startConnect();
    void pollConnect();
    void handleSocket();
    void onDisconnected(const char* why);
    void scheduleReconnect();
    void setState(RelayState next);

    void sendCommand(const String& command);
    void requestBufferList();

    // Dispatches one decoded message by its id.
    void handleMessage(const String& id, weechat::Reader& reader);
    void handleBufferList(weechat::Reader& reader);
    void handleLineAdded(weechat::Reader& reader);
    void handleLines(const String& bufferPointer, weechat::Reader& reader);
    void handleNicklist(weechat::Reader& reader);
    void handleNicklistDiff(weechat::Reader& reader);
    void handleBufferRenamed(weechat::Reader& reader);
    void handleBufferOpened(weechat::Reader& reader);
    void handleBufferClosing(weechat::Reader& reader);
    void handleBufferMoved(weechat::Reader& reader);
    void handleBufferCleared(weechat::Reader& reader);
    void handleLineDataChanged(weechat::Reader& reader);
    void handlePalette(weechat::Reader& reader);
    void handleColors(weechat::Reader& reader);

    // Copies the fields a buffer hdata item carries onto `buffer`.
    void applyBufferItem(RelayBuffer& buffer, const weechat::HDataItem& item);
    void sortBuffers();
    // Fetches a buffer's backlog again, replacing what it shows.
    void reloadLines(RelayBuffer& target);

    RelayBuffer& ensureBuffer(const String& pointer);
    void         appendLine(RelayBuffer& target, const String& prefix,
                            const String& message, uint32_t stamp, bool highlight);

    std::unique_ptr<WiFiClient> m_socket;
    void*    m_job = nullptr;          // in-flight connect, owned by the task
    std::vector<void*> m_orphanedJobs;

    RelayState m_state = RelayState::Offline;
    bool       m_wantConnection = false;
    bool       m_userQuit       = false;
    String     m_lastError;

    // Incoming frames are assembled here: a message can span several reads.
    std::vector<uint8_t> m_rx;

    uint32_t m_connectStartedAt = 0;
    uint32_t m_reconnectAt      = 0;
    uint32_t m_reconnectDelay   = 0;
    uint32_t m_lastMessageAt    = 0;
    uint32_t m_lastPingAt       = 0;

    std::vector<std::unique_ptr<RelayBuffer>> m_buffers;

    // ANSI for each of WeeChat's colour options, by \x19NN index, and the
    // palette aliases a colour option's value may use instead of a number.
    std::vector<String> m_optionColors;
    std::vector<std::pair<String, String>> m_paletteAliases;   // alias, number

    struct {
        String   host;
        uint16_t port       = 9000;
        String   password;
        bool     tls        = false;
        uint16_t backlog    = 50;
        uint16_t scrollback = 400;
    } m_cfg;
};
