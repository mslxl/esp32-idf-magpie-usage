import { expect, test } from "bun:test";
import { existsSync, readFileSync } from "node:fs";
import { createContext, runInContext } from "node:vm";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { unpack } from "../third_party/pocketjs/framework/compiler/pak";
import type { QuotaCard } from "../quota-pages";
import { PROP } from "../third_party/pocketjs/contracts/spec/spec";

// Execute the packaged product and its Solid renderer at the native host boundary.
// Build with `just package-pinned` before this test; no TSX reimplementation is used.
function device(cards: QuotaCard[], initialTime = Date.UTC(2026, 9, 25, 12)) {
  type Node = { text: string; children: number[]; style?: Record<string, number | string> };
  const pack = readFileSync(new URL("../.pocket-build/package/note4-hello.pak", import.meta.url));
  const advances = new Map<number, Map<number, number>>();
  for (const blob of unpack(pack).filter(value => value.key.startsWith("ui:font."))) {
    const view = new DataView(blob.data.buffer, blob.data.byteOffset, blob.data.byteLength);
    const glyphs = new Map<number, number>();
    for (let index = 0; index < view.getUint16(6, true); ++index) glyphs.set(view.getUint32(16 + index * 8, true), blob.data[22 + index * 8]);
    advances.set(blob.data[12], glyphs);
  }
  const measure = (text: string, slot = 2) => Array.from(text).reduce((width, scalar) => width + (advances.get(slot)?.get(scalar.codePointAt(0)!) ?? 8), 0);
  const nodes = new Map<number, Node>([[1, { text: "", children: [] }]]);
  let serial = 1, version = 1, available = true;
  let hidden: string[] = [];
  let ranks: number[] = [];
  let cardFailure = false;
  let textFailure = false;
  let materialized: boolean | undefined;
  const detach = (id: number) => { for (const node of nodes.values()) node.children = node.children.filter(child => child !== id); };
  const destroy = (id: number) => {
    for (const child of nodes.get(id)?.children ?? []) destroy(child);
    detach(id); nodes.delete(id);
  };
  const noop = () => {};
  const ui = {
    __host: "note4-hello", __hostAbi: 1, __tickHz: 60, __viewport: { w: 400, h: 300 }, __textures: {},
    createNode: () => { const id = ++serial; nodes.set(id, { text: "", children: [] }); return id; },
    destroyNode: destroy,
    insertBefore: (parent: number, child: number, anchor: number) => {
      detach(child); const children = nodes.get(parent)!.children;
      const index = anchor ? children.indexOf(anchor) : -1;
      children.splice(index < 0 ? children.length : index, 0, child);
    },
    removeChild: (_parent: number, child: number) => detach(child),
    setStyle: noop, setProp: (id: number, property: number, value: number | string) => {
      const node = nodes.get(id)!; node.style ??= {};
      const name = Object.entries(PROP).find(([, code]) => code === property)?.[0] ?? String(property);
      node.style[name] = value;
    }, setText: (id: number, text: string) => {
      if (textFailure) { textFailure = false; throw new Error("synthetic UI allocation failure"); }
      nodes.get(id)!.text = text;
    },
    replaceText: (id: number, text: string) => { nodes.get(id)!.text = text; },
    uploadTexture: () => 0, setImage: noop, setSprite: noop, animate: () => 1, cancelAnim: noop,
    setFocus: noop, measureText: measure,
  };
  const status = { state: "connected", slot: 0, round: 1, pairing: false, passkey: 0, magpiePhase: 5, magpieResult: 0,
    magpieErrorPage: false, magpieReading: false, magpieRetrySeconds: 0, timezone: "UTC", layout: "A", adaptiveFont: true };
  let clock = initialTime;
  class DeviceDate extends Date { static now() { return clock; } }
  const context = createContext({ ui, console, Date: DeviceDate, __pak: pack.buffer.slice(pack.byteOffset, pack.byteOffset + pack.byteLength),
    note4: { get status() { return { ...status }; }, quotas: { materialized: (accepted: boolean) => { materialized = accepted; }, info: () => ({ version, available, count: cards.length }), card: (i: number) => { if (cardFailure) throw new Error("synthetic allocation failure"); return cards[i]; }, visible: (i: number) => !hidden.includes(cards[i].provider), rank: (i: number) => ranks[i] ?? 0 },
      ble: { name: "test", requestAction: noop }, display: { setMode: noop } } });
  runInContext(readFileSync(new URL("../.pocket-build/package/note4-hello.js", import.meta.url), "utf8"), context);
  const frame = (buttons = 0) => runInContext(`frame(${buttons}, 0)`, context);
  const textAt = (id: number): string[] => { const node = nodes.get(id); return node ? [node.text, ...node.children.flatMap(textAt)] : []; };
  frame();
  const styledNodes = (id = 1, inherited: Record<string, number | string> = {}): Node[] => {
    const node = nodes.get(id); if (!node) return [];
    const style = { ...inherited, ...node.style };
    return [{ ...node, style }, ...node.children.flatMap(child => styledNodes(child, style))];
  };
  return { frame, text: () => textAt(1).filter(Boolean), status, nodes: styledNodes, measure,
    materialized: () => materialized,
    hide(providers: string[]) { hidden = providers; ++version; frame(); },
    advance(milliseconds: number) { clock += milliseconds; frame(); },
    update(next: QuotaCard[], nextRanks: number[] = []) { cards = next; ranks = nextRanks; ++version; frame(); },
    settle(accepted: boolean) { runInContext(`note4.quotas.settle(${accepted})`, context); },
    failNextText() { textFailure = true; },
    failCards(value: boolean) { cardFailure = value; ++version; frame(); },
    unavailable() { available = false; ++version; frame(); } };
}
const card = (provider: string, windows: QuotaCard["windows"] = []): QuotaCard => ({
  provider, user: "", name: "", alias: "", seat: "", from: "", kind: "", plan: "", balance: "", error: "",
  renew: "", asOf: "", readAt: "", until: "", lastServedAt: "", windows,
});

