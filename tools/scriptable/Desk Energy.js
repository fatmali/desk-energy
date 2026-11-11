// Variables used by Scriptable.
// icon-color: orange; icon-glyph: paw;

const relayURL = "https://YOUR-WORKER.YOUR-SUBDOMAIN.workers.dev/v1/energy";
const credentialName = "desk-energy-e6e2e0-upload";

class SyncError extends Error {}

function list(value, name) {
  if (value === undefined) throw new SyncError("Missing Shortcut field: " + name);
  if (value === null || value === "") return [];
  const result = Array.isArray(value) ? value : [value];
  if (result.length > 10000) throw new SyncError(name + " exceeds 10,000 samples. Narrow the Health query.");
  return result;
}

function timestamp(value, name, now) {
  if (typeof value !== "string" ||
      !/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d{1,3})?(?:Z|[+-]\d\d:\d\d)$/.test(value)) {
    throw new SyncError(name + ": format dates as ISO 8601, including time and timezone.");
  }
  const time = Date.parse(value);
  if (!Number.isFinite(time) || time < Date.UTC(2024, 0, 1) ||
      time >= Date.UTC(2100, 0, 1) || time > now) {
    throw new SyncError(name + " contains an invalid or future date.");
  }
  return time;
}

function quantity(input, kind) {
  let value = input;
  if (typeof input === "string") {
    const match = input.trim().match(/^(\d+(?:[.,]\d+)?)\s*([a-zA-Z/]+)?$/);
    if (!match) throw new SyncError(kind + ": pass numeric Health values without thousands separators.");
    value = Number(match[1].replace(",", "."));
    const unit = match[2] || "";
    const accepted = kind === "HRV" ? ["", "ms", "s"]
      : kind === "Resting heart rate" ? ["", "bpm", "count/min", "beats/min"] : ["", "kcal", "Cal"];
    if (!accepted.includes(unit)) throw new SyncError(kind + " has an unsupported unit.");
    if (kind === "HRV" && unit === "s") value *= 1000;
  }
  const low = kind === "HRV" ? 0.001 : kind === "Resting heart rate" ? 20 : 0;
  const high = kind === "HRV" ? 1000 : kind === "Resting heart rate" ? 250 : 20000;
  if (typeof value !== "number" || !Number.isFinite(value) || value < low || value > high) {
    throw new SyncError(kind + " contains an invalid value.");
  }
  return value;
}

function median(values) {
  const sorted = values.slice().sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}

function dayKey(time) {
  const date = new Date(time);
  return date.getFullYear() + "-" + date.getMonth() + "-" + date.getDate();
}

function metric(input, prefix, label, now) {
  const values = list(input[prefix + "_values"], prefix + "_values");
  const dates = list(input[prefix + "_dates"], prefix + "_dates");
  if (values.length !== dates.length) throw new SyncError(label + ": values and dates must have equal counts.");
  if (!values.length) return null;
  const samples = values.map((value, index) => ({
    value: quantity(value, label), time: timestamp(dates[index], label, now),
  })).sort((a, b) => b.time - a.time);
  const latest = samples[0];
  const days = new Map();
  for (const sample of samples) {
    if (sample.time === latest.time) {
      if (sample.value !== latest.value) throw new SyncError(label + " has conflicting latest samples.");
      continue;
    }
    const key = dayKey(sample.time);
    if (!days.has(key)) days.set(key, []);
    days.get(key).push(sample.value);
  }
  return {
    latest: latest.value,
    observed_at: new Date(latest.time).toISOString(),
    baseline: days.size ? median(Array.from(days.values(), median)) : null,
    baseline_days: days.size,
  };
}

function activity(input, now, dayStart) {
  const keys = ["activity_values", "activity_starts", "activity_ends", "activity_sources"];
  if (keys.every(key => input[key] === undefined)) {
    return { value: null, warning: "Activity was not supplied; activity drain is omitted." };
  }
  const arrays = keys.map(key => list(input[key], key));
  if (arrays.some(array => array.length !== arrays[0].length)) {
    throw new SyncError("Activity values, start dates, end dates and sources must have equal counts.");
  }
  const samples = arrays[0].map((value, index) => {
    const start = timestamp(arrays[1][index], "Activity start", now);
    const end = timestamp(arrays[2][index], "Activity end", now);
    const source = arrays[3][index];
    if (start >= end || typeof source !== "string" || !source.trim()) {
      throw new SyncError("Activity needs a positive duration and a source for every sample.");
    }
    return { start, end, source, value: quantity(value, "Activity") };
  }).filter(sample => sample.start >= dayStart).sort((a, b) => a.start - b.start);
  if (!samples.length) return { value: null, warning: null };
  if (samples.some((sample, index) => sample.source !== samples[0].source ||
      (index > 0 && sample.start < samples[index - 1].end))) {
    return { value: null, warning: "Activity overlaps or has multiple sources; activity drain is omitted." };
  }
  const kilocalories = samples.reduce((sum, sample) => sum + sample.value, 0);
  if (kilocalories > 20000) throw new SyncError("Today's activity total is outside the supported range.");
  return {
    value: { kilocalories, observed_at: new Date(samples[samples.length - 1].end).toISOString() },
    warning: null,
  };
}

