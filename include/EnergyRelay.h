#pragma once

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "EnergyRelayConfig.h"
#include "EnergyState.h"

class EnergyRelay {
public:
  void begin(Preferences &preferences, EnergyState &energy);
  bool configure(JsonVariantConst settings);
  bool disable();
  bool tick();
  bool enabled() const { return configuration_.settings.host[0] != '\0'; }

private:
  struct Configuration {
    EnergyRelayConfig settings;
    uint32_t generation = 0;
  };
  struct Result {
    uint32_t generation = 0;
    int status = 0;
    size_t size = 0;
    char body[EnergyState::MaxBody + 1] = {};
  };
  void run();
  void poll(const Configuration &configuration, Result &result);
  Preferences *preferences_ = nullptr;
  EnergyState *energy_ = nullptr;
  QueueHandle_t configurations_ = nullptr, results_ = nullptr;
  Configuration configuration_;
  bool ready_ = false;
};
