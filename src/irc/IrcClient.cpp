#include "irc/IrcClient.h"

#include <SD.h>
#include <WiFi.h>

#include <map>
#include <time.h>

#include "core/Log.h"
#include "core/Settings.h"
#include "core/Storage.h"
#include "irc/ChannelList.h"

namespace {

constexpr const char* TAG = "irc";

// Numerics that mean "you did not get into that channel". Every one of them is
// worth retrying: +i and +b can be lifted, +l frees up, +k can be set again.
bool isJoinFailure(int numeric) {
    switch (numeric) {
        case 471:  // ERR_CHANNELISFULL      +l
        case 473:  // ERR_INVITEONLYCHAN     +i
        case 474:  // ERR_BANNEDFROMCHAN     +b
        case 475:  // ERR_BADCHANNELKEY      +k
        case 477:  // ERR_NEEDREGGEDNICK
        case 479:  // ERR_BADCHANNAME / ERR_LINKCHANNEL
        case 485:  // ERR_UNIQOPRIVSNEEDED / cannot join
        case 489:  // ERR_SECUREONLYCHAN
        case 519:  // too many users
            return true;
        default:
            return false;
    }
}

const char* joinFailureReason(int numeric) {
    switch (numeric) {
        case 471: return "+l full";
        case 473: return "+i invite only";
        case 474: return "+b banned";
        case 475: return "+k needs key";
        case 477: return "needs registration";
        case 479: return "bad channel name";
        case 485: return "restricted";
        case 489: return "+S secure only";
        case 519: return "too many users";
        default:  return "rejected";
    }
}

// Numerics whose text belongs in a channel window rather than the status one.
bool isChannelNumeric(int numeric) {
    switch (numeric) {
        case 331: case 332: case 333:      // topic
        case 353: case 366:                // names
        case 324: case 329:                // channel modes / creation time
            return true;
        default:
            return false;
    }
}

// The network services. Messages from these are automated, not conversations,
// so they belong in the status window rather than each getting a window.
bool isService(const String& nick) {
    static const char* const kServices[] = {
        "NickServ", "ChanServ", "OperServ", "MemoServ", "HostServ",
        "BotServ", "SaslServ", "Global", "StatServ", "HelpServ", nullptr
    };
    for (const char* const* name = kServices; *name; name++) {
        if (irc::equalsIgnoreCaseIrc(nick, *name)) return true;
    }
    return false;
}

String ctrlColor(uint8_t index) {
    // Control byte, up to three digits, terminator.
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "\x03%02u", index);
    return String(buffer);
}

constexpr char RESET = '\x0F';

// A connection attempt in flight on its own task.
//
// The task owns the job while `abandoned` is set (the client gave up on it and
// must not touch it again); otherwise the client owns it once `done` is set.
struct ConnectJob {
    String        host;
    uint16_t      port      = 0;
    bool          tls       = false;
    String        caPem;                 // empty means do not verify
    WiFiClient*   socket    = nullptr;
    IPAddress     resolved;              // 0.0.0.0 when DNS failed
    volatile bool done      = false;
    volatile bool ok        = false;
    volatile bool abandoned = false;
};

void connectTask(void* arg) {
    ConnectJob* job = static_cast<ConnectJob*>(arg);

    WiFiClient* socket;
    if (job->tls) {
        auto* secure = new WiFiClientSecure();
        if (job->caPem.isEmpty()) secure->setInsecure();
        else                      secure->setCACert(job->caPem.c_str());
        secure->setHandshakeTimeout(12);
        secure->setTimeout(12);
        socket = secure;
    } else {
        socket = new WiFiClient();
        socket->setTimeout(8);
    }

    // Resolve first and record it, so a DNS failure is distinguishable from
    // the server refusing or resetting the connection. Logged by the caller:
    // the log buffer is not safe to touch from this task.
    WiFi.hostByName(job->host.c_str(), job->resolved);

    const bool ok = socket->connect(job->host.c_str(), job->port);

    // Publish and exit. The task never frees the job or the socket: the client
    // owns both, and splitting that ownership across two threads left the job
    // leaked whenever the task finished just before it was abandoned.
    job->socket = socket;
    job->ok     = ok;
    job->done   = true;
    vTaskDelete(nullptr);
}

} // namespace

// --- lifecycle ------------------------------------------------------------

void IrcClient::begin() {
    m_buffers.clear();

    auto status = std::unique_ptr<IrcBuffer>(new IrcBuffer());
    status->name = "status";
    status->kind = BufferKind::Status;
    m_buffers.push_back(std::move(status));

    applySettings();
    m_nick = settings::getText("irc_nick");

    if (onBufferListChanged) onBufferListChanged();
}

void IrcClient::applySettings() {
    // Logged because these values drive every timer in the client, and a wrong
    // one looks like a network fault rather than a configuration problem.
    m_cfg.joinDelayMs      = settings::getInt("irc_joindly");
    m_cfg.reconnectDelayS  = settings::getInt("irc_recondly");
    m_cfg.reconnectMaxS    = settings::getInt("irc_reconmax");
    m_cfg.kickDelayS       = settings::getInt("irc_kickdly");
    m_cfg.lockDelayS       = settings::getInt("irc_lockdly");
    m_cfg.pingTimeoutS     = settings::getInt("irc_pingout");
    m_cfg.autoReconnect    = settings::getBool("irc_recon");
    m_cfg.rejoinOnKick     = settings::getBool("irc_rejoin");
    m_cfg.retryFailedJoins = settings::getBool("irc_retryjn");
    m_cfg.showJoinPart     = settings::getBool("irc_joinpart");
    m_cfg.showModes        = settings::getBool("irc_showmode");
    m_cfg.showRaw          = settings::getBool("irc_showraw");
    m_cfg.allowCtcp        = settings::getBool("irc_beepctcp");
    m_cfg.scrollback       = settings::getInt("irc_scrollbk");

    for (auto& buffer : m_buffers) buffer->doc.setMaxLines(m_cfg.scrollback);

    LOG_I(TAG, "timers: join=%lums recon=%lus max=%lus kick=%lus lock=%lus ping=%lus",
          (unsigned long)m_cfg.joinDelayMs, (unsigned long)m_cfg.reconnectDelayS,
          (unsigned long)m_cfg.reconnectMaxS, (unsigned long)m_cfg.kickDelayS,
          (unsigned long)m_cfg.lockDelayS, (unsigned long)m_cfg.pingTimeoutS);
}

