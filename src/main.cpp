#include <Arduino.h>
#include "app_config.h"
#include "control.h"
#include "web_api.h"

namespace {
void sensorTask(void *param) {
  (void)param;
  TickType_t last_wake = xTaskGetTickCount();
  for (;;) {
    controlUpdateTemperature();
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TEMP_SAMPLE_MS));
  }
}

void controlTask(void *param) {
  (void)param;
  TickType_t last_wake = xTaskGetTickCount();
  for (;;) {
    controlUpdateState();
    controlComputeControl();
    uint32_t now_ms = millis();
    controlUpdateSsrOutput(now_ms);
    controlLogStatus(now_ms);
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
  }
}

void webTask(void *param) {
  (void)param;
  for (;;) {
    webHandleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
} // namespace

void setup() {
  Serial.begin(115200);

  controlInit();
  webSetup();

  xTaskCreatePinnedToCore(sensorTask, "sensor", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(controlTask, "control", 4096, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(webTask, "web", 4096, nullptr, 1, nullptr, 0);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
