// @vitest-environment jsdom
import { afterEach, expect, it, vi } from "vitest";
import { Opcode, type Frame } from "./protocol";

const mock = vi.hoisted(() => ({ listener: undefined as ((frame: Frame) => void) | undefined,
  listeners: new Set<(frame: Frame) => void>(),
  send: vi.fn(async (_opcode: number, _payload?: Uint8Array, _slot?: number) => 1) }));
vi.mock("./transport", () => ({
  Note4Transport: class {
    onEvent(listener: (frame: Frame) => void) {
      mock.listeners.add(listener);
      mock.listener = frame => { for (const current of mock.listeners) current(frame); };
      return () => { mock.listeners.delete(listener); };
    }
    async connect() { return "magpie-usage-test"; }
    send = mock.send;
  },
}));
import App from "./App";

let dispose: (() => void) | undefined;
afterEach(() => {
  dispose?.();
  document.body.replaceChildren();
  mock.send.mockReset();
  mock.send.mockImplementation(async () => 1);
});

it("keeps SSID and password inputs mounted and focused while typing", async () => {
  const root = document.createElement("div");
  document.body.append(root);
  dispose = App(root);
  root.querySelector<HTMLButtonElement>("button")!.click();
  await vi.waitFor(() => expect(root.textContent).toContain("已连接，正在读取配置"));
  mock.listener!({ opcode: Opcode.Status, requestId: 1, slot: 0, sequence: 0,
    totalSequences: 1, payload: new Uint8Array([1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]) });

  const ssid = root.querySelector<HTMLInputElement>('input[type="text"]')!;
  ssid.focus();
  for (const character of "Test AP") {
    ssid.value += character;
    ssid.dispatchEvent(new Event("input", { bubbles: true }));
    expect(root.querySelector('input[type="text"]')).toBe(ssid);
    expect(document.activeElement).toBe(ssid);
  }
  const mode = root.querySelector<HTMLSelectElement>("select")!;
  mode.value = "1";
  mode.dispatchEvent(new Event("change", { bubbles: true }));
  const password = root.querySelector<HTMLInputElement>('input[type="password"]')!;
  password.focus();
  for (const character of "test-password") {
    password.value += character;
    password.dispatchEvent(new Event("input", { bubbles: true }));
    expect(root.querySelector('input[type="password"]')).toBe(password);
    expect(document.activeElement).toBe(password);
  }
  expect(password.value).toBe("test-password");
  root.querySelector<HTMLButtonElement>('button[aria-label="添加 Wi-Fi"]')!.click();
  expect(root.querySelectorAll("section.slot")).toHaveLength(2);
  expect(root.querySelector('input[type="password"]')).toBe(password);
});

