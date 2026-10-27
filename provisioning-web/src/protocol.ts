import { ProtocolDataError, ValidationError, type FieldId } from "./errors";
import type { MessageKey } from "./i18n";

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

function encodeUtf8(value: string, limit: number, field: FieldId, key: MessageKey): Uint8Array {
  const bytes = new TextEncoder().encode(value);
  if (bytes.length > limit) throw new ValidationError(key, { max: limit }, field);
  return bytes;
}

export function encodeSetSlotPayload(value: SetSlotPayload): Uint8Array {
  const ssid = encodeUtf8(value.ssid, MAX_SSID_BYTES, "ssid", "validation.ssidTooLong");
  const password = value.passwordMode === PasswordMode.Set
    ? encodeUtf8(value.password ?? "", MAX_PASSWORD_BYTES, "password", "validation.passwordTooLong")
    : new Uint8Array();
  if (value.passwordMode === PasswordMode.Set && password.length === 0) throw new ValidationError("validation.passwordEmpty", {}, "password");
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
  if (/[\x00-\x20\x7f]/.test(endpoint) || !/^https?:\/\/[^/?#@\\\s]+(?:\/[^?#@\\\s]*)?$/.test(endpoint)) throw new ValidationError("validation.endpointFormat", {}, "endpoint");
  const authority = endpoint.split("/")[2];
  const match = /^(?:\[[0-9a-fA-F:.]+\]|[a-zA-Z0-9.-]+)(?::([0-9]+))?$/.exec(authority);
  if (!match || (match[1] !== undefined && (Number(match[1]) < 1 || Number(match[1]) > 65535))) throw new ValidationError("validation.endpointHost", {}, "endpoint");
}

export function encodeSetMagpiePayload(value: MagpieSettings, savedEndpoint: string): Uint8Array {
  const endpoint = encodeUtf8(value.endpoint, MAX_ENDPOINT_BYTES, "endpoint", "validation.endpointTooLong");
  if (value.endpoint) validateEndpoint(value.endpoint);
  if (![PasswordMode.Unchanged, PasswordMode.Set, PasswordMode.Clear].includes(value.keyMode)) throw new ValidationError("validation.keyMode", {}, "keyMode");
  if (value.keyMode === PasswordMode.Unchanged && value.endpoint !== savedEndpoint) throw new ValidationError("validation.keyAfterEndpointChange", {}, "keyMode");
  const key = value.keyMode === PasswordMode.Set ? encodeUtf8(value.key, MAX_KEY_BYTES, "key", "validation.keyTooLong") : new Uint8Array();
  if (value.keyMode === PasswordMode.Set && (!key.length || !/^[\x21-\x7e]+$/.test(value.key))) throw new ValidationError("validation.legacyKeyFormat", {}, "key");
  if (!endpoint.length && (key.length || (value.keyMode === PasswordMode.Unchanged && value.keyPresent))) throw new ValidationError("validation.keyClearWithEndpoint", {}, "keyMode");
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
  if (payload.length < 3) throw new ProtocolDataError("invalid Magpie metadata");
  const length = payload[0] | payload[1] << 8;
  if (length > MAX_ENDPOINT_BYTES || payload.length !== length + 3 || payload[2] > 1) throw new ProtocolDataError("invalid Magpie metadata length");
  const endpoint = new TextDecoder("utf-8", { fatal: true }).decode(payload.slice(3));
  if (endpoint) validateEndpoint(endpoint);
  return { endpoint, keyPresent: payload[2] !== 0 };
}

import { SUPPORTED_TIMEZONES } from "../../supported-timezones";
export { SUPPORTED_TIMEZONES };
/** Language of the text rendered on the device screen (web settings flag bit 3). */
export type DeviceLanguage = "en" | "zh";
export type WebMetadata = MagpieMetadata & { revision: number; allowPlainHttp: boolean;
  intervalSeconds: number; timezone: string; layout: "A" | "B"; adaptiveFont: boolean; language: DeviceLanguage };

export const WebFlag = { AllowPlainHttp: 1, AdaptiveFont: 2, LayoutB: 4, Chinese: 8 } as const;
const KNOWN_WEB_FLAGS = 15;
export type WebSettings = WebMetadata & { key: string; keyMode: PasswordModeValue };

export function encodeBegin(revision: number): Uint8Array {
  if (!Number.isInteger(revision) || revision < 0 || revision > 0xffffffff) throw new ValidationError("validation.revision");
  const bytes = new Uint8Array(5);
  bytes[0] = 2;
  new DataView(bytes.buffer).setUint32(1, revision, true);
  return bytes;
}

function validateWeb(value: WebMetadata): void {
  encodeBegin(value.revision);
  if (value.endpoint) validateEndpoint(value.endpoint);
  if (value.endpoint.startsWith("http://") && !value.allowPlainHttp) throw new ValidationError("validation.httpNotAllowed", {}, "http");
  if (!Number.isInteger(value.intervalSeconds) || value.intervalSeconds < 30 || value.intervalSeconds > 3600) throw new ValidationError("validation.interval", {}, "interval");
  if (value.timezone && !(SUPPORTED_TIMEZONES as readonly string[]).includes(value.timezone)) throw new ValidationError("validation.timezone", {}, "timezone");
  if (value.layout !== "A" && value.layout !== "B") throw new ValidationError("validation.layout", {}, "layout");
  if (value.language !== "en" && value.language !== "zh") throw new ValidationError("validation.language", {}, "language");
}

export function canonicalEndpoint(endpoint: string): string {
  if (!endpoint) return "";
  validateEndpoint(endpoint);
  if (new TextEncoder().encode(endpoint).length > MAX_ENDPOINT_BYTES) throw new ValidationError("validation.endpointTooLong", { max: MAX_ENDPOINT_BYTES }, "endpoint");
  const parts = /^(https?):\/\/([^/]+)(.*)$/.exec(endpoint)!;
  const path = parts[3].replace(/\/+$/, "");
  if (path.includes("%") || path.includes("//") || path.split("/").some((segment) => segment === "." || segment === "..")) throw new ValidationError("validation.endpointPath", {}, "endpoint");
  const authority = parts[2].toLowerCase().replace(/:([0-9]+)$/, (_, digits: string) => {
    const port = Number(digits);
    return port === (parts[1] === "https" ? 443 : 80) ? "" : `:${port}`;
  });
  return `${parts[1]}://${authority}${path}`;
}

export function encodeWebSettings(value: WebSettings, savedEndpoint: string): Uint8Array {
  validateWeb(value);
  const normalized = canonicalEndpoint(value.endpoint);
  const endpoint = encodeUtf8(normalized, MAX_ENDPOINT_BYTES, "endpoint", "validation.endpointTooLong");
  const timezone = encodeUtf8(value.timezone, 63, "timezone", "validation.timezone");
  if (![0, 1, 2].includes(value.keyMode)) throw new ValidationError("validation.keyMode", {}, "keyMode");
  if (value.keyMode === PasswordMode.Unchanged && normalized !== canonicalEndpoint(savedEndpoint)) throw new ValidationError("validation.keyAfterEndpointChange", {}, "keyMode");
  const key = value.keyMode === PasswordMode.Set ? encodeUtf8(value.key, 256, "key", "validation.keyFormat") : new Uint8Array();
  if (value.keyMode === PasswordMode.Set && (key.length < 16 || !/^[A-Za-z0-9._~-]+$/.test(value.key))) throw new ValidationError("validation.keyFormat", {}, "key");
  if (!endpoint.length && (key.length || (value.keyMode === PasswordMode.Unchanged && value.keyPresent))) throw new ValidationError("validation.keyClearWithEndpoint", {}, "keyMode");
  const bytes = new Uint8Array(14 + endpoint.length + key.length + timezone.length);
  const view = new DataView(bytes.buffer);
  bytes.set(encodeBegin(value.revision));
  bytes[5] = value.keyMode;
  bytes[6] = (value.allowPlainHttp ? WebFlag.AllowPlainHttp : 0) | (value.adaptiveFont ? WebFlag.AdaptiveFont : 0) |
    (value.layout === "B" ? WebFlag.LayoutB : 0) | (value.language === "zh" ? WebFlag.Chinese : 0);
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
  if (bytes.length < 12 || bytes[0] !== 2 || bytes[5] > 1 || bytes[6] > KNOWN_WEB_FLAGS) {
    throw new ProtocolDataError("unsupported web settings schema", "error.firmwareUpgrade");
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const endpointLength = view.getUint16(10, true), zoneLength = bytes[9];
  if (endpointLength > 512 || zoneLength > 63 || bytes.length !== 12 + endpointLength + zoneLength) throw new ProtocolDataError("invalid web settings length");
  const decoder = new TextDecoder("utf-8", { fatal: true });
  const value: WebMetadata = { revision: view.getUint32(1, true), keyPresent: bytes[5] === 1,
    allowPlainHttp: Boolean(bytes[6] & WebFlag.AllowPlainHttp), adaptiveFont: Boolean(bytes[6] & WebFlag.AdaptiveFont),
    layout: bytes[6] & WebFlag.LayoutB ? "B" : "A", language: bytes[6] & WebFlag.Chinese ? "zh" : "en",
    intervalSeconds: view.getUint16(7, true), endpoint: decoder.decode(bytes.slice(12, 12 + endpointLength)),
    timezone: decoder.decode(bytes.slice(12 + endpointLength)) };
  validateWeb(value);
  return value;
}

export function decodeStatusPayload(payload: Uint8Array): DeviceStatus {
  if (payload.length < 2) throw new ProtocolDataError("invalid STATUS payload");
  const state = payload[0];
  const configuredMask = payload[1];
  const slots: DeviceSlot[] = [];
  let offset = 2;
  for (let index = 0; index < MAX_SLOTS; index += 1) {
    if (offset >= payload.length) throw new ProtocolDataError("missing status slot");
    const ssidLength = payload[offset++];
    if (ssidLength > MAX_SSID_BYTES || offset + ssidLength + 1 > payload.length) throw new ProtocolDataError("invalid status SSID");
    const ssid = new TextDecoder().decode(payload.slice(offset, offset + ssidLength));
    offset += ssidLength;
    const passwordPresent = payload[offset++] !== 0;
    slots.push({ ssid, passwordPresent });
  }
  if (offset !== payload.length) throw new ProtocolDataError("unexpected status data");
  return { state, configuredMask, slots };
}

export type ScanResult = { ssid: string; rssi: number; channel: number; secure: boolean };

export function decodeScanResult(payload: Uint8Array): ScanResult {
  const length = payload[0];
  if (!length || payload.length !== length + 4) throw new ProtocolDataError("invalid scan result");
  const rawRssi = payload[length + 1];
  return {
    ssid: new TextDecoder().decode(payload.slice(1, length + 1)),
    rssi: rawRssi > 127 ? rawRssi - 256 : rawRssi,
    channel: payload[length + 2],
    secure: payload[length + 3] !== 0,
  };
}
