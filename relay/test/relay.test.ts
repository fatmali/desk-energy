import { env } from "cloudflare:workers";
import { runInDurableObject } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import worker from "../src/index";
import { calculate, epoch, parseSummary } from "../src/model";
import type { Summary } from "../src/model";

const now = Math.floor(Date.now() / 1000);
const iso = (time: number) => new Date(time * 1000).toISOString();
const day = Math.floor(now / 86400) * 86400;
const fixture = (time = now): Summary => ({
  version: 1, captured_at: iso(time), day_start: iso(day), next_day_start: iso(day + 86400),
  hrv: { latest: 40, baseline: 50, baseline_days: 7, observed_at: iso(time - 10) },
  resting_hr: { latest: 64, baseline: 60, baseline_days: 7, observed_at: iso(time - 20) },
  activity: { kilocalories: 100, observed_at: iso(time - 5) },
});

function request(method = "GET", body?: unknown, token = method === "GET" ? "a" : "b") {
  return new Request("https://relay.example/v1/energy", {
    method,
    headers: { Authorization: `Bearer ${token.repeat(64)}`, "Content-Type": "application/json" },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });
}

describe("daytime model", () => {
  it("matches the existing personal-baseline formula and rounds down", () => {
    const result = calculate(parseSummary(fixture(), now), null);
    expect(result.reading.score).toBe(77);
    expect(result.reading.coverage).toBe("limited");
    expect(result.reading.observed_at).toBe(now - 5);
    expect(result.reading.valid_until).toBe(Math.min(now + 21600, day + 86400));
  });

  it("does not invent a baseline or infer energy from activity alone", () => {
    const sample = fixture();
    sample.hrv = null;
    sample.resting_hr = { latest: 60, baseline: null, baseline_days: 0, observed_at: iso(now) };
    expect(calculate(parseSummary(sample, now), null).reading.score).toBeNull();
  });

  it("requires seven baseline days before a metric can affect the score", () => {
    const sample = fixture();
    sample.hrv!.baseline_days = 6;
    sample.resting_hr = null;
    sample.activity = null;
    expect(calculate(parseSummary(sample, now), null).reading.score).toBeNull();
    sample.hrv!.baseline_days = 7;
    const result = calculate(parseSummary(sample, now), null);
    expect(result.reading.score).toBe(80);
    expect(result.reading.observed_at).toBe(now - 10);
  });

  it("accepts a metric at the 36-hour age limit but ignores older observations", () => {
    const sample = fixture();
    sample.hrv!.observed_at = iso(now - 36 * 3600);
    sample.resting_hr = null;
    sample.activity = null;
    expect(calculate(parseSummary(sample, now), null).reading.score).toBe(80);
    sample.hrv!.observed_at = iso(now - 36 * 3600 - 1);
    const result = calculate(parseSummary(sample, now), null);
    expect(result.reading.score).toBeNull();
    expect(result.reading.observed_at).toBeNull();
  });

  it("scores only eligible metrics when the other metric has insufficient history", () => {
    const sample = fixture();
    sample.hrv!.baseline_days = 1;
    sample.hrv!.latest = 1;
    sample.activity = null;
    const result = calculate(parseSummary(sample, now), null);
    expect(result.reading.score).toBe(80);
    expect(result.reading.observed_at).toBe(now - 20);
  });

  it("drains three points per hour without recharging within a local day", () => {
    const first = calculate(fixture(), null);
    const next = fixture(now + 3600);
    next.hrv!.latest = 100;
    next.resting_hr!.latest = 60;
    next.activity = null;
    const result = calculate(next, first.session);
    expect(result.reading.score).toBe(77);
    expect(result.session!.reserve).toBe(80);
    const later = calculate(fixture(now + 7200), result.session);
    expect(later.reading.score).toBe(71);
  });

  it("resets the daytime session on the next local day", () => {
    const first = calculate(fixture(), null);
    const next = fixture(day + 86400 + 3600);
    next.day_start = iso(day + 86400);
    next.next_day_start = iso(day + 172800);
    expect(calculate(next, first.session).reading.score).toBe(77);
  });

  it("caps activity drain at thirty points", () => {
    const sample = fixture();
    sample.activity!.kilocalories = 10000;
    expect(calculate(sample, null).reading.score).toBe(50);
  });

  it.each([
    ["raw history", () => ({ ...fixture(), samples: [] })],
    ["future observation", () => ({ ...fixture(), hrv: { ...fixture().hrv, observed_at: iso(now + 1) } })],
    ["population fallback", () => ({ ...fixture(), hrv: { ...fixture().hrv, baseline_days: 0 } })],
    ["zero baseline", () => ({ ...fixture(), hrv: { ...fixture().hrv, baseline: 0 } })],
    ["negative activity", () => ({ ...fixture(), activity: { kilocalories: -1, observed_at: iso(now) } })],
    ["stale snapshot", () => fixture(now - 901)],
    ["future snapshot", () => fixture(now + 301)],
    ["wrong day", () => ({ ...fixture(), day_start: iso(now + 1) })],
    ["missing metric", () => ({ ...fixture(), hrv: undefined })],
    ["string number", () => ({ ...fixture(), hrv: { ...fixture().hrv, latest: "40" } })],
    ["infinite metric", () => ({ ...fixture(), hrv: { ...fixture().hrv, latest: Infinity } })],
    ["impossible calendar date", () => ({ ...fixture(), captured_at: "2026-02-30T12:00:00Z" })],
    ["rolled-over clock hour", () => ({ ...fixture(), captured_at: "2026-11-10T24:00:00Z" })],
  ])("rejects %s", (_label, input) => {
    expect(() => parseSummary(input(), now)).toThrow();
  });
});

