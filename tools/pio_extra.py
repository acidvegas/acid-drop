"""PlatformIO pre-build hook.

Third-party libraries such as RadioLib include <SPI.h> from their own headers.
PlatformIO's dependency finder resolves those bundled Arduino libraries for the
project, but does not hand their include paths to sibling libraries, so those
builds fail with "SPI.h: No such file or directory".

Adding the bundled libraries' src directories to the shared CPPPATH fixes it for
every library in the build at once.
"""

import os

Import("env")  # noqa: F821 - injected by PlatformIO

BUNDLED = [
    "SPI",
    "Wire",
    "FS",
    "SD",
    "SPIFFS",
    "WiFi",
    "WiFiClientSecure",
    "Preferences",
    "ESPmDNS",
    "Update",
    "Ticker",
    "ArduinoOTA",
    "NetworkClientSecure",
    "Network",
]

framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")  # noqa: F821

added = []
for name in BUNDLED:
    path = os.path.join(framework_dir, "libraries", name, "src")
    if os.path.isdir(path):
        env.Append(CPPPATH=[path])  # noqa: F821
        added.append(name)

print("acid-drop: shared include paths ->", ", ".join(added))
