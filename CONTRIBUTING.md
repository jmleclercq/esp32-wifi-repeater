# Contributing

Thanks for your interest! This is a small project and I'd like to keep it that way: **simple, readable,
and focused on being a WiFi repeater for the ESP32-C6**. Bug reports, hardware reports and focused
improvements are all welcome.

By taking part you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Ways to help
- **Report a bug**: open an issue with the *Bug report* form.
- **Tell me what hardware works**: the *Hardware report* form is the most useful contribution for
  other boards (ESP32-C6 dev kits, other flash sizes, BOOT button on a different pin...).
- **Suggest a feature**: use the *Feature request* form. Please explain the use case first.
  Features that add a lot of code or RAM use may be declined to keep the project small;
  broader routers such as [esp32_nat_router](https://github.com/martin-ger/esp32_nat_router) may suit you better.
- **Improve the docs**: typos, clearer steps, translations of the README are welcome.

## Before you open an issue
- Check the [README](README.md) (limits, locked-out recovery) and existing issues.
- **Never post passwords.** Your WiFi SSID and MAC addresses can identify you: redact them in logs
  and screenshots if you prefer.

## Development setup
You need [ESP-IDF v5.5.x](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32c6/get-started/)
and an ESP32-C6 board.

```bash
. ~/esp/esp-idf/export.sh
cp sdkconfig.local.example sdkconfig.local     # your own test credentials (git-ignored)
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The code is in `main/` (see "How it works" in the README for what each file does).

## Pull requests
1. Fork, create a branch, keep the change focused (one topic per PR).
2. Build with no warnings and **test on a real board**: say in the PR which board and what you checked
   (uplink, clients, the web page, a speed test if you touched the data path).
3. Match the existing style: plain C, `ESP_ERROR_CHECK` for setup that must not fail, short comments that
   explain *why*, no new dependencies unless really needed.
4. If you add a setting, wire it through `config.c`, the web form and `docs`/README together.
5. Never commit `sdkconfig.local` or anything containing credentials.
6. The **Build** check must pass. It compiles the firmware with ESP-IDF v5.5.1 for the ESP32-C6.

Commit messages: a short imperative summary line, then a few lines of explanation if the reason is
not obvious.

## Releases
Maintainer only: pushing a `v*` tag builds the firmware, publishes a GitHub release with the images
and refreshes the [web installer](https://jmleclercq.github.io/esp32-wifi-repeater/).