it("shows the full account directory including repeated exact identities and original Unicode names", async () => {
  const { crc32 } = await import("./account-directory");
  const u32 = (n: number) => { const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, n, true); return b; };
  const parts: Uint8Array[] = [new Uint8Array([2]), u32(2)];
  for (const name of ["昵称🙂", "另一张卡"]) {
    parts.push(u32(0));
    for (const text of ["厂商", "same", name, "alias", "seat", "from", "unknown", "plan"]) {
      const bytes = new TextEncoder().encode(text); parts.push(u32(bytes.length), bytes);
    }
  }
  const { encodeDisplayPrefs, defaultPrefs } = await import("./display-prefs");
  const savedPrefs = encodeDisplayPrefs({ ...defaultPrefs(), hidden: [{ provider: "Missing provider", user: "gone account" }] });
  parts.push(u32(savedPrefs.length), savedPrefs);
  const body = new Uint8Array(parts.reduce((n, p) => n + p.length, 0)); let at = 0;
  for (const p of parts) { body.set(p, at); at += p.length; }
  mock.send.mockImplementation(async (opcode, payload) => {
    if (opcode === Opcode.OpenAccountDirectory) {
      const bytes = new Uint8Array(33); bytes[0] = 2;
      [1, 1, 0, 0, body.length, crc32(body), 1, 2].forEach((n, i) => new DataView(bytes.buffer).setUint32(1 + i * 4, n, true));
      emit(Opcode.AccountDirectory, bytes);
    } else if (opcode === Opcode.ReadAccountDirectory) {
      const view = new DataView(payload!.buffer), offset = view.getUint32(5, true), size = view.getUint32(9, true);
      const bytes = new Uint8Array(9 + size); bytes[0] = 2;
      new DataView(bytes.buffer).setUint32(1, 1, true); new DataView(bytes.buffer).setUint32(5, offset, true);
      bytes.set(body.subarray(offset, offset + size), 9); emit(Opcode.AccountDirectoryChunk, bytes);
    }
    return 1;
  });
  const root = document.createElement("div"); document.body.append(root);
  dispose = App(root);
  button(root, "连接设备").click();
  await vi.waitFor(() => expect(mock.send).toHaveBeenCalledWith(Opcode.GetWebSettings));
  emit(Opcode.Status, new Uint8Array([1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]));
  emit(Opcode.WebSettings, new Uint8Array([2, 0, 0, 0, 0, 0, 2, 30, 0, 0, 0, 0]));
  button(root, "Magpie 配置").click();
  expect(button(root, "读取完整账号目录")).toBeDefined();
  button(root, "读取完整账号目录").click();
  await vi.waitFor(() => expect(root.textContent).toContain("昵称🙂"));
  expect(root.textContent).toContain("另一张卡");
  expect(root.textContent).toContain("1 个偏好组 · 2 张额度卡片");
  expect(root.textContent).toContain("Missing provider · gone account");
  button(root, "清理当前不存在的账号偏好").click();
  expect(root.textContent).not.toContain("Missing provider · gone account");
  const hidden = root.querySelectorAll<HTMLInputElement>('input[aria-label^="隐藏 "]');
  hidden[0].click();
  expect(hidden[0].checked).toBe(true); expect(hidden[1].checked).toBe(true);
  button(root, "保存账号显示偏好").click();
  await vi.waitFor(() => expect(root.textContent).toContain("账号显示偏好已保存"));
  const submitted = mock.send.mock.calls.filter(([opcode]) => opcode === Opcode.PutDisplayPrefsChunk);
  const { decodeDisplayPrefs } = await import("./display-prefs");
  expect(decodeDisplayPrefs(submitted[0][1]!.subarray(5)).hidden).toEqual([{ provider: "厂商", user: "same" }]);
  expect(mock.send.mock.calls.some(([opcode]) => opcode === Opcode.FinishDisplayPrefs)).toBe(true);
  expect(mock.send.mock.calls.at(-1)![0]).toBe(Opcode.Commit);
  hidden[1].click(); expect(hidden[0].checked).toBe(false);
  input(root.querySelector<HTMLInputElement>('input[type="url"]')!, "https://new-instance.test");
  const mode = root.querySelector<HTMLSelectElement>('[aria-label="Key 处理"]')!;
  mode.value = "1"; mode.dispatchEvent(new Event("change", { bubbles: true }));
  input(root.querySelector<HTMLInputElement>('[aria-label="Magpie Key"]')!, "web-key-0123456789");
  button(root, "应用设置").click();
  await vi.waitFor(() => expect(root.textContent).toContain("设置已保存，仍处于设置模式"));
  expect(root.querySelector('[aria-label="完整账号目录"]')).toBeNull();
  mock.send.mockImplementation(async () => 1);
});

function emit(opcode: number, payload: Uint8Array, requestId = 1) {
  mock.listener!({ opcode, payload, requestId, slot: 0, sequence: 0, totalSequences: 1 });
}
function input(element: HTMLInputElement, value: string) {
  element.value = value;
  element.dispatchEvent(new Event("input", { bubbles: true }));
}
function button(root: HTMLElement, text: string): HTMLButtonElement {
  return Array.from(root.querySelectorAll("button")).find((element) => element.textContent === text)!;
}

