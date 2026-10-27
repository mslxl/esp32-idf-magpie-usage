// Page state, grouped by concern, with pure update functions. App.ts owns the single mutable
// reference and the side effects (Bluetooth, rendering); everything here is unit-testable.
import { defaultPrefs, identityKey, type DisplayPrefs, type Identity } from "./display-prefs";
import type { AccountDirectory } from "./account-directory";
import { ValidationError, type FieldId } from "./errors";
import type { Language, MessageKey, Text } from "./i18n";
import {
  canonicalEndpoint,
  encodeSetSlotPayload,
  encodeWebSettings,
  MAX_SLOTS,
  PasswordMode,
  type DeviceLanguage,
  type DeviceStatus,
  type PasswordModeValue,
  type ScanResult,
  type WebMetadata,
  type WebSettings,
} from "./protocol";

export type Tab = "network" | "magpie";
export type ProgressKey = "connecting" | "reconnecting" | "scanning" | "savingNetwork" | "savingMagpie" |
  "savingPrefs" | "loadingDirectory" | "refreshing" | "exiting";
/** Inline errors keyed by field id; network fields are suffixed with the slot index ("ssid-0"). */
export type FieldErrors = Partial<Record<string, Text>>;

export type Slot = { ssid: string; password: string; passwordPresent: boolean; passwordMode: PasswordModeValue };
export type WifiProgress = { slot: number; round: number; result: "applying" | "connecting" | "connected" | "failed"; failedSlots: number[] };

export type SessionState = {
  supported: boolean;
  phase: "idle" | "connected" | "lost";
  deviceName: string;
  /** Device is in setup mode and accepts edits. */
  editing: boolean;
  /** Device reported APPLYING and has not finished. */
  applying: boolean;
  /** Shared configuration revision expected by BEGIN. */
  revision: number;
  wifi?: WifiProgress;
};
export type UiState = {
  language: Language;
  tab: Tab;
  busy?: ProgressKey;
  status?: Text;
  alert?: Text;
  exitDialog: boolean;
};
export type NetworkState = {
  slots: Slot[];
  visible: number;
  scans: ScanResult[];
  scanning: boolean;
  /** Slot that a picked scan result fills; the last focused SSID field. */
  target?: number;
  dirty: boolean;
  errors: FieldErrors;
};
export type MagpieDraft = Omit<WebSettings, "revision" | "intervalSeconds"> & { interval: string };
export type MagpieState = { draft: MagpieDraft; savedEndpoint: string; ready: boolean; dirty: boolean; errors: FieldErrors };
export type DirectoryState = { value?: AccountDirectory; prefs: DisplayPrefs; dirty: boolean };

export type AppState = {
  session: SessionState;
  ui: UiState;
  network: NetworkState;
  magpie: MagpieState;
  directory: DirectoryState;
};

export const emptySlot = (): Slot => ({ ssid: "", password: "", passwordPresent: false, passwordMode: PasswordMode.Clear });

export function initialMagpie(): MagpieState {
  return {
    draft: { endpoint: "", key: "", keyPresent: false, keyMode: PasswordMode.Clear, allowPlainHttp: false,
      interval: "30", timezone: "", layout: "A", adaptiveFont: true, language: "en" },
    savedEndpoint: "", ready: false, dirty: false, errors: {},
  };
}

export function initialState(language: Language, supported: boolean): AppState {
  return {
    session: { supported, phase: "idle", deviceName: "", editing: false, applying: false, revision: 0 },
    ui: { language, tab: "network", exitDialog: false, status: { key: "status.connectPrompt" } },
    network: { slots: Array.from({ length: MAX_SLOTS }, emptySlot), visible: 1, scans: [], scanning: false, dirty: false, errors: {} },
    magpie: initialMagpie(),
    directory: { prefs: defaultPrefs(), dirty: false },
  };
}

// ---------- session / ui ----------

export const isLocked = (state: AppState) => Boolean(state.ui.busy) || state.session.applying || !state.session.editing;
export const hasUnsavedChanges = (state: AppState) => state.network.dirty || state.magpie.dirty || state.directory.dirty;
export const tabDirty = (state: AppState, tab: Tab) => tab === "network" ? state.network.dirty : state.magpie.dirty || state.directory.dirty;

export function clearSecrets(state: AppState): AppState {
  return { ...state,
    network: { ...state.network, slots: state.network.slots.map((slot) => ({ ...slot, password: "" })) },
    magpie: { ...state.magpie, draft: { ...state.magpie.draft, key: "" } } };
}

