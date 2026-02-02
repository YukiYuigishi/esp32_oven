#include "storage.h"
#include "app_state.h"
#include "control.h"
#include "profile.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {
const char kConfigPath[] = "/config.json";
const char kProfilesPath[] = "/profiles.json";

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
} // namespace

bool storageInit() {
  if (LittleFS.begin(false)) {
    return true;
  }
  Serial.println("LittleFS mount failed, attempting format");
  if (LittleFS.begin(true)) {
    Serial.println("LittleFS formatted");
    return true;
  }
  Serial.println("LittleFS mount failed");
  return false;
}

bool storageLoadConfig() {
  if (!LittleFS.exists(kConfigPath)) {
    return false;
  }
  File file = LittleFS.open(kConfigPath, "r");
  if (!file) {
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Serial.println("config.json parse failed");
    return false;
  }

  ControlConfig cfg;
  controlGetConfig(cfg);
  if (doc["kp"].is<float>()) cfg.kp = doc["kp"].as<float>();
  if (doc["bias"].is<float>()) cfg.bias = doc["bias"].as<float>();
  if (doc["setpoint_c"].is<float>()) cfg.setpoint_c = doc["setpoint_c"].as<float>();
  if (doc["tmax_c"].is<float>()) cfg.tmax_c = doc["tmax_c"].as<float>();
  if (doc["ssr_active_high"].is<bool>()) cfg.ssr_active_high = doc["ssr_active_high"].as<bool>();
  if (doc["switch_active_high"].is<bool>()) cfg.switch_active_high = doc["switch_active_high"].as<bool>();
  if (doc["window_ms"].is<uint32_t>()) cfg.window_ms = doc["window_ms"].as<uint32_t>();
  if (doc["min_on_ms"].is<uint32_t>()) cfg.min_on_ms = doc["min_on_ms"].as<uint32_t>();
  if (doc["min_off_ms"].is<uint32_t>()) cfg.min_off_ms = doc["min_off_ms"].as<uint32_t>();
  if (doc["smooth_window"].is<uint32_t>()) {
    cfg.smooth_window = static_cast<uint8_t>(doc["smooth_window"].as<uint32_t>());
  }

  controlSetConfig(cfg);
  profileSetTempLimits(-100.0f, cfg.tmax_c);
  return true;
}

bool storageLoadProfiles() {
  if (!LittleFS.exists(kProfilesPath)) {
    return false;
  }
  File file = LittleFS.open(kProfilesPath, "r");
  if (!file) {
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Serial.println("profiles.json parse failed");
    return false;
  }

  JsonArray profiles = doc["profiles"].as<JsonArray>();
  if (profiles.isNull()) {
    return false;
  }

  profileClearAll();
  for (JsonObject item : profiles) {
    Profile profile{};
    profile.name = item["name"].as<String>();
    String end_behavior = item["end_behavior"] | "hold_last";
    parseEndBehavior(end_behavior, profile.end_behavior);

    JsonArray points = item["points"].as<JsonArray>();
    if (points.isNull()) {
      continue;
    }
    uint8_t count = 0;
    for (JsonObject point : points) {
      if (count >= 32) {
        break;
      }
      profile.points[count].t_sec = point["t_sec"] | 0;
      profile.points[count].temp_c = point["temp_c"] | 0.0f;
      count++;
    }
    profile.count = count;

    String error;
    profileAddOrUpdate(profile, error);
  }

  return true;
}

bool storageSaveConfig() {
  ControlConfig cfg;
  controlGetConfig(cfg);

  JsonDocument doc;
  doc["kp"] = cfg.kp;
  doc["bias"] = cfg.bias;
  doc["setpoint_c"] = cfg.setpoint_c;
  doc["tmax_c"] = cfg.tmax_c;
  doc["ssr_active_high"] = cfg.ssr_active_high;
  doc["switch_active_high"] = cfg.switch_active_high;
  doc["window_ms"] = cfg.window_ms;
  doc["min_on_ms"] = cfg.min_on_ms;
  doc["min_off_ms"] = cfg.min_off_ms;
  doc["smooth_window"] = cfg.smooth_window;

  File file = LittleFS.open(kConfigPath, "w");
  if (!file) {
    return false;
  }
  bool ok = serializeJson(doc, file) > 0;
  file.close();
  return ok;
}

bool storageSaveProfiles() {
  JsonDocument doc;
  JsonArray profiles = doc["profiles"].to<JsonArray>();

  Profile profile{};
  for (uint8_t i = 0; profileGetByIndex(i, profile); ++i) {
    JsonObject item = profiles.add<JsonObject>();
    item["name"] = profile.name;
    item["end_behavior"] = endBehaviorToString(profile.end_behavior);
    JsonArray points = item["points"].to<JsonArray>();
    for (uint8_t p = 0; p < profile.count; ++p) {
      JsonObject point = points.add<JsonObject>();
      point["t_sec"] = profile.points[p].t_sec;
      point["temp_c"] = profile.points[p].temp_c;
    }
  }

  File file = LittleFS.open(kProfilesPath, "w");
  if (!file) {
    return false;
  }
  bool ok = serializeJson(doc, file) > 0;
  file.close();
  return ok;
}
