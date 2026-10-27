import { Opcode, type Frame } from "./protocol";
import { decodeDisplayPrefs, type DisplayPrefs } from "./display-prefs";
import { ProtocolDataError, TransportError } from "./errors";
export type DirectoryCard = { group: number; provider: string; user: string; name: string;
  alias: string; seat: string; from: string; kind: string; plan: string };
export type AccountDirectory = { token: number; generation: number; settingsRevision: number;
  instanceRevision: number; groups: number; cards: DirectoryCard[]; prefs: DisplayPrefs };
type Peer = { send(opcode: number, payload: Uint8Array): Promise<number>;
  onEvent(listener: (frame: Frame) => void): () => void;
  onDisconnect(listener: () => void): () => void };

export function crc32(bytes: Uint8Array): number {
  const table = Array.from({ length: 256 }, (_, value) => {
    for (let i = 0; i < 8; ++i) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
    return value >>> 0;
  });
  let crc = 0xffffffff;
  for (const byte of bytes) crc = (crc >>> 8) ^ table[(crc ^ byte) & 255];
  return (~crc) >>> 0;
}
async function exchange(peer: Peer, opcode: number, payload: Uint8Array, response: number): Promise<Uint8Array> {
  let finish: (bytes: Uint8Array) => void = () => {};
  let fail: (error: Error) => void = () => {};
  const received = new Promise<Uint8Array>((resolve, reject) => { finish = resolve; fail = reject; });
  let responseId: number | undefined;
  const remove = peer.onEvent(frame => {
    if (frame.opcode === response) { responseId = frame.requestId; finish(frame.payload); }
  });
  const removeDisconnect = peer.onDisconnect(() => fail(new TransportError("disconnected")));
  const timer = setTimeout(() => fail(new TransportError("timeout")), 5000);
  void received.catch(() => undefined);
  try {
    const id = await peer.send(opcode, payload);
    const bytes = await received;
    if (responseId !== id) throw new ProtocolDataError("directory response does not match the request");
    return bytes;
  } finally { clearTimeout(timer); remove(); removeDisconnect(); }
}
export async function readAccountDirectory(peer: Peer): Promise<AccountDirectory> {
  const metadata = await exchange(peer, Opcode.OpenAccountDirectory, new Uint8Array([2]), Opcode.AccountDirectory);
  if (metadata.length !== 33 || metadata[0] !== 2) throw new ProtocolDataError("unsupported account directory schema", "error.firmwareUpgrade");
  const view = new DataView(metadata.buffer, metadata.byteOffset, metadata.byteLength);
  const token = view.getUint32(1, true), generation = view.getUint32(5, true), settingsRevision = view.getUint32(9, true);
  const instanceRevision = view.getUint32(13, true), length = view.getUint32(17, true), checksum = view.getUint32(21, true);
  const groups = view.getUint32(25, true), count = view.getUint32(29, true);
  if (length < 5 || count > Math.floor((length - 5) / 36) || groups > count) throw new ProtocolDataError("invalid account directory length");
  let document: Uint8Array;
  try { document = new Uint8Array(length); } catch { throw new ProtocolDataError("account directory too large for memory"); }
  for (let offset = 0; offset < length;) {
    const size = Math.min(183, length - offset), request = new Uint8Array(13), requestView = new DataView(request.buffer);
    request[0] = 2; requestView.setUint32(1, token, true); requestView.setUint32(5, offset, true); requestView.setUint32(9, size, true);
    const chunk = await exchange(peer, Opcode.ReadAccountDirectory, request, Opcode.AccountDirectoryChunk);
    const header = new DataView(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    if (chunk.length !== size + 9 || chunk[0] !== 2 || header.getUint32(1, true) !== token || header.getUint32(5, true) !== offset)
      throw new ProtocolDataError("account directory chunk mismatch");
    document.set(chunk.subarray(9), offset); offset += size;
  }
  if (crc32(document) !== checksum) throw new ProtocolDataError("account directory CRC mismatch");
  const data = new DataView(document.buffer), decoder = new TextDecoder("utf-8", { fatal: true });
  let offset = 5;
  if (document[0] !== 2 || data.getUint32(1, true) !== count) throw new ProtocolDataError("account directory count mismatch");
  const seen = new Map<number, string>(), identities = new Map<string, number>();
  const text = () => {
    if (offset > length - 4) throw new ProtocolDataError("account directory string missing");
    const size = data.getUint32(offset, true); offset += 4;
    if (size > length - offset) throw new ProtocolDataError("account directory string out of bounds");
    const value = decoder.decode(document.subarray(offset, offset + size)); offset += size; return value;
  };
  const cards: DirectoryCard[] = [];
  for (let i = 0; i < count; ++i) {
    if (offset > length - 4) throw new ProtocolDataError("account directory record missing");
    const group = data.getUint32(offset, true); offset += 4;
    const provider = text(), user = text(), name = text(), alias = text(), seat = text(), from = text(), kind = text(), plan = text();
    if (!provider || group >= groups) throw new ProtocolDataError("invalid account preference group");
    const identity = JSON.stringify([provider, user]);
    if ((seen.has(group) && seen.get(group) !== identity) || (identities.has(identity) && identities.get(identity) !== group))
      throw new ProtocolDataError("conflicting account identity references");
    seen.set(group, identity); identities.set(identity, group);
    cards.push({ group, provider, user, name, alias, seat, from, kind, plan });
  }
  if (offset > length - 4) throw new ProtocolDataError("account directory preference length missing");
  const prefsLength = data.getUint32(offset, true); offset += 4;
  if (prefsLength !== length - offset || seen.size !== groups) throw new ProtocolDataError("account directory has trailing data");
  const prefs = decodeDisplayPrefs(document.subarray(offset));
  return { token, generation, settingsRevision, instanceRevision, groups, cards, prefs };
}
