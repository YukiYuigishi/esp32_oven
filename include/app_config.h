#pragma once

#include <Arduino.h>

// Pin assignment (VSPI defaults)
constexpr int PIN_MAX31855_SCK = 18;
constexpr int PIN_MAX31855_MISO = 19;
constexpr int PIN_MAX31855_CS = 5;
constexpr int PIN_SSR = 4;
constexpr int PIN_RUN_SWITCH = 2;

// Control timing (ms)
constexpr uint32_t TEMP_SAMPLE_MS = 200;
constexpr uint32_t CONTROL_PERIOD_MS = 200;

// Time-proportional control window
constexpr uint32_t WINDOW_MS = 1000;

// AP/mDNS defaults
constexpr char AP_SSID[] = "esp32-oven";
constexpr char AP_PASSWORD[] = "esp32-oven";
constexpr char MDNS_HOST[] = "esp32-oven";

constexpr uint8_t MAX_SMOOTH_WINDOW = 10;
