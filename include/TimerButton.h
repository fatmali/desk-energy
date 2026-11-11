#pragma once

#include <stdint.h>

class TimerButton {
public:
  enum class Action { None, Toggle, Reset };
  static constexpr uint32_t DebounceMs = 30;
  static constexpr uint32_t HoldMs = 1500;

  Action update(bool pressed, uint32_t now) {
    if (pressed != rawPressed_) {
      rawPressed_ = pressed;
      changedAt_ = now;
    }
    if (uint32_t(now - changedAt_) < DebounceMs) {
      return Action::None;
    }
    if (pressed != stablePressed_) {
      stablePressed_ = pressed;
      if (pressed) {
        pressedAt_ = now;
        resetSent_ = false;
      } else {
        if (!armed_) {
          armed_ = true;
          return Action::None;
        }
        if (!resetSent_) {
          return Action::Toggle;
        }
      }
    }
    if (armed_ && stablePressed_ && !resetSent_ &&
        uint32_t(now - pressedAt_) >= HoldMs) {
      resetSent_ = true;
      return Action::Reset;
    }
    return Action::None;
  }

private:
  // Ignore a button held during startup until it has been released.
  bool rawPressed_ = true;
  bool stablePressed_ = true;
  bool armed_ = false;
  bool resetSent_ = false;
  uint32_t changedAt_ = 0;
  uint32_t pressedAt_ = 0;
};
