#include <assert.h>
#include <stdio.h>
#include <string>
#include "EnergyStorage.h"

struct FakeStorage {
  std::string value;
  bool fail = false;
  unsigned writes = 0;
  bool isKey(const char *) const { return !value.empty(); }
  size_t getString(const char *, char *buffer, size_t capacity) const {
    if (value.size() + 1 > capacity || fail) return 0;
    memcpy(buffer, value.c_str(), value.size() + 1);
    return value.size() + 1;
  }
  size_t putString(const char *, const char *body) {
    if (fail) return 0;
    ++writes;
    value = body;
    return value.size();
  }
};

int main() {
  const char *body = "{\"version\":1,\"score\":80,\"coverage\":\"limited\",\"reason\":\"modeled\","
      "\"computed_at\":1735804800,\"observed_at\":1735804800,\"valid_until\":1735826400}";
  const int64_t now = 1735804800;
  FakeStorage storage;
  EnergyState energy;
  assert(EnergyStorage::restore(storage, energy, 0, 0) == EnergyAcceptError::None);
  assert(energy.status(now, 0) == EnergyStatus::Missing);
  storage.fail = true;
  assert(EnergyStorage::accept(storage, energy, body, strlen(body), now, 0) == EnergyAcceptError::StorageFailure);
  assert(energy.status(now, 0) == EnergyStatus::Missing);
  storage.fail = false;
  assert(EnergyStorage::accept(storage, energy, body, strlen(body), now, 0) == EnergyAcceptError::None);
  assert(EnergyStorage::accept(storage, energy, body, strlen(body), now + 3600, 3600000) == EnergyAcceptError::None);
  assert(storage.writes == 1);
  EnergyState rebooted;
  assert(EnergyStorage::restore(storage, rebooted, 0, 0) == EnergyAcceptError::None);
  assert(rebooted.status(0, 0) == EnergyStatus::ClockUnavailable);
  assert(rebooted.status(now + 7199, 1) == EnergyStatus::Fresh);
  assert(rebooted.status(now + 7200, 1) == EnergyStatus::Stale);
  assert(rebooted.status(now + 21600, 1) == EnergyStatus::Expired);
  rebooted.clear();
  storage.value = "{bad";
  assert(EnergyStorage::restore(storage, rebooted, 0, 0) == EnergyAcceptError::InvalidJson);
  storage.value = std::string(513, 'x');
  assert(EnergyStorage::restore(storage, rebooted, now, 0) == EnergyAcceptError::StorageFailure);
  storage.value = body;
  assert(EnergyStorage::restore(storage, rebooted, now - 301, 0) == EnergyAcceptError::FutureTimestamp);
  assert(EnergyStorage::restore(storage, rebooted, now + 21600, 0) == EnergyAcceptError::None);
  assert(rebooted.status(now + 21600, 0) == EnergyStatus::Expired);
  puts("PASS: persisted energy, restart age, unavailable clock, duplicate writes and storage failures");
}