function summarize(input, now) {
  if (typeof input === "string") {
    if (input.length > 2 * 1024 * 1024) throw new SyncError("The Health input is too large. Narrow the Health queries.");
    if (input.startsWith("DESK_ENERGY_V1\n") || input.startsWith("DESK_ENERGY_V1\r\n")) {
      const fields = ["hrv_values", "hrv_dates", "rhr_values", "rhr_dates",
        "activity_values", "activity_starts", "activity_ends", "activity_sources"];
      const parsed = {};
      let section;
      for (const raw of input.split(/\r?\n/).slice(1)) {
        const line = raw.trim();
        if (!line) continue;
        const match = line.match(/^\[([a-z_]+)\]$/);
        if (match) {
          section = match[1];
          if (!fields.includes(section) || Object.prototype.hasOwnProperty.call(parsed, section)) {
            throw new SyncError("Unknown or duplicate Health input section.");
          }
          parsed[section] = [];
        } else {
          if (!section) throw new SyncError("Health input is missing a section heading.");
          parsed[section].push(line);
        }
      }
      input = parsed;
    } else {
      try { input = JSON.parse(input); }
      catch { throw new SyncError("Pass the generated Health text or a valid JSON dictionary."); }
    }
  }
  if (!input || typeof input !== "object" || Array.isArray(input)) {
    throw new SyncError("Pass the Health text or dictionary as Scriptable's Parameter.");
  }
  const dayStart = new Date(now);
  dayStart.setHours(0, 0, 0, 0);
  const nextDay = new Date(dayStart);
  nextDay.setDate(nextDay.getDate() + 1);
  const active = activity(input, now, dayStart.getTime());
  return {
    summary: {
      version: 1, captured_at: new Date(now).toISOString(),
      day_start: dayStart.toISOString(), next_day_start: nextDay.toISOString(),
      hrv: metric(input, "hrv", "HRV", now),
      resting_hr: metric(input, "rhr", "Resting heart rate", now),
      activity: active.value,
    },
    warning: active.warning,
  };
}

async function notify(title, body) {
  const notification = new Notification();
  notification.title = title;
  notification.body = body;
  notification.sound = null;
  await notification.schedule();
}

async function setup() {
  const alert = new Alert();
  alert.title = "Desk Energy setup";
  alert.message = "Paste your private upload code. It stays in Scriptable's encrypted Keychain, not in this script or your Shortcut.";
  alert.addSecureTextField("64-character upload code");
  alert.addAction("Save");
  alert.addCancelAction("Cancel");
  if (await alert.presentAlert() === -1) return;
  const secret = alert.textFieldValue(0).trim();
  if (!/^[a-f0-9]{64}$/.test(secret)) throw new SyncError("The upload code must contain exactly 64 lowercase hexadecimal characters.");
  Keychain.set(credentialName, secret);
  await notify("Desk Energy configured", "Run your Health-reading Shortcut to sync. No Health data was sent during setup.");
}

async function run() {
  const input = args.shortcutParameter ?? args.plainTexts[0];
  if (input === undefined || input === null || input === "") {
    if (!config.runsInApp) throw new SyncError("The Shortcut did not pass its Health dictionary as Parameter.");
    await setup();
    return;
  }
  if (!Keychain.contains(credentialName)) {
    throw new SyncError("Open Desk Energy in Scriptable and run it once to save your upload code.");
  }
  const { summary, warning } = summarize(input, Date.now());
  const secret = Keychain.get(credentialName);
  if (!/^[a-f0-9]{64}$/.test(secret)) throw new SyncError("The saved upload code is invalid. Run setup again.");
  const request = new Request(relayURL);
  request.method = "POST";
  request.headers = { Authorization: "Bearer " + secret, "Content-Type": "application/json" };
  request.timeoutInterval = 15;
  request.allowInsecureRequest = false;
  request.onRedirect = () => null;
  request.body = JSON.stringify(summary);
  if (request.body.length > 2048) throw new SyncError("The calculated summary exceeds the relay size limit.");
  let response;
  try { response = await request.loadString(); }
  catch { throw new SyncError("The relay could not be reached. Check the phone's internet connection and retry."); }
  if (request.response.statusCode !== 200) {
    const status = request.response.statusCode;
    throw new SyncError(status === 401 ? "Relay authorization failed. Check your upload code."
      : status === 409 ? "A newer or conflicting update is already saved. Wait a few seconds and retry."
      : "The relay rejected the update (HTTP " + status + ").");
  }
  let reading;
  try { reading = JSON.parse(response); }
  catch { throw new SyncError("The relay returned an invalid receipt."); }
  if (!reading || Object.keys(reading).length !== 7 || reading.version !== 1 ||
      reading.computed_at !== Math.floor(Date.parse(summary.captured_at) / 1000) ||
      !Number.isInteger(reading.valid_until) || reading.valid_until <= reading.computed_at ||
      reading.valid_until > reading.computed_at + 21600 ||
      !(reading.observed_at === null || (Number.isInteger(reading.observed_at) &&
        reading.observed_at >= 1704067200 && reading.observed_at <= reading.computed_at)) ||
      !(reading.score === null
        ? reading.coverage === "unavailable" && reading.reason === "missing_sleep"
        : Number.isInteger(reading.score) && reading.score >= 0 && reading.score <= 100 &&
          reading.coverage === "limited" && reading.reason === "modeled")) {
    throw new SyncError("The relay did not return a valid acceptance receipt.");
  }
  if (warning) await notify("Desk Energy: limited activity", warning);
  Script.setShortcutOutput({ synced: true, score: reading.score, coverage: reading.coverage });
}

try {
  await run();
} catch (error) {
  const message = error instanceof SyncError ? error.message : "Desk Energy failed unexpectedly. Reopen Scriptable and retry.";
  console.error(message);
  await notify("Desk Energy sync failed", message);
  throw new Error(message);
}
Script.complete();
