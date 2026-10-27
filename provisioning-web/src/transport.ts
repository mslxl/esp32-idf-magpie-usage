import { DeviceError, TransportError } from "./errors";
import { decodeFrame, encodeFrame, Frame, Opcode, splitPayload } from "./protocol";

export const NOTE4_SERVICE_UUID = "7c2e0000-5b6e-4d54-9a4d-4e4f54453400";
export const CONTROL_UUID = "7c2e0001-5b6e-4d54-9a4d-4e4f54453400";
export const DATA_UUID = "7c2e0002-5b6e-4d54-9a4d-4e4f54453400";
export const STATUS_UUID = "7c2e0003-5b6e-4d54-9a4d-4e4f54453400";

const ACK_TIMEOUT_MS = 3000;
const MAX_RETRIES = 2;
// Outgoing writes stay at eight payload bytes so every write fits the minimum ATT MTU of 23.
const WRITE_CHUNK_BYTES = 8;

type CharacteristicEvent = Event & { target: BluetoothCharacteristic | null };
type NotificationListener = (event: CharacteristicEvent) => void;

interface BluetoothCharacteristic {
  writeValueWithResponse(value: ArrayBuffer): Promise<void>;
  startNotifications(): Promise<BluetoothCharacteristic>;
  addEventListener(type: "characteristicvaluechanged", listener: NotificationListener): void;
  removeEventListener?(type: "characteristicvaluechanged", listener: NotificationListener): void;
  value?: DataView;
}
interface BluetoothService { getCharacteristic(uuid: string): Promise<BluetoothCharacteristic>; }
interface BluetoothServer { getPrimaryService(uuid: string): Promise<BluetoothService>; }
interface BluetoothGatt {
  connected?: boolean;
  connect(): Promise<BluetoothServer>;
  disconnect?(): void;
}
interface BluetoothDevice {
  name?: string;
  gatt?: BluetoothGatt;
  addEventListener(type: "gattserverdisconnected", listener: () => void): void;
  removeEventListener?(type: "gattserverdisconnected", listener: () => void): void;
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

/** The operations the setup page needs from a device link; implemented by Note4Transport and test fakes. */
export interface Transport {
  readonly supported: boolean;
  onEvent(listener: (frame: DeviceEvent) => void): () => void;
  onDisconnect(listener: () => void): () => void;
  connect(): Promise<string>;
  reconnect(): Promise<string>;
  disconnect(): void;
  send(opcode: number, payload?: Uint8Array<ArrayBufferLike>, slot?: number): Promise<number>;
}

export class Note4Transport implements Transport {
  private device?: BluetoothDevice;
  private control?: BluetoothCharacteristic;
  private data?: BluetoothCharacteristic;
  private status?: BluetoothCharacteristic;
  private requestId = 1;
  private listeners = new Set<(frame: DeviceEvent) => void>();
  private disconnectListeners = new Set<() => void>();
  private pending = new Map<number, PendingRequest>();
  private incoming = new Map<string, IncomingTransaction>();
  private connected = false;
  private sendQueue: Promise<void> = Promise.resolve();
  // Stable references so listeners can be removed and are never registered twice.
  private readonly notificationListener: NotificationListener = (event) => this.handleNotification(event);
  private readonly disconnectListener = () => this.handleDisconnect();

  get supported(): boolean {
    return typeof navigator !== "undefined" && Boolean((navigator as BluetoothNavigator).bluetooth);
  }

  get isConnected(): boolean {
    return this.connected;
  }

  /** A device was chosen before, so reconnect() can try it without the chooser. */
  get hasDevice(): boolean {
    return Boolean(this.device?.gatt);
  }

  onEvent(listener: (frame: DeviceEvent) => void): () => void {
    this.listeners.add(listener);
    return () => { this.listeners.delete(listener); };
  }

  onDisconnect(listener: () => void): () => void {
    this.disconnectListeners.add(listener);
    return () => { this.disconnectListeners.delete(listener); };
  }

  /** Opens the browser device chooser and connects to the chosen device. */
  async connect(): Promise<string> {
    const bluetooth = (navigator as BluetoothNavigator).bluetooth;
    if (!bluetooth) throw new TransportError("unsupported");
    const device = await bluetooth.requestDevice({ filters: [{ services: [NOTE4_SERVICE_UUID] }] });
    this.adoptDevice(device);
    return this.open(device);
  }