test("storage initialization failure shows a recovery page without old quotas", () => {
  const app = device([card("Codex")]);
  Object.assign(app.status, { storageError: true });
  app.frame();
  expect(app.text()).toContain("STORAGE UNAVAILABLE");
  expect(app.text().join(" ")).toContain("Press OK to retry");
  expect(app.text()).not.toContain("Codex");
});

test("candidate materialization failure retains the visible page and can recover", () => {
  const app = device([card("Codex")]);
  const previous = app.text();
  app.failCards(true);
  expect(app.text()).toEqual(previous);
  app.failCards(false);
  app.update([card("Claude")]);
  expect(app.text()).toContain("Claude");
  expect(app.text()).not.toContain("Codex");
});

test("a rejected presented candidate restores the previous complete page", () => {
  const app = device([card("Codex")]);
  app.settle(true);
  const previous = app.text();
  app.update([card("Claude")]);
  expect(app.text()).toContain("Claude");
  app.settle(false);
  expect(app.text()).toEqual(previous);
  app.update([card("DeepSeek")]);
  app.settle(true);
  expect(app.text()).toContain("DeepSeek");
});

test("a rejected UI mutation can restore the complete previous page", () => {
  const app = device([card("Codex")]);
  app.settle(true);
  const previous = app.text();
  app.failNextText();
  app.update([card("Claude")]);
  expect(app.materialized()).toBe(false);
  app.settle(false);
  expect(app.text()).toEqual(previous);
  app.update([card("DeepSeek")]);
  expect(app.text()).toContain("DeepSeek");
});

test("product package includes a real 15px font atlas for adaptive layout", () => {
  const pack = readFileSync(new URL("../.pocket-build/package/note4-hello.pak", import.meta.url));
  const atlas = unpack(pack).find(blob => blob.key === "ui:font.21");
  expect(atlas).toBeDefined();
  expect(atlas!.data[12]).toBe(21);
  expect(atlas!.data[8]).toBeGreaterThan(0);
  expect(atlas!.data[10]).toBeGreaterThanOrEqual(15);
});

test("packaged PocketJS page packs providers, turns pages and clamps after an empty update", () => {
  const app = device([card("Codex", [{ name: "5h", used: "0" }]), card("Claude")]);
  expect(app.text()).toContain("Codex");
  expect(app.text()).toContain("Claude");
  expect(app.text()).toContain("Used: 0%");
  app.update([card("Codex", Array.from({ length: 30 }, (_, i) => ({ name: `window ${i}`, used: "110" })))]);
  const observed = new Set<string>();
  for (let i = 0; i < 15; ++i) {
    for (const text of app.text()) observed.add(text);
    app.frame(0); app.frame(0x40);
  }
  for (let i = 0; i < 30; ++i) expect(observed.has(`window ${i}`)).toBe(true);
  app.update([]);
  expect(app.text()).toContain("No accounts");
  expect(app.text().some(text => text.startsWith("0 / 0"))).toBe(true);
});

test("saved B layout reaches PocketJS coordinates and reserves the offline banner", () => {
  const app = device(Array.from({ length: 10 }, (_, index) => card(index % 2 ? "Claude" : "Codex")));
  app.status.layout = "B"; app.status.adaptiveFont = false; app.frame();
  expect(app.nodes().some(node => node.text && node.style?.insetL === 204)).toBe(true);
  app.status.state = "connecting"; app.frame();
  for (const node of app.nodes().filter(node => node.text && Number(node.style?.insetT) >= 62 && Number(node.style?.insetT) < 274)) {
    expect(Number(node.style!.insetT) + Number(node.style!.height)).toBeLessThanOrEqual(270);
    expect(app.measure(node.text, Number(node.style!.fontSlot ?? 2))).toBeLessThanOrEqual(Number(node.style!.width));
    expect(Number(node.style!.insetL) + Number(node.style!.width)).toBeLessThanOrEqual(392);
  }
});