void IrcClient::setState(IrcState next) {
    if (m_state == next) return;
    m_state = next;
    if (onStateChanged) onStateChanged(next);
}

String IrcClient::stateText() const {
    // Without a network there is nothing to reconnect to, and saying so is
    // more honest than showing an endless "Reconnecting".
    if (WiFi.status() != WL_CONNECTED && m_state != IrcState::Ready) {
        return "No network";
    }

    switch (m_state) {
        case IrcState::Offline:      return "Offline";
        case IrcState::Connecting:   return "Connecting";
        case IrcState::Registering:  return "Registering";
        case IrcState::JoinDelay:    return "Joining";
        case IrcState::Ready:        return "Connected";
        case IrcState::Reconnecting: return "Reconnecting";
    }
    return "?";
}

bool IrcClient::isConnected() const {
    return m_socket && m_socket->connected();
}

// --- connection -----------------------------------------------------------

void IrcClient::connect() {
    m_wantConnection  = true;
    m_triedTlsAlready = false;
    m_reconnectDelay  = 0;
    m_reconnectAt     = 0;

    // Do not dial before there is a network. loop() picks this up the moment
    // WiFi associates; attempting it now only produces a socket error and then
    // pushes the first real attempt out behind a backoff delay.
    if (WiFi.status() != WL_CONNECTED) {
        addStatus("Waiting for a network connection", LINE_LOCAL);
        setState(IrcState::Offline);
        return;
    }

    if (m_state == IrcState::Offline || m_state == IrcState::Reconnecting) startConnect();
}

void IrcClient::disconnect(const String& quitMessage, bool stayOffline) {
    // Stop waiting for an attempt in flight, but keep hold of it: the task is
    // still writing to it and only this thread frees anything.
    if (m_job != nullptr) {
        static_cast<ConnectJob*>(m_job)->abandoned = true;
        m_orphanedJobs.push_back(m_job);
        m_job = nullptr;
    }

    if (isConnected()) {
        sendRaw("QUIT :" + (quitMessage.isEmpty() ? String("ACID DROP") : quitMessage));
        m_socket->flush();
        m_socket->stop();
    }
    m_socket.reset();

    if (stayOffline) m_wantConnection = false;

    for (auto& buffer : m_buffers) buffer->joined = false;
    setState(stayOffline ? IrcState::Offline : IrcState::Reconnecting);
}

void IrcClient::startConnect() {
    const String host = settings::getText("irc_server");
    int          port = settings::getInt("irc_port");
    bool         tls  = settings::getBool("irc_tls");

    // Second attempt within this cycle, after TLS failed.
    if (m_triedTlsAlready && settings::getBool("irc_fallback")) {
        tls  = false;
        port = 6667;
        addStatus("TLS failed, retrying in plaintext on port 6667", LINE_ERROR);
    }

    setState(IrcState::Connecting);
    addStatus("Connecting to " + host + ":" + String(port) + (tls ? " (TLS)" : ""), LINE_LOCAL);
    LOG_I(TAG, "connecting to %s:%d tls=%d", host.c_str(), port, tls ? 1 : 0);

    m_socket.reset();
    m_usingTls = tls;

    auto* job = new ConnectJob();
    job->host = host;
    job->port = static_cast<uint16_t>(port);
    job->tls  = tls;

    // The certificate is read here, on this task: the SD card shares the SPI
    // bus with the display and must not be touched from another thread.
    if (tls && settings::getBool("irc_tlsverif") && storage::ensureSdCard()) {
        File ca = SD.open("/irc-ca.pem", FILE_READ);
        if (ca && ca.size() > 0) {
            job->caPem.reserve(ca.size() + 1);
            while (ca.available()) job->caPem += static_cast<char>(ca.read());
            addStatus("Using CA certificate from /irc-ca.pem", LINE_LOCAL);
        } else {
            addStatus("Certificate verification is on but /irc-ca.pem is missing "
                      "- connecting without verification", LINE_ERROR);
        }
        if (ca) ca.close();
    }

    m_job = job;
    m_connectStartedAt = millis();

    // 12kB: an mbedTLS handshake needs most of that.
    if (xTaskCreate(connectTask, "irc-connect", 12288, job, 1, nullptr) != pdPASS) {
        LOG_E(TAG, "could not start the connect task");
        m_job = nullptr;
        delete job;
        scheduleReconnect();
    }
}

void IrcClient::reapOrphanedJobs() {
    for (size_t i = 0; i < m_orphanedJobs.size();) {
        auto* job = static_cast<ConnectJob*>(m_orphanedJobs[i]);
        if (!job->done) {
            i++;                       // still running; look again next pass
            continue;
        }

        if (job->socket) {
            job->socket->stop();
            delete job->socket;
        }
        delete job;
        m_orphanedJobs.erase(m_orphanedJobs.begin() + i);
        LOG_D(TAG, "reaped an abandoned connect attempt");
    }
}

void IrcClient::pollConnect() {
    auto* job = static_cast<ConnectJob*>(m_job);
    if (job == nullptr) {
        scheduleReconnect();
        return;
    }
    if (!job->done) {
        // Give up on a task that has run far past any sane handshake.
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
        const bool resolvedOk = resolved != IPAddress(0, 0, 0, 0);

        LOG_W(TAG, "connect failed (tls=%d), dns=%s, free heap %u",
              m_usingTls ? 1 : 0,
              resolvedOk ? resolved.toString().c_str() : "FAILED",
              (unsigned)ESP.getFreeHeap());

        if (!resolvedOk) {
            addStatus("Cannot resolve the server name - check DNS or the network",
                      LINE_ERROR);
        } else {
            // Reaching the host and being refused is a different problem from
            // not reaching it at all, and the user can act on the difference.
            addStatus("Reached " + resolved.toString() +
                      " but the connection was refused or reset. The network may "
                      "block IRC, or the server may be throttling this address.",
                      LINE_ERROR);
        }
        if (socket) {
            socket->stop();
            delete socket;
        }

        if (m_usingTls && !m_triedTlsAlready && settings::getBool("irc_fallback")) {
            m_triedTlsAlready = true;
            startConnect();          // immediate plaintext retry
            return;
        }
        scheduleReconnect();
        return;
    }

    m_socket.reset(socket);

    m_rxBuffer       = "";
    m_lastServerLine = millis();
    m_nickAttempt    = 0;
    m_saslDone       = false;
    m_capsEnabled.clear();

    addStatus("Connected, registering", LINE_LOCAL);
    registerConnection();
    setState(IrcState::Registering);
}

