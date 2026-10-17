## About this fork

Private fork of [DutchDevelop/BLLEDController](https://github.com/DutchDevelop/BLLEDController), based on release 2.3.0 (`Release_V_2.3.0`), with security and reliability fixes. Versions are tagged `2.3.0-br.N`. Licence unchanged (see [LICENSE](LICENSE): CC BY-NC-SA based, non-commercial, share-alike).

What changed in `2.3.0-br.2` (on top of br.1):

- **Door quick open-and-close toggles the LEDs reliably.** In 2.3.0 a toggle-off was undone within the same printer message, and opening the door relit the strip before the toggle decided, so it could only ever end "off". It now flips the state from before the door opened, and the window is a setting (default 4 s, 0 = off; 2.3.0 used a fixed 2 s, hard to hit because door state arrives in ~1 s printer reports).
- **Finish colour ends on the door, a timer, or whichever comes first.** Timer mode no longer treats the first minutes after any boot as a finished print.
- **States readable without colour.** Errors blink (1 per second, well under the photosensitivity threshold), pause / first-layer / clog pulse, finished pulses slowly; printing and idle stay steady. Can be turned off.
- **Brightness applies live** while dragging the slider and saves when released.
- **Clearer setup page.** The LED modes are one labelled radio group (choosing test colour or RGB cycle used to silently switch off "replicate printer light"); the finish ending is a three-way choice; the text beside every switch is clickable and announced by screen readers; switches show keyboard focus.
- **Room to grow.** Built for a partition layout with 1.875 MB firmware slots (was 1.25 MB). Switching a device to it needs one USB flash ([docs/USB-MIGRATION.md](docs/USB-MIGRATION.md)); until then builds still install over the air while they fit the old slot, and every build says whether they do.
- **Tests.** The real LED and MQTT code compiles and runs on a Mac (`pio test -e native`): 49 tests drive printer states and real-shaped MQTT messages and check what the LEDs would do.

What changed in `2.3.0-br.1` compared with 2.3.0:

- **Login actually protects the device.** When a WebUI username/password is set, every settings, upload, scan and log endpoint requires it (in 2.3.0, `/config.json`, `/submitWiFi`, `/update` and `/configrestore` did not). Stored secrets (WiFi password, printer access code, WebUI password) are never sent back to the browser: on the WiFi setup page, leave those fields blank to keep the saved value, and clear the username to remove the login. Forgot the password? Sending the Wi-Fi setup over USB serial again (the web flasher's setup step) clears the login.
- **Recovers after a power cut.** If the saved WiFi isn't reachable at boot, the device keeps the `BLLED_AP` setup hotspot up (now with a working captive portal) but tries your network for 15 seconds every minute and restarts into normal mode once it's back (2.3.0 stayed in AP mode until power-cycled).
- **No more 5-second freezes.** Printer discovery no longer blocks the main loop for ~5.5 s every 10 s.
- **Gentler on the printer.** MQTT reconnects back off from 3 to 30 seconds instead of retrying continuously.
- **LED fixes.** Colour changes actually fade; RGB cycle respects the brightness limit; an HMS error colour clears when the error does (even with debug logging off); with the inactivity timeout disabled, closing the door no longer turns the lights off mid-print.
- **Robustness.** A malformed restored config or colour value can no longer crash or hang the device at boot; restored backups are validated before they replace the config; the firmware update page reports failures.
- **Thread safety.** LED/printer state and the Web Serial log are protected against concurrent access from the loop, MQTT and web-server tasks; only the MQTT task talks to the printer.
- **Reproducible builds.** Platform and library versions are pinned in `platformio.ini`.

### Working on this fork

| Task | Command |
|---|---|
| Build | `pio run -e esp32dev` → `.firmware/BLLC_V<version>.bin.ota` (over the air) and `.firmware/BLLC_V<version>.bin` (full 4 MB image, USB) |
| Run the tests | `pio test -e native` |
| Update a device over the air | `tools/flash.sh <device-ip>` (reads the WebUI login from the macOS Keychain; see the script header) |
| Try the setup pages without hardware | `python3 tools/mock_device.py src/www 8765`, then http://localhost:8765 (Wi-Fi setup) or http://localhost:8765/setup |
| Check a device for main-loop freezes | `python3 tools/ws_timing.py <device-ip> 60` |
| Switch a device to the larger partition layout | [docs/USB-MIGRATION.md](docs/USB-MIGRATION.md) |

[TESTING.md](TESTING.md) is the on-device checklist (with results); [TODO.md](TODO.md) lists what's next.

The upstream instructions below install the *official* firmware. This fork's builds go on over the air (above), or over USB as in the migration guide.

## BL Led Controller

The BL Led Controller is an ESP32 based device that connects to your Bambulab X1,X1C,P1P Or P1S and controls the LED strip based on the state of the printer.

### Flashing and Setup
1. go to the [Web Flasher](https://dutchdevelop.github.io/blledsetup/) (Or [here](https://softwarecrash.github.io/BLLED-Flasher/) for Nightly and Dev builds)
2. connect your ESP32
3. Select the Firmware build you want
4. Click on Flash
5. Search and connect to a WiFi Hotspot called "BLLED-AP"
6. Surf to http://192.168.4.1
7. Select your WiFi and Enter passwort, Optional enter all the Printer informations
8. enjoy :)

![main setup_LED](https://github.com/user-attachments/assets/52778d28-98cd-496e-bfce-aca49cd4de89)
![main_setup_printer](https://github.com/user-attachments/assets/1969e4bd-0887-4a0c-a965-8ec575e23396)
![main_setup_error](https://github.com/user-attachments/assets/b026260c-5c33-4f5c-922a-fcc601cb9be1)
![main_setup_debug](https://github.com/user-attachments/assets/6d5a6e48-b052-4305-99f5-5d9c906eb8c1)
![main_setup_navigation](https://github.com/user-attachments/assets/78041d10-2edb-42e2-b600-6cde735d2d1f)
![setup_wifi](https://github.com/user-attachments/assets/4ef7631c-38f9-4424-ad88-72a335286c27)
![setup_wifi_printer](https://github.com/user-attachments/assets/da354544-710d-4e10-9dff-fc2cc1951857)
![ota](https://github.com/user-attachments/assets/7f223361-f811-4ff4-b3fc-45b2a1222c07)
![backup_restore](https://github.com/user-attachments/assets/b21899ba-c97f-48b6-8bc3-a87797029d14)
![navigation](https://github.com/user-attachments/assets/027fe724-ffc6-453b-ba73-c82c1ddb012e)





- Connects to Bambulab X1,X1C,P1P Or P1S
- Controls LED strip based on printer state

### Development Environment

To contribute to the BL Led Controller project, you'll need the following tools:

### Tools & Libraries Used

- [Visual Studio Code](https://code.visualstudio.com/): A lightweight and powerful source code editor.
- [PlatformIO](https://platformio.org/): An open-source ecosystem for IoT development.
- [Python](https://www.python.org/): A programming language used for scripting and automation.
- [qpdf](https://qpdf.sourceforge.io/): A command-line tool and library of compression tools (`gzip`)

### Building and Running the Project
1. Clone the repository to your local machine.
2. Open the project folder in Visual Studio Code.
3. Ensure that PlatformIO is installed and configured in your Visual Studio Code environment.
4. Connect your BLLED device ESP32 to your computer.
6. Build the project by clicking on the PlatformIO icon in the sidebar and selecting "Build" from the available options.
7. Once the build process is complete, upload the firmware to your device using the "Upload" option in PlatformIO.
8. After uploading the firmware, your BL Led Controller device should be ready to use.

### Setup Instructions
Once you have uploaded the firmware to your device, please visit the [dutchdevelop.com/bl-led-controller](https://dutchdevelop.com/bl-led-controller) website for detailed setup instructions.


### Development Notes

#### Generating .h Files for Compressed HTML

In embedded applications, HTML content is efficiently stored in PROGMEM memory. To achieve this, .h files are generated from compressed HTML files for webpages (i.e., `src/www/setuppage.html`) that are run on the device.

- The `compress_html.py` is used to compress HTML files and generate corresponding .h files and is integrated into the build process and executed as a pre-build step in `platform.ini`
- The generated .h files should not be checked into git (see `.gitignore`)

### License

The BL Led Controller is released under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0) license. See the [LICENSE](https://github.com/DutchDevelop/BLLEDController/blob/main/LICENSE) file for more details.

### Credits
- **[DutchDeveloper](https://dutchdevelop.com/)**: Lead programmer
- **[Modbot](https://github.com/Modbot)**: Tester for X1C, P1P & P1S
- **[xps3riments](https://github.com/xps3riments)**: Inspiration for the foundation of the code
- **[longrackslabs](https://github.com/longrackslabs)**: Build process, documentation, developer & community support
- **[SoftWareCrash](https://github.com/softwarecrash)**: Any small unimportant changes

### Author

This project was created by [DutchDeveloper](https://dutchdevelop.com/).
