export class InputError extends Error {}

export interface MetricSummary {
  latest: number;
  observed_at: string;
  baseline: number | null;
  baseline_days: number;
}

export interface Summary {
  version: 1;
  captured_at: string;
  day_start: string;
  next_day_start: string;
  hrv: MetricSummary | null;
  resting_hr: MetricSummary | null;
  activity: { kilocalories: number; observed_at: string } | null;
}

export interface Reading {
  version: 1;
  score: number | null;
  coverage: "limited" | "unavailable";
  reason: "modeled" | "missing_sleep";
  computed_at: number;
  observed_at: number | null;
  valid_until: number;
}

export interface Session {
  dayStart: number;
  startedAt: number;
  reserve: number;
  lastScore: number;
}

// ---- model tuning: every number the score depends on lives here ----
const HOUR = 3600;
const MAX_SUMMARY_AGE = 900;          // reject summaries captured more than 15 min ago
const MAX_CLOCK_SKEW = 300;           // or more than 5 min in the future
const MAX_METRIC_AGE = 36 * HOUR;     // HRV / resting HR older than this are ignored
const MIN_BASELINE_DAYS = 7;          // baselines built on fewer days are ignored
const HRV_WEIGHT = 0.25;
const RESTING_HR_WEIGHT = 0.15;
const RESTING_HR_POINTS_PER_BPM = 5;  // penalty per bpm above baseline
const AWAKE_DRAIN_PER_HOUR = 3;
const ACTIVITY_DRAIN_PER_KCAL = 0.025;
const ACTIVITY_DRAIN_CAP = 30;
const READING_TTL = 6 * HOUR;

function object(value: unknown, keys: string[]): Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value) ||
      Object.keys(value).length !== keys.length ||
      keys.some(key => !Object.hasOwn(value, key))) {
    throw new InputError("Unexpected summary fields.");
  }
  return value as Record<string, unknown>;
}

function number(value: unknown, low: number, high: number): number {
  if (typeof value !== "number" || !Number.isFinite(value) || value < low || value > high) {
    throw new InputError("Invalid summary number.");
  }
  return value;
}

const ISO = /^(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)(?:\.\d{1,3})?(?:Z|[+-](\d\d):(\d\d))$/;

export function epoch(value: unknown): number {
  const match = typeof value === "string" ? ISO.exec(value) : null;
  if (!match) throw new InputError("Dates must be ISO 8601 with a timezone.");
  // Date.parse silently rolls impossible dates over (Feb 30 becomes Mar 2,
  // 24:00 becomes the next day), so the calendar fields are checked here.
  const [year, month, day, hour, minute, second, offsetHour = 0, offsetMinute = 0] =
    match.slice(1).map(part => (part === undefined ? undefined : Number(part)));
  const daysInMonth = new Date(Date.UTC(year!, month!, 0)).getUTCDate();
  if (month! < 1 || month! > 12 || day! < 1 || day! > daysInMonth ||
      hour! > 23 || minute! > 59 || second! > 59 || offsetHour > 14 || offsetMinute > 59) {
    throw new InputError("Invalid summary date.");
  }
  const result = Math.floor(Date.parse(value as string) / 1000);
  if (!Number.isFinite(result) || result < 1704067200 || result >= 4102444800) {
    throw new InputError("Invalid summary date.");
  }
  return result;
}

function metric(value: unknown, low: number, high: number, captured: number): MetricSummary | null {
  if (value === null) return null;
  const item = object(value, ["latest", "observed_at", "baseline", "baseline_days"]);
  const latest = number(item.latest, low, high);
  const observed = epoch(item.observed_at);
  const days = number(item.baseline_days, 0, 10000);
  if (observed > captured || !Number.isInteger(days) ||
      (item.baseline === null) !== (days === 0)) {
    throw new InputError("Invalid baseline or observation time.");
  }
  const baseline = item.baseline === null ? null : number(item.baseline, low, high);
  return { latest, observed_at: String(item.observed_at), baseline, baseline_days: days };
}

