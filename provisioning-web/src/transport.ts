import { decodeFrame, encodeFrame, Frame, Opcode, splitPayload } from "./protocol";

export const NOTE4_SERVICE_UUID = "7c2e0000-5b6e-4d54-9a4d-4e4f54453400";
export const CONTROL_UUID = "7c2e0001-5b6e-4d54-9a4d-4e4f54453400";
export const DATA_UUID = "7c2e0002-5b6e-4d54-9a4d-4e4f54453400";
export const STATUS_UUID = "7c2e0003-5b6e-4d54-9a4d-4e4f54453400";

const ACK_TIMEOUT_MS = 3000;
const MAX_RETRIES = 2;

type CharacteristicEvent = Event & { target: BluetoothCharacteristic | null };

interface BluetoothCharacteristic {
  writeValueWithResponse(value: ArrayBuffer): Promise<void>;
  startNotifications(): Promise<BluetoothCharacteristic>;
  addEventListener(type: "characteristicvaluechanged", listener: (event: CharacteristicEvent) => void): void;
  value?: DataView;
}
interface BluetoothService { getCharacteristic(uuid: string): Promise<BluetoothCharacteristic>; }
interface BluetoothServer { getPrimaryService(uuid: string): Promise<BluetoothService>; }
interface BluetoothDevice {
  name?: string;
  gatt?: { connect(): Promise<BluetoothServer> };
  addEventListener(type: "gattserverdisconnected", listener: () => void): void;
}
interface BluetoothNavigator extends Navigator {
  bluetooth?: {
    requestDevice(options: { filters: Array<{ services: string[] }> }): Promise<BluetoothDevice>;
  };
}

type PendingRequest = {
  resolve: () => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
};

type IncomingTransaction = {
  totalSequences: number;
  chunks: Array<Uint8Array | undefined>;
};

export type DeviceEvent = Frame;

export class Note4Transport {
  private device?: BluetoothDevice;
  private control?: BluetoothCharacteristic;
  private data?: BluetoothCharacteristic;
  private requestId = 1;
  private listeners = new Set<(frame: DeviceEvent) => void>();
  private pending = new Map<number, PendingRequest>();
  private incoming = new Map<string, IncomingTransaction>();
  private connected = false;
  private sendQueue: Promise<void> = Promise.resolve();

  get supported(): boolean {
    return Boolean((navigator as BluetoothNavigator).bluetooth);
  }

  get isConnected(): boolean {
    return this.connected;
  }

