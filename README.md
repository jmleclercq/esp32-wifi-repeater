# ESP32-C6 WiFi Repeater

A small NAT-based WiFi repeater for the **ESP32-C6**, written with ESP-IDF.
The board joins your existing WiFi network (station) and exposes its own access
point; traffic between the two is routed with lwIP NAPT.

```
 Clients ~~~ [ESP32-C6 AP 192.168.4.1/24]──NAT──[STA]~~~ Router ── Internet
```

## Status
Prototype: STA + AP + NAPT, DNS forwarded to clients, automatic reconnection.
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
Settings can also be changed with `idf.py menuconfig` under *WiFi Repeater*.

On Linux, add yourself to the serial group once: `sudo usermod -aG dialout $USER`.

## Notes
- Single 2.4 GHz radio: throughput is shared between upstream and AP, expect tens of Mbit/s at best.
- The AP channel follows the upstream network's channel.
- Clients get addresses in 192.168.4.0/24, so the upstream network must use a different subnet.

## License
See [LICENSE](LICENSE).