/** The device left setup (or the link closed): drop drafts that can no longer be saved. */
export function stopEditing(state: AppState): AppState {
  return clearSecrets({ ...state,
    session: { ...state.session, editing: false, applying: false },
    ui: { ...state.ui, exitDialog: false },
    network: { ...state.network, dirty: false, errors: {}, scanning: false },
    magpie: { ...state.magpie, dirty: false, errors: {} },
    directory: { ...state.directory, value: undefined, dirty: false } });
}

/** Resets per-connection data after a fresh BLE connection. */
export function connected(state: AppState, deviceName: string): AppState {
  return clearSecrets({ ...state,
    session: { ...state.session, phase: "connected", deviceName, editing: false, applying: false, wifi: undefined },
    network: { ...state.network, dirty: false, errors: {}, scans: [], scanning: false },
    magpie: { ...state.magpie, ready: false, dirty: false, errors: {} },
    directory: { ...state.directory, value: undefined, dirty: false } });
}

export function disconnected(state: AppState): AppState {
  const next = stopEditing(state);
  return { ...next, session: { ...next.session, phase: next.session.phase === "idle" ? "idle" : "lost" } };
}

export function wifiEvent(state: AppState, event: { kind: "applying" } | { kind: "connecting"; slot: number; round: number } |
  { kind: "slotFailed"; slot: number } | { kind: "connected" } | { kind: "failed" }): AppState {
  const previous = state.session.wifi ?? { slot: 0, round: 1, result: "applying" as const, failedSlots: [] };
  let wifi: WifiProgress;
  switch (event.kind) {
    case "applying": wifi = { ...previous, result: "applying" }; break;
    case "connecting": wifi = { ...previous, slot: event.slot, round: event.round, result: "connecting",
      // A new round starts over; failures from the previous round are no longer relevant.
      failedSlots: event.round !== previous.round ? [] : previous.failedSlots }; break;
    case "slotFailed": wifi = { ...previous, failedSlots: previous.failedSlots.includes(event.slot) ? previous.failedSlots : [...previous.failedSlots, event.slot] }; break;
    case "connected": wifi = { ...previous, result: "connected" }; break;
    case "failed": wifi = { ...previous, result: "failed" }; break;
  }
  return { ...state, session: { ...state.session, wifi } };
}

// ---------- network ----------

export function applyDeviceStatus(network: NetworkState, status: DeviceStatus): NetworkState {
  const lastConfigured = status.slots.reduce((last, slot, index) => slot.ssid ? index : last, -1);
  return { ...network, visible: Math.max(1, lastConfigured + 1), errors: {},
    slots: status.slots.map((slot) => ({ ssid: slot.ssid, password: "", passwordPresent: slot.passwordPresent,
      passwordMode: slot.ssid ? PasswordMode.Unchanged : PasswordMode.Clear })) };
}

function withoutErrors(errors: FieldErrors, ...keys: string[]): FieldErrors {
  const next = { ...errors };
  for (const key of keys) delete next[key];
  return next;
}

export function editSlot(network: NetworkState, index: number, patch: Partial<Slot>): NetworkState {
  return { ...network, dirty: true, errors: withoutErrors(network.errors, `ssid-${index}`, `password-${index}`),
    slots: network.slots.map((slot, position) => position === index ? { ...slot, ...patch } : slot) };
}

/** Changing the SSID drops the "keep password" option unless the saved SSID is restored. */
export function editSsid(network: NetworkState, index: number, ssid: string): NetworkState {
  const previous = network.slots[index];
  const keep = ssid.length > 0 && ssid === previous.ssid && previous.passwordPresent;
  return editSlot(network, index, { ssid, password: "", passwordPresent: keep,
    passwordMode: keep ? PasswordMode.Unchanged : PasswordMode.Clear });
}

export type AddSlotCheck = { ok: true } | { ok: false; reason: MessageKey };

export function canAddSlot(network: NetworkState): AddSlotCheck {
  if (network.visible >= MAX_SLOTS) return { ok: false, reason: "network.addHintMax" };
  const complete = network.slots.slice(0, network.visible).every((slot) => {
    if (!slot.ssid) return false;
    try { encodeSetSlotPayload(slot); return true; } catch { return false; }
  });
  return complete ? { ok: true } : { ok: false, reason: "network.addHintIncomplete" };
}

export function addSlot(network: NetworkState): NetworkState {
  if (!canAddSlot(network).ok) return network;
  return { ...network, visible: network.visible + 1, dirty: true, target: network.visible };
}

