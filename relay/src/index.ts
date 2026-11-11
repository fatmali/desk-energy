import { DurableObject } from "cloudflare:workers";
import { calculate, epoch, InputError, parseSummary } from "./model";
import type { Reading, Session, Summary } from "./model";

interface Stored {
  digest: string;
  reading: Reading;
  session: Session | null;
}

export class EnergyRelay extends DurableObject<Env> {
  latest(): Reading | null {
    return this.ctx.storage.kv.get<Stored>("latest")?.reading ?? null;
  }

  update(summary: Summary, digest: string): { status: number; reading?: Reading; error?: string } {
    const old = this.ctx.storage.kv.get<Stored>("latest");
    const captured = epoch(summary.captured_at);
    if (old && captured <= old.reading.computed_at) {
      return captured === old.reading.computed_at && digest === old.digest
        ? { status: 200, reading: old.reading }
        : { status: 409, error: "Older or conflicting update rejected." };
    }
    const next = calculate(summary, old?.session ?? null);
    this.ctx.storage.kv.put("latest", { ...next, digest } satisfies Stored);
    return { status: 200, reading: next.reading };
  }

  clear(now: number): Reading {
    const old = this.ctx.storage.kv.get<Stored>("latest");
    const computed = Math.max(now, (old?.reading.computed_at ?? 0) + 1);
    const reading: Reading = {
      version: 1, score: null, coverage: "unavailable", reason: "missing_sleep",
      computed_at: computed, observed_at: null, valid_until: computed + 6 * 3600,
    };
    this.ctx.storage.kv.put("latest", { reading, session: null, digest: "cleared" } satisfies Stored);
    return reading;
  }
}

const headers = {
  "Content-Type": "application/json",
  "Cache-Control": "no-store",
  "X-Content-Type-Options": "nosniff",
  "Referrer-Policy": "no-referrer",
};

function json(value: unknown, status = 200): Response {
  return new Response(JSON.stringify(value), { status, headers });
}

function authorized(request: Request, secret: string): boolean {
  if (!/^[a-f0-9]{64}$/.test(secret ?? "")) throw new Error("Missing relay secret.");
  const token = request.headers.get("Authorization");
  if (!token || !/^Bearer [a-f0-9]{64}$/.test(token)) return false;
  const encoder = new TextEncoder();
  return crypto.subtle.timingSafeEqual(encoder.encode(token.slice(7)), encoder.encode(secret));
}

async function boundedBody(request: Request): Promise<string> {
  const maxBytes = 2048;
  const declared = request.headers.get("Content-Length");
  if (declared !== null && (!/^\d+$/.test(declared) || Number(declared) > maxBytes)) {
    throw new InputError("Summary exceeds 2048 bytes.");
  }
  if (!request.body) throw new InputError("Missing summary.");
  const reader = request.body.getReader();
  const chunks: Uint8Array[] = [];
  let length = 0;
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      length += value.byteLength;
      if (length > maxBytes) {
        await reader.cancel();
        throw new InputError("Summary exceeds 2048 bytes.");
      }
      chunks.push(value);
    }
  } finally {
    reader.releaseLock();
  }
  const bytes = new Uint8Array(length);
  let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength; }
  try { return new TextDecoder("utf-8", { fatal: true, ignoreBOM: false }).decode(bytes); }
  catch { throw new InputError("Invalid UTF-8 summary."); }
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      const url = new URL(request.url);
      if (url.pathname !== "/v1/energy" || url.search) return json({ error: "Not found." }, 404);
      if (!["GET", "POST", "DELETE"].includes(request.method)) return json({ error: "Method not allowed." }, 405);
      if (!authorized(request, request.method === "GET" ? env.READ_TOKEN : env.WRITE_TOKEN)) {
        return json({ error: "Unauthorized." }, 401);
      }
      const device = env.ENERGY.getByName("desk-e6e2e0");
      if (request.method === "GET") {
        const reading = await device.latest();
        return reading ? json(reading) : new Response(null, { status: 204, headers });
      }
      const now = Math.floor(Date.now() / 1000);
      if (request.method === "DELETE") return json(await device.clear(now));
      if (request.headers.get("Content-Type")?.split(";")[0]?.trim() !== "application/json") {
        return json({ error: "Use application/json." }, 415);
      }
      const text = await boundedBody(request);
      let input: unknown;
      try { input = JSON.parse(text); }
      catch { throw new InputError("Invalid JSON summary."); }
      const summary = parseSummary(input, now);
      const hash = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(JSON.stringify(summary)));
      const digest = Array.from(new Uint8Array(hash), byte => byte.toString(16).padStart(2, "0")).join("");
      const result = await device.update(summary, digest);
      return result.reading ? json(result.reading, result.status) : json({ error: result.error }, result.status);
    } catch (error) {
      if (error instanceof InputError) return json({ error: error.message }, 422);
      console.error(JSON.stringify({ event: "relay_failed", type: error instanceof Error ? error.name : "unknown" }));
      return json({ error: "Relay unavailable; retry later." }, 503);
    }
  },
} satisfies ExportedHandler<Env>;
