#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

class DailyFocusCounter {
public:
  struct Saved {
    int32_t day = -1;
    uint32_t count = 0;
  };

  Saved saved;
  bool timeKnown = false;

  void completed(uint64_t uptimeMs) {
    // 64 completions cover more than a day even when every break is skipped.
    pending_[next_] = uptimeMs;
    next_ = (next_ + 1) % pending_.size();
    if (pendingCount_ < pending_.size()) ++pendingCount_;
  }

  bool synchronize(int64_t epochMs, uint64_t uptimeMs, int32_t utcOffsetSeconds) {
    timeKnown = epochMs >= 1704067200000LL;
    if (!timeKnown) return false;
    const int64_t localMs = epochMs + int64_t(utcOffsetSeconds) * 1000;
    const int32_t today = localMs / 86400000;
    bool changed = false;
    if (saved.day != today) {
      saved.day = today;
      saved.count = 0;
      changed = true;
    }
    for (size_t i = 0; i < pendingCount_; ++i) {
      if (pending_[i] <= uptimeMs &&
          (localMs - int64_t(uptimeMs - pending_[i])) / 86400000 == today) {
        ++saved.count;
        changed = true;
      }
    }
    pendingCount_ = 0;
    next_ = 0;
    return changed;
  }

private:
  std::array<uint64_t, 64> pending_ = {};
  size_t pendingCount_ = 0;
  size_t next_ = 0;
};
