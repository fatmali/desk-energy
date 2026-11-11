#include <assert.h>
#include <stdio.h>

#include "PomodoroTimer.h"

using Phase = PomodoroTimer::Phase;
using State = PomodoroTimer::State;

void testReadyAndPauseResume() {
  PomodoroTimer timer;
  assert(timer.phase() == Phase::Focus);
  assert(timer.state() == State::Ready);
  assert(timer.remainingSeconds() == 25 * 60);
  assert(timer.completedSessions() == 0);
  assert(!timer.update(100000));
  timer.pause(100000);
  assert(timer.state() == State::Ready);

  timer.start(100);
  assert(!timer.update(1099));
  assert(timer.remainingMs() == PomodoroTimer::FocusDurationMs - 999);
  assert(timer.remainingSeconds() == 1500);
  assert(!timer.update(1100));
  assert(timer.remainingSeconds() == 1499);
  timer.start(2000);
  timer.pause(2600);
  assert(timer.state() == State::Paused);
  const uint32_t pausedRemaining = PomodoroTimer::FocusDurationMs - 2500;
  assert(timer.remainingMs() == pausedRemaining);
  assert(!timer.update(500000));
  assert(timer.remainingMs() == pausedRemaining);
  timer.start(500000);
  assert(!timer.update(500001));
  assert(timer.remainingMs() == pausedRemaining - 1);
}

void testExactBoundary() {
  PomodoroTimer timer;
  timer.start(0);
  assert(!timer.update(PomodoroTimer::FocusDurationMs - 1));
  assert(timer.remainingMs() == 1);
  assert(timer.remainingSeconds() == 1);
  assert(timer.update(PomodoroTimer::FocusDurationMs));
  assert(timer.phase() == Phase::ShortBreak);
  assert(timer.state() == State::Running);
  assert(timer.remainingMs() == PomodoroTimer::ShortBreakDurationMs);
  assert(timer.completedSessions() == 1);

  timer.reset();
  timer.start(0);
  timer.pause(PomodoroTimer::FocusDurationMs);
  assert(timer.phase() == Phase::ShortBreak);
  assert(timer.state() == State::Paused);
  assert(timer.remainingMs() == PomodoroTimer::ShortBreakDurationMs);
}

void testLongBreakCycleAndSkippedFocus() {
  PomodoroTimer timer;
  uint32_t now = 1234;
  timer.start(now);
  for (uint32_t session = 1; session <= 5; ++session) {
    if (session == 4) {
      timer.skip(now);
      assert(timer.phase() == Phase::ShortBreak);
      assert(timer.completedSessions() == 3);
      timer.skip(now);
      assert(timer.phase() == Phase::Focus);
    }
    now += PomodoroTimer::FocusDurationMs;
    assert(timer.update(now));
    assert(timer.completedSessions() == session);
    assert(timer.phase() == (session == 4 ? Phase::LongBreak : Phase::ShortBreak));
    const uint32_t breakDuration = session == 4
        ? PomodoroTimer::LongBreakDurationMs : PomodoroTimer::ShortBreakDurationMs;
    assert(timer.remainingMs() == breakDuration);
    now += breakDuration;
    assert(timer.update(now));
    assert(timer.phase() == Phase::Focus);
    assert(timer.remainingMs() == PomodoroTimer::FocusDurationMs);
    assert(timer.state() == State::Running);
  }

  timer.reset();
  assert(timer.completedSessions() == 0);
  assert(timer.phase() == Phase::Focus);
  assert(timer.state() == State::Ready);
}

void testDelayedUpdates() {
  PomodoroTimer timer;
  timer.start(0);
  const uint32_t now = 2 * PomodoroTimer::FocusDurationMs
      + PomodoroTimer::ShortBreakDurationMs + 1234;
  assert(timer.update(now));
  assert(timer.phase() == Phase::ShortBreak);
  assert(timer.completedSessions() == 2);
  assert(timer.remainingMs() == PomodoroTimer::ShortBreakDurationMs - 1234);
  assert(!timer.update(now));
}

void testMillisRollover() {
  PomodoroTimer timer;
  timer.start(UINT32_MAX - 500);
  assert(!timer.update(499));
  assert(timer.remainingMs() == PomodoroTimer::FocusDurationMs - 1000);
  timer.pause(999);
  timer.start(UINT32_MAX - 10);
  assert(!timer.update(989));
  assert(timer.remainingMs() == PomodoroTimer::FocusDurationMs - 2500);
}

void testSkipPreservesState() {
  PomodoroTimer timer;
  timer.skip(0);
  assert(timer.phase() == Phase::ShortBreak);
  assert(timer.state() == State::Ready);
  assert(timer.completedSessions() == 0);
  timer.skip(0);
  timer.start(0);
  timer.skip(2000);
  assert(timer.phase() == Phase::ShortBreak);
  assert(timer.state() == State::Running);
  assert(timer.remainingMs() == PomodoroTimer::ShortBreakDurationMs);
  timer.pause(2500);
  timer.skip(9000);
  assert(timer.phase() == Phase::Focus);
  assert(timer.state() == State::Paused);
  assert(timer.remainingMs() == PomodoroTimer::FocusDurationMs);
  assert(timer.completedSessions() == 0);
}

int main() {
  testReadyAndPauseResume();
  testExactBoundary();
  testLongBreakCycleAndSkippedFocus();
  testDelayedUpdates();
  testMillisRollover();
  testSkipPreservesState();
  puts("PASS: all 6 Pomodoro timing tests");
}
