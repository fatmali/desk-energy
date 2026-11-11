#pragma once

#include "EnergyState.h"

namespace EnergyStorage {
template <typename Storage>
EnergyAcceptError restore(Storage &storage, EnergyState &energy, int64_t now, uint32_t uptime) {
  if (!storage.isKey("energy")) return EnergyAcceptError::None;
  char body[EnergyState::MaxBody + 1] = {};
  const size_t size = storage.getString("energy", body, sizeof(body));
  if (size < 2 || size > sizeof(body) || body[size - 1] != '\0') {
    return EnergyAcceptError::StorageFailure;
  }
  // Validate stored data without pretending the boot clock has been synchronized.
  if (now < 1704067200) {
    JsonDocument document;
    if (deserializeJson(document, body, size - 1, DeserializationOption::NestingLimit(2))) {
      return EnergyAcceptError::InvalidJson;
    }
    now = document["computed_at"] | int64_t(0);
  }
  return energy.accept(body, size - 1, now, uptime);
}

template <typename Storage>
EnergyAcceptError accept(Storage &storage, EnergyState &energy, const char *body, size_t size,
                         int64_t now, uint32_t uptime) {
  EnergyState next = energy;
  const EnergyAcceptError result = next.accept(body, size, now, uptime);
  if (result != EnergyAcceptError::None) return result;
  if (next.reading() == energy.reading()) return EnergyAcceptError::None;
  char saved[EnergyState::MaxBody + 1] = {};
  memcpy(saved, body, size);
  if (storage.putString("energy", saved) != size) return EnergyAcceptError::StorageFailure;
  energy = next;
  return EnergyAcceptError::None;
}

}
