#pragma once
// Hold the BOOT button (GPIO9 on ESP32-C6 boards) for 5 s to erase the saved
// settings and restart with the build-time defaults.
void reset_button_start(void);