/** Records a scan result, keeping the strongest signal per SSID. */
export function addScanResult(network: NetworkState, scan: ScanResult): NetworkState {
  const existing = network.scans.find((item) => item.ssid === scan.ssid);
  if (existing && existing.rssi >= scan.rssi) return network;
  return { ...network, scans: [...network.scans.filter((item) => item.ssid !== scan.ssid), scan] };
}

export const sortedScans = (scans: ScanResult[]) => [...scans].sort((a, b) => b.rssi - a.rssi || a.ssid.localeCompare(b.ssid));

/** Signal strength as 1–4 bars. */
export const signalBars = (rssi: number) => rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;

/** Slot a picked scan result goes to: the last focused SSID field, else the first empty visible slot. */
export function scanTarget(network: NetworkState): number {
  if (network.target !== undefined && network.target < network.visible) return network.target;
  const empty = network.slots.slice(0, network.visible).findIndex((slot) => !slot.ssid);
  return empty >= 0 ? empty : network.visible - 1;
}

export function pickScan(network: NetworkState, ssid: string): NetworkState {
  const index = scanTarget(network);
  return { ...editSsid(network, index, ssid), target: index };
}

export function validateNetwork(network: NetworkState): FieldErrors {
  const errors: FieldErrors = {};
  const visible = network.slots.slice(0, network.visible);
  if (!network.slots.some((slot) => slot.ssid.length > 0)) errors["ssid-0"] = { key: "validation.networkRequired" };
  const seen = new Set<string>();
  visible.forEach((slot, index) => {
    if (slot.ssid && seen.has(slot.ssid)) errors[`ssid-${index}`] = { key: "validation.duplicateSsid" };
    seen.add(slot.ssid);
    try {
      encodeSetSlotPayload(slot);
    } catch (error) {
      if (!(error instanceof ValidationError)) throw error;
      errors[`${error.field ?? "ssid"}-${index}`] ??= { key: error.key, params: error.params };
    }
  });
  return errors;
}

/** After a successful save: new passwords become "saved" and are cleared from the form. */
export function networkSaved(network: NetworkState): NetworkState {
  return { ...network, dirty: false, errors: {}, slots: network.slots.map((slot) => ({ ...slot, password: "",
    passwordPresent: slot.ssid.length > 0 && (slot.passwordMode === PasswordMode.Set ||
      (slot.passwordMode === PasswordMode.Unchanged && slot.passwordPresent)),
    passwordMode: slot.ssid ? PasswordMode.Unchanged : PasswordMode.Clear })) };
}

// ---------- magpie ----------

export function magpieFromDevice(magpie: MagpieState, metadata: WebMetadata, browserZone: string): MagpieState {
  const draft: MagpieDraft = magpie.dirty ? magpie.draft : {
    endpoint: metadata.endpoint, key: "", keyPresent: metadata.keyPresent,
    keyMode: metadata.keyPresent ? PasswordMode.Unchanged : PasswordMode.Clear,
    allowPlainHttp: metadata.allowPlainHttp, interval: String(metadata.intervalSeconds),
    timezone: metadata.timezone || browserZone, layout: metadata.layout, adaptiveFont: metadata.adaptiveFont,
    // Devices without the language bit report English; there is no separate "unset" value.
    language: metadata.language,
  };
  return { ...magpie, draft, savedEndpoint: metadata.endpoint, ready: true, errors: magpie.dirty ? magpie.errors : {} };
}

const magpieFieldOf: Partial<Record<keyof MagpieDraft, FieldId>> = { endpoint: "endpoint", key: "key", keyMode: "keyMode",
  allowPlainHttp: "http", interval: "interval", timezone: "timezone", layout: "layout", language: "language" };

export function editMagpie(magpie: MagpieState, patch: Partial<MagpieDraft>): MagpieState {
  const cleared = Object.keys(patch).map((name) => magpieFieldOf[name as keyof MagpieDraft]).filter(Boolean) as string[];
  // Key handling errors depend on the endpoint and vice versa.
  if (cleared.includes("endpoint") || cleared.includes("keyMode")) cleared.push("keyMode", "key", "http");
  return { ...magpie, dirty: true, draft: { ...magpie.draft, ...patch }, errors: withoutErrors(magpie.errors, ...cleared) };
}

/** Parses the raw interval text; undefined unless it is a whole number from 30 to 3600. */
export function parseInterval(text: string): number | undefined {
  if (!/^\s*\d+\s*$/.test(text)) return undefined;
  const value = Number(text);
  return value >= 30 && value <= 3600 ? value : undefined;
}

export type MagpieValidation = { errors: FieldErrors; payload?: Uint8Array };

