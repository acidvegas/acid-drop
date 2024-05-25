<p align="center">
  <img src="./.screens/aciddrop.png" />
</p>


# Work in progress
This is a custom firmware being developed for the [LilyGo T-Deck](https://www.lilygo.cc/products/t-deck), currently it is experimental & buggy.

I am terrible at C++ and still am trying to wrap my head around the language's logic, so most of this code has been shit out and written like a cowboy.

The project started out with as IRC bridge for [Meshtastic](https://meshtastic.org/) over MQTT, just so I could say "we have IRC over radio", which later led to me modifying the official Meshtastic [firmware](https://github.com/meshtastic/firmware) to add extended features. I am deep in the rabbit whole of embedded development and am starting from scratch making this an entirely custom firmware.

This is being developed in my free time as a fun project. It is no where near being useful.

# Flashing the Firmware
1. Add your user to the `dialout` group: `sudo gpasswd -a YOURUSERNAME dialout`
2. Install [Visual Studio Code](https://code.visualstudio.com/)
3. Install the [PlatformIO plugin](https://platformio.org/install/ide?install=vscode)
4. Press F1 and select PlatformIO : Build
5. Press F1 and select PlatformIO : Upload

**Note:** Your device will likely be `/dev/ttyAMC0` or `/dev/ttyUSB0`. You will also need to re-login after adding your user to the `dialout` group.

# Roapmap
- [X] IRC Client
- [ ] ChatGPT
- [ ] SSH Client
- [ ] Wardriving
- [ ] Gotify
- [ ] Meshtastic
- [ ] Notifcations *(All notifications will go here, from IRC, Gotify, Meshtastic, or anything)* *(Need to add internal speaker support for notification sounds)*
- [X] Status bar *(Time, Date, Notification icons, Wifi icon, Battery icon with Percent)*
- Trackball support
- Serial logging + debug logs stored in memory that can be viewed on the device itself.
- Allow specifying the IRC server, port, TLS, nick, etc...
- Proper disconnection & reconnection handling

# Contributors
Join us in **#comms** on **[irc.supernets.org](irc://irc.supernets.org)** if you want to get your hands dirty.

___

###### Mirrors for this repository: [acid.vegas](https://git.acid.vegas/acid-drop) • [SuperNETs](https://git.supernets.org/acidvegas/acid-drop) • [GitHub](https://github.com/acidvegas/acid-drop) • [GitLab](https://gitlab.com/acidvegas/acid-drop) • [Codeberg](https://codeberg.org/acidvegas/acid-drop)