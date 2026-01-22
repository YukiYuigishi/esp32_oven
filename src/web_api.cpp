#include "web_api.h"
#include "app_config.h"
#include "control.h"
#include "profile.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace {
WebServer g_server(80);
bool g_server_started = false;

const char *stateName(RunState state) {
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

void handleStatus() {
  ControlStatus status{};
  controlGetStatus(status);

  String json = "{";
  json += "\"ok\":true,";
  json += "\"data\":{";
  json += "\"state\":\"";
  json += stateName(status.state);
  json += "\",";
  json += "\"t_meas\":";
  json += String(status.t_meas_c, 2);
  json += ",";
  json += "\"t_set\":";
  json += String(status.t_set_c, 2);
  json += ",";
  json += "\"duty\":";
  json += String(status.duty, 3);
  json += ",";
  json += "\"delta\":";
  json += String(status.t_set_c - status.t_meas_c, 2);
  json += ",";
  json += "\"run_switch\":";
  json += status.run_switch_enabled ? "true" : "false";
  json += ",";
  json += "\"active_profile\":";
  json += "\"";
  json += profileGetActiveName();
  json += "\"";
  json += ",";
  json += "\"fault\":";
  json += String(status.last_fault);
  json += "}}";
  g_server.send(200, "application/json", json);
}

void handleRun() {
  if (g_server.hasArg("plain")) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, g_server.arg("plain"));
    if (err) {
      g_server.send(400, "application/json", "{\"ok\":false,\"error\":\"BAD_JSON\"}");
      return;
    }
    if (doc["profile_id"]) {
      String name = doc["profile_id"].as<String>();
      if (!profileStartRun(name)) {
        g_server.send(404, "application/json", "{\"ok\":false,\"error\":\"PROFILE_NOT_FOUND\"}");
        return;
      }
    }
  }
  bool ok = controlTryStartRun();
  g_server.send(ok ? 200 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void handleStop() {
  controlStopRun();
  g_server.send(200, "application/json", "{\"ok\":true}");
}

void handleNotFound() {
  String uri = g_server.uri();
  if (uri.startsWith("/api/profiles/")) {
    String name = uri.substring(strlen("/api/profiles/"));
    if (name.isEmpty()) {
      g_server.send(400, "application/json", "{\"ok\":false,\"error\":\"PROFILE_ID_REQUIRED\"}");
      return;
    }
    if (g_server.method() == HTTP_GET) {
      Profile profile{};
      if (!profileGet(name, profile)) {
        g_server.send(404, "application/json", "{\"ok\":false,\"error\":\"PROFILE_NOT_FOUND\"}");
        return;
      }
      JsonDocument doc;
      doc["name"] = profile.name;
      doc["end_behavior"] = profile.end_behavior == EndBehavior::STOP ? "stop" : "hold_last";
      JsonArray points = doc["points"].to<JsonArray>();
      for (uint8_t i = 0; i < profile.count; ++i) {
        JsonObject point = points.add<JsonObject>();
        point["t_sec"] = profile.points[i].t_sec;
        point["temp_c"] = profile.points[i].temp_c;
      }
      String payload;
      serializeJson(doc, payload);
      g_server.send(200, "application/json", payload);
      return;
    }
    if (g_server.method() == HTTP_DELETE) {
      if (!profileDelete(name)) {
        g_server.send(404, "application/json", "{\"ok\":false,\"error\":\"PROFILE_NOT_FOUND\"}");
        return;
      }
      g_server.send(200, "application/json", "{\"ok\":true}");
      return;
    }
  }
  g_server.send(404, "application/json", "{\"ok\":false,\"error\":\"NOT_FOUND\"}");
}

void handleProfilesList() {
  String payload;
  profileList(payload);
  g_server.send(200, "application/json", payload);
}

void handleProfilesUpsert() {
  if (!g_server.hasArg("plain")) {
    g_server.send(400, "application/json", "{\"ok\":false,\"error\":\"BODY_REQUIRED\"}");
    return;
  }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, g_server.arg("plain"));
  if (err) {
    g_server.send(400, "application/json", "{\"ok\":false,\"error\":\"BAD_JSON\"}");
    return;
  }
  Profile profile{};
  profile.name = doc["name"].as<String>();
  String end_behavior = doc["end_behavior"] | "hold_last";
  if (end_behavior == "stop") {
    profile.end_behavior = EndBehavior::STOP;
  }
  JsonArray points = doc["points"].as<JsonArray>();
  if (points.isNull()) {
    g_server.send(400, "application/json", "{\"ok\":false,\"error\":\"POINTS_REQUIRED\"}");
    return;
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
  if (!profileAddOrUpdate(profile, error)) {
    String payload = "{\"ok\":false,\"error\":\"";
    payload += error;
    payload += "\"}";
    g_server.send(400, "application/json", payload);
    return;
  }
  g_server.send(200, "application/json", "{\"ok\":true}");
}

bool setupMdns() {
  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("mDNS: http://");
    Serial.print(MDNS_HOST);
    Serial.println(".local/");
    return true;
  }
  Serial.println("mDNS start failed");
  return false;
}

bool setupWifi() {
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
    setupMdns();
    return true;
  }
  Serial.println("WiFi connect failed");
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
    setupMdns();
  } else {
    Serial.println("AP start failed");
  }
  return ok;
}

void setupServer() {
  g_server.on("/api/status", HTTP_GET, handleStatus);
  g_server.on("/api/profiles", HTTP_GET, handleProfilesList);
  g_server.on("/api/profiles", HTTP_POST, handleProfilesUpsert);
  g_server.on("/api/run", HTTP_POST, handleRun);
  g_server.on("/api/stop", HTTP_POST, handleStop);
  g_server.onNotFound(handleNotFound);
  g_server.begin();
  g_server_started = true;
}
} // namespace

bool webSetup() {
  if (!setupWifi()) {
    return false;
  }
  setupServer();
  return true;
}

void webHandleClient() {
  if (g_server_started) {
    g_server.handleClient();
  }
}
