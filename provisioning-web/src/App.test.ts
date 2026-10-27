// @vitest-environment jsdom
import { afterEach, expect, it, vi } from "vitest";
import App from "./App";
import { DeviceError } from "./errors";
import { LANGUAGE_STORAGE_KEY } from "./i18n";
import { Opcode, type Frame } from "./protocol";
import type { Transport } from "./transport";

type Fake = Transport & {
  send: ReturnType<typeof vi.fn<(opcode: number, payload?: Uint8Array, slot?: number) => Promise<number>>>;
  connect: ReturnType<typeof vi.fn<() => Promise<string>>>;
  reconnect: ReturnType<typeof vi.fn<() => Promise<string>>>;
  disconnect: ReturnType<typeof vi.fn<() => void>>;
  emit(opcode: number, payload: Uint8Array, requestId?: number, slot?: number): void;
  drop(): void;
};

function fakeTransport(supported = true): Fake {
  const events = new Set<(frame: Frame) => void>(), drops = new Set<() => void>();
  return {
    supported,
    send: vi.fn(async () => 1),
    connect: vi.fn(async () => "magpie-usage-test"),
    reconnect: vi.fn(async () => "magpie-usage-test"),
    disconnect: vi.fn(),
    onEvent(listener) { events.add(listener); return () => { events.delete(listener); }; },
    onDisconnect(listener) { drops.add(listener); return () => { drops.delete(listener); }; },
    emit(opcode, payload, requestId = 1, slot = 0) {
      for (const listener of events) listener({ opcode, payload, requestId, slot, sequence: 0, totalSequences: 1 });
    },
    drop() { for (const listener of drops) listener(); },
  };
}

function memoryStorage(): Storage {
  const values = new Map<string, string>();
  return { getItem: (key: string) => values.get(key) ?? null, setItem: (key: string, value: string) => { values.set(key, value); },
    removeItem: (key: string) => { values.delete(key); }, clear: () => values.clear(), key: () => null, get length() { return values.size; } };
}