  /** Reconnects to the remembered device; falls back to the chooser when that fails. */
  async reconnect(): Promise<string> {
    const device = this.device;
    if (device?.gatt) {
      try {
        return await this.open(device);
      } catch {
        // The device may have moved out of range or lost its bond; let the user choose again.
        this.adoptDevice(undefined);
      }
    }
    return this.connect();
  }

  /** Closes the GATT connection; the remembered device stays available for reconnect(). */
  disconnect(): void {
    const gatt = this.device?.gatt;
    this.handleDisconnect();
    try { gatt?.disconnect?.(); } catch { /* already disconnected */ }
  }

  private adoptDevice(device: BluetoothDevice | undefined): void {
    if (this.device === device) return;
    this.device?.removeEventListener?.("gattserverdisconnected", this.disconnectListener);
    this.detachStatus();
    this.device = device;
    device?.addEventListener("gattserverdisconnected", this.disconnectListener);
  }

  private detachStatus(): void {
    this.status?.removeEventListener?.("characteristicvaluechanged", this.notificationListener);
    this.status = undefined;
  }

  private async open(device: BluetoothDevice): Promise<string> {
    const gatt = device.gatt;
    if (!gatt) throw new TransportError("gatt");
    let gattConnected = false;
    try {
      const server = await gatt.connect();
      if (!server) throw new TransportError("gatt");
      gattConnected = true;
      const service = await server.getPrimaryService(NOTE4_SERVICE_UUID);
      this.control = await service.getCharacteristic(CONTROL_UUID);
      this.data = await service.getCharacteristic(DATA_UUID);
      const status = await service.getCharacteristic(STATUS_UUID);
      this.detachStatus();
      status.addEventListener("characteristicvaluechanged", this.notificationListener);
      this.status = status;
      await status.startNotifications();
      this.connected = true;
      return device.name ?? "Magpie Usage";
    } catch (error) {
      // Leave no half-open GATT connection behind when service discovery or notifications fail.
      this.control = undefined;
      this.data = undefined;
      this.detachStatus();
      if (gattConnected) try { gatt.disconnect?.(); } catch { /* ignore */ }
      throw error;
    }
  }

  send(opcode: number, payload: Uint8Array<ArrayBufferLike> = new Uint8Array(0), slot = 0): Promise<number> {
    const snapshot = new Uint8Array(payload);
    const request = this.sendQueue.then(() => this.sendRequest(opcode, snapshot, slot));
    this.sendQueue = request.then(() => undefined, () => undefined);
    return request;
  }

  private async sendRequest(opcode: number, payload: Uint8Array, slot: number): Promise<number> {
    if (!this.control || !this.data || !this.connected) throw new TransportError("notConnected");
    const requestId = this.nextRequestId();
    const chunks = splitPayload(payload, WRITE_CHUNK_BYTES);
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
        lastError = error instanceof Error ? error : new TransportError("timeout");
        if (!this.connected || attempt === MAX_RETRIES || lastError instanceof DeviceError) break;
      }
    }
    throw lastError ?? new TransportError("timeout");
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
      if (!characteristic) throw new TransportError("notConnected");
      await characteristic.writeValueWithResponse(frame.buffer as ArrayBuffer);
    }
  }

  private waitForAck(requestId: number): Promise<void> {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(requestId);
        reject(new TransportError("timeout"));
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

  // Notification payloads may be any size the frame format allows (up to 192 bytes, depending
  // on the negotiated MTU); fragments are concatenated in sequence order.
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
      pending.reject(new DeviceError(frame.payload[0] ?? 0xff));
    }
  }

  private handleDisconnect(): void {
    const wasConnected = this.connected;
    this.connected = false;
    this.control = undefined;
    this.data = undefined;
    for (const [requestId, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(new TransportError("disconnected"));
      this.pending.delete(requestId);
    }
    this.incoming.clear();
    // Failed connection attempts are reported by connect()/reconnect() instead.
    if (wasConnected) for (const listener of this.disconnectListeners) listener();
  }

  private emit(frame: DeviceEvent): void {
    for (const listener of this.listeners) listener(frame);
  }
}
