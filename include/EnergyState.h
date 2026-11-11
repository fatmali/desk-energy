#pragma once

#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>

enum class EnergyCoverage { Full, Limited, Unavailable };
enum class EnergyReason { Modeled, MissingSleep, StaleSleep, StaleSnapshot };
enum class EnergyStatus { Missing, Fresh, Stale, Expired, Unavailable, ClockUnavailable };
inline bool energyScoreVisible(EnergyStatus status) {
  return status == EnergyStatus::Fresh || status == EnergyStatus::Stale;
}

inline const char *energyStatusText(EnergyStatus status) {
  switch (status) {
    case EnergyStatus::Missing: return "Waiting for first sync";
    case EnergyStatus::Fresh: return "Fresh";
    case EnergyStatus::Stale: return "STALE: older Watch data";
    case EnergyStatus::Expired: return "Reading expired - sync phone";
    case EnergyStatus::Unavailable: return "No recent usable Watch data";
    case EnergyStatus::ClockUnavailable: return "Waiting for clock sync";
  }
  return "Unknown energy status";
}

enum class EnergyAcceptError {
  None, BodyTooLarge, InvalidJson, UnsupportedVersion, InvalidReading,
  ClockUnavailable, FutureTimestamp, OutOfOrder, ConflictingTimestamp, StorageFailure
};

inline const char *energyErrorText(EnergyAcceptError error) {
  switch (error) {
    case EnergyAcceptError::None: return "Accepted";
    case EnergyAcceptError::BodyTooLarge: return "Energy payload exceeds 512 bytes";
    case EnergyAcceptError::InvalidJson: return "Invalid energy JSON";
    case EnergyAcceptError::UnsupportedVersion: return "Unsupported energy schema version";
    case EnergyAcceptError::InvalidReading: return "Invalid energy reading";
    case EnergyAcceptError::ClockUnavailable: return "Energy update requires a valid clock";
    case EnergyAcceptError::FutureTimestamp: return "Energy timestamp is in the future";
    case EnergyAcceptError::OutOfOrder: return "Older energy update rejected";
    case EnergyAcceptError::ConflictingTimestamp: return "Conflicting energy update timestamp";
    case EnergyAcceptError::StorageFailure: return "Energy reading could not be saved or removed";
  }
  return "Unknown energy error";
}

struct EnergyReading {
  int score = -1;
  EnergyCoverage coverage = EnergyCoverage::Unavailable;
  EnergyReason reason = EnergyReason::MissingSleep;
  int64_t computedAt = 0;
  int64_t observedAt = 0;
  int64_t validUntil = 0;

  bool operator==(const EnergyReading &other) const {
    return score == other.score && coverage == other.coverage && reason == other.reason &&
        computedAt == other.computedAt && observedAt == other.observedAt &&
        validUntil == other.validUntil;
  }
};

class EnergyState {
public:
  static constexpr size_t MaxBody = 512;