const EMPTY_STATUS = new Uint8Array([1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
const WEB_SETTINGS = new Uint8Array([2, 0, 0, 0, 0, 0, 2, 30, 0, 0, 0, 0]);
const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

let dispose: (() => void) | undefined;
afterEach(() => {
  dispose?.();
  dispose = undefined;
  document.body.replaceChildren();
});

function mount(transport: Fake, storage: Storage = memoryStorage()): HTMLElement {
  const root = document.createElement("div");
  document.body.append(root);
  dispose = App(root, { transport, storage, browserZone: "UTC" });
  return root;
}

async function connected(transport = fakeTransport(), storage?: Storage): Promise<{ root: HTMLElement; transport: Fake }> {
  const root = mount(transport, storage);
  button(root, "Connect device").click();
  await vi.waitFor(() => expect(transport.send).toHaveBeenCalledWith(Opcode.GetWebSettings));
  await flush();
  transport.emit(Opcode.Status, EMPTY_STATUS);
  transport.emit(Opcode.WebSettings, WEB_SETTINGS);
  await flush();
  return { root, transport };
}

function button(root: ParentNode, text: string): HTMLButtonElement {
  return Array.from(root.querySelectorAll("button")).find((element) => element.textContent?.trim() === text)!;
}
const tab = (root: HTMLElement, name: string) => root.querySelector<HTMLButtonElement>(`[data-tab="${name}"]`)!;
const status = (root: HTMLElement) => root.querySelector('[role="status"]')!.textContent;
const alert = (root: HTMLElement) => root.querySelector('[role="alert"]')!.textContent;
function input(element: HTMLInputElement, value: string) {
  element.value = value;
  element.dispatchEvent(new Event("input", { bubbles: true }));
}
function choose(element: HTMLSelectElement, value: string) {
  element.value = value;
  element.dispatchEvent(new Event("change", { bubbles: true }));
}

it("keeps SSID and password inputs mounted and focused while typing", async () => {
  const { root } = await connected();
  const add = root.querySelector<HTMLButtonElement>(".add-slot")!;
  expect(add.disabled).toBe(true);
  expect(root.querySelector(`#${add.getAttribute("aria-describedby")}`)!.textContent).toContain("Complete the networks above");

  const ssid = root.querySelector<HTMLInputElement>("#ssid-0")!;
  expect(root.querySelector('label[for="ssid-0"]')!.textContent).toBe("Network name (SSID)");
  ssid.focus();
  for (const character of "Test AP") {
    input(ssid, ssid.value + character);
    await flush();
    expect(root.querySelector("#ssid-0")).toBe(ssid);
    expect(document.activeElement).toBe(ssid);
  }
  choose(root.querySelector<HTMLSelectElement>("#password-mode-0")!, "1");
  await flush();
  const password = root.querySelector<HTMLInputElement>("#password-0")!;
  expect(password.getAttribute("autocomplete")).toBe("new-password");
  expect(password.getAttribute("maxlength")).toBe("64");
  password.focus();
  for (const character of "test-password") {
    input(password, password.value + character);
    await flush();
    expect(root.querySelector("#password-0")).toBe(password);
    expect(document.activeElement).toBe(password);
  }
  expect(root.querySelector<HTMLButtonElement>(".add-slot")!.disabled).toBe(false);
  root.querySelector<HTMLButtonElement>(".add-slot")!.click();
  await flush();
  expect(root.querySelectorAll("section.slot")).toHaveLength(2);
  expect(document.activeElement).toBe(root.querySelector("#ssid-1"));
  expect(root.querySelector("#password-0")).toBe(password);
  expect(password.value).toBe("test-password");
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
  const { encodeDisplayPrefs, defaultPrefs, decodeDisplayPrefs } = await import("./display-prefs");
  const savedPrefs = encodeDisplayPrefs({ ...defaultPrefs(), hidden: [{ provider: "Missing provider", user: "gone account" }] });
  parts.push(u32(savedPrefs.length), savedPrefs);
  const body = new Uint8Array(parts.reduce((n, p) => n + p.length, 0)); let at = 0;
  for (const p of parts) { body.set(p, at); at += p.length; }
  const { root, transport } = await connected();
  transport.send.mockImplementation(async (opcode, payload) => {
    if (opcode === Opcode.OpenAccountDirectory) {
      const bytes = new Uint8Array(33); bytes[0] = 2;
      [1, 1, 0, 0, body.length, crc32(body), 1, 2].forEach((n, i) => new DataView(bytes.buffer).setUint32(1 + i * 4, n, true));
      transport.emit(Opcode.AccountDirectory, bytes);
    } else if (opcode === Opcode.ReadAccountDirectory) {
      const view = new DataView(payload!.buffer), offset = view.getUint32(5, true), size = view.getUint32(9, true);
      const bytes = new Uint8Array(9 + size); bytes[0] = 2;
      new DataView(bytes.buffer).setUint32(1, 1, true); new DataView(bytes.buffer).setUint32(5, offset, true);
      bytes.set(body.subarray(offset, offset + size), 9); transport.emit(Opcode.AccountDirectoryChunk, bytes);
    }
    return 1;
  });
  tab(root, "magpie").click();
  await flush();
  button(root, "Load account directory").click();
  await vi.waitFor(() => expect(root.textContent).toContain("昵称🙂"));
  expect(root.textContent).toContain("另一张卡");
  expect(root.textContent).toContain("1 preference groups · 2 quota cards");
  expect(root.textContent).toContain("Missing provider · gone account");
  button(root, "Remove preferences for accounts not present").click();
  await flush();
  expect(root.textContent).not.toContain("Missing provider · gone account");
  expect(tab(root, "magpie").textContent).toContain("(unsaved changes)");
  const hidden = root.querySelectorAll<HTMLInputElement>('input[aria-label^="Hide "]');
  hidden[0].click();
  await flush();
  expect(hidden[0].checked).toBe(true); expect(hidden[1].checked).toBe(true);
  button(root, "Save account display preferences").click();
  await vi.waitFor(() => expect(status(root)).toContain("Account display preferences saved"));
  const submitted = transport.send.mock.calls.filter(([opcode]) => opcode === Opcode.PutDisplayPrefsChunk);
  expect(decodeDisplayPrefs(submitted[0][1]!.subarray(5)).hidden).toEqual([{ provider: "厂商", user: "same" }]);
  expect(transport.send.mock.calls.some(([opcode]) => opcode === Opcode.FinishDisplayPrefs)).toBe(true);
  expect(transport.send.mock.calls.at(-1)![0]).toBe(Opcode.Commit);
  hidden[1].click();
  await flush();
  expect(hidden[0].checked).toBe(false);
  input(root.querySelector<HTMLInputElement>("#magpie-endpoint")!, "https://new-instance.test");
  choose(root.querySelector<HTMLSelectElement>("#magpie-keyMode")!, "1");
  await flush();
  input(root.querySelector<HTMLInputElement>("#magpie-key")!, "web-key-0123456789");
  button(root, "Save Magpie settings").click();
  await vi.waitFor(() => expect(status(root)).toContain("Settings saved. The device is still in setup mode."));
  expect(root.querySelector("#directory-title")).toBeNull();
});

it("saves each tab with its own button, stays in setup, and exits only explicitly", async () => {
  const { root, transport } = await connected();
  input(root.querySelector<HTMLInputElement>("#ssid-0")!, "home");
  await flush();
  expect(tab(root, "network").textContent).toContain("(unsaved changes)");
  tab(root, "magpie").click();
  await flush();
  const endpoint = root.querySelector<HTMLInputElement>("#magpie-endpoint")!;
  endpoint.focus();
  for (const character of "https://magpie.test") {
    input(endpoint, endpoint.value + character);
    await flush();
    expect(document.activeElement).toBe(endpoint);
  }
  choose(root.querySelector<HTMLSelectElement>("#magpie-keyMode")!, "1");
  await flush();
  const key = root.querySelector<HTMLInputElement>("#magpie-key")!;
  input(key, "web-key-0123456789");
  tab(root, "network").click();
  await flush();
  expect(root.querySelector<HTMLInputElement>("#ssid-0")!.value).toBe("home");
  expect(key.value).toBe("web-key-0123456789");
  transport.send.mockClear();
  button(root, "Save network settings").click();
  await flush();
  await vi.waitFor(() => expect(status(root)).toContain("Settings saved"));
  expect(transport.send.mock.calls.map(([opcode]) => opcode)).toEqual([Opcode.Begin, ...Array(5).fill(Opcode.SetSlot), Opcode.Commit]);
  expect(tab(root, "network").textContent).not.toContain("(unsaved changes)");
  expect(tab(root, "magpie").textContent).toContain("(unsaved changes)");
  expect(key.value).toBe("web-key-0123456789");
  tab(root, "magpie").click();
  transport.send.mockClear();
  button(root, "Save Magpie settings").click();
  await vi.waitFor(() => expect(root.querySelector("#magpie-key")).toBeNull());
  expect(transport.send.mock.calls.map(([opcode]) => opcode)).toEqual([Opcode.Begin, Opcode.SetWebSettings, Opcode.Commit]);
  expect(root.querySelector<HTMLInputElement>("#ssid-0")!.value).toBe("home");
  input(endpoint, "https://changed.test");
  await flush();
  button(root, "Exit setup").click();
  await flush();
  const dialog = root.querySelector<HTMLDialogElement>("dialog")!;
  expect(dialog.hasAttribute("open")).toBe(true);
  expect(transport.send.mock.calls.some(([opcode]) => opcode === Opcode.ExitSetup)).toBe(false);
  dialog.querySelector<HTMLButtonElement>(".btn-danger")!.click();
  await vi.waitFor(() => expect(transport.send).toHaveBeenCalledWith(Opcode.ExitSetup));
  await vi.waitFor(() => expect(status(root)).toContain("Setup finished"));
  expect(button(root, "Save Magpie settings").disabled).toBe(true);
  expect(dialog.hasAttribute("open")).toBe(false);
});

it("maps a stale revision NACK to a localized message and keeps the draft", async () => {
  const { root, transport } = await connected();
  input(root.querySelector<HTMLInputElement>("#ssid-0")!, "unsaved-network");
  transport.send.mockRejectedValueOnce(new DeviceError(10));
  button(root, "Save network settings").click();
  await vi.waitFor(() => expect(alert(root)).toContain("configuration changed after it was read"));
  expect(root.querySelector<HTMLInputElement>("#ssid-0")!.value).toBe("unsaved-network");
  expect(transport.send.mock.calls.some(([opcode]) => opcode === Opcode.Commit)).toBe(false);
});

it("clears new secrets and shows Wi-Fi progress when the device starts connecting", async () => {
  const { root, transport } = await connected();
  input(root.querySelector<HTMLInputElement>("#ssid-0")!, "home");
  tab(root, "magpie").click();
  choose(root.querySelector<HTMLSelectElement>("#magpie-keyMode")!, "1");
  await flush();
  const key = root.querySelector<HTMLInputElement>("#magpie-key")!;
  input(key, "unsaved-key");
  transport.emit(Opcode.Connecting, new Uint8Array([1]), 0, 0);
  await flush();
  expect(key.value).toBe("");
  expect(root.querySelector(".wifi-progress")!.textContent).toContain("Trying network 1 (home), round 1 of 3");
  transport.emit(Opcode.ConnectionFailed, new Uint8Array(), 0, 0);
  transport.emit(Opcode.Failure, new Uint8Array(), 0, 0);
  await flush();
  expect(root.querySelector(".wifi-progress")!.textContent).toContain("Network 1 (home) failed");
  expect(alert(root)).toContain("All networks failed");
});

it("keeps the raw interval text while editing and validates it inline on save", async () => {
  const { root, transport } = await connected();
  tab(root, "magpie").click();
  await flush();
  const interval = root.querySelector<HTMLInputElement>("#magpie-interval")!;
  input(interval, "");
  await flush();
  expect(interval.value).toBe("");
  input(interval, "1");
  await flush();
  expect(interval.value).toBe("1");
  transport.send.mockClear();
  button(root, "Save Magpie settings").click();
  await flush();
  expect(transport.send).not.toHaveBeenCalled();
  expect(interval.getAttribute("aria-invalid")).toBe("true");
  expect(interval.getAttribute("aria-describedby")).toContain("magpie-interval-error");
  expect(root.querySelector("#magpie-interval-error")!.textContent).toContain("30 to 3600");
  expect(alert(root)).toContain("fix the highlighted fields");
  expect(document.activeElement).toBe(interval);
  input(interval, "120");
  await flush();
  expect(interval.hasAttribute("aria-invalid")).toBe(false);
  button(root, "Save Magpie settings").click();
  await vi.waitFor(() => expect(status(root)).toContain("Settings saved"));
  const payload = transport.send.mock.calls.find(([opcode]) => opcode === Opcode.SetWebSettings)![1]!;
  expect(new DataView(payload.buffer).getUint16(7, true)).toBe(120);
});

it("switches the page language, persists it, and saves the device display language separately", async () => {
  const storage = memoryStorage();
  const { root, transport } = await connected(fakeTransport(), storage);
  expect(document.documentElement.lang).toBe("en");
  tab(root, "magpie").click();
  await flush();
  const deviceLanguage = root.querySelector<HTMLSelectElement>("#magpie-language")!;
  expect(deviceLanguage.value).toBe("en");
  button(root, "中文").click();
  await flush();
  expect(document.documentElement.lang).toBe("zh-CN");
  expect(storage.getItem(LANGUAGE_STORAGE_KEY)).toBe("zh");
  expect(root.querySelector("h1")!.textContent).toBe("Magpie Usage 设置");
  // The page language does not change the device language.
  expect(deviceLanguage.value).toBe("en");
  choose(deviceLanguage, "zh");
  button(root, "保存 Magpie 设置").click();
  await vi.waitFor(() => expect(status(root)).toContain("设置已保存"));
  const payload = transport.send.mock.calls.find(([opcode]) => opcode === Opcode.SetWebSettings)![1]!;
  expect(payload[6] & 8).toBe(8);
  dispose!();
  document.body.replaceChildren();
  const again = mount(fakeTransport(), storage);
  expect(button(again, "连接设备")).toBeDefined();
});

it("explains unsupported browsers and disables connecting", () => {
  const root = mount(fakeTransport(false));
  expect(root.textContent).toContain("Web Bluetooth is not available");
  expect(button(root, "Connect device").disabled).toBe(true);
});

it("treats a cancelled chooser as a gentle notice and shows progress while connecting", async () => {
  const transport = fakeTransport();
  let finish: (name: string) => void = () => {};
  transport.connect.mockImplementationOnce(() => Promise.reject(new DOMException("User cancelled the requestDevice() chooser.", "NotFoundError")));
  const root = mount(transport);
  button(root, "Connect device").click();
  await vi.waitFor(() => expect(status(root)).toBe("No device selected."));
  expect(alert(root)).toBe("");
  transport.connect.mockImplementationOnce(() => new Promise((resolve) => { finish = resolve; }));
  button(root, "Connect device").click();
  await flush();
  const busy = button(root, "Connecting…");
  expect(busy.getAttribute("aria-busy")).toBe("true");
  expect(busy.disabled).toBe(true);
  finish("magpie-usage-test");
  await vi.waitFor(() => expect(root.textContent).toContain("Connected to magpie-usage-test"));
});

it("offers Reconnect after a disconnect and disconnects on unmount", async () => {
  const { root, transport } = await connected();
  transport.drop();
  await flush();
  expect(root.textContent).toContain("Disconnected from magpie-usage-test");
  expect(status(root)).toContain("Bluetooth connection was closed");
  button(root, "Reconnect").click();
  await vi.waitFor(() => expect(transport.reconnect).toHaveBeenCalled());
  await vi.waitFor(() => expect(root.textContent).toContain("Connected to magpie-usage-test"));
  expect(transport.connect).toHaveBeenCalledTimes(1);
  dispose!();
  dispose = undefined;
  expect(transport.disconnect).toHaveBeenCalled();
});

it("lists scan results by signal strength and fills the SSID on click", async () => {
  const { root, transport } = await connected();
  button(root, "Scan for Wi‑Fi").click();
  await vi.waitFor(() => expect(transport.send).toHaveBeenCalledWith(Opcode.Scan));
  const scan = (ssid: string, rssi: number, secure: boolean) => {
    const bytes = new TextEncoder().encode(ssid);
    return new Uint8Array([bytes.length, ...bytes, rssi & 255, 6, secure ? 1 : 0]);
  };
  transport.emit(Opcode.ScanResult, scan("Weak", -80, true));
  transport.emit(Opcode.ScanResult, scan("Strong", -40, false));
  transport.emit(Opcode.ScanDone, new Uint8Array());
  await flush();
  const items = [...root.querySelectorAll<HTMLButtonElement>(".scan-item")];
  expect(items.map((item) => item.querySelector(".scan-ssid")!.textContent)).toEqual(["Strong", "Weak"]);
  expect(items[1].querySelector(".lock")).not.toBeNull();
  expect(items[0].querySelector(".lock")).toBeNull();
  expect(items[0].querySelectorAll(".signal i.on")).toHaveLength(4);
  expect(status(root)).toContain("2 networks found");
  items[1].click();
  await flush();
  expect(root.querySelector<HTMLInputElement>("#ssid-0")!.value).toBe("Weak");
  expect(document.activeElement).toBe(root.querySelector("#ssid-0"));
});

it("moves between tabs with the arrow keys using a roving tabindex", async () => {
  const { root } = await connected();
  const network = tab(root, "network");
  expect(network.tabIndex).toBe(0);
  expect(tab(root, "magpie").tabIndex).toBe(-1);
  network.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowRight", bubbles: true }));
  await flush();
  expect(tab(root, "magpie").getAttribute("aria-selected")).toBe("true");
  expect(document.activeElement).toBe(tab(root, "magpie"));
  expect(root.querySelector<HTMLElement>("#network-panel")!.hidden).toBe(true);
});
