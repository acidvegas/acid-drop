<p align="center">
  <img src="./.screens/aciddrop2.png" />
</p>


# Work in progress
This is a custom firmware being developed for the [LilyGo T-Deck](https://www.lilygo.cc/products/t-deck), currently it is experimental & buggy.

I am terrible at C++ and still am trying to wrap my head around the language's logic, so most of this code has been shit out and written like a cowboy.

The project started out with as IRC bridge for [Meshtastic](https://meshtastic.org/) over MQTT, just so I could say "we have IRC over radio", which later led to me modifying the official Meshtastic [firmware](https://github.com/meshtastic/firmware) to add extended features. I am deep in the rabbit whole of embedded development and am starting from scratch making this an entirely custom firmware.

This is being developed in my free time as a fun project. It is no where near being useful.

# Previews
![](./.screens/preview1.png) ![](./.screens/preview2.png)

# Flashing the Firmware
###### Using VS Code
1. Add your user to the `dialout` group: `sudo gpasswd -a YOURUSERNAME dialout` *(You will need to re-login after adding your user to the `dialout` group for it to take affect)*
2. Install [Visual Studio Code](https://code.visualstudio.com/)
3. Install the [PlatformIO plugin](https://platformio.org/install/ide?install=vscode)
4. Hold down the trackball on the device, turn it on, and plug it in to the computer.
5. Press **F1** and select `PlatformIO: Build`
6. Press **F1** and select `PlatformIO: Upload`

###### Using ESP Tool
1. Take the `firmware.bin` file from the release page and download it. *(no releases yet, we still building)*
2. Install [esptool](https://pypi.org/project/esptool/): `pip install esptool`
3. Hold down the trackball on the device, turn it on, and plug it in to the computer.
4. Confirm the serial device in your `/dev` directory *(Your device will likely be `/dev/ttyAMC0` or `/dev/ttyUSB0`)*
5. Flash the device: `esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 115200 write_flash -z 0x1000 firmware.bin`

# Roapmap
- [X] Wifi scanning & selection menu
- [ ] Saved wifi profiles
- [X] IRC Client
- [X] `/raw` command for IRC client to send raw data to the server
- [ ] Add scrolling backlog for IRC to see the last 200 messages
- [ ] ChatGPT
- [ ] SSH Client
- [ ] Wardriving
- [ ] Gotify
- [ ] Meshtastic
- [ ] Notifcations *(All notifications will go here, from IRC, Gotify, Meshtastic, or anything)* *(Need to add internal speaker support for notification sounds)*
- [X] Status bar *(Time, Date, Notification icons, Wifi icon, Battery icon with Percent)*
- [ ] Trackball support
- [ ] Serial logging + debug logs stored in memory that can be viewed on the device itself.
- [ ] Allow specifying the IRC server, port, TLS, nick, etc...
- [ ] Proper disconnection & reconnection handling

# Known issues
- Messages that exceed the screen width and wrap to the next line will throw off thje logic of calculating the max lines able to be displayed on the screen. Messages eventually go off screen.
- Input messages exceeding the screen width go off screen, and need to wrap and create a new input line, pushing up the backlog of messages.
- Some colors are not working when its only a background color or only contains a space


# Contributors
Join us in **#comms** on **[irc.supernets.org](irc://irc.supernets.org)** if you want to get your hands dirty.

___

###### Mirrors for this repository: [acid.vegas](https://git.acid.vegas/acid-drop) • [SuperNETs](https://git.supernets.org/acidvegas/acid-drop) • [GitHub](https://github.com/acidvegas/acid-drop) • [GitLab](https://gitlab.com/acidvegas/acid-drop) • [Codeberg](https://codeberg.org/acidvegas/acid-drop)