describe("authenticated persistent relay", () => {
  beforeEach(async () => {
    await runInDurableObject(env.ENERGY.getByName("desk-e6e2e0"), async (_instance, state) => {
      await state.storage.deleteAll();
    });
  });

  it("separates reader/writer permissions and starts empty", async () => {
    expect((await worker.fetch(request(), env)).status).toBe(204);
    expect((await worker.fetch(request("GET", undefined, "b"), env)).status).toBe(401);
    expect((await worker.fetch(request("POST", fixture(), "a"), env)).status).toBe(401);
    expect((await worker.fetch(request("DELETE", undefined, "a"), env)).status).toBe(401);
  });

  it("persists only the reading, session and irreversible input digest", async () => {
    expect((await worker.fetch(request("POST", fixture()), env)).status).toBe(200);
    const response = await worker.fetch(request(), env);
    expect(response.headers.get("Cache-Control")).toBe("no-store");
    expect(await response.json()).toMatchObject({ version: 1, score: 77, computed_at: now });
    await runInDurableObject(env.ENERGY.getByName("desk-e6e2e0"), async (_instance, state) => {
      const saved = state.storage.kv.get<Record<string, unknown>>("latest")!;
      expect(Object.keys(saved).sort()).toEqual(["digest", "reading", "session"]);
      expect(JSON.stringify(saved)).not.toContain("baseline");
      expect(JSON.stringify(saved)).not.toContain("kilocalories");
    });
  });

  it("accepts identical retries but rejects conflicts and older inputs", async () => {
    await worker.fetch(request("POST", fixture()), env);
    expect((await worker.fetch(request("POST", fixture()), env)).status).toBe(200);
    expect((await worker.fetch(request("POST", fixture(now - 1)), env)).status).toBe(409);
    const changed = fixture();
    changed.hrv!.latest = 45;
    expect((await worker.fetch(request("POST", changed), env)).status).toBe(409);
  });

  it("keeps the newest reading when requests overlap", async () => {
    await Promise.all([0, -2, -1].map(offset => worker.fetch(request("POST", fixture(now + offset)), env)));
    const result = await worker.fetch(request(), env);
    expect(await result.json()).toMatchObject({ computed_at: now });
  });

  it("clears the score and prevents replay from resurrecting it", async () => {
    await worker.fetch(request("POST", fixture()), env);
    expect((await worker.fetch(request("DELETE"), env)).status).toBe(200);
    expect((await worker.fetch(request("POST", fixture()), env)).status).toBe(409);
    expect(await (await worker.fetch(request(), env)).json()).toMatchObject({ score: null });
  });

  it("bounds the request body and validates JSON", async () => {
    expect((await worker.fetch(request("POST", "x".repeat(2049)), env)).status).toBe(422);
    const invalid = new Request("https://relay.example/v1/energy", {
      method: "POST", headers: { Authorization: `Bearer ${"b".repeat(64)}`, "Content-Type": "application/json" },
      body: "{",
    });
    expect((await worker.fetch(invalid, env)).status).toBe(422);
    expect((await worker.fetch(request("POST", { ...fixture(), raw_history: [] }), env)).status).toBe(422);
  });

  it("never exposes summaries through other routes or query parameters", async () => {
    expect((await worker.fetch(new Request("https://relay.example/"), env)).status).toBe(404);
    expect((await worker.fetch(new Request("https://relay.example/v1/energy?token=test"), env)).status).toBe(404);
    expect(epoch(fixture().captured_at)).toBe(now);
  });
});