void IrcClient::onDisconnected(const char* why) {
    LOG_W(TAG, "disconnected: %s", why);
    addStatus(String("Disconnected: ") + why, LINE_ERROR);

    if (m_socket) m_socket->stop();
    m_socket.reset();

    for (auto& buffer : m_buffers) buffer->joined = false;

    scheduleReconnect();
}

void IrcClient::scheduleReconnect() {
    if (!m_wantConnection || !m_cfg.autoReconnect) {
        setState(IrcState::Offline);
        return;
    }

    // Exponential backoff, so a server that is down does not get hammered.
    if (m_reconnectDelay == 0) {
        m_reconnectDelay = m_cfg.reconnectDelayS;
    } else {
        m_reconnectDelay *= 2;
        if (m_reconnectDelay > m_cfg.reconnectMaxS) m_reconnectDelay = m_cfg.reconnectMaxS;
    }

    // Start each cycle from the configured settings again. Without this the
    // plaintext fallback latches: one TLS failure and every future attempt
    // goes to 6667 forever, which never recovers on a server that only
    // accepts TLS.
    m_triedTlsAlready = false;

    m_reconnectAt = millis() + m_reconnectDelay * 1000UL;
    addStatus("Reconnecting in " + String(m_reconnectDelay) + "s", LINE_LOCAL);
    setState(IrcState::Reconnecting);
}

// --- main pump ------------------------------------------------------------

void IrcClient::loop() {
    const uint32_t now = millis();

    // Free anything left over from attempts we stopped waiting for.
    if (!m_orphanedJobs.empty()) reapOrphanedJobs();

    if (!m_wantConnection) return;

    if (WiFi.status() != WL_CONNECTED) {
        if (m_state != IrcState::Offline && m_state != IrcState::Reconnecting) {
            onDisconnected("WiFi lost");
        }
        return;
    }

    if (m_state == IrcState::Connecting) {
        pollConnect();
        return;
    }

    if (m_state == IrcState::Reconnecting) {
        if (static_cast<int32_t>(now - m_reconnectAt) >= 0) startConnect();
        return;
    }

    if (m_state == IrcState::Offline) {
        startConnect();
        return;
    }

    if (!isConnected()) {
        onDisconnected("connection closed");
        return;
    }

    handleSocket();

    // The join delay the server needs before it will let us in. Some networks
    // apply connection throttling or run a host check for the first few
    // seconds, and joining too early gets the JOIN silently dropped.
    if (m_state == IrcState::JoinDelay && static_cast<int32_t>(now - m_joinAt) >= 0) {
        queueConfiguredChannels();
        setState(IrcState::Ready);
    }

    processJoinQueue();

    // Nothing from the server for too long means the link is dead even though
    // the socket still looks open, which is the usual way a TCP session rots.
    if (m_state >= IrcState::Registering && m_cfg.pingTimeoutS > 0) {
        // Signed, and re-read rather than using the `now` from the top of the
        // loop: the connection completing sets m_lastServerLine part way
        // through this same iteration, so it can be *ahead* of that timestamp.
        // As unsigned arithmetic that underflows to about 4.29 billion and
        // trips the timeout instantly, killing the link seconds after connect.
        const uint32_t nowMs   = millis();
        const int32_t  silence = static_cast<int32_t>(nowMs - m_lastServerLine);
        const int32_t  limit   = static_cast<int32_t>(m_cfg.pingTimeoutS * 1000UL);

        if (silence > limit) {
            LOG_W(TAG, "ping timeout after %ld ms (limit %ld)",
                  (long)silence, (long)limit);
            onDisconnected("ping timeout");
        } else if (silence > limit / 2 &&
                   static_cast<int32_t>(nowMs - m_lastPingSent) > 30000) {
            m_lastPingSent = nowMs;
            sendRaw("PING :" + String(nowMs));
        }
    }
}

void IrcClient::handleSocket() {
    // Bounded per call so a burst of traffic cannot starve the UI, but large
    // enough to actually keep up: a MOTD and a NAMES burst run to tens of
    // kilobytes, and draining 64 bytes per frame would take minutes.
    int budget = 2048;

    while (m_socket->available() && budget-- > 0) {
        const char c = static_cast<char>(m_socket->read());

        if (c == '\n') {
            String line = m_rxBuffer;
            m_rxBuffer = "";
            line.replace("\r", "");
            if (!line.isEmpty()) handleLine(line);
            continue;
        }

        if (m_rxBuffer.length() < 1024) {
            m_rxBuffer += c;
        } else {
            // A line this long is not something any sane server sends.
            LOG_W(TAG, "oversized line, dropping");
            m_rxBuffer = "";
        }
    }
}

// --- registration ---------------------------------------------------------

void IrcClient::registerConnection() {
    m_capNegotiating = true;
    m_saslRequested  = false;

    sendRaw("CAP LS 302");

    m_nick = settings::getText("irc_nick");
    const String user = settings::getText("irc_user");
    const String real = settings::getText("irc_real");

    sendRaw("NICK " + m_nick);
    sendRaw("USER " + user + " 0 * :" + real);
}

void IrcClient::sendCapRequest() {
    // Only the capabilities we actually act on.
    String wanted = "multi-prefix server-time";
    if (settings::getBool("irc_sasl") && !settings::getText("irc_saslpass").isEmpty()) {
        wanted += " sasl";
        m_saslRequested = true;
    }
    sendRaw("CAP REQ :" + wanted);
}

void IrcClient::finishCapNegotiation() {
    if (!m_capNegotiating) return;
    m_capNegotiating = false;
    sendRaw("CAP END");
}

void IrcClient::startSasl() {
    sendRaw("AUTHENTICATE PLAIN");
}

// --- line handling --------------------------------------------------------

void IrcClient::handleLine(const String& raw) {
    m_lastServerLine = millis();

    LOG_D(TAG, "<< %s", raw.c_str());

    const IrcMessage message = IrcMessage::parse(raw);

    // PING has to be answered before anything else can go wrong.
    if (message.command == "PING") {
        sendRaw("PONG :" + message.param(0));
        return;
    }

    if (m_cfg.showRaw) {
        addLine(status(), raw, LINE_RAW);
    }

    dispatch(message);
}

