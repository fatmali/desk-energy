#pragma once

#include <stdint.h>
#include <string>

inline bool validDisplayText(const std::string &text, size_t limit) {
  if (text.size() > limit) {
    return false;
  }
  for (unsigned char c : text) {
    if (c < 32 || c > 126) {
      return false;
    }
  }
  return true;
}

class JobStatus {
public:
  std::string id;
  std::string label;
  std::string kind;
  std::string state = "none";
  uint32_t updatedAt = 0;

  bool report(const std::string &newId, const std::string &newLabel,
              const std::string &newKind, const std::string &newState,
              uint32_t now) {
    if (newId.size() != 32 || newLabel.empty() ||
        !validDisplayText(newLabel, 48) ||
        (newKind != "build" && newKind != "test") ||
        (newState != "running" && newState != "heartbeat" && newState != "passed" &&
         newState != "failed" && newState != "cancelled")) {
      return false;
    }
    for (char c : newId) {
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
        return false;
      }
    }
    // Older runs must not overwrite the result of a more recent run.
    if (newState != "running" && newId != id) {
      return false;
    }
    if (newId == id && state != "running" && state != "unknown") {
      return false;
    }
    id = newId;
    label = newLabel;
    kind = newKind;
    state = newState == "heartbeat" ? "running" : newState;
    updatedAt = now;
    return true;
  }

  bool expire(uint32_t now) {
    if (state == "running" && uint32_t(now - updatedAt) >= 60000) {
      state = "unknown";
      return true;
    }
    return false;
  }
};