test("HTTP replay through the native model drives packaged PocketJS smart order", () => {
  const name = `test_magpie_poll${process.platform === "win32" ? ".exe" : ""}`;
  const executable = [new URL(`./build/${process.env.NOTE4_TEST_CONFIG || "Debug"}/${name}`, import.meta.url),
    new URL(`./build/${name}`, import.meta.url)].map(fileURLToPath).find(existsSync);
  if (!executable) throw new Error("Run just test before the product replay");
  const observations = execFileSync(executable, ["--order-replay"], { encoding: "utf8" }).trim().split(/\r?\n/)
    .map(line => JSON.parse(line) as { cards: { provider: string; windows: { used: number }[] }[]; ranks: number[] });
  const app = device([]);
  const expected = [["A", "B"], ["B", "A"], ["B", "A"]];
  observations.forEach((observation, index) => {
    app.update(observation.cards.map(value => card(value.provider, value.windows.map(window => ({ used: String(window.used) })))), observation.ranks);
    expect(app.text().filter(text => text === "A" || text === "B")).toEqual(expected[index]);
  });
  expect(observations).toHaveLength(3);
});

test("saved hidden groups disappear together and restore without losing the full directory", () => {
  const app = device([card("Codex"), card("Codex"), card("Claude")]);
  app.hide(["Codex"]);
  expect(app.text()).not.toContain("Codex"); expect(app.text()).toContain("Claude");
  app.hide(["Codex", "Claude"]);
  expect(app.text()).toContain("No accounts match display settings");
  app.hide([]); expect(app.text()).toContain("Codex"); expect(app.text()).toContain("Claude");
});

test("packaged time labels use covered Chinese glyphs and update with the saved timezone", () => {
  const cached = { ...card("Codex", [{ name: "5h", used: "30", resetsAt: "2026-10-25T11:00:00Z" }]),
    asOf: "2026-10-25T10:00:00Z", until: "2026-10-25T20:00:00Z" };
  const app = device([cached]);
  expect(app.text()).toContain("Read: 缓存 · 2小时前");
  expect(app.text()).toContain("Until: 今天 20:00");
  expect(app.text()).toContain("重置时间已过 · 当前额度未知");
  expect(app.text()).not.toContain("Used: 30%");
  app.status.timezone = "Asia/Shanghai"; app.frame();
  expect(app.text()).toContain("Until: 明天 04:00");
  app.advance(3600000);
  expect(app.text()).toContain("Read: 缓存 · 3小时前");
  expect(app.text().join(" ")).not.toContain("[U+");
});

test("offline keeps the current quota page until the error deadline and preserves authentication priority", () => {
  const app = device([card("Codex", Array.from({ length: 20 }, (_, i) => ({ name: `window ${i}`, used: "10" })))]);
  app.frame(0x40); app.frame(0);
  const footer = app.text().find(text => text.includes("UP/DOWN"));
  app.status.state = "connecting"; app.status.magpiePhase = 6; app.status.magpieResult = 5; app.frame();
  expect(app.text().find(text => text.includes("UP/DOWN"))?.split(" / ")[0]).toBe(footer!.split(" / ")[0]);
  expect(app.text()).toContain("Last data - offline; reconnecting");
  app.status.magpieErrorPage = true; app.frame();
  expect(app.text()).toContain("QUOTA READING FAILED");
  expect(app.text()).toContain("Offline. Reconnecting to Wi-Fi. Hold DOWN for setup.");
  app.status.magpieResult = 2; app.frame();
  expect(app.text()).toContain("MAGPIE LOGIN EXPIRED");
  expect(app.text()).toContain("Hold DOWN and save a valid Web key");
});

test("temporary read failure retains the last page and setup takes over the screen", () => {
  const app = device([card("Codex")]);
  app.status.magpiePhase = 6; app.status.magpieResult = 6; app.frame();
  expect(app.text()).toContain("Codex");
  expect(app.text()).toContain("Last data - request timed out");
  app.status.magpieResult = 2; app.frame();
  expect(app.text()).toContain("MAGPIE LOGIN EXPIRED");
  expect(app.text()).not.toContain("Codex");
  app.frame(0x40);
  expect(app.text()).not.toContain("Codex");
  app.status.magpieResult = 6;
  Object.assign(app.status, { magpieErrorPage: true }); app.frame();
  expect(app.text()).toContain("QUOTA READING FAILED");
  expect(app.text()).not.toContain("Codex");
  app.status.state = "configuring"; app.frame();
  expect(app.text()).toContain("DEVICE SETTINGS");
  expect(app.text()).not.toContain("Codex");
});
