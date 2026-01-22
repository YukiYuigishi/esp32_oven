#include "control.h"
#include "app_config.h"
#include "profile.h"
#include <Adafruit_MAX31855.h>

namespace {
Adafruit_MAX31855 g_thermocouple(PIN_MAX31855_SCK, PIN_MAX31855_CS, PIN_MAX31855_MISO);

bool isRunSwitchEnabled() {
  int level = digitalRead(PIN_RUN_SWITCH);
  bool active_high = g_control.config.switch_active_high;
  return active_high ? (level == HIGH) : (level == LOW);
}

void setSsrOutput(bool on) {
  bool active_high = g_control.config.ssr_active_high;
  int level = on ? (active_high ? HIGH : LOW) : (active_high ? LOW : HIGH);
  digitalWrite(PIN_SSR, level);
}

void pushSample(float temp_c) {
  if (g_control.config.smooth_window == 0) {
    return;
  }
  uint8_t window = min(g_control.config.smooth_window, MAX_SMOOTH_WINDOW);
  g_control.temp_samples[g_control.sample_index] = temp_c;
  g_control.sample_index = (g_control.sample_index + 1) % window;
  if (g_control.sample_count < window) {
    g_control.sample_count++;
  }
}

float getSmoothedTemp() {
  uint8_t window = min(g_control.config.smooth_window, MAX_SMOOTH_WINDOW);
  if (window <= 1 || g_control.sample_count == 0) {
    return g_control.status.t_meas_c;
  }
  float sum = 0.0f;
  for (uint8_t i = 0; i < g_control.sample_count; ++i) {
    sum += g_control.temp_samples[i];
  }
  return sum / static_cast<float>(g_control.sample_count);
}
} // namespace

void controlInit() {
  if (!g_control_mutex) {
    g_control_mutex = xSemaphoreCreateMutex();
  }

  pinMode(PIN_SSR, OUTPUT);
  setSsrOutput(false);
  pinMode(PIN_RUN_SWITCH, INPUT_PULLUP);

  xSemaphoreTake(g_control_mutex, portMAX_DELAY);
  g_control.window_start_ms = millis();
  xSemaphoreGive(g_control_mutex);

  profileInit();
  profileSetTempLimits(-100.0f, g_control.config.tmax_c);
}

void controlUpdateTemperature() {
  float temp_c = g_thermocouple.readCelsius();
  uint8_t fault = g_thermocouple.readError();

  xSemaphoreTake(g_control_mutex, portMAX_DELAY);
  if (!isnan(temp_c) && fault == 0) {
    g_control.status.t_meas_c = temp_c;
    g_control.status.last_fault = 0;
    pushSample(temp_c);
  } else {
    g_control.status.last_fault = fault == 0 ? 0xFF : fault;
    g_control.status.state = RunState::FAULT;
  }
  xSemaphoreGive(g_control_mutex);
}

void controlUpdateState() {
  xSemaphoreTake(g_control_mutex, portMAX_DELAY);
  g_control.status.run_switch_enabled = isRunSwitchEnabled();

  if (!g_control.status.run_switch_enabled) {
    g_control.status.state = RunState::SWITCH_DISABLED;
    xSemaphoreGive(g_control_mutex);
    return;
  }

  if (g_control.status.state == RunState::FAULT) {
    xSemaphoreGive(g_control_mutex);
    return;
  }

  if (g_control.status.state == RunState::SWITCH_DISABLED ||
      g_control.status.state == RunState::IDLE) {
    g_control.status.state = RunState::IDLE;
  }

  xSemaphoreGive(g_control_mutex);
}

void controlComputeControl() {
  xSemaphoreTake(g_control_mutex, portMAX_DELAY);
  if (g_control.status.state != RunState::RUNNING) {
    g_control.status.duty = 0.0f;
    xSemaphoreGive(g_control_mutex);
    return;
  }

  float t_meas = getSmoothedTemp();
  if (isnan(t_meas)) {
    g_control.status.state = RunState::FAULT;
    g_control.status.duty = 0.0f;
    xSemaphoreGive(g_control_mutex);
    return;
  }

  if (t_meas >= g_control.config.tmax_c) {
    g_control.status.state = RunState::FAULT;
    g_control.status.duty = 0.0f;
    xSemaphoreGive(g_control_mutex);
    return;
  }

  ProfileSetpoint setpoint = profileGetSetpoint(millis());
  if (setpoint.active) {
    g_control.status.t_set_c = setpoint.setpoint_c;
    if (setpoint.completed && setpoint.end_behavior == EndBehavior::STOP) {
      g_control.status.state = RunState::IDLE;
      g_control.status.duty = 0.0f;
      xSemaphoreGive(g_control_mutex);
      return;
    }
  } else {
    g_control.status.t_set_c = g_control.config.setpoint_c;
  }
  float error = g_control.status.t_set_c - t_meas;
  float u = g_control.config.kp * error + g_control.config.bias;
  if (u < 0.0f) u = 0.0f;
  if (u > 1.0f) u = 1.0f;
  g_control.status.duty = u;
  xSemaphoreGive(g_control_mutex);
}

