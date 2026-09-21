#include "irc/IrcCommands.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "irc/ChannelList.h"
#include "irc/IrcClient.h"

namespace irccmd {
namespace {

constexpr const char* TAG = "irccmd";

struct Parsed {
    String verb;      // lower case, no slash
    String rest;      // everything after the first space, untrimmed tail
};

Parsed split(const String& line) {
    Parsed out;
    const int space = line.indexOf(' ');
    out.verb = space < 0 ? line.substring(1) : line.substring(1, space);
    out.rest = space < 0 ? String() : line.substring(space + 1);
    out.verb.toLowerCase();
    out.rest.trim();
    return out;
}

// Pulls the first whitespace-separated word off `text`, leaving the remainder.
String takeWord(String& text) {
    text.trim();
    const int space = text.indexOf(' ');
    if (space < 0) {
        const String word = text;
        text = "";
        return word;
    }
    const String word = text.substring(0, space);
    text = text.substring(space + 1);
    text.trim();
    return word;
}

bool matches(const String& verb, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (verb == name) return true;
    }
    return false;
}

// The channel a channel-scoped command should act on: an explicit first
// argument if it looks like a channel, otherwise the current window.
String targetChannel(const Context& context, String& rest) {
    String candidate = rest;
    const String first = takeWord(candidate);

    if (irc::isChannel(first)) {
        rest = candidate;
        return first;
    }
    if (context.window && context.window->isChannel()) return context.window->name;
    return String();
}

// Builds "MODE #chan +ooo nick1 nick2 nick3" from a list of nicks.
void applyNickMode(const Context& context, const String& channel, char sign, char flag,
                   String nicks) {
    if (channel.isEmpty()) {
        context.echo("Not a channel window");
        return;
    }

    String modes;
    String targets;
    int    count = 0;

    while (!nicks.isEmpty() && count < 4) {   // servers cap modes per line
        const String nick = takeWord(nicks);
        if (nick.isEmpty()) break;
        modes   += flag;
        targets += " " + nick;
        count++;
    }

    if (count == 0) {
        context.echo("Usage: name at least one nick");
        return;
    }

    context.client->sendRaw("MODE " + channel + " " + String(sign) + modes + targets);
}

const char* const kHelp[] = {
    "Windows:  /window N  /next  /prev  /close  /clear  /0..9",
    "Channels: /join #c [key]  /part [#c]  /cycle  /topic [t]  /names  /list",
    "          /invite nick  /kick nick [r]  /ban nick  /unban  /kickban",
    "          /mode [#c] modes  /op /deop /voice /devoice /halfop /dehalfop",
    "Messages: /msg t text  /query nick  /notice t text  /me action",
    "          /amsg text  /say text  /ctcp t verb",
    "User:     /nick n  /away [r]  /back  /whois n  /whowas n  /who m  /ison n",
    "Server:   /connect  /server host [port]  /disconnect  /reconnect  /quit [m]",
    "Client:   /settings  /channels  /raw line  /help",
    "Anything else is sent to the server as typed, e.g. /lusers or /motd.",
    nullptr,
};

} // namespace

const char* const* helpText() { return kHelp; }

