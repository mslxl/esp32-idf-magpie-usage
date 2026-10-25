import { Opcode, type Frame } from "./protocol";
import { decodeDisplayPrefs, type DisplayPrefs } from "./display-prefs";
export type DirectoryCard = { group: number; provider: string; user: string; name: string;
  alias: string; seat: string; from: string; kind: string; plan: string };
export type AccountDirectory = { token: number; generation: number; settingsRevision: number;
  instanceRevision: number; groups: number; cards: DirectoryCard[]; prefs: DisplayPrefs };
type Peer = { send(opcode: number, payload: Uint8Array): Promise<number>;
  onEvent(listener: (frame: Frame) => void): () => void };

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
    else if (frame.opcode === Opcode.Nack && frame.requestId === 0) fail(new Error("目录读取时蓝牙已断开"));
  });
  const timer = setTimeout(() => fail(new Error("目录响应超时")), 5000);
  void received.catch(() => undefined);
  try {
    const id = await peer.send(opcode, payload);
    const bytes = await received;
    if (responseId !== id) throw new Error("目录响应请求不匹配");
    return bytes;
  } finally { clearTimeout(timer); remove(); }
}
export async function readAccountDirectory(peer: Peer): Promise<AccountDirectory> {
  const metadata = await exchange(peer, Opcode.OpenAccountDirectory, new Uint8Array([2]), Opcode.AccountDirectory);
  if (metadata.length !== 33 || metadata[0] !== 2) throw new Error("不支持账号目录 schema");
  const view = new DataView(metadata.buffer, metadata.byteOffset, metadata.byteLength);
  const token = view.getUint32(1, true), generation = view.getUint32(5, true), settingsRevision = view.getUint32(9, true);
  const instanceRevision = view.getUint32(13, true), length = view.getUint32(17, true), checksum = view.getUint32(21, true);
  const groups = view.getUint32(25, true), count = view.getUint32(29, true);
  if (length < 5 || count > Math.floor((length - 5) / 36) || groups > count) throw new Error("无效账号目录长度");
  let document: Uint8Array;
  try { document = new Uint8Array(length); } catch { throw new Error("账号目录内存不足"); }
  for (let offset = 0; offset < length;) {
    const size = Math.min(183, length - offset), request = new Uint8Array(13), requestView = new DataView(request.buffer);
    request[0] = 2; requestView.setUint32(1, token, true); requestView.setUint32(5, offset, true); requestView.setUint32(9, size, true);
    const chunk = await exchange(peer, Opcode.ReadAccountDirectory, request, Opcode.AccountDirectoryChunk);
    const header = new DataView(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    if (chunk.length !== size + 9 || chunk[0] !== 2 || header.getUint32(1, true) !== token || header.getUint32(5, true) !== offset)
      throw new Error("账号目录块不匹配");
    document.set(chunk.subarray(9), offset); offset += size;
  }
  if (crc32(document) !== checksum) throw new Error("账号目录 CRC 校验失败");
  const data = new DataView(document.buffer), decoder = new TextDecoder("utf-8", { fatal: true });
  let offset = 5;
  if (document[0] !== 2 || data.getUint32(1, true) !== count) throw new Error("账号目录数量不匹配");
  const seen = new Map<number, string>(), identities = new Map<string, number>();
  const text = () => {
    if (offset > length - 4) throw new Error("账号目录字符串缺失");
    const size = data.getUint32(offset, true); offset += 4;
    if (size > length - offset) throw new Error("账号目录字符串越界");
    const value = decoder.decode(document.subarray(offset, offset + size)); offset += size; return value;
  };
  const cards: DirectoryCard[] = [];
  for (let i = 0; i < count; ++i) {
    if (offset > length - 4) throw new Error("账号目录记录缺失");
    const group = data.getUint32(offset, true); offset += 4;
    const provider = text(), user = text(), name = text(), alias = text(), seat = text(), from = text(), kind = text(), plan = text();
    if (!provider || group >= groups) throw new Error("无效账号偏好组");
    const identity = JSON.stringify([provider, user]);
    if ((seen.has(group) && seen.get(group) !== identity) || (identities.has(identity) && identities.get(identity) !== group))
      throw new Error("账号身份引用冲突");
    seen.set(group, identity); identities.set(identity, group);
    cards.push({ group, provider, user, name, alias, seat, from, kind, plan });
  }
  if (offset > length - 4) throw new Error("账号目录缺少偏好长度");
  const prefsLength = data.getUint32(offset, true); offset += 4;
  if (prefsLength !== length - offset || seen.size !== groups) throw new Error("账号目录包含多余数据");
  const prefs = decodeDisplayPrefs(document.subarray(offset));
  return { token, generation, settingsRevision, instanceRevision, groups, cards, prefs };
}