void controlUpdateSsrOutput(uint32_t now_ms) {
  xSemaphoreTake(g_control_mutex, portMAX_DELAY);
  if (g_control.status.state != RunState::RUNNING) {
    xSemaphoreGive(g_control_mutex);
    setSsrOutput(false);
    return;
  }

  if (now_ms - g_control.window_start_ms >= g_control.config.window_ms) {
    g_control.window_start_ms = now_ms;
  }

  uint32_t on_time_ms = static_cast<uint32_t>(g_control.status.duty * g_control.config.window_ms);
  if (on_time_ms > 0 && g_control.config.min_on_ms > 0 && on_time_ms < g_control.config.min_on_ms) {
    on_time_ms = g_control.config.min_on_ms;
  }
  if (on_time_ms < g_control.config.window_ms && g_control.config.min_off_ms > 0) {
    uint32_t off_time_ms = g_control.config.window_ms - on_time_ms;
    if (off_time_ms < g_control.config.min_off_ms) {
      on_time_ms = g_control.config.window_ms - g_control.config.min_off_ms;
    }
  }

  uint32_t elapsed_ms = now_ms - g_control.window_start_ms;
  bool ssr_on = elapsed_ms < on_time_ms;
  xSemaphoreGive(g_control_mutex);
  setSsrOutput(ssr_on);
}

void controlLogStatus(uint32_t now_ms) {
  static uint32_t last_log_ms = 0;
  if (now_ms - last_log_ms < 1000) {
    return;
  }
  last_log_ms = now_ms;

  xSemaphoreTake(g_control_mutex, portMAX_DELAY);
  Serial.print("state=");
  Serial.print(static_cast<int>(g_control.status.state));
  Serial.print(" t=");
  Serial.print(g_control.status.t_meas_c, 2);
  Serial.print(" set=");
  Serial.print(g_control.status.t_set_c, 2);
  Serial.print(" duty=");
  Serial.print(g_control.status.duty, 3);
  Serial.print(" delta=");
  Serial.print(g_control.status.t_set_c - g_control.status.t_meas_c, 2);
  Serial.print(" switch=");
  Serial.print(g_control.status.run_switch_enabled ? "EN" : "DIS");
  Serial.print(" fault=");
  Serial.println(g_control.status.last_fault, HEX);
  xSemaphoreGive(g_control_mutex);
}

bool controlTryStartRun() {
  xSemaphoreTake(g_control_mutex, portMAX_DELAY);
  if (!g_control.status.run_switch_enabled) {
    g_control.status.state = RunState::SWITCH_DISABLED;
    xSemaphoreGive(g_control_mutex);
    return false;
  }
  if (g_control.status.state == RunState::FAULT) {
    xSemaphoreGive(g_control_mutex);
    return false;
  }
  g_control.status.state = RunState::RUNNING;
  xSemaphoreGive(g_control_mutex);
  return true;
}

void controlStopRun() {
  xSemaphoreTake(g_control_mutex, portMAX_DELAY);
  g_control.status.state = g_control.status.run_switch_enabled ? RunState::IDLE
                                                               : RunState::SWITCH_DISABLED;
  g_control.status.duty = 0.0f;
  if (g_control.status.state != RunState::FAULT) {
    g_control.status.last_fault = 0;
  }
  xSemaphoreGive(g_control_mutex);
  setSsrOutput(false);
  profileClearActive();
}

void controlGetStatus(ControlStatus &out_status) {
  xSemaphoreTake(g_control_mutex, portMAX_DELAY);
  out_status = g_control.status;
  xSemaphoreGive(g_control_mutex);
}
