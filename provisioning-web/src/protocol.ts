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
  SetMagpie: 7,
  GetMagpie: 8,
  ExitSetup: 9,
  GetWebSettings: 10,
  SetWebSettings: 11,
  OpenAccountDirectory: 12,
  ReadAccountDirectory: 13,
  BeginDisplayPrefs: 14,
  PutDisplayPrefsChunk: 15,
  FinishDisplayPrefs: 16,
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
  MagpieStatus: 0x8a,
  WebSettings: 0x8b,
  AccountDirectory: 0x8c,
  AccountDirectoryChunk: 0x8d,
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

export const MAX_ENDPOINT_BYTES = 512;
export const MAX_KEY_BYTES = 256;
export type MagpieMetadata = { endpoint: string; keyPresent: boolean };
export type MagpieSettings = MagpieMetadata & { key: string; keyMode: PasswordModeValue };

export function validateEndpoint(endpoint: string): void {
  if (/[\x00-\x20\x7f]/.test(endpoint) || !/^https?:\/\/[^/?#@\\\s]+(?:\/[^?#@\\\s]*)?$/.test(endpoint)) throw new Error("请输入有效的 http/https Magpie 网关地址，不要包含账号、query 或 fragment");
  const authority = endpoint.split("/")[2];
  const match = /^(?:\[[0-9a-fA-F:.]+\]|[a-zA-Z0-9.-]+)(?::([0-9]+))?$/.exec(authority);
  if (!match || (match[1] !== undefined && (Number(match[1]) < 1 || Number(match[1]) > 65535))) throw new Error("Magpie 网关主机或端口无效");
}

export function encodeSetMagpiePayload(value: MagpieSettings, savedEndpoint: string): Uint8Array {
  const endpoint = encodeUtf8(value.endpoint, MAX_ENDPOINT_BYTES, "endpoint");
  if (value.endpoint) validateEndpoint(value.endpoint);
  if (![PasswordMode.Unchanged, PasswordMode.Set, PasswordMode.Clear].includes(value.keyMode)) throw new Error("Key 处理方式无效");
  if (value.keyMode === PasswordMode.Unchanged && value.endpoint !== savedEndpoint) throw new Error("更换 endpoint 后请重新设置或清除 Key");
  const key = value.keyMode === PasswordMode.Set ? encodeUtf8(value.key, MAX_KEY_BYTES, "Key") : new Uint8Array();
  if (value.keyMode === PasswordMode.Set && (!key.length || !/^[\x21-\x7e]+$/.test(value.key))) throw new Error("Key 不能为空或包含空白及控制字符");
  if (!endpoint.length && (key.length || (value.keyMode === PasswordMode.Unchanged && value.keyPresent))) throw new Error("清空 endpoint 时请同时清除 Key");
  const payload = new Uint8Array(5 + endpoint.length + key.length);
  const view = new DataView(payload.buffer);
  payload[0] = value.keyMode;
  view.setUint16(1, endpoint.length, true);
  view.setUint16(3, key.length, true);
  payload.set(endpoint, 5);
  payload.set(key, 5 + endpoint.length);
  return payload;
}

export function decodeMagpieMetadata(payload: Uint8Array): MagpieMetadata {
  if (payload.length < 3) throw new Error("invalid Magpie metadata");
  const length = payload[0] | payload[1] << 8;
  if (length > MAX_ENDPOINT_BYTES || payload.length !== length + 3 || payload[2] > 1) throw new Error("invalid Magpie metadata length");
  const endpoint = new TextDecoder("utf-8", { fatal: true }).decode(payload.slice(3));
  if (endpoint) validateEndpoint(endpoint);
  return { endpoint, keyPresent: payload[2] !== 0 };
}

import { SUPPORTED_TIMEZONES } from "../../supported-timezones";
export { SUPPORTED_TIMEZONES };
export type WebMetadata = MagpieMetadata & { revision: number; allowPlainHttp: boolean;
  intervalSeconds: number; timezone: string; layout: "A" | "B"; adaptiveFont: boolean };
export type WebSettings = WebMetadata & { key: string; keyMode: PasswordModeValue };

export function encodeBegin(revision: number): Uint8Array {
  if (!Number.isInteger(revision) || revision < 0 || revision > 0xffffffff) throw new Error("无效配置版本");
  const bytes = new Uint8Array(5);
  bytes[0] = 2;
  new DataView(bytes.buffer).setUint32(1, revision, true);
  return bytes;
}

function validateWeb(value: WebMetadata): void {
  encodeBegin(value.revision);
  if (value.endpoint) validateEndpoint(value.endpoint);
  if (value.endpoint.startsWith("http://") && !value.allowPlainHttp) throw new Error("使用 HTTP 必须显式允许明文连接");
  if (!Number.isInteger(value.intervalSeconds) || value.intervalSeconds < 30 || value.intervalSeconds > 3600) throw new Error("读取间隔必须为 30～3600 秒");
  if (value.timezone && !(SUPPORTED_TIMEZONES as readonly string[]).includes(value.timezone)) throw new Error("设备不支持该 IANA 时区");
  if (value.layout !== "A" && value.layout !== "B") throw new Error("无效布局");
}

export function canonicalEndpoint(endpoint: string): string {
  if (!endpoint) return "";
  validateEndpoint(endpoint);
  if (new TextEncoder().encode(endpoint).length > MAX_ENDPOINT_BYTES) throw new Error("Web 地址过长");
  const parts = /^(https?):\/\/([^/]+)(.*)$/.exec(endpoint)!;
  const path = parts[3].replace(/\/+$/, "");
  if (path.includes("%") || path.includes("//") || path.split("/").some((segment) => segment === "." || segment === "..")) throw new Error("Web 地址路径有歧义");
  const authority = parts[2].toLowerCase().replace(/:([0-9]+)$/, (_, digits: string) => {
    const port = Number(digits);
    return port === (parts[1] === "https" ? 443 : 80) ? "" : `:${port}`;
  });
  return `${parts[1]}://${authority}${path}`;
}

export function encodeWebSettings(value: WebSettings, savedEndpoint: string): Uint8Array {
  validateWeb(value);
  const normalized = canonicalEndpoint(value.endpoint);
  const endpoint = encodeUtf8(normalized, MAX_ENDPOINT_BYTES, "Web 地址");
  const timezone = encodeUtf8(value.timezone, 63, "时区");
  if (![0, 1, 2].includes(value.keyMode)) throw new Error("无效 Web key 操作");
  if (value.keyMode === PasswordMode.Unchanged && normalized !== canonicalEndpoint(savedEndpoint)) throw new Error("更换 Web 地址后请重新设置 Web key");
  const key = value.keyMode === PasswordMode.Set ? encodeUtf8(value.key, 256, "Web key") : new Uint8Array();
  if (value.keyMode === PasswordMode.Set && (key.length < 16 || !/^[A-Za-z0-9._~-]+$/.test(value.key))) throw new Error("Web key 必须是 16～256 字节的字母、数字或 -._~");
  if (!endpoint.length && (key.length || (value.keyMode === PasswordMode.Unchanged && value.keyPresent))) throw new Error("清空 Web 地址时请同时清除 Web key");
  const bytes = new Uint8Array(14 + endpoint.length + key.length + timezone.length);
  const view = new DataView(bytes.buffer);
  bytes.set(encodeBegin(value.revision));
  bytes[5] = value.keyMode;
  bytes[6] = (value.allowPlainHttp ? 1 : 0) | (value.adaptiveFont ? 2 : 0) | (value.layout === "B" ? 4 : 0);
  view.setUint16(7, value.intervalSeconds, true);
  bytes[9] = timezone.length;
  view.setUint16(10, endpoint.length, true);
  view.setUint16(12, key.length, true);
  bytes.set(endpoint, 14);
  bytes.set(key, 14 + endpoint.length);
  bytes.set(timezone, 14 + endpoint.length + key.length);
  return bytes;
}

export function decodeWebSettings(bytes: Uint8Array): WebMetadata {
  if (bytes.length < 12 || bytes[0] !== 2 || bytes[5] > 1 || bytes[6] > 7) throw new Error("不支持 Web 设置 schema，请升级固件");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const endpointLength = view.getUint16(10, true), zoneLength = bytes[9];
  if (endpointLength > 512 || zoneLength > 63 || bytes.length !== 12 + endpointLength + zoneLength) throw new Error("无效 Web 设置长度");
  const decoder = new TextDecoder("utf-8", { fatal: true });
  const value: WebMetadata = { revision: view.getUint32(1, true), keyPresent: bytes[5] === 1,
    allowPlainHttp: Boolean(bytes[6] & 1), adaptiveFont: Boolean(bytes[6] & 2), layout: bytes[6] & 4 ? "B" : "A",
    intervalSeconds: view.getUint16(7, true), endpoint: decoder.decode(bytes.slice(12, 12 + endpointLength)),
    timezone: decoder.decode(bytes.slice(12 + endpointLength)) };
  validateWeb(value);
  return value;
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
