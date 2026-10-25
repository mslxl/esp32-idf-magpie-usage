import { expect, it } from "vitest";
import { defaultPrefs, encodeDisplayPrefs, decodeDisplayPrefs, uploadDisplayPrefs } from "./display-prefs";
import { Opcode } from "./protocol";
import { crc32 } from "./account-directory";
import { casefold } from "./casefold";

it("normalizes query whitespace and folds the same account names as the device", () => {
  for (const query of [" strasse ", "\tSTRASSE\r\n", " strasse　", "﻿STRASSE "]) {
    const prefs = decodeDisplayPrefs(encodeDisplayPrefs({ ...defaultPrefs(), query }));
    expect(casefold("Straße").includes(casefold(prefs.query))).toBe(true);
  }
});
it("transfers a preference document larger than one transaction without treating FINISH as COMMIT", async () => {
  const prefs = { ...defaultPrefs(), hidden: Array.from({ length: 80 }, (_, i) => ({ provider: "厂商", user: `account ${i}🙂` })) };
  const bytes = encodeDisplayPrefs(prefs);
  expect(bytes.length).toBeGreaterThan(1024); expect(decodeDisplayPrefs(bytes)).toEqual(prefs);
  const calls: { opcode: number; payload: Uint8Array }[] = [];
  await uploadDisplayPrefs({ async send(opcode, payload) { calls.push({ opcode, payload }); return calls.length; } }, 7, prefs);
  expect(calls[0].opcode).toBe(Opcode.BeginDisplayPrefs);
  expect(new DataView(calls[0].payload.buffer).getUint32(9, true)).toBe(crc32(bytes));
  const chunks = calls.filter(call => call.opcode === Opcode.PutDisplayPrefsChunk);
  const combined = new Uint8Array(bytes.length);
  for (const chunk of chunks) {
    expect(chunk.payload.length).toBeLessThanOrEqual(192);
    combined.set(chunk.payload.subarray(5), new DataView(chunk.payload.buffer).getUint32(1, true));
  }
  expect(combined).toEqual(bytes);
  expect(calls.at(-1)!.opcode).toBe(Opcode.FinishDisplayPrefs);
  expect(calls.some(call => call.opcode === Opcode.Commit)).toBe(false);
});