/** Validates the draft and encodes it, mapping failures to inline field errors. */
export function validateMagpie(magpie: MagpieState, revision: number): MagpieValidation {
  const errors: FieldErrors = {};
  const intervalSeconds = parseInterval(magpie.draft.interval);
  if (intervalSeconds === undefined) errors.interval = { key: "validation.interval" };
  const { interval: _interval, ...rest } = magpie.draft;
  const settings: WebSettings = { ...rest, intervalSeconds: intervalSeconds ?? 30, revision };
  try {
    const payload = encodeWebSettings(settings, magpie.savedEndpoint);
    return Object.keys(errors).length ? { errors } : { errors, payload };
  } catch (error) {
    if (!(error instanceof ValidationError)) throw error;
    errors[error.field ?? "endpoint"] ??= { key: error.key, params: error.params };
    return { errors };
  }
}

export function magpieSaved(magpie: MagpieState): MagpieState {
  const draft = magpie.draft;
  const keyPresent = draft.keyMode === PasswordMode.Set || (draft.keyMode === PasswordMode.Unchanged && draft.keyPresent);
  return { ...magpie, dirty: false, errors: {}, savedEndpoint: canonicalEndpoint(draft.endpoint),
    draft: { ...draft, key: "", keyPresent, keyMode: keyPresent ? PasswordMode.Unchanged : PasswordMode.Clear,
      interval: String(parseInterval(draft.interval) ?? draft.interval).trim() } };
}

/** Saving a different Magpie instance invalidates the account directory and its preferences. */
export function endpointChanged(magpie: MagpieState): boolean {
  return canonicalEndpoint(magpie.draft.endpoint) !== canonicalEndpoint(magpie.savedEndpoint);
}

export const deviceLanguageOptions: readonly DeviceLanguage[] = ["en", "zh"];

// ---------- account directory ----------

export function directoryLoaded(directory: DirectoryState, next: AccountDirectory): DirectoryState {
  const instanceChanged = directory.value !== undefined && directory.value.instanceRevision !== next.instanceRevision;
  const replacePrefs = !directory.dirty || instanceChanged;
  return { value: next, prefs: replacePrefs ? next.prefs : directory.prefs, dirty: replacePrefs ? false : directory.dirty };
}

export function directoryGroups(directory: DirectoryState): Identity[] {
  const unique = new Map<string, Identity>();
  for (const card of directory.value?.cards ?? []) unique.set(identityKey(card), { provider: card.provider, user: card.user });
  return [...unique.values()];
}

export function customGroups(directory: DirectoryState): Identity[] {
  const saved = directory.prefs.custom, keys = new Set(saved.map(identityKey));
  return [...saved, ...directoryGroups(directory).filter((item) => !keys.has(identityKey(item)))];
}

export function missingGroups(directory: DirectoryState): Identity[] {
  const current = new Set(directoryGroups(directory).map(identityKey)), missing = new Map<string, Identity>();
  for (const item of [...directory.prefs.hidden, ...directory.prefs.custom]) {
    if (!current.has(identityKey(item))) missing.set(identityKey(item), item);
  }
  return [...missing.values()];
}

export function editPrefs(directory: DirectoryState, patch: Partial<DisplayPrefs>): DirectoryState {
  return { ...directory, prefs: { ...directory.prefs, ...patch }, dirty: true };
}

export function moveGroup(directory: DirectoryState, identity: Identity, direction: -1 | 1): DirectoryState {
  const ordered = customGroups(directory), at = ordered.findIndex((item) => identityKey(item) === identityKey(identity));
  if (at < 0 || at + direction < 0 || at + direction >= ordered.length) return directory;
  [ordered[at], ordered[at + direction]] = [ordered[at + direction], ordered[at]];
  return editPrefs(directory, { custom: ordered });
}

export function toggleHidden(directory: DirectoryState, identity: Identity): DirectoryState {
  const key = identityKey(identity), hidden = directory.prefs.hidden;
  return editPrefs(directory, { hidden: hidden.some((item) => identityKey(item) === key)
    ? hidden.filter((item) => identityKey(item) !== key)
    : [...hidden, { provider: identity.provider, user: identity.user }] });
}

export function cleanupMissing(directory: DirectoryState): DirectoryState {
  const keys = new Set(directoryGroups(directory).map(identityKey));
  return editPrefs(directory, { hidden: directory.prefs.hidden.filter((item) => keys.has(identityKey(item))),
    custom: directory.prefs.custom.filter((item) => keys.has(identityKey(item))) });
}

/** Preferences as uploaded: the custom order includes groups the user has not moved yet. */
export const prefsForUpload = (directory: DirectoryState): DisplayPrefs => ({ ...directory.prefs, custom: customGroups(directory) });