export function parseSummary(value: unknown, now: number): Summary {
  const item = object(value,
    ["version", "captured_at", "day_start", "next_day_start", "hrv", "resting_hr", "activity"]);
  if (item.version !== 1) throw new InputError("Unsupported summary version.");
  const captured = epoch(item.captured_at);
  const dayStart = epoch(item.day_start);
  const nextDay = epoch(item.next_day_start);
  if (captured < now - MAX_SUMMARY_AGE || captured > now + MAX_CLOCK_SKEW) {
    throw new InputError("Summary is stale or the phone clock is incorrect.");
  }
  if (dayStart > captured || nextDay <= captured ||
      nextDay - dayStart < 23 * HOUR || nextDay - dayStart > 25 * HOUR) {
    throw new InputError("Invalid local-day boundaries.");
  }
  let activity: Summary["activity"] = null;
  if (item.activity !== null) {
    const input = object(item.activity, ["kilocalories", "observed_at"]);
    const observed = epoch(input.observed_at);
    if (observed < dayStart || observed > captured) throw new InputError("Invalid activity date.");
    activity = {
      kilocalories: number(input.kilocalories, 0, 20000),
      observed_at: String(input.observed_at),
    };
  }
  return {
    version: 1,
    captured_at: String(item.captured_at),
    day_start: String(item.day_start),
    next_day_start: String(item.next_day_start),
    hrv: metric(item.hrv, 0.001, 1000, captured),
    resting_hr: metric(item.resting_hr, 20, 250, captured),
    activity,
  };
}

type Scorable = MetricSummary & { baseline: number };

/** A metric counts only if it is recent and its baseline has enough history. */
function scorable(item: MetricSummary | null, captured: number): Scorable | null {
  if (!item || item.baseline === null || item.baseline_days < MIN_BASELINE_DAYS) return null;
  if (captured - epoch(item.observed_at) > MAX_METRIC_AGE) return null;
  return item as Scorable;
}

/** 100 at or above baseline, falling in proportion below it. */
function hrvScore(item: Scorable): number {
  return Math.min(100, item.latest / item.baseline * 100);
}

/** 100 at or below baseline, minus a fixed penalty per bpm above it. */
function restingHrScore(item: Scorable): number {
  return Math.max(0, 100 - Math.max(0, item.latest - item.baseline) * RESTING_HR_POINTS_PER_BPM);
}

/** Stored state is trusted only if it is well formed and belongs to this day. */
function sameDaySession(previous: Session | null, dayStart: number): Session | null {
  if (!previous || previous.dayStart !== dayStart) return null;
  const sane = [previous.startedAt, previous.reserve, previous.lastScore].every(Number.isFinite);
  return sane ? previous : null;
}

export function calculate(summary: Summary, previous: Session | null):
    { reading: Reading; session: Session | null } {
  const captured = epoch(summary.captured_at);
  const dayStart = epoch(summary.day_start);
  const hrv = scorable(summary.hrv, captured);
  const restingHr = scorable(summary.resting_hr, captured);

  // Only inputs that actually fed the score count as "observed".
  const observations = [hrv, restingHr, summary.activity]
    .filter(item => item !== null).map(item => epoch(item.observed_at));
  const reading: Reading = {
    version: 1, score: null, coverage: "unavailable", reason: "missing_sleep",
    computed_at: captured,
    observed_at: observations.length ? Math.max(...observations) : null,
    valid_until: Math.min(epoch(summary.next_day_start), captured + READING_TTL),
  };

  let weighted = 0;
  let weight = 0;
  if (hrv) { weighted += hrvScore(hrv) * HRV_WEIGHT; weight += HRV_WEIGHT; }
  if (restingHr) { weighted += restingHrScore(restingHr) * RESTING_HR_WEIGHT; weight += RESTING_HR_WEIGHT; }
  if (!weight) return { reading, session: previous };

  const today = sameDaySession(previous, dayStart);
  // Reserve and score only ever fall within a day.
  const reserve = Math.min(weighted / weight, today?.reserve ?? 100);
  // A summary delivered out of order must not push the start later or make elapsed time negative.
  const startedAt = Math.min(today?.startedAt ?? captured, captured);
  const awakeDrain = (captured - startedAt) / HOUR * AWAKE_DRAIN_PER_HOUR;
  const activityDrain = Math.min(ACTIVITY_DRAIN_CAP,
    (summary.activity?.kilocalories ?? 0) * ACTIVITY_DRAIN_PER_KCAL);
  const score = Math.min(
    Math.floor(Math.max(0, reserve - awakeDrain - activityDrain)),
    today?.lastScore ?? 100);

  return {
    reading: { ...reading, score, coverage: "limited", reason: "modeled" },
    session: { dayStart, startedAt, reserve, lastScore: score },
  };
}
