#pragma once

#include <ArduinoJson.h>
#include <string.h>

struct EnergyRelayConfig {
  char host[128] = {};
  char token[65] = {};

  bool parse(JsonVariantConst input) {
    const JsonObjectConst object = input.as<JsonObjectConst>();
    if (object.isNull() || object.size() != 2 || !object["host"].is<const char *>() ||
        !object["token"].is<const char *>()) return false;
    const char *name = object["host"], *secret = object["token"];
    const size_t length = strlen(name);
    const char suffix[] = ".workers.dev";
    if (length <= sizeof(suffix) || length >= sizeof(host) || strlen(secret) != 64 ||
        strcmp(name + length - (sizeof(suffix) - 1), suffix) != 0) return false;
    unsigned labels = 0, labelLength = 0;
    for (size_t i = 0; i < length; ++i) {
      const char c = name[i];
      if (c == '.') {
        if (!labelLength || name[i - 1] == '-') return false;
        labelLength = 0;
        ++labels;
      } else {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') ||
            (!labelLength && c == '-') || ++labelLength > 63) return false;
      }
    }
    if (labels != 3 || !labelLength) return false;
    for (size_t i = 0; i < 64; ++i) {
      if (!((secret[i] >= '0' && secret[i] <= '9') || (secret[i] >= 'a' && secret[i] <= 'f'))) return false;
    }
    strcpy(host, name);
    strcpy(token, secret);
    return true;
  }
};
