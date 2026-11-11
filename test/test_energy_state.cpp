#include <assert.h>
#include <stdio.h>
#include <string>

#include "EnergyState.h"

static EnergyAcceptError accept(EnergyState &state, const JsonDocument &document,
                                int64_t now, uint32_t uptime = 0) {
  std::string body;
  serializeJson(document, body);
  return state.accept(body.data(), body.size(), now, uptime);
}

int main() {
  const std::string fixture = "{\"version\":1,\"score\":76,\"coverage\":\"limited\","
      "\"reason\":\"modeled\",\"computed_at\":1735804800,\"observed_at\":1735804800,"
      "\"valid_until\":1735826400}";
  JsonDocument document;
  assert(!deserializeJson(document, fixture));
  const int64_t now = 1735804800;
  EnergyState state;
  assert(state.status(now, 0) == EnergyStatus::Missing);
  assert(state.accept(fixture.data(), fixture.size(), now, 0) == EnergyAcceptError::None);
  assert(state.reading().score == 76);
  assert(state.reading().coverage == EnergyCoverage::Limited);
  assert(state.status(now, 0) == EnergyStatus::Fresh);
  assert(state.status(now + 7199, 7199000) == EnergyStatus::Fresh);
  assert(energyScoreVisible(state.status(now + 7199, 7199000)));
  assert(state.status(now + 7200, 7200000) == EnergyStatus::Stale);
  assert(energyScoreVisible(state.status(now + 7200, 7200000)));
  assert(state.status(now + 21600, 21600000) == EnergyStatus::Expired);
  assert(!energyScoreVisible(state.status(now + 21600, 21600000)));
  assert(state.status(0, 0) == EnergyStatus::ClockUnavailable);
  assert(state.status(now - 301, 0) == EnergyStatus::ClockUnavailable);
  assert(accept(state, document, now + 7200, 7200000) == EnergyAcceptError::None);
  assert(state.status(now, 7200000) == EnergyStatus::Stale);

  for (int score : {-1, 101}) {
    document["score"] = score;
    assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  }
  document["score"] = "76";
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document["score"] = 76.5;
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document["score"] = true;
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document["score"] = nullptr;
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document["score"] = 76;
  document["version"] = 2;
  assert(accept(state, document, now) == EnergyAcceptError::UnsupportedVersion);
  document["version"] = 1;
  document["coverage"] = "certain";
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document["coverage"] = "limited";
  document["reason"] = "measured";
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document["reason"] = "modeled";
  document["raw_hrv"] = 50;
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document.remove("raw_hrv");
  document.remove("observed_at");
  document["extra"] = nullptr;
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document.remove("extra");
  document["observed_at"] = now + 1;
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document["observed_at"] = now;
  document["computed_at"] = "1735804800";
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document["computed_at"] = now + 301;
  document["valid_until"] = now + 21600 + 301;
  assert(accept(state, document, now) == EnergyAcceptError::FutureTimestamp);
  document["computed_at"] = now;
  document["valid_until"] = now + 21601;
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document["valid_until"] = now;
  assert(accept(state, document, now) == EnergyAcceptError::InvalidReading);
  document["valid_until"] = now + 21600;
  document["score"] = 75;
  assert(accept(state, document, now) == EnergyAcceptError::ConflictingTimestamp);
  document["computed_at"] = now - 1;
  document["observed_at"] = now - 1;
  document["valid_until"] = now + 21599;
  assert(accept(state, document, now) == EnergyAcceptError::OutOfOrder);
  assert(state.reading().score == 76);

  const std::string huge(513, ' ');
  assert(state.accept(huge.data(), huge.size(), now, 0) == EnergyAcceptError::BodyTooLarge);
  assert(state.accept(nullptr, 0, now, 0) == EnergyAcceptError::InvalidJson);
  const std::string nul = fixture + '\0';
  assert(state.accept(nul.data(), nul.size(), now, 0) == EnergyAcceptError::InvalidJson);
  assert(state.accept("{", 1, now, 0) == EnergyAcceptError::InvalidJson);
  for (const char *suffix : {"false", "{}", "garbage"}) {
    const std::string trailing = fixture + suffix;
    assert(state.accept(trailing.data(), trailing.size(), now, 0) == EnergyAcceptError::InvalidJson);
  }
  const std::string whitespace = fixture + " \n\t";
  assert(state.accept(whitespace.data(), whitespace.size(), now, 0) == EnergyAcceptError::None);
  assert(state.accept(fixture.data(), fixture.size(), 0, 0) == EnergyAcceptError::ClockUnavailable);

  state.clear();
  assert(state.status(now, 0) == EnergyStatus::Missing);
  assert(state.reading().score == -1);
  assert(!deserializeJson(document, fixture));
  document["observed_at"] = now - 7200;
  assert(accept(state, document, now) == EnergyAcceptError::None);
  assert(state.status(now, 0) == EnergyStatus::Stale);
  assert(energyScoreVisible(state.status(now, 0)));
  document["computed_at"] = now + 1;
  document["observed_at"] = now - 30 * 3600;
  assert(accept(state, document, now + 1) == EnergyAcceptError::None);
  assert(state.status(now + 1, 0) == EnergyStatus::Stale);
  assert(energyScoreVisible(state.status(now + 1, 0)));
  assert(strcmp(energyStatusText(state.status(now + 1, 0)), "STALE: older Watch data") == 0);
  document["computed_at"] = now;
  document["observed_at"] = now - 7200;
  state.clear();
  const uint32_t start = UINT32_MAX - 1000;
  assert(accept(state, document, now, start) == EnergyAcceptError::None);
  assert(state.status(now, uint32_t(start + 7200000U)) == EnergyStatus::Stale);
  assert(state.status(now, uint32_t(start + 21600000U)) == EnergyStatus::Expired);
  state.clear();
  document["score"] = nullptr;
  document["coverage"] = "unavailable";
  document["reason"] = "missing_sleep";
  document["observed_at"] = nullptr;
  assert(accept(state, document, now) == EnergyAcceptError::None);
  assert(state.status(now, 0) == EnergyStatus::Unavailable);
  assert(state.reading().score == -1);
  assert(!energyScoreVisible(state.status(now, 0)));
  for (const EnergyStatus hidden : {EnergyStatus::Missing, EnergyStatus::Expired,
       EnergyStatus::Unavailable, EnergyStatus::ClockUnavailable}) {
    assert(!energyScoreVisible(hidden));
    assert(strlen(energyStatusText(hidden)) > 0);
    assert(strcmp(energyStatusText(hidden), "Waiting for fresh data") != 0);
  }
  assert(strlen(energyErrorText(EnergyAcceptError::OutOfOrder)) > 0);
  puts("PASS: energy wire fixture, schema, rejection, freshness, expiry and clock rollover");
}
