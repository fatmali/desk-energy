#pragma once

#include <stdint.h>

class PomodoroTimer {
public:
  enum class Phase { Focus, ShortBreak, LongBreak };
  enum class State { Ready, Running, Paused };

  static constexpr uint32_t FocusDurationMs = 25UL * 60 * 1000;
  static constexpr uint32_t ShortBreakDurationMs = 5UL * 60 * 1000;
  static constexpr uint32_t LongBreakDurationMs = 15UL * 60 * 1000;

  explicit PomodoroTimer(void (*onFocusCompleted)(uint32_t) = nullptr)
      : onFocusCompleted_(onFocusCompleted) {}

  Phase phase() const { return phase_; }
  State state() const { return state_; }
  uint32_t remainingMs() const { return remainingMs_; }
  uint32_t remainingSeconds() const { return (remainingMs_ + 999) / 1000; }
  uint32_t completedSessions() const { return completedSessions_; }

  uint32_t durationMs() const {
    if (phase_ == Phase::Focus) {
      return FocusDurationMs;
    }
    return phase_ == Phase::ShortBreak ? ShortBreakDurationMs
                                      : LongBreakDurationMs;
  }

  void start(uint32_t now) {
    if (state_ != State::Running) {
      lastUpdateMs_ = now;
      state_ = State::Running;
    }
  }

  bool update(uint32_t now) {
    if (state_ != State::Running) {
      return false;
    }

    // Unsigned subtraction keeps elapsed time correct across millis() rollover.
    uint32_t elapsed = now - lastUpdateMs_;
    lastUpdateMs_ = now;
    bool changedPhase = false;
    while (elapsed >= remainingMs_) {
      elapsed -= remainingMs_;
      if (phase_ == Phase::Focus && onFocusCompleted_) {
        onFocusCompleted_(now - elapsed);
      }
      advance(true);
      changedPhase = true;
    }
    remainingMs_ -= elapsed;
    return changedPhase;
  }

  void pause(uint32_t now) {
    update(now);
    if (state_ == State::Running) {
      state_ = State::Paused;
    }
  }

  void reset() { *this = PomodoroTimer{onFocusCompleted_}; }

  void skip(uint32_t now) {
    update(now);
    advance(false);
    lastUpdateMs_ = now;
  }

private:
  void advance(bool completed) {
    if (phase_ == Phase::Focus) {
      if (completed) {
        ++completedSessions_;
        ++sessionsSinceLongBreak_;
      }
      if (sessionsSinceLongBreak_ == 4) {
        phase_ = Phase::LongBreak;
        sessionsSinceLongBreak_ = 0;
      } else {
        phase_ = Phase::ShortBreak;
      }
    } else {
      phase_ = Phase::Focus;
    }
    remainingMs_ = durationMs();
  }

  Phase phase_ = Phase::Focus;
  State state_ = State::Ready;
  uint32_t remainingMs_ = FocusDurationMs;
  uint32_t lastUpdateMs_ = 0;
  uint32_t completedSessions_ = 0;
  uint8_t sessionsSinceLongBreak_ = 0;
  void (*onFocusCompleted_)(uint32_t) = nullptr;
};
