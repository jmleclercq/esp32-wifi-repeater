# Security Policy

## Supported versions
Only the **latest release** receives fixes.

## Reporting a vulnerability
Please **do not open a public issue** for a security problem.

Report it privately from the repository's **Security** tab → **Report a vulnerability**
(GitHub private vulnerability reporting). Include what you found, how to reproduce it, and the
firmware version (shown on the web page).

This is a hobby project maintained in spare time: I'll answer as soon as I can and credit you in the
fix if you wish.

## Things to know about the design
These are known properties, not vulnerabilities, but worth understanding before you deploy:

- **Placeholder passwords at first boot.** A freshly flashed repeater uses the SSID `ESP32-Repeater` and
  the password `changeme123` until you change them from the web page. Do this right away.
- **The web interface uses plain HTTP** with Basic authentication (user `admin`). It is only reachable from
  the repeater's own WiFi network (192.168.4.0/24), never from the upstream side, so it relies on the WPA2
  encryption of that network.
- **The upstream WiFi password is stored in clear text in flash (NVS).** Flash encryption is not enabled,
  so anyone with physical access to the board can read it.
- **No firmware signing and no OTA.** Updates are flashed over USB.
- The repeater is a NAT router, not a firewall: it applies no filtering to its clients' traffic.