void IrcClient::dispatch(const IrcMessage& message) {
    const String& command = message.command;

    if (message.isNumeric()) {
        handleNumeric(message);
        return;
    }

    if (command == "PRIVMSG")      { handlePrivmsg(message, false); return; }
    if (command == "NOTICE")       { handlePrivmsg(message, true);  return; }
    if (command == "JOIN")         { handleJoin(message);           return; }
    if (command == "PART")         { handlePart(message);           return; }
    if (command == "KICK")         { handleKick(message);           return; }
    if (command == "QUIT")         { handleQuit(message);           return; }
    if (command == "NICK")         { handleNickChange(message);     return; }
    if (command == "MODE")         { handleMode(message);           return; }
    if (command == "TOPIC")        { handleTopic(message);          return; }
    if (command == "PONG")         { return; }

    if (command == "ERROR") {
        addStatus(message.param(0), LINE_ERROR);
        return;
    }

    if (command == "CAP") {
        const String subcommand = message.param(1);
        const String caps       = message.param(2);

        if (subcommand == "LS") {
            sendCapRequest();
        } else if (subcommand == "ACK") {
            for (const String& cap : irc::splitList(caps, ' ')) m_capsEnabled.push_back(cap);
            if (m_saslRequested && caps.indexOf("sasl") >= 0) {
                startSasl();
            } else {
                finishCapNegotiation();
            }
        } else if (subcommand == "NAK") {
            addStatus("Server refused capabilities: " + caps, LINE_ERROR);
            finishCapNegotiation();
        }
        return;
    }

    if (command == "AUTHENTICATE") {
        if (message.param(0) == "+") {
            String account = settings::getText("irc_saslusr");
            if (account.isEmpty()) account = m_nick;
            const String password = settings::getText("irc_saslpass");

            // SASL PLAIN: authzid \0 authcid \0 password
            String payload;
            payload += account;
            payload += '\0';
            payload += account;
            payload += '\0';
            payload += password;

            const String encoded = irc::base64Encode(
                reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
            sendRaw("AUTHENTICATE " + encoded);
        }
        return;
    }

    // Anything we do not model still belongs somewhere the user can see it.
    if (!m_cfg.showRaw) {
        addStatus(command + " " + message.param(message.params.size() ? message.params.size() - 1 : 0),
                  LINE_SERVER);
    }
}

void IrcClient::handleNumeric(const IrcMessage& message) {
    const int code = message.numeric();
    const uint32_t stamp = lineStamp(message);

    switch (code) {
        case 1: {   // RPL_WELCOME
            // The server has accepted us. Everything else waits for the join
            // delay, which is the whole point of this state.
            m_nick = message.param(0);
            m_reconnectDelay = 0;
            m_joinAt = millis() + m_cfg.joinDelayMs;

            addStatus(message.param(1), LINE_SERVER);
            addStatus("Registered as " + m_nick + ", joining in " +
                      String(m_cfg.joinDelayMs / 1000.0f, 1) + "s", LINE_LOCAL);

            // NickServ, only when SASL did not already authenticate us.
            const String nickservPass = settings::getText("irc_nspass");
            if (!m_saslDone && !nickservPass.isEmpty()) {
                sendRaw("PRIVMSG NickServ :IDENTIFY " + nickservPass);
                addStatus("Sent NickServ IDENTIFY", LINE_LOCAL);
            }

            setState(IrcState::JoinDelay);
            return;
        }

        case 433:   // ERR_NICKNAMEINUSE
        case 436: { // ERR_NICKCOLLISION
            m_nickAttempt++;
            String next;
            if (m_nickAttempt == 1) {
                next = settings::getText("irc_altnick");
                if (next.isEmpty()) next = m_nick + "_";
            } else {
                next = settings::getText("irc_nick") + String(random(10, 99));
            }
            addStatus("Nick in use, trying " + next, LINE_ERROR);
            m_nick = next;
            sendRaw("NICK " + next);
            return;
        }

        case 903:   // RPL_SASLSUCCESS
            m_saslDone = true;
            addStatus("SASL authentication succeeded", LINE_LOCAL);
            finishCapNegotiation();
            return;

        case 902:   // ERR_NICKLOCKED
        case 904:   // ERR_SASLFAIL
        case 905:   // ERR_SASLTOOLONG
        case 906:   // ERR_SASLABORTED
        case 907:   // ERR_SASLALREADY
            addStatus("SASL failed: " + message.param(message.params.size() - 1), LINE_ERROR);
            finishCapNegotiation();
            return;

        case 324: { // RPL_CHANNELMODEIS
            // findBuffer, not ensureBuffer: a numeric must never conjure a
            // window. The server sends these for a channel it forced us into,
            // which arrive after we have already parted it.
            IrcBuffer* found = findBuffer(message.param(1));
            if (found == nullptr) return;
            IrcBuffer& channel = *found;
            String modes;
            for (size_t i = 2; i < message.params.size(); i++) {
                if (i > 2) modes += ' ';
                modes += message.params[i];
            }
            channel.modes = modes;
            return;
        }

        case 332: { // RPL_TOPIC
            IrcBuffer* found = findBuffer(message.param(1));
            if (found == nullptr) return;
            IrcBuffer& channel = *found;
            channel.topic = message.param(2);
            addLine(channel, ctrlColor(10) + "*" + RESET + " Topic: " + channel.topic, LINE_TOPIC);
            return;
        }

        case 353: { // RPL_NAMREPLY
            IrcBuffer* found = findBuffer(message.param(2));
            if (found == nullptr) return;
            IrcBuffer& channel = *found;

            // First reply of a batch replaces the list; the rest append. NAMES
            // can be re-requested at any time, and without this the roster
            // doubles every time it is refreshed.
            if (!channel.namesLoading) {
                channel.nicks.clear();
                channel.namesLoading = true;
            }

            for (String name : irc::splitList(message.param(3), ' ')) {
                while (!name.isEmpty() && strchr("@+%~&!", name[0])) name = name.substring(1);
                if (!name.isEmpty()) channel.nicks.push_back(name);
            }
            return;
        }

        case 366: { // RPL_ENDOFNAMES
            IrcBuffer* found = findBuffer(message.param(1));
            if (found == nullptr) return;
            IrcBuffer& channel = *found;
            channel.namesLoading = false;
            addLine(channel, ctrlColor(14) + "* " + String(channel.nicks.size()) +
                             " users" + RESET, LINE_SERVER);
            return;
        }

        default:
            break;
    }

    if (isJoinFailure(code)) {
        const String channelName = message.param(1);
        IrcBuffer& channel = ensureBuffer(channelName, BufferKind::Channel);
        channel.joined = false;
        scheduleJoinRetry(channel, joinFailureReason(code));
        return;
    }

    // Everything else: show the human-readable trailing parameter.
    const String text = message.params.empty() ? String()
                                               : message.params[message.params.size() - 1];

    if (isChannelNumeric(code) && message.params.size() >= 2 && irc::isChannel(message.param(1))) {
        if (IrcBuffer* channel = findBuffer(message.param(1))) {
            addLine(*channel, text, LINE_SERVER);
        } else if (!m_cfg.showRaw) {
            addStatus(text, LINE_SERVER);   // no window for it, and none wanted
        }
    } else if (!m_cfg.showRaw) {
        (void)stamp;
        addStatus(text, code >= 400 ? LINE_ERROR : LINE_SERVER);
    }
}

// --- messages -------------------------------------------------------------

void IrcClient::handlePrivmsg(const IrcMessage& message, bool isNotice) {
    const String target = message.param(0);
    String       text   = message.param(1);
    const String from   = message.nick;
    const uint32_t stamp = lineStamp(message);

    // CTCP is wrapped in \001.
    if (text.length() >= 2 && text[0] == '\001') {
        String payload = text.substring(1);
        if (payload.endsWith("\001")) payload = payload.substring(0, payload.length() - 1);
        handleCtcp(message, target, payload);
        return;
    }

    const bool toMe = irc::equalsIgnoreCaseIrc(target, m_nick);

    // Anything addressed to us used to open a private window, which meant one
    // per service and one per server that ever sent a notice. Only an actual
    // person gets a window:
    //   - notices go to the status window, as they do in every other client
    //   - so does anything from a server, which has no user@host in its prefix
    //   - so do the network services, which are bots, not conversations
    const bool fromServer = message.fromServer() || from.indexOf('.') >= 0;
    const bool fromService = isService(from);

    IrcBuffer* target_buffer;
    if (!toMe) {
        target_buffer = &ensureBuffer(target, BufferKind::Channel);
    } else if (isNotice || fromServer || fromService) {
        target_buffer = &status();
    } else {
        target_buffer = &ensureBuffer(from, BufferKind::Query);
    }

    IrcBuffer& where = *target_buffer;
    const bool mention = isHighlight(text) ||
                         (toMe && !isNotice && !fromServer && !fromService);

    String line;
    if (isNotice) {
        line = ctrlColor(13) + "-" + from + "-" + RESET + " " + text;
    } else {
        line = formatNick(from) + " " + text;
    }

    where.doc.append(line, stamp, isNotice ? LINE_NOTICE : LINE_MESSAGE, mention);
    if (mention) {
        where.doc.unreadHighlight = true;
        if (onHighlight) onHighlight(from, text, where);
    }
    where.doc.unread++;
    if (onBufferChanged) onBufferChanged(where);
}

void IrcClient::handleCtcp(const IrcMessage& message, const String& target, String payload) {
    const String from  = message.nick;
    const uint32_t stamp = lineStamp(message);

    int space = payload.indexOf(' ');
    String verb = space < 0 ? payload : payload.substring(0, space);
    String rest = space < 0 ? String() : payload.substring(space + 1);
    verb.toUpperCase();

    if (verb == "ACTION") {
        const bool toMe = irc::equalsIgnoreCaseIrc(target, m_nick);
        IrcBuffer& where = toMe ? ensureBuffer(from, BufferKind::Query)
                                : ensureBuffer(target, BufferKind::Channel);
        const bool mention = isHighlight(rest);

        where.doc.append(ctrlColor(13) + "*" + RESET + " " + from + " " + rest,
                         stamp, LINE_ACTION, mention);
        where.doc.unread++;
        if (mention) {
            where.doc.unreadHighlight = true;
            if (onHighlight) onHighlight(from, rest, where);
        }
        if (onBufferChanged) onBufferChanged(where);
        return;
    }

    addStatus("CTCP " + verb + " from " + from, LINE_SERVER);
    if (!m_cfg.allowCtcp) return;

    if (verb == "VERSION") {
        sendRaw("NOTICE " + from + " :\001VERSION ACID DROP (LilyGo T-Deck Plus)\001");
    } else if (verb == "PING") {
        sendRaw("NOTICE " + from + " :\001PING " + rest + "\001");
    } else if (verb == "TIME") {
        char buffer[40];
        const time_t now = time(nullptr);
        struct tm parts;
        localtime_r(&now, &parts);
        strftime(buffer, sizeof(buffer), "%a %b %d %H:%M:%S %Y", &parts);
        sendRaw("NOTICE " + from + " :\001TIME " + String(buffer) + "\001");
    } else if (verb == "CLIENTINFO") {
        sendRaw("NOTICE " + from + " :\001CLIENTINFO ACTION VERSION PING TIME CLIENTINFO\001");
    }
}

// --- membership -----------------------------------------------------------

void IrcClient::handleJoin(const IrcMessage& message) {
    // extended-join sends extra params; the channel is always the first.
    const String channelName = message.param(0);
    const uint32_t stamp = lineStamp(message);

    // Check before creating anything: a channel the server pushed us into
    // should leave no window behind at all, and ensureBuffer() would make one.
    if (irc::equalsIgnoreCaseIrc(message.nick, m_nick)) {
        IrcBuffer* known = findBuffer(channelName);
        if (known == nullptr || !known->requested) {
            LOG_I(TAG, "server put us in %s unasked, parting", channelName.c_str());
            addStatus("Left " + channelName + " (joined by the server, not requested)",
                      LINE_LOCAL);
            sendRaw("PART " + channelName + " :not requested");

            // Drop the window too, if one had already been made for it.
            for (size_t i = 1; i < m_buffers.size(); i++) {
                if (irc::equalsIgnoreCaseIrc(m_buffers[i]->name, channelName)) {
                    m_buffers.erase(m_buffers.begin() + i);
                    if (onBufferListChanged) onBufferListChanged();
                    break;
                }
            }
            return;
        }
    }

    IrcBuffer& channel = ensureBuffer(channelName, BufferKind::Channel);

    if (irc::equalsIgnoreCaseIrc(message.nick, m_nick)) {

        channel.joined      = true;
        channel.retryAt     = 0;
        channel.retryCount  = 0;
        channel.retryReason = "";
        channel.nicks.clear();
        addLine(channel, ctrlColor(9) + "-> You joined " + channelName + RESET, LINE_JOIN);
        LOG_I(TAG, "joined %s", channelName.c_str());
        if (onBufferListChanged) onBufferListChanged();
        return;
    }

    channel.nicks.push_back(message.nick);
    if (!m_cfg.showJoinPart) return;

    addLine(channel,
            ctrlColor(9) + "->" + RESET + " " + formatNick(message.nick) +
            ctrlColor(14) + " (" + message.user + "@" + message.host + ")" + RESET +
            " joined " + ctrlColor(10) + channelName + RESET,
            LINE_JOIN);
    (void)stamp;
}

void IrcClient::handlePart(const IrcMessage& message) {
    const String channelName = message.param(0);
    const String reason      = message.param(1);

    IrcBuffer* channel = findBuffer(channelName);
    if (!channel) return;

    if (irc::equalsIgnoreCaseIrc(message.nick, m_nick)) {
        channel->joined    = false;
        channel->requested = false;
        channel->nicks.clear();
        addLine(*channel, ctrlColor(4) + "<- You left " + channelName + RESET, LINE_PART);
        return;
    }

    for (size_t i = 0; i < channel->nicks.size(); i++) {
        if (irc::equalsIgnoreCaseIrc(channel->nicks[i], message.nick)) {
            channel->nicks.erase(channel->nicks.begin() + i);
            break;
        }
    }

    if (!m_cfg.showJoinPart) return;
    addLine(*channel,
            ctrlColor(4) + "<-" + RESET + " " + formatNick(message.nick) + " left " +
            ctrlColor(10) + channelName + RESET +
            (reason.isEmpty() ? String() : " (" + reason + ")"),
            LINE_PART);
}

void IrcClient::handleKick(const IrcMessage& message) {
    const String channelName = message.param(0);
    const String victim      = message.param(1);
    const String reason      = message.param(2);

    IrcBuffer& channel = ensureBuffer(channelName, BufferKind::Channel);

    if (irc::equalsIgnoreCaseIrc(victim, m_nick)) {
        channel.joined = false;
        channel.nicks.clear();
        addLine(channel,
                ctrlColor(4) + "!!" + RESET + " You were kicked from " + channelName +
                " by " + message.nick + (reason.isEmpty() ? String() : " (" + reason + ")"),
                LINE_KICK, true);

        if (m_cfg.rejoinOnKick && channel.retryEnabled) {
            channel.retryAt     = millis() + m_cfg.kickDelayS * 1000UL;
            channel.retryCount  = 0;
            channel.retryReason = "kicked";
            addLine(channel, ctrlColor(14) + "* Rejoining in " + String(m_cfg.kickDelayS) +
                             "s" + RESET, LINE_LOCAL);
        }
        LOG_W(TAG, "kicked from %s by %s", channelName.c_str(), message.nick.c_str());
        return;
    }

    for (size_t i = 0; i < channel.nicks.size(); i++) {
        if (irc::equalsIgnoreCaseIrc(channel.nicks[i], victim)) {
            channel.nicks.erase(channel.nicks.begin() + i);
            break;
        }
    }

    addLine(channel,
            ctrlColor(4) + "!!" + RESET + " " + formatNick(victim) + " was kicked by " +
            formatNick(message.nick) + (reason.isEmpty() ? String() : " (" + reason + ")"),
            LINE_KICK);
}

void IrcClient::handleQuit(const IrcMessage& message) {
    removeNickEverywhere(message.nick, message.param(0), lineStamp(message));
}

void IrcClient::removeNickEverywhere(const String& nick, const String& reason, uint32_t stamp) {
    const bool show = m_cfg.showJoinPart;

    for (auto& buffer : m_buffers) {
        if (!buffer->isChannel()) continue;

        bool present = false;
        for (size_t i = 0; i < buffer->nicks.size(); i++) {
            if (irc::equalsIgnoreCaseIrc(buffer->nicks[i], nick)) {
                buffer->nicks.erase(buffer->nicks.begin() + i);
                present = true;
                break;
            }
        }
        if (!present || !show) continue;

        buffer->doc.append(ctrlColor(4) + "<<" + RESET + " " + formatNick(nick) + " quit" +
                           (reason.isEmpty() ? String() : " (" + reason + ")"),
                           stamp, LINE_QUIT, false);
        if (onBufferChanged) onBufferChanged(*buffer);
    }
}

void IrcClient::handleNickChange(const IrcMessage& message) {
    const String from = message.nick;
    const String to   = message.param(0);
    const uint32_t stamp = lineStamp(message);

    if (irc::equalsIgnoreCaseIrc(from, m_nick)) {
        m_nick = to;
        addStatus("You are now known as " + to, LINE_NICK);
    }

    for (auto& buffer : m_buffers) {
        bool present = false;
        for (String& name : buffer->nicks) {
            if (irc::equalsIgnoreCaseIrc(name, from)) { name = to; present = true; break; }
        }
        // A query window follows the nick it is talking to.
        if (buffer->kind == BufferKind::Query && irc::equalsIgnoreCaseIrc(buffer->name, from)) {
            buffer->name = to;
            present = true;
            if (onBufferListChanged) onBufferListChanged();
        }
        if (!present) continue;

        buffer->doc.append(ctrlColor(6) + "*" + RESET + " " + from + " is now known as " +
                           formatNick(to), stamp, LINE_NICK, false);
        if (onBufferChanged) onBufferChanged(*buffer);
    }
}

void IrcClient::handleMode(const IrcMessage& message) {
    if (!m_cfg.showModes) return;

    const String target = message.param(0);
    String modes;
    for (size_t i = 1; i < message.params.size(); i++) {
        if (i > 1) modes += ' ';
        modes += message.params[i];
    }

    const String text = ctrlColor(11) + "*" + RESET + " " +
                        (message.nick.isEmpty() ? target : message.nick) +
                        " sets mode " + modes;

    if (irc::isChannel(target)) {
        if (IrcBuffer* channel = findBuffer(target)) addLine(*channel, text, LINE_MODE);
        else                                        addStatus(text, LINE_MODE);
    } else {
        addStatus(text, LINE_MODE);
    }
}

void IrcClient::handleTopic(const IrcMessage& message) {
    const String channelName = message.param(0);
    const String topic       = message.param(1);

    IrcBuffer* found = findBuffer(channelName);
    if (found == nullptr) return;
    IrcBuffer& channel = *found;
    channel.topic = topic;
    addLine(channel,
            ctrlColor(10) + "*" + RESET + " " + formatNick(message.nick) +
            " changed the topic: " + topic,
            LINE_TOPIC);
}

// --- joining --------------------------------------------------------------

void IrcClient::queueConfiguredChannels() {
    for (const IrcChannelConfig& saved : channels::all()) {
        if (!saved.autojoin) continue;

        IrcBuffer& channel = ensureBuffer(saved.name, BufferKind::Channel);
        channel.key          = saved.key;
        channel.retryEnabled = saved.retry;
        channel.retryAt      = millis();   // join on the next pump
        channel.retryCount   = 0;
    }

    // Rejoin channels we were in before a drop - but only ones that are on the
    // saved list. A buffer can exist for a channel we never asked for, because
    // a TOPIC or NAMES numeric creates one, and re-arming those was undoing
    // the part of a channel the server had forced us into.
    for (auto& buffer : m_buffers) {
        if (!buffer->isChannel() || buffer->joined || buffer->retryAt != 0) continue;

        const IrcChannelConfig* saved = channels::find(buffer->name);
        if (saved == nullptr || !saved->autojoin) continue;

        buffer->retryAt = millis();
    }

    if (onBufferListChanged) onBufferListChanged();
}

void IrcClient::processJoinQueue() {
    if (m_state != IrcState::Ready || !isConnected()) return;

    const uint32_t now = millis();

    for (auto& buffer : m_buffers) {
        if (!buffer->isChannel() || buffer->joined) continue;
        if (buffer->retryAt == 0) continue;
        if (static_cast<int32_t>(now - buffer->retryAt) < 0) continue;

        buffer->retryCount++;
        buffer->retryAt = 0;

        String command = "JOIN " + buffer->name;
        if (!buffer->key.isEmpty()) command += " " + buffer->key;
        buffer->requested = true;
        sendRaw(command);

        if (buffer->retryCount > 1) {
            LOG_I(TAG, "join attempt %u for %s (%s)",
                  buffer->retryCount, buffer->name.c_str(), buffer->retryReason.c_str());
        }

        // If the join fails we will hear about it as a numeric and reschedule.
        // If it succeeds, JOIN clears the retry state. Either way, arm a
        // fallback so a server that answers with neither does not strand us.
        if (m_cfg.retryFailedJoins && buffer->retryEnabled) {
            buffer->retryAt = now + m_cfg.lockDelayS * 1000UL * 4;
        }
        return;   // one JOIN per pump, to stay under flood limits
    }
}

void IrcClient::scheduleJoinRetry(IrcBuffer& target, const String& reason) {
    if (!m_cfg.retryFailedJoins || !target.retryEnabled) {
        addLine(target, ctrlColor(4) + "!!" + RESET + " Cannot join " + target.name +
                        " (" + reason + ")", LINE_ERROR);
        target.retryAt = 0;
        return;
    }

    target.retryReason = reason;
    target.retryAt     = millis() + m_cfg.lockDelayS * 1000UL;

    // Only say so the first few times; after that it is just noise.
    if (target.retryCount <= 3) {
        addLine(target,
                ctrlColor(4) + "!!" + RESET + " " + target.name + " rejected the join (" +
                reason + "), retrying every " + String(m_cfg.lockDelayS) + "s",
                LINE_ERROR);
    }
}

// --- sending --------------------------------------------------------------

bool IrcClient::sendRaw(const String& line) {
    if (!isConnected()) {
        LOG_W(TAG, "send while offline: %s", line.c_str());
        return false;
    }

    // 512 bytes including CRLF, per RFC 2812.
    String out = line;
    if (out.length() > 510) out = out.substring(0, 510);

    LOG_D(TAG, ">> %s", out.c_str());

    out += "\r\n";
    return m_socket->print(out) == static_cast<int>(out.length());
}

void IrcClient::say(const String& target, const String& text) {
    if (target.isEmpty() || text.isEmpty()) return;

    IrcBuffer& where = ensureBuffer(target, irc::isChannel(target) ? BufferKind::Channel
                                                                   : BufferKind::Query);

    // A line over the protocol limit used to be truncated, losing the tail
    // silently. Split it instead, on a space where there is one nearby.
    const String prefix = "PRIVMSG " + target + " :";
    const int    room   = 510 - static_cast<int>(prefix.length());
    if (room <= 0) return;

    int offset = 0;
    while (offset < static_cast<int>(text.length())) {
        int take = static_cast<int>(text.length()) - offset;
        if (take > room) {
            take = room;
            // Walk back to a space, but not so far that we send a sliver.
            int space = take;
            while (space > room / 2 && text[offset + space] != ' ') space--;
            if (space > room / 2) take = space;
        }

        const String chunk = text.substring(offset, offset + take);
        if (!sendRaw(prefix + chunk)) return;
        addLine(where, formatNick(m_nick) + " " + chunk, LINE_MESSAGE);

        offset += take;
        while (offset < static_cast<int>(text.length()) && text[offset] == ' ') offset++;
    }
}

void IrcClient::action(const String& target, const String& text) {
    if (!sendRaw("PRIVMSG " + target + " :\001ACTION " + text + "\001")) return;

    IrcBuffer& where = ensureBuffer(target, irc::isChannel(target) ? BufferKind::Channel
                                                                   : BufferKind::Query);
    addLine(where, ctrlColor(13) + "*" + RESET + " " + m_nick + " " + text, LINE_ACTION);
}

void IrcClient::notice(const String& target, const String& text) {
    if (!sendRaw("NOTICE " + target + " :" + text)) return;
    IrcBuffer& where = ensureBuffer(target, irc::isChannel(target) ? BufferKind::Channel
                                                                   : BufferKind::Query);
    addLine(where, ctrlColor(13) + "-> -" + target + "-" + RESET + " " + text, LINE_NOTICE);
}

void IrcClient::join(const String& channel, const String& key) {
    IrcBuffer& buffer = ensureBuffer(channel, BufferKind::Channel);
    buffer.key        = key;
    buffer.retryAt    = millis();
    buffer.retryCount = 0;

    // A channel you joined by hand should survive a reconnect, so remember it.
    buffer.requested = true;
    channels::rememberJoin(channel, key);
    if (const IrcChannelConfig* saved = channels::find(channel)) {
        buffer.retryEnabled = saved->retry;
    }

    if (onBufferListChanged) onBufferListChanged();
}

void IrcClient::part(const String& channel, const String& reason) {
    IrcBuffer* buffer = findBuffer(channel);
    if (buffer) {
        // An explicit part means stop trying to get back in.
        buffer->retryAt     = 0;
        buffer->retryReason = "";
    }
    // Parting is a deliberate act, so stop auto-joining it next time too.
    if (IrcChannelConfig* saved = channels::find(channel)) {
        saved->autojoin = false;
        channels::save();
    }
    sendRaw("PART " + channel + (reason.isEmpty() ? String() : " :" + reason));
}

void IrcClient::setNick(const String& nick) {
    sendRaw("NICK " + nick);
}

// --- buffers --------------------------------------------------------------

IrcBuffer* IrcClient::findBuffer(const String& name) {
    for (auto& buffer : m_buffers) {
        if (irc::equalsIgnoreCaseIrc(buffer->name, name)) return buffer.get();
    }
    return nullptr;
}

IrcBuffer& IrcClient::ensureBuffer(const String& name, BufferKind kind) {
    if (name.isEmpty()) return status();

    if (IrcBuffer* existing = findBuffer(name)) return *existing;

    // Anyone can open a window on us by sending a private message, so there
    // has to be a ceiling. Past it, traffic lands in the status window rather
    // than growing the list without bound.
    constexpr size_t kMaxBuffers = 24;
    if (m_buffers.size() >= kMaxBuffers) {
        LOG_W(TAG, "window limit reached, routing %s to the status window", name.c_str());
        return status();
    }

    auto created = std::unique_ptr<IrcBuffer>(new IrcBuffer());
    created->name = name;
    created->kind = kind;
    created->doc.setMaxLines(m_cfg.scrollback);

    m_buffers.push_back(std::move(created));
    if (onBufferListChanged) onBufferListChanged();

    return *m_buffers.back();
}

bool IrcClient::closeBuffer(size_t index) {
    if (index == 0 || index >= m_buffers.size()) return false;

    IrcBuffer& buffer = *m_buffers[index];
    if (buffer.isChannel() && buffer.joined) part(buffer.name, "Closing window");

    m_buffers.erase(m_buffers.begin() + index);
    if (onBufferListChanged) onBufferListChanged();
    return true;
}

// --- output helpers -------------------------------------------------------

void IrcClient::addLine(IrcBuffer& target, const String& text, uint8_t kind, bool highlight) {
    target.doc.append(text, static_cast<uint32_t>(time(nullptr)), kind, highlight);
    if (onBufferChanged) onBufferChanged(target);
}

void IrcClient::addStatus(const String& text, uint8_t kind) {
    addLine(status(), text, kind);
}

uint32_t IrcClient::lineStamp(const IrcMessage& message) const {
    // Prefer the server's own timestamp when server-time is enabled, so replayed
    // history lands at the time it happened rather than the time we read it.
    const String serverTime = message.tag("time");
    if (serverTime.length() >= 19) {
        struct tm parts = {};
        if (strptime(serverTime.c_str(), "%Y-%m-%dT%H:%M:%S", &parts)) {
            // The tag is UTC. mktime() would read the fields as local time and
            // shift every replayed line by the timezone offset, so count the
            // days directly instead.
            static const uint16_t kDaysBeforeMonth[12] = {
                0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
            };

            const int year = parts.tm_year + 1900;
            if (year >= 1970 && year < 2100) {
                uint32_t days = 0;
                for (int y = 1970; y < year; y++) {
                    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
                    days += leap ? 366 : 365;
                }
                days += kDaysBeforeMonth[parts.tm_mon];

                const bool leapThisYear = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
                if (leapThisYear && parts.tm_mon > 1) days++;
                days += parts.tm_mday - 1;

                return days * 86400UL + parts.tm_hour * 3600UL +
                       parts.tm_min * 60UL + parts.tm_sec;
            }
        }
    }
    return static_cast<uint32_t>(time(nullptr));
}

bool IrcClient::isHighlight(const String& text) const {
    const String plain = textfmt::strip(text);

    auto containsWord = [&plain](const String& needle) {
        if (needle.isEmpty()) return false;
        String haystack = plain;
        haystack.toLowerCase();
        String lower = needle;
        lower.toLowerCase();

        int at = haystack.indexOf(lower);
        while (at >= 0) {
            const bool leftOk  = at == 0 ||
                                 !isalnum(static_cast<unsigned char>(haystack[at - 1]));
            const size_t after = at + lower.length();
            const bool rightOk = after >= haystack.length() ||
                                 !isalnum(static_cast<unsigned char>(haystack[after]));
            if (leftOk && rightOk) return true;
            at = haystack.indexOf(lower, at + 1);
        }
        return false;
    };

    if (containsWord(m_nick)) return true;

    for (const String& term : irc::splitList(settings::getText("irc_hilight"))) {
        if (containsWord(term)) return true;
    }
    return false;
}

String IrcClient::colorForNick(const String& nick) const {
    // Skip white, black and the two greys so nicks stay readable on black.
    static const uint8_t kPalette[] = {2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13};
    const uint8_t count = sizeof(kPalette) / sizeof(kPalette[0]);

    switch (settings::getEnum("irc_nickcol")) {
        case 0:
            return String();
        case 2: {
            // Rolled once per nick and remembered: re-rolling per line made a
            // nick change colour on every message it sent.
            static std::map<String, uint8_t> assigned;

            // One entry per nick ever seen would grow without bound on a busy
            // network; the assignment is arbitrary anyway, so start over.
            if (assigned.size() > 256) assigned.clear();

            auto it = assigned.find(nick);
            if (it == assigned.end()) {
                it = assigned.emplace(nick, kPalette[random(count)]).first;
            }
            return ctrlColor(it->second);
        }
        default: {
            // FNV-1a keeps a nick the same colour across sessions and devices.
            uint32_t hash = 2166136261u;
            for (unsigned int i = 0; i < nick.length(); i++) {
                hash ^= static_cast<uint8_t>(tolower(nick[i]));
                hash *= 16777619u;
            }
            return ctrlColor(kPalette[hash % count]);
        }
    }
}

String IrcClient::formatNick(const String& nick) const {
    const String color = colorForNick(nick);
    if (color.isEmpty()) return "<" + nick + ">";
    return color + "<" + nick + ">" + RESET;
}
