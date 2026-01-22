#pragma once

#include <Arduino.h>

struct ProfilePoint {
  uint32_t t_sec = 0;
  float temp_c = 0.0f;
};

enum class EndBehavior {
  HOLD_LAST,
  STOP
};

struct Profile {
  String name;
  EndBehavior end_behavior = EndBehavior::HOLD_LAST;
  uint8_t count = 0;
  ProfilePoint points[32];
};

struct ProfileSetpoint {
  bool active = false;
  bool completed = false;
  EndBehavior end_behavior = EndBehavior::HOLD_LAST;
  float setpoint_c = NAN;
};

void profileInit();
void profileSetTempLimits(float min_c, float max_c);

bool profileAddOrUpdate(const Profile &profile, String &error);
bool profileDelete(const String &name);
bool profileGet(const String &name, Profile &out_profile);
void profileList(String &json_out);

bool profileStartRun(const String &name);
void profileClearActive();
ProfileSetpoint profileGetSetpoint(uint32_t now_ms);
String profileGetActiveName();
