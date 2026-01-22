#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "app_config.h"

enum class RunState {
  IDLE,
  RUNNING,
  SWITCH_DISABLED,
  FAULT
};

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
};

struct ControlStatus {
  float t_meas_c = NAN;
  float t_set_c = NAN;
  float duty = 0.0f;
  uint8_t last_fault = 0;
  RunState state = RunState::IDLE;
  bool run_switch_enabled = false;
};

struct ControlData {
  ControlConfig config;
  ControlStatus status;
  uint32_t window_start_ms = 0;
  float temp_samples[MAX_SMOOTH_WINDOW] = {};
  uint8_t sample_index = 0;
  uint8_t sample_count = 0;
};

extern ControlData g_control;
extern SemaphoreHandle_t g_control_mutex;
