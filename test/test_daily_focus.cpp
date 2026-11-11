#include <assert.h>
#include <stdio.h>
#include <vector>

#include "DailyFocusCounter.h"
#include "PomodoroTimer.h"

static std::vector<uint32_t> completions;
static void record(uint32_t now) { completions.push_back(now); }

int main() {
  PomodoroTimer timer(record);
  timer.start(100);
  const uint32_t first = 100 + PomodoroTimer::FocusDurationMs;
  const uint32_t second = first + PomodoroTimer::ShortBreakDurationMs +
      PomodoroTimer::FocusDurationMs;
  assert(timer.update(second + 1000));
  assert(completions == std::vector<uint32_t>({first, second}));
  timer.reset();
  timer.start(0);
  timer.skip(100);
  assert(completions.size() == 2);
  timer.reset();
  timer.start(UINT32_MAX - 1000);
  const uint32_t wrapped = UINT32_MAX - 1000 + PomodoroTimer::FocusDurationMs;
  timer.pause(wrapped);
  assert(completions.size() == 3 && completions.back() == wrapped);

  constexpr int32_t offset = 10800;
  constexpr int64_t midnight = 1704067200000LL + 21LL * 60 * 60 * 1000;
  DailyFocusCounter daily;
  daily.completed(900);
  assert(!daily.synchronize(0, 1000, offset));
  assert(!daily.timeKnown);
  assert(daily.synchronize(midnight - 1, 1000, offset));
  assert(daily.saved.count == 1);
  const auto saved = daily.saved;
  assert(daily.synchronize(midnight, 1001, offset));
  assert(daily.saved.count == 0 && daily.saved.day == saved.day + 1);
  daily.completed(1002);
  daily.completed(1500);
  assert(daily.synchronize(midnight + 999, 2000, offset));
  assert(daily.saved.count == 2);
  assert(!daily.synchronize(midnight + 1999, 3000, offset));
  assert(daily.saved.count == 2);

  DailyFocusCounter restored;
  restored.saved = daily.saved;
  assert(!restored.synchronize(0, 0, offset));
  assert(restored.saved.count == 2);
  assert(!restored.synchronize(midnight + 2000, 1000, offset));
  assert(restored.timeKnown && restored.saved.count == 2);

  DailyFocusCounter unsynchronized;
  unsynchronized.completed(500);
  unsynchronized.completed(2000);
  unsynchronized.completed(2500);
  assert(unsynchronized.synchronize(midnight + 1000, 3000, offset));
  assert(unsynchronized.saved.count == 2);

  DailyFocusCounter backlog;
  constexpr uint64_t interval = PomodoroTimer::FocusDurationMs;
  const uint64_t now = 70 * interval;
  const int64_t localNow = midnight + 23LL * 60 * 60 * 1000;
  uint32_t expected = 0;
  for (uint64_t i = 1; i <= 70; ++i) {
    backlog.completed(i * interval);
    if (localNow - int64_t(now - i * interval) >= midnight) ++expected;
  }
  assert(backlog.synchronize(localNow, now, offset));
  assert(backlog.saved.count == expected);
  puts("PASS: completion timestamps, daily rollover, persistence and offline clock sync");
}
