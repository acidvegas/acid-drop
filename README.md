<p align="center">
  <img src="./.screens/aciddrop2.png" />
</p>

# ACID DROP

IRC client firmware for the [LilyGo T-Deck Plus](https://www.lilygo.cc/products/t-deck) and the original T-Deck. It boots straight into the chat window and does nothing but IRC.

Development happens in **#superbowl** on **[irc.supernets.org](irc://irc.supernets.org)**.

![](./.screens/preview.png)

## Features

- Connects directly to an IRC server, through a **ZNC** bouncer, or to a **WeeChat relay**
- ASCII/ANSI art, Unicode, emoji, and full mIRC colours & formatting
- TLS with optional certificate verification, SASL PLAIN, and NickServ
- Multiple windows for the server, channels, and private messages
- Nick and command completion
- Auto-reconnect, rejoin on kick, and retrying of `+i`, `+k`, `+b` and full channels
- Per-channel keys and auto-join, and an ignore list with wildcard masks
- Channel details: modes, topic, and nick list
- Themeable colours and a settings menu on the device, all applied live

## Flashing

Requires [PlatformIO](https://platformio.org/).

```sh
pio run -e t-deck-plus -t upload    # T-Deck Plus
pio run -e t-deck      -t upload    # original T-Deck
```

Hold the trackball down, turn the device on, then plug it in to enter download mode.

The upload port in `platformio.ini` is set to `/dev/cu.usbmodem*`, which is the macOS path. On Linux, add yourself to the `dialout` group and pass the port yourself:

```sh
pio run -e t-deck-plus -t upload --upload-port /dev/ttyACM0
```

Do not attach a serial monitor. On this board it resets the chip. Use **Settings > System > System log** instead.

## Usage

Set up WiFi under **Settings > WiFi**. The device connects on its own once the network is up.

### Connecting

**Settings > IRC > Server > Mode** picks how you connect. Only one mode is active at a time. Picking ZNC or WeeChat stops the device from connecting to the server set under **IRC > Server**, and switching modes disconnects the old one.

| Mode          | Configure under  | Notes                                                                                                                                                       |
| ------------- | ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Direct IRC    | **IRC > Server** | The device is the IRC client and uses its own auto-join channel list                                                                                        |
| ZNC bouncer   | **IRC > ZNC**    | Host, port, TLS, username, password, and network *(empty for the default)*. The local auto-join list is ignored, ZNC decides what channels you are in       |
| WeeChat relay | **IRC > Relay**  | Host, port, password, and TLS. Shows every network and buffer WeeChat has. Everything you type is passed to WeeChat as-is, so its commands and aliases work |

### Keys

| Key                  | Action                                           |
|----------------------|--------------------------------------------------|
| Trackball up/down    | Scroll the chat, or move focus                   |
| Trackball left/right | Switch windows, or adjust the focused control    |
| Trackball click      | Select                                           |
| Trackball hold       | Back to the chat window from anywhere            |
| `$`                  | Accept the grey completion, press again to cycle |
| Hold `w` at boot     | Erase all settings                               |

### Commands

| Command                                         | Description                     |
|-------------------------------------------------|---------------------------------|
| `/join` `/part` `/cycle` `/close`               | Channels and windows            |
| `/msg` `/query` `/notice` `/me` `/ctcp` `/amsg` | Messaging                       |
| `/say <text>`                                   | Send text starting with a slash |
| `/nick` `/away` `/back`                         | Your status                     |
| `/topic` `/names` `/mode` `/invite` `/list`     | Channel info                    |
| `/op` `/voice` `/halfop` *(and `de-` versions)* | Channel privileges              |
| `/kick` `/ban` `/unban` `/kickban`              | Moderation                      |
| `/whois` `/whowas` `/who` `/ison`               | Look people up                  |
| `/ignore` `/unignore` `/ignores`                | Ignore list                     |
| `/connect` `/disconnect` `/reconnect` `/quit`   | Connection                      |
| `/server <host> [+port]`                        | Switch server, `+` means TLS    |
| `/raw <line>`                                   | Send a raw line                 |
| `/clear` `/window N` `/next` `/prev` `/0`..`/9` | Window control                  |
| `/settings` `/channels` `/help`                 | Open menus, list commands       |

Unknown commands are sent to the server as-is.

## Known limitations

- ANSI art wider than the screen is wrapped, not scrolled
- Emoji are monochrome
- Certificate verification only knows the roots compiled into `src/net/CaCerts.h` *(currently the Sectigo roots irc.supernets.org uses)*
- WeeChat relay only supports plaintext password auth, not hashed
- WeeChat relay backlog is 50 lines per buffer and is not kept across reconnects
- GPS, LoRa, Bluetooth, the microphone, and the SD card are not used
