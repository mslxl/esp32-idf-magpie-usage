import { expect, it } from "vitest";
import { readAccountDirectory } from "./account-directory";
import { Opcode, type Frame } from "./protocol";

it("reads a complete stable directory across UTF-8 byte chunks and rejects corruption", async () => {
  const strings = ["厂商", "same", "昵称🙂", "", "", "source", "unknown", ""];
  const record = (group: number) => {
    const values = [u32(group), ...strings.flatMap(text => { const bytes = new TextEncoder().encode(text); return [u32(bytes.length), bytes]; })];
    return concat(values);
  };
  const body = concat([new Uint8Array([2]), u32(30), ...Array.from({ length: 30 }, () => record(0)), u32(0)]);
  let listener: ((frame: Frame) => void) | undefined, corrupt = false;
  const transport = {
    onEvent(next: (frame: Frame) => void) { listener = next; return () => { listener = undefined; }; },
    async send(opcode: number, payload: Uint8Array) {
      const token = 42;
      const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
      let bytes: Uint8Array;
      if (opcode === Opcode.OpenAccountDirectory) {
        bytes = concat([new Uint8Array([2]), ...[token, 5, 7, 3, body.length, 0x0, 1, 30].map(u32)]);
        new DataView(bytes.buffer).setUint32(21, crc32(body), true);
      } else {
        const offset = view.getUint32(5, true), count = view.getUint32(9, true);
        expect(count).toBeLessThanOrEqual(183);
        bytes = concat([new Uint8Array([2]), u32(token), u32(offset), body.slice(offset, offset + count)]);
        if (corrupt && offset === 0) bytes[9] ^= 1;
      }
      listener!({ opcode: opcode === Opcode.OpenAccountDirectory ? Opcode.AccountDirectory : Opcode.AccountDirectoryChunk,
        payload: bytes, requestId: 1, slot: 0, sequence: 0, totalSequences: 1 });
      return 1;
    },
  };
  const directory = await readAccountDirectory(transport);
  expect(directory.cards).toHaveLength(30);
  expect(directory.cards[0]).toEqual({ group: 0, provider: "厂商", user: "same", name: "昵称🙂", alias: "", seat: "", from: "source", kind: "unknown", plan: "" });
  expect(directory.settingsRevision).toBe(7);
  expect(directory.instanceRevision).toBe(3);
  corrupt = true;
  await expect(readAccountDirectory(transport)).rejects.toThrow("CRC");
});
function u32(value: number) { const bytes = new Uint8Array(4); new DataView(bytes.buffer).setUint32(0, value, true); return bytes; }
function concat(parts: Uint8Array[]) { const bytes = new Uint8Array(parts.reduce((n, part) => n + part.length, 0)); let offset = 0; for (const part of parts) { bytes.set(part, offset); offset += part.length; } return bytes; }
// IEEE CRC-32 independent bit-at-a-time fixture encoder.
function crc32(bytes: Uint8Array) { let crc = 0xffffffff; for (const byte of bytes) { crc ^= byte; for (let i = 0; i < 8; ++i) crc = crc & 1 ? (crc >>> 1) ^ 0xedb88320 : crc >>> 1; } return (~crc) >>> 0; }
