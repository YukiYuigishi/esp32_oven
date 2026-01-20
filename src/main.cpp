#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Adafruit_MAX31855.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

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

constexpr char AP_SSID[] = "esp32-oven";
constexpr char AP_PASSWORD[] = "esp32-oven";
constexpr char MDNS_HOST[] = "esp32-oven";

// Basic configuration (later to be persisted and updated via API)
struct ControlConfig {
  float kp = 0.03f;
  float bias = 0.0f;
  float setpoint_c = 100.0f;  // placeholder until profile runner is implemented
  float tmax_c = 300.0f;
  bool ssr_active_high = true;
  bool switch_active_high = false; // pull-up, active LOW by default
  uint32_t window_ms = WINDOW_MS;
  uint32_t min_on_ms = 0;
  uint32_t min_off_ms = 0;
  uint8_t smooth_window = 1; // 1 = no smoothing
  bool auto_run = false;     // API-driven run/stop for now
};

enum class RunState {
  IDLE,
  RUNNING,
  SWITCH_DISABLED,
  FAULT
};

struct ControlStatus {
  float t_meas_c = NAN;
  float t_set_c = NAN;
  float duty = 0.0f;
  uint8_t last_fault = 0;
  RunState state = RunState::IDLE;
  bool run_switch_enabled = false;
};

ControlConfig g_config;
ControlStatus g_status;

Adafruit_MAX31855 g_thermocouple(PIN_MAX31855_SCK, PIN_MAX31855_CS, PIN_MAX31855_MISO);
WebServer g_server(80);
bool g_server_started = false;

uint32_t g_last_sample_ms = 0;
uint32_t g_last_control_ms = 0;
uint32_t g_window_start_ms = 0;

// Simple moving average buffer
constexpr uint8_t MAX_SMOOTH_WINDOW = 10;
float g_temp_samples[MAX_SMOOTH_WINDOW] = {};
uint8_t g_sample_index = 0;
uint8_t g_sample_count = 0;

static bool isRunSwitchEnabled() {
  int level = digitalRead(PIN_RUN_SWITCH);
  return g_config.switch_active_high ? (level == HIGH) : (level == LOW);
}

static void setSsrOutput(bool on) {
  int level = on ? (g_config.ssr_active_high ? HIGH : LOW)
                 : (g_config.ssr_active_high ? LOW : HIGH);
  digitalWrite(PIN_SSR, level);
}

static void pushSample(float temp_c) {
  if (g_config.smooth_window == 0) {
    return;
  }
  uint8_t window = min(g_config.smooth_window, MAX_SMOOTH_WINDOW);
  g_temp_samples[g_sample_index] = temp_c;
  g_sample_index = (g_sample_index + 1) % window;
  if (g_sample_count < window) {
    g_sample_count++;
  }
}

static float getSmoothedTemp() {
  uint8_t window = min(g_config.smooth_window, MAX_SMOOTH_WINDOW);
  if (window <= 1 || g_sample_count == 0) {
    return g_status.t_meas_c;
  }
  float sum = 0.0f;
  for (uint8_t i = 0; i < g_sample_count; ++i) {
    sum += g_temp_samples[i];
  }
  return sum / static_cast<float>(g_sample_count);
}

static void updateTemperature() {
  float temp_c = g_thermocouple.readCelsius();
  uint8_t fault = g_thermocouple.readError();

  if (!isnan(temp_c) && fault == 0) {
    g_status.t_meas_c = temp_c;
    g_status.last_fault = 0;
    pushSample(temp_c);
  } else {
    g_status.last_fault = fault == 0 ? 0xFF : fault;
    g_status.state = RunState::FAULT;
  }
}

static void computeControl() {
  if (g_status.state != RunState::RUNNING) {
    g_status.duty = 0.0f;
    return;
  }

  float t_meas = getSmoothedTemp();
  if (isnan(t_meas)) {
    g_status.state = RunState::FAULT;
    g_status.duty = 0.0f;
    return;
  }

  if (t_meas >= g_config.tmax_c) {
    g_status.state = RunState::FAULT;
    g_status.duty = 0.0f;
    return;
  }

  g_status.t_set_c = g_config.setpoint_c;
  float error = g_status.t_set_c - t_meas;
  float u = g_config.kp * error + g_config.bias;
  if (u < 0.0f) u = 0.0f;
  if (u > 1.0f) u = 1.0f;
  g_status.duty = u;
}

static void updateSsrOutput(uint32_t now_ms) {
  if (g_status.state != RunState::RUNNING) {
    setSsrOutput(false);
    return;
  }

  if (now_ms - g_window_start_ms >= g_config.window_ms) {
    g_window_start_ms = now_ms;
  }

  uint32_t on_time_ms = static_cast<uint32_t>(g_status.duty * g_config.window_ms);
  if (on_time_ms > 0 && g_config.min_on_ms > 0 && on_time_ms < g_config.min_on_ms) {
    on_time_ms = g_config.min_on_ms;
  }
  if (on_time_ms < g_config.window_ms && g_config.min_off_ms > 0) {
    uint32_t off_time_ms = g_config.window_ms - on_time_ms;
    if (off_time_ms < g_config.min_off_ms) {
      on_time_ms = g_config.window_ms - g_config.min_off_ms;
    }
  }

  uint32_t elapsed_ms = now_ms - g_window_start_ms;
  setSsrOutput(elapsed_ms < on_time_ms);
}

