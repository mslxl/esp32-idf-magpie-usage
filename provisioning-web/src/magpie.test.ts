import { expect, it } from "vitest";
import { decodeMagpieMetadata, encodeSetMagpiePayload, PasswordMode, splitPayload } from "./protocol";

it("encodes Magpie settings with bounded lengths and preserves key semantics", () => {
  const value = { endpoint: "https://magpie.test/proxy", key: "test-key", keyPresent: false, keyMode: PasswordMode.Set };
  const bytes = encodeSetMagpiePayload(value, "");
  expect(Array.from(bytes.slice(0, 5))).toEqual([1, 25, 0, 8, 0]);
  expect(new TextDecoder().decode(bytes.slice(5))).toBe(value.endpoint + value.key);
  expect(() => encodeSetMagpiePayload({ ...value, keyMode: PasswordMode.Unchanged }, "https://other.test")).toThrow();
  expect(() => encodeSetMagpiePayload({ ...value, key: "bad key" }, "")).toThrow();
  expect(() => encodeSetMagpiePayload({ ...value, key: "k".repeat(257) }, "")).toThrow();
  expect(() => encodeSetMagpiePayload({ ...value, endpoint: "" }, "")).toThrow();
  for (const endpoint of ["https://", "ftp://host", "http://user@host", "http://host?a=b", "http://host#x", "http://host:0", "http://host:65536", "http://host:", "http://host/path with space"]) {
    expect(() => encodeSetMagpiePayload({ ...value, endpoint }, "")).toThrow();
  }
  const maximum = encodeSetMagpiePayload({ ...value, endpoint: "https://host/" + "a".repeat(499), key: "k".repeat(256) }, "");
  expect(maximum.length).toBe(773);
  expect(splitPayload(maximum, 8).length).toBe(97);
  expect(Array.from(encodeSetMagpiePayload({ ...value, endpoint: "", key: "", keyMode: PasswordMode.Clear }, value.endpoint))).toEqual([2, 0, 0, 0, 0]);
});

it("decodes endpoint and key presence without a key field", () => {
  const endpoint = new TextEncoder().encode("http://192.168.1.2:8080/proxy");
  const payload = new Uint8Array([endpoint.length, 0, 1, ...endpoint]);
  expect(decodeMagpieMetadata(payload)).toEqual({ endpoint: "http://192.168.1.2:8080/proxy", keyPresent: true });
  expect(() => decodeMagpieMetadata(payload.slice(0, -1))).toThrow();
});
