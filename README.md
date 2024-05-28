<p align="center">
  <img src="./.screens/aciddrop2.png" />
</p>


# Work in progress
This is a custom firmware being developed for the [LilyGo T-Deck](https://www.lilygo.cc/products/t-deck), currently it is experimental & buggy.

I am terrible at C++ and still am trying to wrap my head around the language's logic, so most of this code has been shit out and written like a cowboy.

The project started out with as IRC bridge for [Meshtastic](https://meshtastic.org/) over MQTT, just so I could say "we have IRC over radio", which later led to me modifying the official Meshtastic [firmware](https://github.com/meshtastic/firmware) to add extended features. I am deep in the rabbit hole of embedded development and am starting from scratch making this an entirely custom firmware.

This is being developed in my free time as a fun project. It is no where near being useful.

A compiled "release" will be done once I finish somoe fo the basic features, but feel free t compile this on your own to test it out!

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
7. Press the RST *(reset)* button ont he device.

###### Using ESP Tool
1. Take the `firmware.bin` file from the release page and download it.
2. Install [esptool](https://pypi.org/project/esptool/): `pip install esptool`
3. Hold down the trackball on the device, turn it on, and plug it in to the computer.
4. Confirm the serial device in your `/dev` directory *(Your device will likely be `/dev/ttyAMC0` or `/dev/ttyUSB0`)*
5. Flash the device: `esptool.py --chip esp32-s3 --port /dev/ttyUSB0 --baud 115200 write_flash -z 0x1000 firmware.bin`
6. Press the RST *(reset)* button ont he device.

# Connecting to WiFi
The device will scan for WiFi networks on boot. Once the list is displayed, you can scroll up and down the list with the "u" key for UP and the "d" key for down.

# Commands
| Command         | Description                 |
| --------------- | --------------------------- |
| `/debug`        | Show hardware information   |
| `/me <message>` | Send an ACTION message      |
| `/nick <new>`   | Change your NICK on IRC     |
| `/raw <data>`   | Send RAW data to the server |

# Debugging over Serial
1. Install screen: `apt-get install screen` *(or whatever package manager you use)*
2. Plug in your device via USB.
2. Turn the device on, and run: `screen /dev/ttyAMC0 9600` *(again, this can also be /dev/ttyUSB0)*

# Roapmap
###### Device functionality
- [X] Screen timeout on inactivity *(default 30 seconds)*
- [ ] Keyboard backlight timeout on 10 seconds oof inactivity.
- [ ] Trackball support
- [ ] Speaker support

###### Features
- [X] Wifi scanning & selection menu
- [ ] Saved wifi profiles
- [ ] Wifi Hotspot
- [ ] Notifcations Window *(All notifications will go here, from IRC, Gotify, Meshtastic, or anything)*
- [X] Status bar *(Time, Date, Notification, Wifi, and Battery)*
  - [ ] XBM icons for status bar items
- [ ] Allow specifying the IRC server, port, TLS, nick, etc...
- [ ] Screensaver
- [X] Serial debug logs

###### Applications
- [X] IRC Client
  - [X] `/raw` command for IRC client to send raw data to the server
  - [ ] Add scrolling backlog for IRC to see the last 200 messages
  - [ ] Multi-buffer support *(`/join` & `/part` support with switching between buffers with `/0`, `/1`, `/2`, etc)*
  - [X] Hilight support *(so we can see when people mention our NICK)*
  - [X] 99 color support
- [ ] ChatGPT
- [ ] SSH Client
- [ ] Wardriving
- [ ] Evil Portal AP
- [ ] Local Network Probe *(Scans for devices on the wifi network you are connected to, add port scanning)*
- [ ] Gotify
- [ ] Meshtastic
- [ ] Spotify/Music player *(can we play audio throuigh Bluetoth headphones or the on-board speaker?)*
- [ ] Syslog *(All serial logs will be displayed here for on-device debugging)*

# Known issues
- Messages that exceed the screen width and wrap to the next line will throw off thje logic of calculating the max lines able to be displayed on the screen. Messages eventually go off screen.

# Contributors
Join us in **#comms** on **[irc.supernets.org](irc://irc.supernets.org)** if you want to get your hands dirty.

# More screens..
![](./.screens/99colors.png)
![](./.screens/hueg.png)

___

###### Mirrors for this repository: [acid.vegas](https://git.acid.vegas/acid-drop) • [SuperNETs](https://git.supernets.org/acidvegas/acid-drop) • [GitHub](https://github.com/acidvegas/acid-drop) • [GitLab](https://gitlab.com/acidvegas/acid-drop) • [Codeberg](https://codeberg.org/acidvegas/acid-drop)