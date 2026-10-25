import { test, expect } from "bun:test";
import { quotaPages, quotaPage, type QuotaCard } from "../quota-pages";

const measure = (text: string) => Array.from(text).length * 8;
const card = (provider: string, windows: QuotaCard["windows"] = []): QuotaCard => ({
  provider, user: "", name: "", alias: "", seat: "", from: "", kind: "", plan: "", balance: "",
  error: "", renew: "", asOf: "", readAt: "", until: "", lastServedAt: "", windows,
});
test("materializes one requested page while reading cards one at a time", () => {
  const cards = Array.from({ length: 100 }, (_, i) => card(`Provider ${i}`, [{ name: "5h", used: "10" }]));
  const expected = quotaPages(cards, measure, () => true);
  const page = quotaPage({ length: cards.length, card: index => cards[index] }, 3, measure, () => true);
  expect(page.count).toBe(expected.length);
  expect(page.rows).toEqual(expected[3]);
  expect(page.rows.length).toBeLessThan(expected.flat().length);
});

test("packs different providers on one page and retains every duplicate window", () => {
  const pages = quotaPages([card("Codex", [{ name: "5h", used: "0" }, { name: "5h", used: "110" }]), card("Claude")], measure, () => true);
  expect(pages.length).toBe(1);
  const lines = pages.flat().map(row => row.text);
  expect(lines).toContain("Codex");
  expect(lines).toContain("Claude");
  expect(lines).toContain("Used: 0%");
  expect(lines).toContain("Used: 110%");
});
test("B layout uses both 188px columns and sends tall cards to a full-width page", () => {
  const pages = quotaPages([card("Codex"), card("Claude"), card("Copilot"), card("DeepSeek"), card("Codex"), card("Claude")], measure, () => true,
    undefined, undefined, "UTC", { layout: "B", adaptive: false });
  expect(pages).toHaveLength(1);
  expect(pages[0].some(row => row.x === 204 && row.width === 188)).toBe(true);
  const tall = card("Tall", Array.from({ length: 8 }, (_, index) => ({ name: `window ${index}`, used: "10" })));
  const mixed = quotaPages([card("Codex"), tall, card("Claude")], measure, () => true,
    undefined, undefined, "UTC", { layout: "B", adaptive: false });
  const tallRows = mixed.flat().filter(row => row.card === 1);
  expect(tallRows.every(row => row.x === 8 && row.width === 384)).toBe(true);
  for (let index = 0; index < 8; ++index) expect(tallRows.map(row => row.text)).toContain(`window ${index}`);
  expect(mixed.flat().filter(row => row.card === 2).every(row => row.width === 188)).toBe(true);
});

test("long headers retain original text and do not leave an isolated provider title", () => {
  const provider = "ExtraordinarilyLongProvider".repeat(12);
  const pages = quotaPages([{ ...card(provider), user: "account", plan: "plan", name: "LongName".repeat(80) }], measure, () => true);
  const body = pages.flat().filter(row => !row.header).map(row => row.text).join("");
  expect(body).toContain(provider);
  expect(body).toContain("LongName".repeat(80));
  for (const page of pages) {
    expect(page.every(row => measure(row.text) <= row.width - (row.header ? 22 : 0))).toBe(true);
    expect(page.some(row => !row.header)).toBe(true);
    expect(page.every(row => row.y + row.height <= 270)).toBe(true);
  }
});

test("continues large cards without losing windows or missing glyphs", () => {
  const account = card("厂商", Array.from({ length: 30 }, (_, i) => ({ name: `window ${i}`, unlimited: i === 0 ? "true" : "", used: "" })));
  const pages = quotaPages([account], measure, scalar => scalar.codePointAt(0)! < 128);
  expect(pages.length).toBeGreaterThan(1);
  const lines = pages.flat().map(row => row.text);
  expect(lines.some(text => text.includes("[U+5382]"))).toBe(true);
  for (let i = 0; i < 30; i++) expect(lines).toContain(`window ${i}`);
  expect(lines).toContain("Unlimited");
  expect(lines).toContain("Usage unknown");
  expect(pages.every(page => page.every(row => row.y >= 38 && row.y + 20 <= 270))).toBe(true);
});
