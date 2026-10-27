import { afterEach, describe, expect, it, vi } from "vitest";
import { DeviceError } from "./errors";
import { Note4Transport, STATUS_UUID } from "./transport";
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

describe("large notification frames", () => {
  it("reassembles fragments of any size up to the frame maximum", () => {
    const transport = new Note4Transport();
    const listener = vi.fn();
    transport.onEvent(listener);
    const payload = Uint8Array.from({ length: 250 }, (_, i) => i & 255);
    const parts = [payload.subarray(0, 192), payload.subarray(192, 200), payload.subarray(200)];
    parts.forEach((part, sequence) => {
      const bytes = encodeFrame({ opcode: Opcode.AccountDirectoryChunk, requestId: 3, slot: 0, sequence, totalSequences: parts.length, payload: part });
      (transport as unknown as { handleNotification(event: unknown): void }).handleNotification({ target: { value: new DataView(bytes.buffer as ArrayBuffer) } });
    });
    expect(listener).toHaveBeenCalledTimes(1);
    expect(listener.mock.calls[0][0].payload).toEqual(payload);
  });

  it("keeps outgoing writes at eight payload bytes", async () => {
    const transport = new Note4Transport();
    const sizes: number[] = [];
    const writeValueWithResponse = vi.fn(async (value: ArrayBuffer) => {
      const frame = decodeFrame(new Uint8Array(value));
      sizes.push(frame.payload.length);
      if (frame.sequence === frame.totalSequences - 1) ack(transport, Opcode.Ack, frame.requestId, 0);
    });
    Object.assign(transport, { connected: true, control: { writeValueWithResponse }, data: { writeValueWithResponse } });
    await transport.send(Opcode.SetWebSettings, new Uint8Array(20));
    expect(sizes).toEqual([8, 8, 4]);
  });
});

function ack(transport: Note4Transport, opcode: number, requestId: number, code: number) {
  const bytes = encodeFrame({ opcode, requestId, slot: 0, sequence: 0, totalSequences: 1, payload: new Uint8Array([code]) });
  (transport as unknown as { handleNotification(event: unknown): void }).handleNotification({ target: { value: new DataView(bytes.buffer as ArrayBuffer) } });
}

function fakeDevice(options: { failNotifications?: boolean } = {}) {
  const statusListeners: unknown[] = [], deviceListeners: Array<() => void> = [];
  const status = {
    addEventListener: vi.fn((_: string, listener: unknown) => { statusListeners.push(listener); }),
    removeEventListener: vi.fn((_: string, listener: unknown) => { const at = statusListeners.indexOf(listener); if (at >= 0) statusListeners.splice(at, 1); }),
    startNotifications: vi.fn(async () => { if (options.failNotifications) throw new Error("notifications failed"); return status; }),
    writeValueWithResponse: vi.fn(async () => {}),
  };
  const write = { writeValueWithResponse: vi.fn(async () => {}) };
  const gatt = {
    connect: vi.fn(async () => ({ getPrimaryService: async () => ({
      getCharacteristic: async (uuid: string) => uuid === STATUS_UUID ? status : write }) })),
    disconnect: vi.fn(),
  };
  const device = { name: "magpie-usage-abc123", gatt,
    addEventListener: vi.fn((_: string, listener: () => void) => { deviceListeners.push(listener); }),
    removeEventListener: vi.fn((_: string, listener: () => void) => { const at = deviceListeners.indexOf(listener); if (at >= 0) deviceListeners.splice(at, 1); }) };
  const requestDevice = vi.fn(async () => device);
  vi.stubGlobal("navigator", { bluetooth: { requestDevice } });
  return { device, gatt, status, statusListeners, deviceListeners, requestDevice, fireDisconnect: () => [...deviceListeners].forEach((listener) => listener()) };
}

describe("connection lifecycle", () => {
  afterEach(() => { vi.unstubAllGlobals(); });

  it("closes the GATT connection when setup fails after connecting", async () => {
    const fake = fakeDevice({ failNotifications: true });
    const transport = new Note4Transport();
    await expect(transport.connect()).rejects.toThrow("notifications failed");
    expect(fake.gatt.disconnect).toHaveBeenCalledTimes(1);
    expect(fake.statusListeners).toHaveLength(0);
    expect(transport.isConnected).toBe(false);
  });

  it("reports disconnects separately from frames and registers listeners once across reconnects", async () => {
    const fake = fakeDevice();
    const transport = new Note4Transport();
    const frames = vi.fn(), drops = vi.fn();
    transport.onEvent(frames);
    transport.onDisconnect(drops);
    expect(await transport.connect()).toBe("magpie-usage-abc123");
    fake.fireDisconnect();
    fake.fireDisconnect();
    expect(drops).toHaveBeenCalledTimes(1);
    expect(frames).not.toHaveBeenCalled();
    await transport.reconnect();
    expect(fake.requestDevice).toHaveBeenCalledTimes(1);
    expect(fake.gatt.connect).toHaveBeenCalledTimes(2);
    expect(fake.statusListeners).toHaveLength(1);
    expect(fake.deviceListeners).toHaveLength(1);
    transport.disconnect();
    expect(fake.gatt.disconnect).toHaveBeenCalledTimes(1);
    expect(drops).toHaveBeenCalledTimes(2);
  });

  it("falls back to the device chooser when reconnecting to the remembered device fails", async () => {
    const fake = fakeDevice();
    const transport = new Note4Transport();
    await transport.connect();
    fake.fireDisconnect();
    fake.gatt.connect.mockRejectedValueOnce(new DOMException("out of range", "NetworkError"));
    await transport.reconnect();
    expect(fake.requestDevice).toHaveBeenCalledTimes(2);
    expect(transport.isConnected).toBe(true);
  });

  it("rejects with a typed DeviceError carrying the NACK code", async () => {
    const transport = new Note4Transport();
    const writeValueWithResponse = vi.fn(async (value: ArrayBuffer) => { ack(transport, Opcode.Nack, decodeFrame(new Uint8Array(value)).requestId, 10); });
    Object.assign(transport, { connected: true, control: { writeValueWithResponse }, data: { writeValueWithResponse } });
    const error = await transport.send(Opcode.Begin).catch((reason: unknown) => reason);
    expect(error).toBeInstanceOf(DeviceError);
    expect((error as DeviceError).code).toBe(10);
    expect(writeValueWithResponse).toHaveBeenCalledTimes(1);
  });
});
