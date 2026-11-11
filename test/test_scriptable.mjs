import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { calculate, parseSummary } from "../relay/src/model.ts";

const source = readFileSync(new URL("../tools/scriptable/Desk Energy.js", import.meta.url), "utf8");
const fixture = JSON.parse(readFileSync(new URL("./fixtures/shortcut-summary.json", import.meta.url), "utf8"));
const now = Date.parse(fixture.captured_at);
const input = {
  hrv_values: fixture.hrv.filter(sample => sample.Value !== 999).map(sample => sample.Value),
  hrv_dates: fixture.hrv.filter(sample => sample.Value !== 999).map(sample => sample["Start Date"]),
  rhr_values: fixture.resting_hr.map(sample => sample.Value),
  rhr_dates: fixture.resting_hr.map(sample => sample["Start Date"]),
  activity_values: fixture.activity.map(sample => sample.Value),
  activity_starts: fixture.activity.map(sample => sample["Start Date"]),
  activity_ends: fixture.activity.map(sample => sample["End Date"]),
  activity_sources: fixture.activity.map(sample => sample.Source),
};
let checks = 0;
async function execute(parameter, options = {}) {
  const result = { notifications: [], logs: [], requests: [], complete: false };
  const keychain = new Map(options.configured === false ? [] : [["desk-energy-e6e2e0-upload", "b".repeat(64)]]);
  class MockRequest {
    constructor(url) { this.url = url; result.requests.push(this); }
    async loadString() {
      if (options.offline) throw new Error("Untrusted network error with sensitive content");
      this.response = { statusCode: options.status ?? 200 };
      const summary = JSON.parse(this.body);
      const epoch = Math.floor(Date.parse(summary.captured_at) / 1000);
      return JSON.stringify(options.receipt ?? {
        version: 1, score: 77, coverage: "limited", reason: "modeled",
        computed_at: epoch, observed_at: epoch - 1, valid_until: epoch + 21600,
      });
    }
  }
  class Clock extends Date {
    constructor(...args) { super(...(args.length ? args : [now])); }
    static now() { return now; }
  }
  const context = vm.createContext({
    Date: Clock,
    args: { shortcutParameter: parameter, plainTexts: [] },
    config: { runsInApp: options.inApp ?? false },
    Keychain: {
      contains: key => keychain.has(key), get: key => keychain.get(key),
      set: (key, value) => keychain.set(key, value),
    },
    Request: MockRequest,
    Notification: class {
      async schedule() { result.notifications.push({ title: this.title, body: this.body }); }
    },
    Alert: class {
      addSecureTextField() {}
      addAction() {}
      addCancelAction() {}
      async presentAlert() { return options.cancel ? -1 : 0; }
      textFieldValue() { return options.setupCode ?? "c".repeat(64); }
    },
    Script: {
      setShortcutOutput: value => { result.output = JSON.parse(JSON.stringify(value)); },
      complete: () => { result.complete = true; },
    },
    console: { error: message => result.logs.push(message) },
  });
  try { await vm.runInContext("(async () => {\n" + source + "\n})()", context); }
  catch (error) { result.error = error.message; }
  result.keys = keychain;
  return result;
}

const good = await execute(input);
assert(!good.error, good.error);
assert(good.complete);
assert.equal(good.output.synced, true);
assert.equal(good.notifications.length, 0);
const request = good.requests[0];
assert.equal(request.method, "POST");
assert.equal(request.allowInsecureRequest, false);
assert.equal(request.onRedirect(), null);
assert.equal(request.timeoutInterval, 15);
assert.equal(request.headers.Authorization, "Bearer " + "b".repeat(64));
const body = JSON.parse(request.body);
assert.deepEqual(body.hrv, { latest: 40, observed_at: "2026-11-10T12:00:00.000Z", baseline: 60, baseline_days: 2 });
assert.equal(body.resting_hr.baseline, 61);
assert.equal(body.activity.kilocalories, 100);
assert.equal(Object.keys(body).length, 7);
assert(!request.body.includes("hrv_values"));
assert(request.body.length < 2048);
const validated = parseSummary(body, Math.floor(now / 1000));
assert.equal(calculate(validated, null).reading.score, null, "Two baseline days do not meet the model's seven-day minimum");
const enoughHistory = structuredClone(input);
for (let index = 2; index <= 6; index++) {
  const date = new Date(Date.parse("2026-11-10T12:00:00Z") - index * 86400000).toISOString();
  enoughHistory.hrv_values.push(60);
  enoughHistory.hrv_dates.push(date);
  enoughHistory.rhr_values.push(61);
  enoughHistory.rhr_dates.push(date);
}
const qualified = await execute(enoughHistory);
assert(!qualified.error, qualified.error);
const qualifiedSummary = parseSummary(JSON.parse(qualified.requests[0].body), Math.floor(now / 1000));
assert.equal(qualifiedSummary.hrv.baseline_days, 7);
assert.equal(qualifiedSummary.resting_hr.baseline_days, 7);
assert.equal(calculate(qualifiedSummary, null).reading.score, 71);
checks++;