it("saves each tab separately, stays in setup, and exits only explicitly", async () => {
  const root = document.createElement("div");
  document.body.append(root);
  dispose = App(root);
  button(root, "连接设备").click();
  await vi.waitFor(() => expect(mock.send).toHaveBeenCalledWith(Opcode.GetWebSettings));
  emit(Opcode.Status, new Uint8Array([1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]));
  emit(Opcode.WebSettings, new Uint8Array([2, 0, 0, 0, 0, 0, 2, 30, 0, 0, 0, 0]));
  input(root.querySelector<HTMLInputElement>('input[type="text"]')!, "home");
  button(root, "Magpie 配置").click();
  const endpoint = root.querySelector<HTMLInputElement>('input[type="url"]')!;
  endpoint.focus();
  for (const character of "https://magpie.test") {
    input(endpoint, endpoint.value + character);
    expect(document.activeElement).toBe(endpoint);
  }
  const mode = root.querySelector<HTMLSelectElement>('[aria-label="Key 处理"]')!;
  mode.value = "1";
  mode.dispatchEvent(new Event("change", { bubbles: true }));
  const key = root.querySelector<HTMLInputElement>('[aria-label="Magpie Key"]')!;
  key.focus();
  for (const character of "web-key-0123456789") {
    input(key, key.value + character);
    expect(document.activeElement).toBe(key);
  }
  button(root, "网络").click();
  expect(root.querySelector<HTMLInputElement>('input[type="text"]')!.value).toBe("home");
  expect(key.value).toBe("web-key-0123456789");
  mock.send.mockClear();
  button(root, "应用设置").click();
  await vi.waitFor(() => expect(root.textContent).toContain("设置已保存，仍处于设置模式"));
  expect(mock.send.mock.calls.map(([opcode]) => opcode)).toEqual([Opcode.Begin, ...Array(5).fill(Opcode.SetSlot), Opcode.Commit]);
  expect(key.value).toBe("web-key-0123456789");
  expect(button(root, "应用设置").disabled).toBe(false);
  button(root, "Magpie 配置").click();
  mock.send.mockClear();
  button(root, "应用设置").click();
  await vi.waitFor(() => expect(root.querySelector('[aria-label="Magpie Key"]')).toBeNull());
  expect(mock.send.mock.calls.map(([opcode]) => opcode)).toEqual([Opcode.Begin, Opcode.SetWebSettings, Opcode.Commit]);
  expect(root.querySelector<HTMLInputElement>('input[type="text"]')!.value).toBe("home");
  expect(button(root, "应用设置").disabled).toBe(false);
  input(endpoint, "https://changed.test");
  button(root, "退出设置模式").click();
  expect(root.querySelector('[role="dialog"]')).not.toBeNull();
  expect(mock.send.mock.calls.some(([opcode]) => opcode === Opcode.ExitSetup)).toBe(false);
  button(root, "确认退出").click();
  await vi.waitFor(() => expect(mock.send).toHaveBeenCalledWith(Opcode.ExitSetup));
  await vi.waitFor(() => expect(button(root, "应用设置").disabled).toBe(true));
});

it("preserves the draft after a stale revision and requires an explicit retry", async () => {
  const root = document.createElement("div");
  document.body.append(root);
  dispose = App(root);
  button(root, "连接设备").click();
  await vi.waitFor(() => expect(mock.send).toHaveBeenCalledWith(Opcode.GetWebSettings));
  emit(Opcode.Status, new Uint8Array([1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]));
  emit(Opcode.WebSettings, new Uint8Array([2, 0, 0, 0, 0, 0, 2, 30, 0, 0, 0, 0]));
  input(root.querySelector<HTMLInputElement>('input[type="text"]')!, "unsaved-network");
  mock.send.mockRejectedValueOnce(new Error("设备拒绝操作（错误码 10）"));
  button(root, "应用设置").click();
  await vi.waitFor(() => expect(root.textContent).toContain("配置已改变"));
  expect(root.querySelector<HTMLInputElement>('input[type="text"]')!.value).toBe("unsaved-network");
  expect(mock.send.mock.calls.some(([opcode]) => opcode === Opcode.Commit)).toBe(false);
});

it("clears new secrets when the device exits setup", async () => {
  const root = document.createElement("div");
  document.body.append(root);
  dispose = App(root);
  button(root, "连接设备").click();
  await vi.waitFor(() => expect(mock.send).toHaveBeenCalledWith(Opcode.GetWebSettings));
  emit(Opcode.Status, new Uint8Array([1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]));
  emit(Opcode.WebSettings, new Uint8Array([2, 0, 0, 0, 0, 0, 2, 30, 0, 0, 0, 0]));
  button(root, "Magpie 配置").click();
  const mode = root.querySelector<HTMLSelectElement>('[aria-label="Key 处理"]')!;
  mode.value = "1";
  mode.dispatchEvent(new Event("change", { bubbles: true }));
  const key = root.querySelector<HTMLInputElement>('[aria-label="Magpie Key"]')!;
  input(key, "unsaved-key");
  emit(Opcode.Connecting, new Uint8Array([1]), 0);
  expect(key.value).toBe("");
  expect(button(root, "应用设置").disabled).toBe(true);
});