  EnergyAcceptError accept(const char *body, size_t length, int64_t now, uint32_t uptime) {
    if (length > MaxBody) return EnergyAcceptError::BodyTooLarge;
    if (!body || length == 0 || memchr(body, '\0', length)) return EnergyAcceptError::InvalidJson;
    if (!validClock(now)) return EnergyAcceptError::ClockUnavailable;
    JsonDocument document;
    JsonInput input(body, length);
    if (deserializeJson(document, input, DeserializationOption::NestingLimit(2)) ||
        !input.onlyWhitespaceRemains()) {
      return EnergyAcceptError::InvalidJson;
    }
    const JsonObjectConst object = document.as<JsonObjectConst>();
    if (object.isNull() || object.size() != 7 || !object["version"].is<int>()) {
      return EnergyAcceptError::InvalidReading;
    }
    if (object["version"].as<int>() != 1) return EnergyAcceptError::UnsupportedVersion;
    const char *coverage = object["coverage"];
    const char *reason = object["reason"];
    if (!coverage || !reason || object["score"].isUnbound() ||
        object["observed_at"].isUnbound() || !object["computed_at"].is<int64_t>() ||
        !object["valid_until"].is<int64_t>()) {
      return EnergyAcceptError::InvalidReading;
    }
    EnergyReading incoming;
    if (strcmp(coverage, "full") == 0) incoming.coverage = EnergyCoverage::Full;
    else if (strcmp(coverage, "limited") == 0) incoming.coverage = EnergyCoverage::Limited;
    else if (strcmp(coverage, "unavailable") != 0) return EnergyAcceptError::InvalidReading;
    if (strcmp(reason, "modeled") == 0) incoming.reason = EnergyReason::Modeled;
    else if (strcmp(reason, "missing_sleep") == 0) incoming.reason = EnergyReason::MissingSleep;
    else if (strcmp(reason, "stale_sleep") == 0) incoming.reason = EnergyReason::StaleSleep;
    else if (strcmp(reason, "stale_snapshot") == 0) incoming.reason = EnergyReason::StaleSnapshot;
    else return EnergyAcceptError::InvalidReading;

    if (incoming.coverage == EnergyCoverage::Unavailable) {
      if (!object["score"].isNull() || incoming.reason == EnergyReason::Modeled) {
        return EnergyAcceptError::InvalidReading;
      }
    } else {
      if (!object["score"].is<int>() || incoming.reason != EnergyReason::Modeled ||
          !object["observed_at"].is<int64_t>()) {
        return EnergyAcceptError::InvalidReading;
      }
      incoming.score = object["score"];
      if (incoming.score < 0 || incoming.score > 100) return EnergyAcceptError::InvalidReading;
    }
    incoming.computedAt = object["computed_at"];
    incoming.validUntil = object["valid_until"];
    if (!object["observed_at"].isNull()) {
      if (!object["observed_at"].is<int64_t>()) return EnergyAcceptError::InvalidReading;
      incoming.observedAt = object["observed_at"];
      if (!validClock(incoming.observedAt) || incoming.observedAt > incoming.computedAt) {
        return EnergyAcceptError::InvalidReading;
      }
    }
    if (!validClock(incoming.computedAt) || !validClock(incoming.validUntil) ||
        incoming.validUntil <= incoming.computedAt ||
        incoming.validUntil - incoming.computedAt > 6 * 3600) {
      return EnergyAcceptError::InvalidReading;
    }
    if (incoming.computedAt > now + 300) return EnergyAcceptError::FutureTimestamp;
    if (present_) {
      if (incoming.computedAt < reading_.computedAt) return EnergyAcceptError::OutOfOrder;
      if (incoming.computedAt == reading_.computedAt) {
        return incoming == reading_ ? EnergyAcceptError::None : EnergyAcceptError::ConflictingTimestamp;
      }
    }
    reading_ = incoming;
    receivedAt_ = uptime;
    present_ = true;
    return EnergyAcceptError::None;
  }

  EnergyStatus status(int64_t now, uint32_t uptime) const {
    if (!present_) return EnergyStatus::Missing;
    if (!validClock(now) || now < reading_.computedAt - 300) return EnergyStatus::ClockUnavailable;
    const uint32_t receivedAge = uint32_t(uptime - receivedAt_);
    if (now >= reading_.validUntil || receivedAge >= 6 * 3600 * 1000UL) {
      return EnergyStatus::Expired;
    }
    if (reading_.coverage == EnergyCoverage::Unavailable) return EnergyStatus::Unavailable;
    if (now - reading_.computedAt >= 2 * 3600 || now - reading_.observedAt >= 2 * 3600 ||
        receivedAge >= 2 * 3600 * 1000UL) {
      return EnergyStatus::Stale;
    }
    return EnergyStatus::Fresh;
  }

  const EnergyReading &reading() const { return reading_; }

  void clear() {
    reading_ = EnergyReading{};
    receivedAt_ = 0;
    present_ = false;
  }

private:
  class JsonInput {
  public:
    JsonInput(const char *body, size_t length) : body_(body), length_(length) {}
    int read() { return position_ < length_ ? static_cast<unsigned char>(body_[position_++]) : -1; }
    size_t readBytes(char *buffer, size_t length) {
      const size_t count = length < length_ - position_ ? length : length_ - position_;
      memcpy(buffer, body_ + position_, count);
      position_ += count;
      return count;
    }
    bool onlyWhitespaceRemains() const {
      for (size_t index = position_; index < length_; ++index) {
        const char c = body_[index];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
      }
      return true;
    }
  private:
    const char *body_;
    size_t length_;
    size_t position_ = 0;
  };

  static bool validClock(int64_t epoch) {
    return epoch >= 1704067200 && epoch <= 4102444800LL;
  }

  EnergyReading reading_;
  uint32_t receivedAt_ = 0;
  bool present_ = false;
};
