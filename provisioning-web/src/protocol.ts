export const PROTOCOL_VERSION = 1;
export const MAX_PAYLOAD = 192;
export const MAX_SLOTS = 5;
export const MAX_SSID_BYTES = 32;
export const MAX_PASSWORD_BYTES = 64;

export const Opcode = {
  Begin: 1,
  GetStatus: 2,
  Scan: 3,
  SetSlot: 4,
  Commit: 5,
  Abort: 6,
  Ack: 0x80,
  Nack: 0x81,
  Status: 0x82,
  ScanResult: 0x83,
  ScanDone: 0x84,
  Applying: 0x85,
  Connecting: 0x86,
  Connected: 0x87,
  ConnectionFailed: 0x88,
  Failure: 0x89,
} as const;

export const PasswordMode = {
  Unchanged: 0,
  Set: 1,
  Clear: 2,
} as const;

export type PasswordModeValue = (typeof PasswordMode)[keyof typeof PasswordMode];

export type Frame = {
  opcode: number;
  requestId: number;
  slot: number;
  sequence: number;
  totalSequences: number;
  payload: Uint8Array;
};

export type SetSlotPayload = {
  ssid: string;
  passwordMode: PasswordModeValue;
  password?: string;
};

export type DeviceSlot = {
  ssid: string;
  passwordPresent: boolean;
};

export type DeviceStatus = {
  state: number;
  configuredMask: number;
  slots: DeviceSlot[];
};

export function crc16(data: Uint8Array): number {
  let crc = 0xffff;
  for (const value of data) {
    crc ^= value;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc >>> 1) ^ (crc & 1 ? 0xa001 : 0);
    }
  }
  return crc;
}

export function encodeFrame(frame: Frame): Uint8Array {
  if (frame.payload.length > MAX_PAYLOAD) throw new Error("payload too large");
  if (frame.totalSequences < 1 || frame.sequence >= frame.totalSequences || frame.totalSequences > 255) {
    throw new Error("invalid sequence");
  }
  if (frame.requestId < 0 || frame.requestId > 0xffff || frame.slot < 0 || frame.slot > 255) {
    throw new Error("invalid frame field");
  }
  const result = new Uint8Array(12 + frame.payload.length);
  const view = new DataView(result.buffer);
  result[0] = PROTOCOL_VERSION;
  result[1] = frame.opcode;
  view.setUint16(2, frame.requestId, true);
  result[4] = frame.slot;
  result[5] = frame.sequence;
  result[6] = frame.totalSequences;
  view.setUint16(7, frame.payload.length, true);
  result.set(frame.payload, 10);
  view.setUint16(10 + frame.payload.length, crc16(result.subarray(0, 10 + frame.payload.length)), true);
  return result;
}

export function decodeFrame(bytes: Uint8Array): Frame {
  if (bytes.length < 12 || bytes[0] !== PROTOCOL_VERSION) throw new Error("invalid frame");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const payloadLength = view.getUint16(7, true);
  if (payloadLength > MAX_PAYLOAD || bytes.length !== payloadLength + 12) throw new Error("invalid length");
  if (bytes[6] === 0 || bytes[5] >= bytes[6]) throw new Error("invalid sequence");
  const expected = view.getUint16(10 + payloadLength, true);
  if (crc16(bytes.subarray(0, 10 + payloadLength)) !== expected) throw new Error("invalid crc");
  return {
    opcode: bytes[1],
    requestId: view.getUint16(2, true),
    slot: bytes[4],
    sequence: bytes[5],
    totalSequences: bytes[6],
    payload: bytes.slice(10, 10 + payloadLength),
  };
}

export function splitPayload(payload: Uint8Array, max = MAX_PAYLOAD): Uint8Array[] {
  if (max < 1 || max > MAX_PAYLOAD) throw new Error("invalid chunk size");
  const chunks: Uint8Array[] = [];
  for (let offset = 0; offset < payload.length || chunks.length === 0; offset += max) {
    chunks.push(payload.slice(offset, Math.min(offset + max, payload.length)));
  }
  return chunks;
}

function encodeUtf8(value: string, limit: number, field: string): Uint8Array {
  const bytes = new TextEncoder().encode(value);
  if (bytes.length > limit) throw new Error(`${field} too long`);
  return bytes;
}

export function encodeSetSlotPayload(value: SetSlotPayload): Uint8Array {
  const ssid = encodeUtf8(value.ssid, MAX_SSID_BYTES, "SSID");
  const password = value.passwordMode === PasswordMode.Set
    ? encodeUtf8(value.password ?? "", MAX_PASSWORD_BYTES, "password")
    : new Uint8Array();
  if (value.passwordMode === PasswordMode.Set && password.length === 0) throw new Error("password is empty");
  const payload = new Uint8Array(3 + ssid.length + (value.passwordMode === PasswordMode.Set ? 1 + password.length : 0));
  payload[0] = value.ssid.length === 0 ? 0 : 1;
  payload[1] = value.passwordMode;
  payload[2] = ssid.length;
  payload.set(ssid, 3);
  if (value.passwordMode === PasswordMode.Set) {
    payload[3 + ssid.length] = password.length;
    payload.set(password, 4 + ssid.length);
  }
  return payload;
}

export function decodeSetSlotPayload(payload: Uint8Array): SetSlotPayload {
  if (payload.length < 3) throw new Error("invalid SET_SLOT payload");
  const enabled = payload[0] !== 0;
  const mode = payload[1];
  if (mode !== PasswordMode.Unchanged && mode !== PasswordMode.Set && mode !== PasswordMode.Clear) {
    throw new Error("invalid password mode");
  }
  const ssidLength = payload[2];
  const passwordOffset = 3 + ssidLength;
  if (ssidLength > MAX_SSID_BYTES || passwordOffset > payload.length) throw new Error("invalid SSID length");
  const ssidBytes = payload.slice(3, passwordOffset);
  const ssid = new TextDecoder().decode(ssidBytes);
  if (enabled && ssidBytes.length === 0) throw new Error("enabled slot has no SSID");
  if (!enabled && ssidBytes.length !== 0) throw new Error("disabled slot has SSID");
  if (mode === PasswordMode.Set) {
    if (passwordOffset >= payload.length) throw new Error("missing password");
    const passwordLength = payload[passwordOffset];
    if (passwordLength === 0 || passwordLength > MAX_PASSWORD_BYTES || passwordOffset + 1 + passwordLength !== payload.length) {
      throw new Error("invalid password length");
    }
    return { ssid, passwordMode: PasswordMode.Set, password: new TextDecoder().decode(payload.slice(passwordOffset + 1)) };
  }
  if (passwordOffset !== payload.length) throw new Error("unexpected password");
  return { ssid, passwordMode: mode };
}

export function decodeStatusPayload(payload: Uint8Array): DeviceStatus {
  if (payload.length < 2) throw new Error("invalid STATUS payload");
  const state = payload[0];
  const configuredMask = payload[1];
  const slots: DeviceSlot[] = [];
  let offset = 2;
  for (let index = 0; index < MAX_SLOTS; index += 1) {
    if (offset >= payload.length) throw new Error("missing status slot");
    const ssidLength = payload[offset++];
    if (ssidLength > MAX_SSID_BYTES || offset + ssidLength + 1 > payload.length) throw new Error("invalid status SSID");
    const ssid = new TextDecoder().decode(payload.slice(offset, offset + ssidLength));
    offset += ssidLength;
    const passwordPresent = payload[offset++] !== 0;
    slots.push({ ssid, passwordPresent });
  }
  if (offset !== payload.length) throw new Error("unexpected status data");
  return { state, configuredMask, slots };
}
