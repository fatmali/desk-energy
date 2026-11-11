#include <assert.h>
#include <stdio.h>
#include <string>
#include "EnergyRelayConfig.h"

int main() {
  JsonDocument document;
  document["host"] = "desk-energy.account.workers.dev";
  document["token"] = std::string(64, 'a');
  EnergyRelayConfig config;
  assert(config.parse(document.as<JsonVariantConst>()));
  for (const char *host : {"https://desk.account.workers.dev", "desk.account.workers.dev/evil",
       "desk.account.workers.dev:443", "evil.com", "workers.dev", "desk.workers.dev",
       "desk.account.workers.dev.evil.com", "desk..workers.dev", "-desk.account.workers.dev",
       "desk-.account.workers.dev", "desk.account.workers.dev\n"}) {
    document["host"] = host;
    assert(!config.parse(document.as<JsonVariantConst>()));
  }
  document["host"] = "desk-energy.account.workers.dev";
  document["token"] = std::string(64, 'A');
  assert(!config.parse(document.as<JsonVariantConst>()));
  document["token"] = std::string(63, 'a');
  assert(!config.parse(document.as<JsonVariantConst>()));
  document["token"] = std::string(64, 'a');
  document["extra"] = true;
  assert(!config.parse(document.as<JsonVariantConst>()));
  puts("PASS: relay hostname, token, length and unknown-field validation");
}