bool run(const String& line, const Context& context) {
    if (!line.startsWith("/")) return false;

    // "//text" escapes a leading slash so you can say "/me" literally.
    if (line.startsWith("//")) return false;

    IrcClient& client = *context.client;
    IrcBuffer* window = context.window;
    const Parsed parsed = split(line);
    String rest = parsed.rest;

    const String verb = parsed.verb;
    if (verb.isEmpty()) return true;

    // --- window switching: /0 .. /99 -------------------------------------
    bool allDigits = true;
    for (unsigned int i = 0; i < verb.length(); i++) {
        if (!isdigit(static_cast<unsigned char>(verb[i]))) { allDigits = false; break; }
    }
    if (allDigits && verb.length() <= 2) {
        context.selectWindow(verb.toInt());
        return true;
    }

    // --- client-side ------------------------------------------------------
    if (matches(verb, {"help", "h"})) {
        for (const char* const* p = kHelp; *p; p++) context.echo(*p);
        return true;
    }
    if (matches(verb, {"clear"}))              { context.clearWindow(); return true; }
    if (matches(verb, {"close", "wc"}))        { context.closeWindow(); return true; }
    if (matches(verb, {"settings", "config"})) { context.openSettings(); return true; }
    if (matches(verb, {"channels", "chanlist"})) { context.openChannels(); return true; }

    if (matches(verb, {"window", "win", "buffer", "buf"})) {
        const String which = takeWord(rest);
        if (which.isEmpty()) { context.echo("Usage: /window <number>"); return true; }
        context.selectWindow(which.toInt());
        return true;
    }
    if (matches(verb, {"next", "wn"})) { context.nextWindow(); return true; }
    if (matches(verb, {"prev", "wp"})) { context.prevWindow(); return true; }

    // --- connection -------------------------------------------------------
    if (matches(verb, {"connect"})) { client.connect(); return true; }

    if (matches(verb, {"server", "s"})) {
        if (!rest.isEmpty()) {
            const String host = takeWord(rest);
            settings::setText("irc_server", host);

            const String port = takeWord(rest);
            if (!port.isEmpty()) {
                // A leading '+' is the conventional way to say "TLS".
                const bool tls = port.startsWith("+");
                settings::setInt("irc_port", port.substring(tls ? 1 : 0).toInt());
                if (tls) settings::setBool("irc_tls", true);
            }
            context.echo("Server set to " + host);
        }
        client.disconnect(settings::getText("irc_quitmsg"), false);
        client.connect();
        return true;
    }

    if (matches(verb, {"disconnect"})) {
        client.disconnect(rest.isEmpty() ? settings::getText("irc_quitmsg") : rest, true);
        return true;
    }
    if (matches(verb, {"reconnect"})) {
        client.disconnect("Reconnecting", false);
        client.connect();
        return true;
    }
    if (matches(verb, {"quit", "exit"})) {
        client.disconnect(rest.isEmpty() ? settings::getText("irc_quitmsg") : rest, true);
        return true;
    }

    // --- channels ---------------------------------------------------------
    if (matches(verb, {"join", "j"})) {
        if (rest.isEmpty()) { context.echo("Usage: /join #channel [key]"); return true; }
        String name = takeWord(rest);
        if (!irc::isChannel(name)) name = "#" + name;
        client.join(name, takeWord(rest));
        return true;
    }

    if (matches(verb, {"part", "leave", "p"})) {
        const String channel = targetChannel(context, rest);
        if (channel.isEmpty()) { context.echo("Not a channel window"); return true; }
        client.part(channel, rest);
        return true;
    }

    if (matches(verb, {"cycle", "hop"})) {
        const String channel = targetChannel(context, rest);
        if (channel.isEmpty()) { context.echo("Not a channel window"); return true; }
        const String key = window ? window->key : String();
        client.sendRaw("PART " + channel + " :cycling");
        client.join(channel, key);
        return true;
    }

    if (matches(verb, {"topic", "t"})) {
        const String channel = targetChannel(context, rest);
        if (channel.isEmpty()) { context.echo("Not a channel window"); return true; }
        client.sendRaw(rest.isEmpty() ? "TOPIC " + channel
                                      : "TOPIC " + channel + " :" + rest);
        return true;
    }

    if (matches(verb, {"names", "n"})) {
        const String channel = targetChannel(context, rest);
        client.sendRaw(channel.isEmpty() ? "NAMES" : "NAMES " + channel);
        return true;
    }

    if (matches(verb, {"invite", "i"})) {
        const String nick = takeWord(rest);
        if (nick.isEmpty()) { context.echo("Usage: /invite <nick> [#channel]"); return true; }
        const String channel = targetChannel(context, rest);
        if (channel.isEmpty()) { context.echo("Not a channel window"); return true; }
        client.sendRaw("INVITE " + nick + " " + channel);
        return true;
    }

    if (matches(verb, {"kick", "k"})) {
        String working = rest;
        const String channel = targetChannel(context, working);
        const String nick    = takeWord(working);
        if (channel.isEmpty() || nick.isEmpty()) {
            context.echo("Usage: /kick [#channel] <nick> [reason]");
            return true;
        }
        client.sendRaw("KICK " + channel + " " + nick +
                       (working.isEmpty() ? String() : " :" + working));
        return true;
    }

    if (matches(verb, {"ban", "b", "unban", "kickban", "kb"})) {
        const bool unban   = verb == "unban";
        const bool andKick = verb == "kickban" || verb == "kb";

        String working = rest;
        const String channel = targetChannel(context, working);
        const String who     = takeWord(working);
        if (channel.isEmpty() || who.isEmpty()) {
            context.echo("Usage: /" + verb + " [#channel] <nick|mask> [reason]");
            return true;
        }

        // A bare nick becomes a nick!*@* mask; anything with a ! or @ is
        // assumed to already be the mask the user wants.
        const String mask = (who.indexOf('!') >= 0 || who.indexOf('@') >= 0)
                            ? who : who + "!*@*";

        client.sendRaw("MODE " + channel + (unban ? " -b " : " +b ") + mask);
        if (andKick) {
            client.sendRaw("KICK " + channel + " " + who +
                           (working.isEmpty() ? String() : " :" + working));
        }
        return true;
    }

    if (matches(verb, {"mode"})) {
        String working = rest;
        const String channel = targetChannel(context, working);
        if (working.isEmpty()) {
            // Bare /mode asks the server what the current modes are.
            client.sendRaw(channel.isEmpty() ? "MODE " + client.nick() : "MODE " + channel);
            return true;
        }
        client.sendRaw("MODE " + (channel.isEmpty() ? client.nick() : channel) + " " + working);
        return true;
    }

    if (matches(verb, {"op", "deop", "voice", "devoice", "halfop", "dehalfop"})) {
        String working = rest;
        const String channel = targetChannel(context, working);
        const bool   remove  = verb.startsWith("de");
        const char   flag    = verb.endsWith("halfop") ? 'h'
                             : verb.endsWith("voice")  ? 'v'
                                                       : 'o';
        applyNickMode(context, channel, remove ? '-' : '+', flag, working);
        return true;
    }

    if (matches(verb, {"list"})) {
        client.sendRaw(rest.isEmpty() ? "LIST" : "LIST " + rest);
        context.echo("Channel list requested - results appear in the status window");
        return true;
    }

    // --- messaging --------------------------------------------------------
    if (matches(verb, {"msg", "m"})) {
        const String target = takeWord(rest);
        if (target.isEmpty() || rest.isEmpty()) {
            context.echo("Usage: /msg <target> <text>");
            return true;
        }
        client.say(target, rest);
        return true;
    }

    if (matches(verb, {"query", "q"})) {
        const String nick = takeWord(rest);
        if (nick.isEmpty()) { context.echo("Usage: /query <nick> [text]"); return true; }

        client.ensureBuffer(nick, BufferKind::Query);
        for (size_t i = 0; i < client.bufferCount(); i++) {
            if (irc::equalsIgnoreCaseIrc(client.buffer(i).name, nick)) {
                context.selectWindow(i);
                break;
            }
        }
        if (!rest.isEmpty()) client.say(nick, rest);
        return true;
    }

    if (matches(verb, {"notice"})) {
        const String target = takeWord(rest);
        if (target.isEmpty() || rest.isEmpty()) {
            context.echo("Usage: /notice <target> <text>");
            return true;
        }
        client.notice(target, rest);
        return true;
    }

    if (matches(verb, {"me", "action"})) {
        if (!window || window->isStatus()) { context.echo("No target in this window"); return true; }
        if (rest.isEmpty()) { context.echo("Usage: /me <action>"); return true; }
        client.action(window->name, rest);
        return true;
    }

    if (matches(verb, {"say"})) {
        if (!window || window->isStatus()) { context.echo("No target in this window"); return true; }
        client.say(window->name, rest);
        return true;
    }

    if (matches(verb, {"amsg"})) {
        if (rest.isEmpty()) { context.echo("Usage: /amsg <text>"); return true; }
        size_t sent = 0;
        for (size_t i = 0; i < client.bufferCount(); i++) {
            IrcBuffer& buffer = client.buffer(i);
            if (buffer.isChannel() && buffer.joined) {
                client.say(buffer.name, rest);
                sent++;
            }
        }
        context.echo("Sent to " + String(sent) + " channels");
        return true;
    }

    if (matches(verb, {"ctcp"})) {
        const String target = takeWord(rest);
        const String what   = takeWord(rest);
        if (target.isEmpty() || what.isEmpty()) {
            context.echo("Usage: /ctcp <target> <VERSION|PING|TIME|...>");
            return true;
        }
        String payload = what;
        payload.toUpperCase();
        if (!rest.isEmpty()) payload += " " + rest;
        client.sendRaw("PRIVMSG " + target + " :\001" + payload + "\001");
        return true;
    }

    // --- user -------------------------------------------------------------
    if (matches(verb, {"nick"})) {
        if (rest.isEmpty()) { context.echo("Usage: /nick <name>"); return true; }
        const String wanted = takeWord(rest);
        // Persist it so a reconnect keeps the nick you actually wanted.
        settings::setText("irc_nick", wanted);
        client.setNick(wanted);
        return true;
    }

    if (matches(verb, {"away"})) {
        client.sendRaw(rest.isEmpty() ? "AWAY" : "AWAY :" + rest);
        context.echo(rest.isEmpty() ? "Marked back" : "Marked away: " + rest);
        return true;
    }
    if (matches(verb, {"back"})) {
        client.sendRaw("AWAY");
        context.echo("Marked back");
        return true;
    }

    if (matches(verb, {"whois", "wi"})) {
        const String nick = rest.isEmpty() && window && window->kind == BufferKind::Query
                            ? window->name : takeWord(rest);
        if (nick.isEmpty()) { context.echo("Usage: /whois <nick>"); return true; }
        client.sendRaw("WHOIS " + nick);
        return true;
    }
    if (matches(verb, {"whowas"})) {
        if (rest.isEmpty()) { context.echo("Usage: /whowas <nick>"); return true; }
        client.sendRaw("WHOWAS " + rest);
        return true;
    }
    if (matches(verb, {"who"})) {
        client.sendRaw(rest.isEmpty() ? "WHO" : "WHO " + rest);
        return true;
    }
    if (matches(verb, {"ison"})) {
        if (rest.isEmpty()) { context.echo("Usage: /ison <nick> [nick...]"); return true; }
        client.sendRaw("ISON " + rest);
        return true;
    }

    // --- raw --------------------------------------------------------------
    if (matches(verb, {"raw", "quote"})) {
        if (rest.isEmpty()) { context.echo("Usage: /raw <line>"); return true; }
        client.sendRaw(rest);
        return true;
    }

    // --- anything else ----------------------------------------------------
    // Pass it through. This is what makes /lusers, /motd, /stats, /oper and
    // every other server command work without the firmware knowing about them.
    String passthrough = verb;
    passthrough.toUpperCase();
    if (!rest.isEmpty()) passthrough += " " + rest;

    LOG_D(TAG, "passthrough: %s", passthrough.c_str());
    if (!client.sendRaw(passthrough)) {
        context.echo("Not connected");
    }
    return true;
}

} // namespace irccmd