const empty = await execute({ hrv_values: [], hrv_dates: [], rhr_values: [], rhr_dates: [],
  activity_values: [], activity_starts: [], activity_ends: [], activity_sources: [] });
assert(!empty.error, empty.error);
assert.equal(JSON.parse(empty.requests[0].body).hrv, null);
assert.equal(JSON.parse(empty.requests[0].body).activity, null);
checks++;

const single = await execute({ ...input, hrv_values: 40, hrv_dates: input.hrv_dates[0] });
assert(!single.error, single.error);
assert.equal(JSON.parse(single.requests[0].body).hrv.baseline, null);
assert.equal(JSON.parse(single.requests[0].body).hrv.baseline_days, 0);
checks++;

for (const change of [
  { hrv_values: ["0.04 s", 20, 40, 90] },
  { hrv_values: ["40,0 ms", 20, 40, 90] },
]) {
  const result = await execute({ ...input, ...change });
  assert(!result.error, result.error);
  assert.equal(JSON.parse(result.requests[0].body).hrv.latest, 40);
  checks++;
}

for (const change of [
  { activity_sources: ["Synthetic Watch", "Another device"] },
  { activity_ends: [input.activity_ends[0], "2026-11-10T11:01:00Z"] },
]) {
  const result = await execute({ ...input, ...change });
  assert(!result.error, result.error);
  assert.equal(JSON.parse(result.requests[0].body).activity, null);
  assert.equal(result.notifications.length, 1);
  checks++;
}

for (const change of [
  { hrv_dates: [] }, { hrv_values: [true, 20, 40, 90] },
  { hrv_values: [-1, 20, 40, 90] }, { hrv_values: ["40 minutes", 20, 40, 90] },
  { hrv_values: [Infinity, 20, 40, 90] },
  { hrv_dates: ["2026-11-11T00:00:00Z", ...input.hrv_dates.slice(1)] },
  { hrv_dates: ["11/10/2026", ...input.hrv_dates.slice(1)] },
  { activity_sources: undefined }, { activity_values: [20000, 20000] },
  { hrv_values: Array(10001).fill(40) },
  { hrv_values: [40, 41], hrv_dates: [input.hrv_dates[0], input.hrv_dates[0]] },
]) {
  const result = await execute({ ...input, ...change });
  assert(result.error);
  assert.equal(result.requests.length, 0);
  assert.equal(result.notifications.length, 1);
  assert.equal(result.output, undefined);
  checks++;
}

for (const options of [{ status: 401 }, { status: 409 }, { status: 503 }, { offline: true },
  { receipt: { accepted: true } }, { receipt: { version: 1, score: 77 } }]) {
  const result = await execute(input, options);
  assert(result.error);
  assert.equal(result.output, undefined);
  assert.equal(result.complete, false);
  assert(!JSON.stringify(result.notifications).includes("sensitive content"));
  checks++;
}

const missing = await execute(input, { configured: false });
assert(missing.error.includes("save your upload code"));
assert.equal(missing.requests.length, 0);
checks++;
const setup = await execute(undefined, { configured: false, inApp: true });
assert(!setup.error, setup.error);
assert.equal(setup.keys.get("desk-energy-e6e2e0-upload"), "c".repeat(64));
assert.equal(setup.requests.length, 0);
checks++;
const cancelled = await execute(undefined, { configured: false, inApp: true, cancel: true });
assert.equal(cancelled.keys.size, 0);
assert.equal(cancelled.requests.length, 0);
checks++;
const stringInput = await execute(JSON.stringify(input));
assert(!stringInput.error, stringInput.error);
checks++;
const labelled = "DESK_ENERGY_V1\n" + Object.entries(input)
  .map(([key, values]) => `[${key}]\n${values.join("\n")}`).join("\n");
const textInput = await execute(labelled);
assert(!textInput.error, textInput.error);
assert.deepEqual(JSON.parse(textInput.requests[0].body), body);
checks++;
const unavailable = JSON.parse(empty.requests[0].body);
assert.equal(calculate(parseSummary(unavailable, Math.floor(now / 1000)), null).reading.score, null);
checks++;
for (const text of [labelled + "\n[hrv_values]\n1", "DESK_ENERGY_V1\n[unknown]\n1",
  "DESK_ENERGY_V1\n1", "not JSON", "DESK_ENERGY_V1\n[hrv_values]\n"]) {
  const result = await execute(text);
  assert(result.error);
  assert.equal(result.requests.length, 0);
  checks++;
}
console.log(`PASS: ${checks} Scriptable checks: actual script execution, personal daily medians, units, strict dates, activity overlap, summary-only transport, TLS, credentials and explicit failure handling`);
