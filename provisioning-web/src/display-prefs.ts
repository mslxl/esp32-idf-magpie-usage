import { Opcode, type Frame } from "./protocol";
import { crc32 } from "./account-directory";
import { ProtocolDataError } from "./errors";
export type Identity = { provider: string; user: string };
export type DisplayPrefs = { order: "original" | "custom" | "smart"; providerMode: "all" | "selected";
  providers: string[]; kinds: number; hideEmpty: boolean; query: string; hidden: Identity[]; custom: Identity[] };
/** Flag beside the kind bits: hide cards that carry no quota window, balance or error. */
export const HIDE_EMPTY = 16;
export const defaultPrefs = (): DisplayPrefs => ({ order: "original", providerMode: "all", providers: [], kinds: 15, hideEmpty: false, query: "", hidden: [], custom: [] });
export const identityKey = (value: Identity) => JSON.stringify([value.provider, value.user]);
export function encodeDisplayPrefs(prefs: DisplayPrefs): Uint8Array {
  const parts: Uint8Array[] = [];
  const u32 = (value: number) => { if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) throw new ProtocolDataError("preference length overflow"); const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, value, true); parts.push(b); };
  const text = (value: string) => { if (value.includes("\0")) throw new ProtocolDataError("preference text contains NUL"); const bytes = new TextEncoder().encode(value); u32(bytes.length); parts.push(bytes); };
  const identities = (values: Identity[]) => {
    if (new Set(values.map(identityKey)).size !== values.length || values.some(value => !value.provider)) throw new ProtocolDataError("duplicate or empty preference identity");
    u32(values.length); for (const value of values) { text(value.provider); text(value.user); }
  };
  const order = ["original", "custom", "smart"].indexOf(prefs.order), mode = ["all", "selected"].indexOf(prefs.providerMode);
  if (order < 0 || mode < 0 || !Number.isInteger(prefs.kinds) || prefs.kinds < 0 || prefs.kinds > 15) throw new ProtocolDataError("invalid display preferences");
  parts.push(new Uint8Array([2, order, mode, prefs.kinds | (prefs.hideEmpty ? HIDE_EMPTY : 0)])); text(prefs.query.trim());
  if (new Set(prefs.providers).size !== prefs.providers.length || prefs.providers.some(value => !value)) throw new ProtocolDataError("duplicate or empty provider");
  u32(prefs.providers.length); prefs.providers.forEach(text); identities(prefs.hidden); identities(prefs.custom);
  const length = parts.reduce((sum, part) => sum + part.length, 0); if (length > 0xffffffff) throw new ProtocolDataError("preference length overflow");
  const bytes = new Uint8Array(length); let offset = 0; for (const part of parts) { bytes.set(part, offset); offset += part.length; } return bytes;
}
export function decodeDisplayPrefs(bytes: Uint8Array): DisplayPrefs {
  if (!bytes.length) return defaultPrefs();
  if (bytes.length < 20 || bytes[0] !== 2 || bytes[1] > 2 || bytes[2] > 1 || bytes[3] > 31) throw new ProtocolDataError("unsupported display preference schema", "error.firmwareUpgrade");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength), decoder = new TextDecoder("utf-8", { fatal: true }); let offset = 4;
  const u32 = () => { if (offset > bytes.length - 4) throw new ProtocolDataError("preference data truncated"); const value = view.getUint32(offset, true); offset += 4; return value; };
  const text = () => { const size = u32(); if (size > bytes.length - offset) throw new ProtocolDataError("preference text out of bounds"); const value = decoder.decode(bytes.subarray(offset, offset + size)); offset += size; if (value.includes("\0")) throw new ProtocolDataError("invalid preference text"); return value; };
  const list = <T>(minimum: number, read: () => T) => { const count = u32(); if (count > Math.floor((bytes.length - offset) / minimum)) throw new ProtocolDataError("preference count out of bounds"); return Array.from({ length: count }, read); };
  const query = text(), providers = list(4, text);
  const identity = () => ({ provider: text(), user: text() });
  const hidden = list(8, identity), custom = list(8, identity);
  if (offset !== bytes.length) throw new ProtocolDataError("preference data has trailing bytes");
  const prefs: DisplayPrefs = { order: (["original", "custom", "smart"] as const)[bytes[1]], providerMode: bytes[2] ? "selected" : "all", kinds: bytes[3] & 15, hideEmpty: Boolean(bytes[3] & HIDE_EMPTY), query, providers, hidden, custom };
  encodeDisplayPrefs(prefs); return prefs;
}
export async function uploadDisplayPrefs(peer: { send(opcode: number, payload: Uint8Array): Promise<number> }, revision: number, prefs: DisplayPrefs) {
  const bytes = encodeDisplayPrefs(prefs), begin = new Uint8Array(13), view = new DataView(begin.buffer);
  begin[0] = 2; view.setUint32(1, revision, true); view.setUint32(5, bytes.length, true); view.setUint32(9, crc32(bytes), true);
  await peer.send(Opcode.BeginDisplayPrefs, begin);
  for (let offset = 0; offset < bytes.length; offset += 187) {
    const chunk = bytes.subarray(offset, offset + 187), payload = new Uint8Array(5 + chunk.length);
    payload[0] = 2; new DataView(payload.buffer).setUint32(1, offset, true); payload.set(chunk, 5);
    await peer.send(Opcode.PutDisplayPrefsChunk, payload);
  }
  await peer.send(Opcode.FinishDisplayPrefs, new Uint8Array([2]));
}
