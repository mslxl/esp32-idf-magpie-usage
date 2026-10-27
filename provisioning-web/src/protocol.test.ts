import { describe, expect, it } from "vitest";
import {
  decodeFrame,
  decodeSetSlotPayload,
  encodeFrame,
  encodeSetSlotPayload,
  Opcode,
  PasswordMode,
  splitPayload,
} from "./protocol";

describe("NOTE4 BLE protocol", () => {
  it("round trips a frame and rejects a changed CRC", () => {
    const encoded = encodeFrame({
      opcode: Opcode.SetSlot,
      requestId: 0x1234,
      slot: 2,
      sequence: 0,
      totalSequences: 1,
      payload: new Uint8Array([1, 2, 0xa5, 0xff]),
    });
    expect(decodeFrame(encoded)).toMatchObject({
      opcode: Opcode.SetSlot,
      requestId: 0x1234,
      slot: 2,
      payload: new Uint8Array([1, 2, 0xa5, 0xff]),
    });
    encoded[encoded.length - 1] ^= 1;
    expect(() => decodeFrame(encoded)).toThrow("invalid crc");
  });

  it("splits an empty and a multi-chunk payload deterministically", () => {
    expect(splitPayload(new Uint8Array())).toHaveLength(1);
    expect(splitPayload(new Uint8Array(193))).toHaveLength(2);
  });

  it.each([
    [PasswordMode.Unchanged, undefined],
    [PasswordMode.Set, "secret"],
    [PasswordMode.Clear, undefined],
  ])("round trips password mode %s", (passwordMode, password) => {
    const encoded = encodeSetSlotPayload({ ssid: "home", passwordMode, password });
    expect(decodeSetSlotPayload(encoded)).toEqual({ ssid: "home", passwordMode, ...(password ? { password } : {}) });
  });

  it("rejects a password set operation without a password", () => {
    expect(() => encodeSetSlotPayload({ ssid: "home", passwordMode: PasswordMode.Set }))
      .toThrow(expect.objectContaining({ key: "validation.passwordEmpty", field: "password" }));
  });

  it("rejects an enabled slot without an SSID", () => {
    const payload = encodeSetSlotPayload({ ssid: "", passwordMode: PasswordMode.Clear });
    payload[0] = 1;
    expect(() => decodeSetSlotPayload(payload)).toThrow("enabled slot has no SSID");
  });
});
