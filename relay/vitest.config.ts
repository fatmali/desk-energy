import { cloudflareTest } from "@cloudflare/vitest-plugin";
import { defineConfig } from "vitest/config";

export default defineConfig({
  plugins: [cloudflareTest({
    wrangler: { configPath: "./wrangler.jsonc" },
    miniflare: {
      bindings: { READ_TOKEN: "a".repeat(64), WRITE_TOKEN: "b".repeat(64) },
    },
  })],
});
