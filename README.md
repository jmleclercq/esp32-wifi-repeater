# ESP32-C6 WiFi Repeater

A small NAT-based WiFi repeater for the **ESP32-C6**, written with ESP-IDF.
The board joins your existing WiFi network (station) and exposes its own access
point; traffic between the two is routed with lwIP NAPT.

```
 Clients ~~~ [ESP32-C6 AP 192.168.4.1/24]──NAT──[STA]~~~ Router ── Internet
```

## Features
- STA + AP + NAPT, DNS forwarded to clients, automatic reconnection
- Connects to the strongest access point sharing the upstream SSID
- WiFi 6 (11ax) on 20 MHz channels, power save off
- Settings stored in flash (NVS) and editable from a web page, no rebuild needed

See [docs/roadmap.md](docs/roadmap.md).

## Requirements
- ESP32-C6 board with a USB data cable
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32c6/get-started/) v5.5.x

## Build and flash
```bash
. ~/esp/esp-idf/export.sh
cp sdkconfig.local.example sdkconfig.local     # then edit SSIDs and passwords
idf.py set-target esp32c6
idf.py build                                   # picks up sdkconfig.local automatically
idf.py -p /dev/ttyACM0 flash monitor
```
`sdkconfig.local` (git-ignored) and `menuconfig` (*WiFi Repeater*) only provide the
**initial defaults**. Once you save settings from the web page, the values stored
in flash take precedence.

## Web interface
Connect to the repeater's WiFi and open **http://192.168.4.1/** (log in as `admin`).

<p align="center"><img src="docs/images/web-ui.png" alt="Web configuration page" width="320"></p>
<p align="center"><em>Rendered from the firmware's page HTML with example status values.</em></p>

- Shows upstream status (channel, RSSI, IP), connected clients and free memory
- Edits the upstream network, the repeater SSID/password and the admin password
- Password fields are never displayed; leave them blank to keep the current value
- Saving restarts the repeater

Security: the page only answers clients of the repeater's own network
(192.168.4.0/24), never from the upstream side. The admin password defaults to the
repeater WiFi password until you set a separate one. Passwords must be 8-63 characters.

**Locked out?** Erase the stored settings and fall back to the build-time defaults:
`idf.py -p /dev/ttyACM0 erase-flash` then flash again.

On Linux, add yourself to the serial group once: `sudo usermod -aG dialout $USER`.

## Notes
- Single 2.4 GHz radio: throughput is shared between upstream and AP, expect tens of Mbit/s at best.
- The AP channel follows the upstream network's channel.
- Clients get addresses in 192.168.4.0/24, so the upstream network must use a different subnet.

## License
See [LICENSE](LICENSE).
