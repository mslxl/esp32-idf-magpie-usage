import { currentLanguage, translate, type Language, type MessageKey, type Params, type Text } from "./i18n";

// Identifies the form control a validation error belongs to, e.g. "endpoint" or "ssid".
export type FieldId = "ssid" | "password" | "endpoint" | "key" | "keyMode" | "http" | "interval" | "timezone" | "layout" | "language";

/** An error whose user-facing text is a dictionary entry. */
export class LocalizedError extends Error {
  constructor(readonly key: MessageKey, readonly params: Params = {}, readonly field?: FieldId) {
    super(translate("en", key, params));
    this.name = "LocalizedError";
  }
}

/** A user-correctable input problem, optionally attached to a form field. */
export class ValidationError extends LocalizedError {
  constructor(key: MessageKey, params: Params = {}, field?: FieldId) {
    super(key, params, field);
    this.name = "ValidationError";
  }
}

/** Malformed or unsupported data received from the device. */
export class ProtocolDataError extends LocalizedError {
  constructor(readonly detail: string, key: MessageKey = "error.deviceData") {
    super(key, { detail });
    this.name = "ProtocolDataError";
  }
}

export const DeviceErrorCode = {
  Version: 1,
  Length: 2,
  Crc: 3,
  Sequence: 4,
  State: 5,
  Validation: 6,
  Busy: 7,
  Unauthorized: 8,
  UnsupportedSchema: 9,
  StaleRevision: 10,
  NoDirectory: 11,
  ResourceExhausted: 12,
} as const;

/** A NACK from the device; `code` is the one-byte error code from the protocol. */
export class DeviceError extends Error {
  constructor(readonly code: number) {
    super(`device rejected the request (code ${code})`);
    this.name = "DeviceError";
  }
}

export type TransportFailure = "unsupported" | "gatt" | "notConnected" | "timeout" | "disconnected";

export class TransportError extends Error {
  constructor(readonly kind: TransportFailure) {
    super(`transport ${kind}`);
    this.name = "TransportError";
  }
}

const deviceErrorKeys: Record<number, MessageKey> = {
  [DeviceErrorCode.Version]: "deviceError.version",
  [DeviceErrorCode.Length]: "deviceError.transfer",
  [DeviceErrorCode.Crc]: "deviceError.transfer",
  [DeviceErrorCode.Sequence]: "deviceError.transfer",
  [DeviceErrorCode.State]: "deviceError.state",
  [DeviceErrorCode.Validation]: "deviceError.validation",
  [DeviceErrorCode.Busy]: "deviceError.busy",
  [DeviceErrorCode.Unauthorized]: "deviceError.unauthorized",
  [DeviceErrorCode.UnsupportedSchema]: "deviceError.unsupportedSchema",
  [DeviceErrorCode.StaleRevision]: "deviceError.staleRevision",
  [DeviceErrorCode.NoDirectory]: "deviceError.noDirectory",
  [DeviceErrorCode.ResourceExhausted]: "deviceError.resourceExhausted",
};

const transportKeys: Record<TransportFailure, MessageKey> = {
  unsupported: "error.unsupported",
  gatt: "error.gatt",
  notConnected: "error.notConnected",
  timeout: "error.timeout",
  disconnected: "error.disconnected",
};

/** Maps any thrown value to a localizable message descriptor. */
export function describeError(error: unknown): Text {
  if (error instanceof LocalizedError) return { key: error.key, params: error.params };
  if (error instanceof DeviceError) return { key: deviceErrorKeys[error.code] ?? "deviceError.unknown", params: { code: error.code } };
  if (error instanceof TransportError) return { key: transportKeys[error.kind] };
  const name = typeof error === "object" && error !== null && "name" in error ? String(error.name) : "";
  const detail = error instanceof Error ? error.message : String(error);
  // DOMExceptions raised by navigator.bluetooth.requestDevice() and GATT operations.
  if (name === "NotFoundError") return { key: /adapter|bluetooth is (off|disabled)/i.test(detail) ? "error.bluetoothOff" : "error.noDeviceSelected" };
  if (name === "SecurityError") return { key: "error.security" };
  if (name === "NotSupportedError") return { key: "error.unsupported" };
  if (name === "NetworkError" || name === "AbortError") return { key: "error.connectFailed" };
  if (name === "InvalidStateError") return { key: "error.bluetoothOff" };
  return { key: "error.unexpected", params: { detail } };
}

export function toUserMessage(error: unknown, language: Language = currentLanguage()): string {
  const text = describeError(error);
  return translate(language, text.key, text.params);
}

/** True when the user dismissed the device chooser; shown as a gentle notice rather than an error. */
export function isChooserCancel(error: unknown): boolean {
  return describeError(error).key === "error.noDeviceSelected";
}
