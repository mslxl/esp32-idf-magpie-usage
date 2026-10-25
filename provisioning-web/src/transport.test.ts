import { describe, expect, it, vi } from "vitest";
import { Note4Transport } from "./transport";
import { decodeFrame, encodeFrame, Opcode } from "./protocol";

// Exercise the real notification entrypoint without requiring a physical Bluetooth adapter.
function receive(transport: Note4Transport, opcode: number, sequence: number, payload: number[]) {
  const bytes = encodeFrame({ opcode, requestId: 9, slot: 0, sequence, totalSequences: 3,
    payload: new Uint8Array(payload) });
  (transport as unknown as { handleNotification(event: unknown): void }).handleNotification({
    target: { value: new DataView(bytes.buffer as ArrayBuffer) },
  });
}

describe("outgoing transactions", () => {
  it("serializes complete requests so fragments cannot interleave", async () => {
    const transport = new Note4Transport();
    const requests: number[] = [];
    const sequences: number[] = [];
    const writeValueWithResponse = vi.fn(async (value: ArrayBuffer) => {
      const frame = decodeFrame(new Uint8Array(value));
      requests.push(frame.requestId);
      sequences.push(frame.sequence);
      await Promise.resolve();
      if (frame.sequence === frame.totalSequences - 1) {
        const ack = encodeFrame({ opcode: Opcode.Ack, requestId: frame.requestId, slot: 0,
          sequence: 0, totalSequences: 1, payload: new Uint8Array([0]) });
        (transport as unknown as { handleNotification(event: unknown): void }).handleNotification({
          target: { value: new DataView(ack.buffer as ArrayBuffer) },
        });
      }
    });
    Object.assign(transport, { connected: true,
      control: { writeValueWithResponse }, data: { writeValueWithResponse } });
    await Promise.all([
      transport.send(Opcode.SetSlot, new Uint8Array(24)),
      transport.send(Opcode.Scan),
    ]);
    expect(requests).toEqual([1, 1, 1, 2]);
    expect(sequences).toEqual([0, 1, 2, 0]);
  });
});

describe("notification transactions", () => {
  it("waits for every chunk and keeps opcodes separate", () => {
    const transport = new Note4Transport();
    const listener = vi.fn();
    transport.onEvent(listener);
    receive(transport, Opcode.Status, 0, [1]);
    expect(listener).not.toHaveBeenCalled();
    receive(transport, Opcode.ScanResult, 0, [7]);
    receive(transport, Opcode.Status, 2, [3]);
    expect(listener).not.toHaveBeenCalled();
    receive(transport, Opcode.Status, 1, [2]);
    expect(listener).toHaveBeenCalledTimes(1);
    expect(listener.mock.calls[0][0].payload).toEqual(new Uint8Array([1, 2, 3]));
    receive(transport, Opcode.ScanResult, 1, [8]);
    receive(transport, Opcode.ScanResult, 2, [9]);
    expect(listener.mock.calls[1][0].payload).toEqual(new Uint8Array([7, 8, 9]));
  });
});
