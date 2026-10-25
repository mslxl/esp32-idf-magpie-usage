import { expect, it } from "vitest";
import { canonicalEndpoint, decodeWebSettings, encodeWebSettings, PasswordMode } from "./protocol";

it("encodes versioned Web settings and only reads key presence", () => {
  const value = { endpoint: "https://magpie.test", key: "1234567890abcdef", keyPresent: false,
    keyMode: PasswordMode.Set, revision: 0, allowPlainHttp: false, intervalSeconds: 60,
    timezone: "UTC", layout: "A" as const, adaptiveFont: true };
  const bytes = encodeWebSettings(value, "");
  expect(Array.from(bytes.slice(0, 14))).toEqual([2, 0, 0, 0, 0, 1, 2, 60, 0, 3, 19, 0, 16, 0]);
  const metadata = new Uint8Array([2, 1, 0, 0, 0, 1, 2, 60, 0, 3, 19, 0,
    ...new TextEncoder().encode("https://magpie.testUTC")]);
  expect(decodeWebSettings(metadata)).toEqual({ endpoint: value.endpoint, keyPresent: true,
    revision: 1, allowPlainHttp: false, intervalSeconds: 60, timezone: "UTC", layout: "A", adaptiveFont: true });
  for (const patch of [{ key: "short" }, { key: "1234567890abcde!" }, { intervalSeconds: 29 },
    { timezone: "Mars/Base" }, { endpoint: "http://192.168.1.2" }]) {
    expect(() => encodeWebSettings({ ...value, ...patch }, "")).toThrow();
  }
  expect(() => encodeWebSettings({ ...value, endpoint: "http://192.168.1.2", allowPlainHttp: true }, "")).not.toThrow();
  const equivalent = encodeWebSettings({ ...value, endpoint: "https://MAGPIE.test:443/", keyMode: PasswordMode.Unchanged }, value.endpoint);
  expect(new TextDecoder().decode(equivalent.slice(14, -3))).toBe("https://magpie.test");
  expect(canonicalEndpoint("https://host:008080/proxy")).toBe("https://host:8080/proxy");
});
