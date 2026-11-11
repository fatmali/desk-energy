#include "EnergyRelay.h"
#include "EnergyStorage.h"
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <time.h>

namespace {
class BoundedBody : public Stream {
public:
  BoundedBody(char *buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {}
  size_t write(uint8_t value) override {
    if (size >= capacity_) return 0;
    buffer_[size++] = char(value);
    return 1;
  }
  size_t write(const uint8_t *bytes, size_t count) override {
    if (count > capacity_ - size) return 0;
    memcpy(buffer_ + size, bytes, count);
    size += count;
    return count;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t size = 0;
private:
  char *buffer_;
  size_t capacity_;
};
}

void EnergyRelay::begin(Preferences &preferences, EnergyState &energy) {
  preferences_ = &preferences;
  energy_ = &energy;
  configurations_ = xQueueCreate(1, sizeof(Configuration));
  results_ = xQueueCreate(1, sizeof(Result));
  if (!configurations_ || !results_ ||
      xTaskCreate([](void *self) { static_cast<EnergyRelay *>(self)->run(); },
                  "energy-relay", 8192, this, 1, nullptr) != pdPASS) {
    if (configurations_) vQueueDelete(configurations_);
    if (results_) vQueueDelete(results_);
    configurations_ = results_ = nullptr;
    Serial.println("ERROR: Could not start background energy relay.");
    return;
  }
  ready_ = true;
  if (!preferences.isKey("relay")) return;
  char body[384] = {};
  const size_t size = preferences.getString("relay", body, sizeof(body));
  JsonDocument document;
  if (size < 2 || deserializeJson(document, body, size - 1, DeserializationOption::NestingLimit(1)) ||
      !configuration_.settings.parse(document.as<JsonVariantConst>())) {
    Serial.println("ERROR: Saved relay settings are invalid. Provision them again over USB.");
    return;
  }
  ++configuration_.generation;
  xQueueOverwrite(configurations_, &configuration_);
  Serial.println("Energy relay enabled (certificate-validated HTTPS).");
}

bool EnergyRelay::configure(JsonVariantConst settings) {
  if (!ready_) { Serial.println("ERROR: Energy relay is not ready."); return false; }
  Configuration next;
  if (!next.settings.parse(settings)) {
    Serial.println("ERROR: Relay requires a workers.dev hostname and a 64-character read token.");
    return false;
  }
  char body[384] = {};
  const size_t size = serializeJson(settings, body, sizeof(body));
  if (size >= sizeof(body) || preferences_->putString("relay", body) != size) {
    Serial.println("ERROR: Relay settings could not be saved.");
    return false;
  }
  next.generation = configuration_.generation + 1;
  configuration_ = next;
  xQueueOverwrite(configurations_, &configuration_);
  Serial.println("Energy relay settings saved; waiting for Wi-Fi and a synchronized clock.");
  return true;
}

bool EnergyRelay::disable() {
  if (!preferences_) return true;
  if (preferences_->isKey("relay") && !preferences_->remove("relay")) {
    Serial.println("ERROR: Relay settings could not be removed.");
    return false;
  }
  const uint32_t next = configuration_.generation + 1;
  configuration_ = Configuration{};
  configuration_.generation = next;
  if (ready_) xQueueOverwrite(configurations_, &configuration_);
  return true;
}

bool EnergyRelay::tick() {
  if (!ready_) return false;
  Result result;
  if (xQueueReceive(results_, &result, 0) != pdTRUE || !enabled() ||
      result.generation != configuration_.generation) return false;
  if (result.status == 204) return false;
  if (result.status != 200) {
    Serial.printf("ERROR: Energy relay request failed (%d); retaining last reading and retrying.\n", result.status);
    return false;
  }
  const EnergyReading before = energy_->reading();
  const EnergyAcceptError error = EnergyStorage::accept(*preferences_, *energy_,
      result.body, result.size, time(nullptr), millis());
  if (error != EnergyAcceptError::None) {
    Serial.printf("ERROR: Energy relay update rejected: %s\n", energyErrorText(error));
    return false;
  }
  if (before == energy_->reading()) return false;
  Serial.println("Energy relay reading validated and saved.");
  return true;
}

void EnergyRelay::run() {
  Configuration active;
  uint32_t nextPoll = 0, retry = 60000;
  bool wasOnline = false;
  for (;;) {
    Configuration incoming;
    if (xQueueReceive(configurations_, &incoming, pdMS_TO_TICKS(1000)) == pdTRUE) {
      active = incoming;
      nextPoll = millis();
      retry = 60000;
    }
    const bool online = WiFi.status() == WL_CONNECTED;
    if (online && !wasOnline) nextPoll = millis();
    wasOnline = online;
    if (!active.settings.host[0] || !online || time(nullptr) < 1704067200 ||
        int32_t(millis() - nextPoll) < 0) continue;
    Result result;
    result.generation = active.generation;
    poll(active, result);
    xQueueOverwrite(results_, &result);
    if (result.status == 200 || result.status == 204) retry = 60000;
    else retry = retry < 150000 ? retry * 2 : 300000;
    nextPoll = millis() + retry;
  }
}

void EnergyRelay::poll(const Configuration &configuration, Result &result) {
  NetworkClientSecure client;
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(8);
  HTTPClient request;
  request.setConnectTimeout(5000);
  request.setTimeout(5000);
  request.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const String url = String("https://") + configuration.settings.host + "/v1/energy";
  if (!request.begin(client, url)) { result.status = -1000; return; }
  request.addHeader("Authorization", String("Bearer ") + configuration.settings.token);
  request.addHeader("Accept", "application/json");
  request.addHeader("Accept-Encoding", "identity");
  const char *headers[] = {"Content-Type"};
  request.collectHeaders(headers, 1);
  result.status = request.GET();
  if (result.status == 200) {
    BoundedBody body(result.body, EnergyState::MaxBody);
    if (request.getSize() > int(EnergyState::MaxBody) ||
        !request.header("Content-Type").startsWith("application/json") ||
        request.writeToStream(&body) <= 0) {
      result.status = -1001;
    } else result.size = body.size;
  }
  request.end();
}
