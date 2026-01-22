#include "profile.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
constexpr uint8_t kMaxProfiles = 8;
Profile g_profiles[kMaxProfiles];
uint8_t g_profile_count = 0;

String g_active_name;
uint32_t g_active_start_ms = 0;

float g_temp_min_c = -100.0f;
float g_temp_max_c = 500.0f;

SemaphoreHandle_t g_profile_mutex = nullptr;

int findProfileIndex(const String &name) {
  for (uint8_t i = 0; i < g_profile_count; ++i) {
    if (g_profiles[i].name == name) {
      return i;
    }
  }
  return -1;
}

bool parseEndBehavior(const String &value, EndBehavior &out) {
  if (value == "hold_last") {
    out = EndBehavior::HOLD_LAST;
    return true;
  }
  if (value == "stop") {
    out = EndBehavior::STOP;
    return true;
  }
  return false;
}

String endBehaviorToString(EndBehavior value) {
  return value == EndBehavior::STOP ? "stop" : "hold_last";
}

bool validateProfile(const Profile &profile, String &error) {
  if (profile.name.isEmpty()) {
    error = "name_required";
    return false;
  }
  if (profile.count < 2) {
    error = "points_min";
    return false;
  }
  for (uint8_t i = 0; i < profile.count; ++i) {
    if (profile.points[i].temp_c < g_temp_min_c || profile.points[i].temp_c > g_temp_max_c) {
      error = "temp_out_of_range";
      return false;
    }
    if (i > 0 && profile.points[i].t_sec <= profile.points[i - 1].t_sec) {
      error = "points_not_monotonic";
      return false;
    }
  }
  return true;
}

float interpolate(const ProfilePoint &a, const ProfilePoint &b, uint32_t t_sec) {
  if (b.t_sec == a.t_sec) {
    return b.temp_c;
  }
  float ratio = (static_cast<float>(t_sec) - static_cast<float>(a.t_sec)) /
                (static_cast<float>(b.t_sec - a.t_sec));
  return a.temp_c + (b.temp_c - a.temp_c) * ratio;
}
} // namespace

void profileInit() {
  if (!g_profile_mutex) {
    g_profile_mutex = xSemaphoreCreateMutex();
  }
}

void profileSetTempLimits(float min_c, float max_c) {
  g_temp_min_c = min_c;
  g_temp_max_c = max_c;
}

bool profileAddOrUpdate(const Profile &profile, String &error) {
  if (!validateProfile(profile, error)) {
    return false;
  }

  xSemaphoreTake(g_profile_mutex, portMAX_DELAY);
  int index = findProfileIndex(profile.name);
  if (index < 0) {
    if (g_profile_count >= kMaxProfiles) {
      xSemaphoreGive(g_profile_mutex);
      error = "profiles_full";
      return false;
    }
    index = g_profile_count++;
  }
  g_profiles[index] = profile;
  xSemaphoreGive(g_profile_mutex);
  return true;
}

bool profileDelete(const String &name) {
  xSemaphoreTake(g_profile_mutex, portMAX_DELAY);
  int index = findProfileIndex(name);
  if (index < 0) {
    xSemaphoreGive(g_profile_mutex);
    return false;
  }
  for (uint8_t i = index + 1; i < g_profile_count; ++i) {
    g_profiles[i - 1] = g_profiles[i];
  }
  g_profile_count--;
  if (g_active_name == name) {
    g_active_name = "";
  }
  xSemaphoreGive(g_profile_mutex);
  return true;
}

bool profileGet(const String &name, Profile &out_profile) {
  xSemaphoreTake(g_profile_mutex, portMAX_DELAY);
  int index = findProfileIndex(name);
  if (index < 0) {
    xSemaphoreGive(g_profile_mutex);
    return false;
  }
  out_profile = g_profiles[index];
  xSemaphoreGive(g_profile_mutex);
  return true;
}

void profileList(String &json_out) {
  JsonDocument doc;
  JsonArray items = doc["profiles"].to<JsonArray>();

  xSemaphoreTake(g_profile_mutex, portMAX_DELAY);
  for (uint8_t i = 0; i < g_profile_count; ++i) {
    JsonObject item = items.add<JsonObject>();
    item["name"] = g_profiles[i].name;
    item["points"] = g_profiles[i].count;
    item["end_behavior"] = endBehaviorToString(g_profiles[i].end_behavior);
  }
  xSemaphoreGive(g_profile_mutex);

  String payload;
  serializeJson(doc, payload);
  json_out = payload;
}

bool profileStartRun(const String &name) {
  xSemaphoreTake(g_profile_mutex, portMAX_DELAY);
  int index = findProfileIndex(name);
  if (index < 0) {
    xSemaphoreGive(g_profile_mutex);
    return false;
  }
  g_active_name = name;
  g_active_start_ms = millis();
  xSemaphoreGive(g_profile_mutex);
  return true;
}

void profileClearActive() {
  xSemaphoreTake(g_profile_mutex, portMAX_DELAY);
  g_active_name = "";
  xSemaphoreGive(g_profile_mutex);
}

ProfileSetpoint profileGetSetpoint(uint32_t now_ms) {
  ProfileSetpoint out;

  xSemaphoreTake(g_profile_mutex, portMAX_DELAY);
  if (g_active_name.isEmpty()) {
    xSemaphoreGive(g_profile_mutex);
    return out;
  }

  int index = findProfileIndex(g_active_name);
  if (index < 0) {
    g_active_name = "";
    xSemaphoreGive(g_profile_mutex);
    return out;
  }

  const Profile &profile = g_profiles[index];
  out.active = true;
  out.end_behavior = profile.end_behavior;

  uint32_t elapsed_sec = (now_ms - g_active_start_ms) / 1000;
  if (elapsed_sec <= profile.points[0].t_sec) {
    out.setpoint_c = profile.points[0].temp_c;
    xSemaphoreGive(g_profile_mutex);
    return out;
  }

  for (uint8_t i = 1; i < profile.count; ++i) {
    const ProfilePoint &prev = profile.points[i - 1];
    const ProfilePoint &next = profile.points[i];
    if (elapsed_sec <= next.t_sec) {
      out.setpoint_c = interpolate(prev, next, elapsed_sec);
      xSemaphoreGive(g_profile_mutex);
      return out;
    }
  }

  out.completed = true;
  out.setpoint_c = profile.points[profile.count - 1].temp_c;
  xSemaphoreGive(g_profile_mutex);
  return out;
}

String profileGetActiveName() {
  xSemaphoreTake(g_profile_mutex, portMAX_DELAY);
  String name = g_active_name;
  xSemaphoreGive(g_profile_mutex);
  return name;
}