  onEvent(listener: (frame: DeviceEvent) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  async connect(): Promise<string> {
    const bluetooth = (navigator as BluetoothNavigator).bluetooth;
    if (!bluetooth) throw new Error("当前浏览器不支持 Web Bluetooth，请使用 HTTPS 下的 Chromium 浏览器");
    this.device = await bluetooth.requestDevice({ filters: [{ services: [NOTE4_SERVICE_UUID] }] });
    this.device.addEventListener("gattserverdisconnected", () => this.handleDisconnect());
    const server = await this.device.gatt?.connect();
    if (!server) throw new Error("无法连接 Magpie Usage GATT 服务");
    const service = await server.getPrimaryService(NOTE4_SERVICE_UUID);
    this.control = await service.getCharacteristic(CONTROL_UUID);
    this.data = await service.getCharacteristic(DATA_UUID);
    const status = await service.getCharacteristic(STATUS_UUID);
    status.addEventListener("characteristicvaluechanged", (event) => this.handleNotification(event));
    await status.startNotifications();
    this.connected = true;
    return this.device.name ?? "Magpie Usage";
  }

  send(opcode: number, payload: Uint8Array<ArrayBufferLike> = new Uint8Array(0), slot = 0): Promise<number> {
    const snapshot = new Uint8Array(payload);
    const request = this.sendQueue.then(() => this.sendRequest(opcode, snapshot, slot));
    this.sendQueue = request.then(() => undefined, () => undefined);
    return request;
  }

  private async sendRequest(opcode: number, payload: Uint8Array, slot: number): Promise<number> {
    if (!this.control || !this.data || !this.connected) throw new Error("设备尚未连接");
    const requestId = this.nextRequestId();
    const chunks = splitPayload(payload, 8);
    let lastError: Error | undefined;
    for (let attempt = 0; attempt <= MAX_RETRIES; attempt += 1) {
      const acknowledgement = this.waitForAck(requestId);
      void acknowledgement.catch(() => undefined);
      try {
        await this.writeTransaction(requestId, opcode, slot, chunks);
        await acknowledgement;
        return requestId;
      } catch (error) {
        this.cancelPending(requestId);
        lastError = error instanceof Error ? error : new Error("设备未确认操作");
        if (!this.connected || attempt === MAX_RETRIES || lastError.message.startsWith("设备拒绝操作")) break;
      }
    }
    throw lastError ?? new Error("设备未确认操作");
  }

  private nextRequestId(): number {
    const requestId = this.requestId;
    this.requestId = (this.requestId + 1) & 0xffff;
    return requestId === 0 ? this.nextRequestId() : requestId;
  }

  private async writeTransaction(requestId: number, opcode: number, slot: number, chunks: Uint8Array[]): Promise<void> {
    for (let sequence = 0; sequence < chunks.length; sequence += 1) {
      const frame = encodeFrame({ opcode, requestId, slot, sequence, totalSequences: chunks.length, payload: chunks[sequence] });
      const characteristic = sequence === 0 ? this.control : this.data;
      if (!characteristic) throw new Error("设备尚未连接");
      await characteristic.writeValueWithResponse(frame.buffer as ArrayBuffer);
    }
  }

  private waitForAck(requestId: number): Promise<void> {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(requestId);
        reject(new Error("设备确认超时"));
      }, ACK_TIMEOUT_MS);
      this.pending.set(requestId, { resolve, reject, timer });
    });
  }

  private cancelPending(requestId: number): void {
    const pending = this.pending.get(requestId);
    if (!pending) return;
    clearTimeout(pending.timer);
    this.pending.delete(requestId);
  }

  private handleNotification(event: CharacteristicEvent): void {
    const value = event.target?.value;
    if (!value) return;
    try {
      const frame = decodeFrame(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
      if (frame.opcode === Opcode.Ack || frame.opcode === Opcode.Nack) {
        this.finishPending(frame);
        return;
      }
      const complete = this.collectIncoming(frame);
      if (complete) this.emit(complete);
    } catch {
      // Malformed notifications are ignored; the pending request will time out.
    }
  }

  private collectIncoming(frame: Frame): Frame | undefined {
    if (frame.totalSequences === 1) return frame;
    const key = `${frame.requestId}:${frame.opcode}:${frame.slot}`;
    const current = (frame.sequence === 0 ? undefined : this.incoming.get(key)) ?? {
      totalSequences: frame.totalSequences,
      chunks: Array.from({ length: frame.totalSequences }),
    };
    if (current.totalSequences !== frame.totalSequences || frame.sequence >= current.totalSequences) {
      this.incoming.delete(key);
      return undefined;
    }
    current.chunks[frame.sequence] = frame.payload;
    this.incoming.set(key, current);
    if (current.chunks.some((chunk) => chunk === undefined)) return undefined;
    this.incoming.delete(key);
    const payload = new Uint8Array(current.chunks.reduce((size, chunk) => size + (chunk?.length ?? 0), 0));
    let offset = 0;
    for (const chunk of current.chunks) {
      if (!chunk) return undefined;
      payload.set(chunk, offset);
      offset += chunk.length;
    }
    return { ...frame, sequence: 0, totalSequences: 1, payload };
  }

  private finishPending(frame: Frame): void {
    const pending = this.pending.get(frame.requestId);
    if (!pending) {
      this.emit(frame);
      return;
    }
    clearTimeout(pending.timer);
    this.pending.delete(frame.requestId);
    if (frame.opcode === Opcode.Ack) {
      pending.resolve();
    } else {
      const code = frame.payload[0] ?? 0xff;
      pending.reject(new Error(`设备拒绝操作（错误码 ${code}）`));
    }
  }

  private handleDisconnect(): void {
    this.connected = false;
    this.control = undefined;
    this.data = undefined;
    for (const [requestId, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(new Error("Magpie Usage 蓝牙连接已断开"));
      this.pending.delete(requestId);
    }
    this.incoming.clear();
    this.emit({ opcode: Opcode.Nack, requestId: 0, slot: 0, sequence: 0, totalSequences: 1, payload: new TextEncoder().encode("disconnected") });
  }

  private emit(frame: DeviceEvent): void {
    for (const listener of this.listeners) listener(frame);
  }
}
