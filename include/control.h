#pragma once

#include <Arduino.h>
#include "app_state.h"

void controlInit();
void controlUpdateTemperature();
void controlUpdateState();
void controlComputeControl();
void controlUpdateSsrOutput(uint32_t now_ms);
void controlLogStatus(uint32_t now_ms);

bool controlTryStartRun();
void controlStopRun();
void controlGetStatus(ControlStatus &out_status);