static void updateState() {
  g_status.run_switch_enabled = isRunSwitchEnabled();

  if (!g_status.run_switch_enabled) {
    g_status.state = RunState::SWITCH_DISABLED;
    return;
  }

  if (g_status.state == RunState::FAULT) {
    return;
  }

  if (g_status.state == RunState::SWITCH_DISABLED || g_status.state == RunState::IDLE) {
    g_status.state = g_config.auto_run ? RunState::RUNNING : RunState::IDLE;
  }
}

static void logStatus(uint32_t now_ms) {
  static uint32_t last_log_ms = 0;
  if (now_ms - last_log_ms < 1000) {
    return;
  }
  last_log_ms = now_ms;

  Serial.print("state=");
  Serial.print(static_cast<int>(g_status.state));
  Serial.print(" t=");
  Serial.print(g_status.t_meas_c, 2);
  Serial.print(" set=");
  Serial.print(g_status.t_set_c, 2);
  Serial.print(" duty=");
  Serial.print(g_status.duty, 3);
  Serial.print(" switch=");
  Serial.print(g_status.run_switch_enabled ? "EN" : "DIS");
  Serial.print(" fault=");
  Serial.println(g_status.last_fault, HEX);
}

static const char * stateName(RunState state) {
  switch (state) {
    case RunState::IDLE:
      return "IDLE";
    case RunState::RUNNING:
      return "RUNNING";
    case RunState::SWITCH_DISABLED:
      return "DISABLED";
    case RunState::FAULT:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

static void handleStatus() {
  String json = "{";
  json += "\"ok\":true,";
  json += "\"data\":{";
  json += "\"state\":\"";
  json += stateName(g_status.state);
  json += "\",";
  json += "\"t_meas\":";
  json += String(g_status.t_meas_c, 2);
  json += ",";
  json += "\"t_set\":";
  json += String(g_status.t_set_c, 2);
  json += ",";
  json += "\"duty\":";
  json += String(g_status.duty, 3);
  json += ",";
  json += "\"run_switch\":";
  json += g_status.run_switch_enabled ? "true" : "false";
  json += ",";
  json += "\"fault\":";
  json += String(g_status.last_fault);
  json += "}}";
  g_server.send(200, "application/json", json);
}

static bool tryStartRun() {
  if (!g_status.run_switch_enabled) {
    g_status.state = RunState::SWITCH_DISABLED;
    return false;
  }
  if (g_status.state == RunState::FAULT) {
    return false;
  }
  g_status.state = RunState::RUNNING;
  return true;
}

static void handleRun() {
  bool ok = tryStartRun();
  g_server.send(ok ? 200 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleStop() {
  g_status.state = g_status.run_switch_enabled ? RunState::IDLE : RunState::SWITCH_DISABLED;
  g_status.duty = 0.0f;
  setSsrOutput(false);
  if (g_status.state != RunState::FAULT) {
    g_status.last_fault = 0;
  }
  g_server.send(200, "application/json", "{\"ok\":true}");
}

static void handleNotFound() {
  g_server.send(404, "application/json", "{\"ok\":false,\"error\":\"NOT_FOUND\"}");
}

static bool setupWifi() {
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start_ms = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start_ms < 15000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    if (MDNS.begin(MDNS_HOST)) {
      MDNS.addService("http", "tcp", 80);
      Serial.print("mDNS: http://");
      Serial.print(MDNS_HOST);
      Serial.println(".local/");
    } else {
      Serial.println("mDNS start failed");
    }
    return true;
  } else {
    Serial.println("WiFi connect failed");
    // Fall through to AP mode.
  }
#else
  Serial.println("WiFi credentials missing. Starting AP mode.");
#endif

  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (ok) {
    Serial.print("AP started: ");
    Serial.println(AP_SSID);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    if (MDNS.begin(MDNS_HOST)) {
      MDNS.addService("http", "tcp", 80);
      Serial.print("mDNS: http://");
      Serial.print(MDNS_HOST);
      Serial.println(".local/");
    } else {
      Serial.println("mDNS start failed");
    }
  } else {
    Serial.println("AP start failed");
  }
  return ok;
}

static void setupServer() {
  g_server.on("/api/status", HTTP_GET, handleStatus);
  g_server.on("/api/run", HTTP_POST, handleRun);
  g_server.on("/api/stop", HTTP_POST, handleStop);
  g_server.onNotFound(handleNotFound);
  g_server.begin();
  g_server_started = true;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_SSR, OUTPUT);
  setSsrOutput(false);

  pinMode(PIN_RUN_SWITCH, INPUT_PULLUP);

  g_window_start_ms = millis();

  if (setupWifi()) {
    setupServer();
  }
}

void loop() {
  uint32_t now_ms = millis();

  updateState();

  if (now_ms - g_last_sample_ms >= TEMP_SAMPLE_MS) {
    g_last_sample_ms = now_ms;
    updateTemperature();
  }

  if (now_ms - g_last_control_ms >= CONTROL_PERIOD_MS) {
    g_last_control_ms = now_ms;
    computeControl();
  }

  updateSsrOutput(now_ms);
  logStatus(now_ms);
  if (g_server_started) {
    g_server.handleClient();
  }
}
