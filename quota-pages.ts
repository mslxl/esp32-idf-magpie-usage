import { absoluteTime, readingTime, parseInstant } from "./quota-time";
export type QuotaWindow = Partial<Record<"name" | "display" | "unit" | "pool" | "holdsWhy" | "used" |
  "remaining" | "amount" | "limit" | "resetSecs" | "resetsAt" | "unlimited", string>>;
export type QuotaCard = Record<"provider" | "user" | "name" | "alias" | "seat" | "from" | "kind" |
  "plan" | "balance" | "error" | "renew" | "asOf" | "readAt" | "until" | "lastServedAt", string> & {
  windows: QuotaWindow[]; readDeferred?: boolean; balanceSplit?: { label: string; text: string; percent: string; failed: boolean }[];
};
export type QuotaRow = { text: string; x: number; y: number; width: number; height: number; slot: number;
  card: number; header: boolean; icon?: string; progress?: number; right?: string };
export type QuotaPage = QuotaRow[];
type Measure = (text: string, slot?: number) => number;
export type QuotaLayoutOptions = { layout?: string; adaptive?: boolean; top?: number; lineHeight?: (slot: number) => number };
type Line = { text: string; header: boolean; slot: number; height: number; icon?: string; progress?: number; right?: string };

export type QuotaCards = { length: number; card(index: number): QuotaCard };
const compact = (value: string) => value.replace(/\s+/g, " ").trim();
function cardTitle(card: QuotaCard): string {
  const key = (value: string) => compact(value).toLowerCase().replace(/[ _-]/g, "");
  return card.name && key(card.name) === key(card.provider) ? compact(card.name) : card.provider;
}
// PocketJS supplies the shipped atlas metrics and text measurement. Only pagination lives here.
function paginate(cards: QuotaCards, requested: number | undefined, measure: Measure, covered: (scalar: string, slot?: number) => boolean,
  wrap?: (text: string, width: number, slot?: number) => number[], now?: number, timezone = "UTC",
  options: QuotaLayoutOptions = {}, retain = false) {
  const layout = (bodySlot: number) => {
    const retained = new Map<number, QuotaPage>();
    let retainedRows = 0;
    const top = options.top ?? 38, bottom = 270, usable = bottom - top;
    const height = (slot: number) => Math.max(1, options.lineHeight?.(slot) ?? 20);
    const printable = (value: string, slot: number) => Array.from(value, scalar => {
      const cp = scalar.codePointAt(0)!;
      return scalar === "\n" ? scalar : cp >= 32 && cp !== 127 && covered(scalar, slot)
        ? scalar : `[U+${cp.toString(16).toUpperCase().padStart(4, "0")}]`;
    }).join("");
    const lineCache = new Map<string, Line[]>();
    const lines = (value: string, width: number, slot: number, header = false): Line[] => {
      const key = `${slot}:${width}:${header}:${value}`;
      const cached = lineCache.get(key);
      if (cached) return cached.map(row => ({ ...row }));
      const result: Line[] = [];
      for (const paragraph of printable(value, slot).split("\n")) {
        const breaks = wrap?.(paragraph, width, slot) ?? [];
        const append = (text: string) => result.push({ text, header, slot, height: height(slot) });
        if (wrap) {
          let start = 0;
          for (const end of [...breaks, paragraph.length]) {
            if (end > start) append(paragraph.slice(start, end));
            start = end;
          }
        } else {
          let line = "";
          for (const scalar of paragraph) {
            if (line && measure(line + scalar, slot) > width) { append(line); line = ""; }
            line += scalar;
          }
          append(line);
        }
      }
      if (lineCache.size < 256) lineCache.set(key, result.map(row => ({ ...row })));
      return result;
    };
    const sum = (rows: Line[]) => rows.reduce((value, row) => value + row.height, 0);
    const blocksFor = (card: QuotaCard, index: number, width: number, heading: boolean, continued = false): Line[][] => {
      const blocks: Line[][] = [];
      const add = (text: string) => blocks.push(lines(text, width, bodySlot));
      if (heading) {
        const title = lines(cardTitle(card), width - 22, 2, true);
        if (title.length > 2) { blocks.push(lines(`Account ${index + 1}${continued ? " (continued)" : ""}`, width - 22, 2, true)); add(card.provider); }
        else { if (continued) title.push(...lines("(continued)", width - 22, 2, true)); blocks.push(title); }
        blocks[0][0].icon = card.provider;
        blocks[0][0].height = Math.max(18, blocks[0][0].height);
      }
      const identity = new Set([compact(card.provider).toLowerCase(), compact(cardTitle(card)).toLowerCase()]);
      const account = compact(card.user);
      const plan = compact(card.plan);
      const combined = account && !/^(api[ _-]?key|token)$/i.test(account) && plan && !identity.has(plan.toLowerCase()) &&
        measure(`${account}  ${plan}`, bodySlot) <= width;
      if (combined) { add(`${account}  ${plan}`); identity.add(account.toLowerCase()); identity.add(plan.toLowerCase()); }
      for (const [label, raw] of [["Account", card.user], ["Name", card.name], ["Alias", card.alias],
        ["Seat", card.seat], ["From", card.from], ["Kind", card.kind], ["Plan", card.plan]] as const) {
        const value = compact(raw), key = value.toLowerCase();
        if (!value || identity.has(key) || label === "Account" && /^(api[ _-]?key|token)$/i.test(value)) continue;
        identity.add(key);
        add(label === "Name" ? value : `${label}: ${value}`);
      }
      for (const [label, value] of [["Balance", card.balance],
        ["Until", card.until ? now === undefined ? card.until : absoluteTime(card.until, now, timezone) : ""], ["Renew", card.renew],
        ["Read", card.asOf || card.readAt ? now === undefined ? card.asOf || card.readAt : readingTime(card, now) : ""]] as const)
        if (value) add(`${label}: ${value}`);
      for (const part of card.balanceSplit ?? []) add(`${part.label || "Balance part"}: ${part.text}${part.failed ? " (unavailable)" : ""}`);
      if (card.error) add("Provider reading failed; showing available values");
      for (const window of card.windows) {
        const block: Line[] = [];
        const row = (value: string) => block.push(...lines(value, width, bodySlot));
        const name = window.name || "Quota window";
        row(name);
        if (window.display) row(window.display);
        const reset = parseInstant(window.resetsAt || "");
        const expired = now !== undefined && reset !== null && reset <= now && !!(card.asOf || card.readDeferred || card.error);
        if (expired) row("重置时间已过 · 当前额度未知");
        else if (window.unlimited === "true") row("Unlimited");
        else {
          if (window.used !== undefined && window.used !== "") row(`Used: ${window.used}%`);
          if (window.remaining !== undefined && window.remaining !== "") row(`Remaining: ${window.remaining}%`);
          if (!window.used && !window.remaining) row("Usage unknown");
          const percentage = Number(window.used);
          if (window.used !== undefined && window.used !== "" && Number.isFinite(percentage))
            block.push({ text: "", header: false, slot: bodySlot, height: 10, progress: Math.max(0, Math.min(100, percentage)) });
        }
        if (!window.display && block.length >= 2 && !block[0].progress && block[1].text &&
            measure(`${block[0].text}  ${block[1].text}`, bodySlot) <= width && block[0].text === name) {
          block[0].right = block[1].text;
          block.splice(1, 1);
        }
        if (window.amount || window.limit) row(`Amount: ${window.amount || "?"} / ${window.limit || "?"}${window.unit ? ` ${window.unit}` : ""}`);
        if (window.pool) row(`Pool: ${window.pool}`);
        if (window.holdsWhy) row(`Hold: ${window.holdsWhy}`);
        if (window.resetsAt) row(`Reset: ${now === undefined ? window.resetsAt : absoluteTime(window.resetsAt, now, timezone)}`);
        blocks.push(block);
      }
      if (!card.windows.length && !card.balance && !card.error) add("No quota windows");
      return blocks;
    };
    const pages: QuotaPage[] = [];
    let page: QuotaPage = [], count = 0, y = top, column = 0, previousProvider = "", splits = 0;
    const flush = () => {
      if (page.length) {
        if (requested === undefined || requested === count) pages.push(page);
        // Bound retained layout memory independently of the server's account count.
        if (retain && retainedRows + page.length <= 1024) { retained.set(count, page); retainedRows += page.length; }
        ++count;
      }
      page = []; y = top; column = 0; previousProvider = "";
    };
    const nextColumn = () => {
      if (options.layout === "B" && column === 0) { column = 1; y = top; previousProvider = ""; }
      else flush();
    };
    const place = (rows: Line[], card: number, width: number) => {
      for (const row of rows) {
        page.push({ ...row, x: 8 + (column ? 196 : 0), y, width, card });
        y += row.height;
      }
    };
    for (let index = 0; index < cards.length; ++index) {
      const card = cards.card(index);
      let width = options.layout === "B" ? 188 : 384;
      let blocks = blocksFor(card, index, width, previousProvider !== card.provider);
      let total = blocks.reduce((value, block) => value + sum(block), 0);
      const emptyHeight = blocksFor(card, index, width, true).reduce((value, block) => value + sum(block), 0);
      if (width === 188 && emptyHeight > usable) {
        flush(); width = 384; blocks = blocksFor(card, index, width, true);
        total = blocks.reduce((value, block) => value + sum(block), 0);
      }
      if (total <= usable) {
        if (y + total > bottom) {
          nextColumn(); blocks = blocksFor(card, index, width, true);
          total = blocks.reduce((value, block) => value + sum(block), 0);
        }
        if (total <= usable) {
          for (const block of blocks) place(block, index, width);
          y += 8; previousProvider = card.provider;
          if (options.layout === "B" && width === 384) flush();
          continue;
        }
      }
      // Oversized cards occupy full-width pages. Repeat compact identity context on each continuation.
      if (page.length) flush();
      width = 384; blocks = blocksFor(card, index, width, true); ++splits;
      const continuation = () => {
        flush();
        let context = lines(`${card.provider} (continued)`, width - 22, 2, true);
        for (const value of [card.user, card.plan]) if (value) context.push(...lines(value, width, bodySlot));
        if (sum(context) > usable / 3) context = lines(`Account ${index + 1} (continued)`, width - 22, 2, true);
        context[0].icon = card.provider;
        place(context, index, width);
      };
      for (let blockIndex = 0; blockIndex < blocks.length; ++blockIndex) {
        const block = blocks[blockIndex];
        // Keep a complete window together whenever it fits beneath the repeated context.
        if (blockIndex > 0 && y + sum(block) > bottom && sum(block) <= usable * 2 / 3) continuation();
        for (const row of block) {
          if (y + row.height > bottom) continuation();
          place([row], index, width);
        }
      }
      flush();
    }
    flush(); return { pages, count, splits, retained };
  };
  let best = layout(2);
  if (options.adaptive) for (const slot of [21, 1]) {
    const candidate = layout(slot);
    if (candidate.count < best.count || candidate.count === best.count && candidate.splits < best.splits) best = candidate;
  }
  return best;
}

export function quotaPages(cards: QuotaCard[], measure: Measure, covered: (scalar: string, slot?: number) => boolean,
  wrap?: (text: string, width: number, slot?: number) => number[], now?: number, timezone = "UTC", options: QuotaLayoutOptions = {}): QuotaPage[] {
  return paginate({ length: cards.length, card: index => cards[index] }, undefined, measure, covered, wrap, now, timezone, options).pages;
}

export function quotaPage(cards: QuotaCards, requested: number, measure: Measure, covered: (scalar: string, slot?: number) => boolean,
  wrap?: (text: string, width: number, slot?: number) => number[], now?: number, timezone = "UTC", options: QuotaLayoutOptions = {}) {
  let result = paginate(cards, requested, measure, covered, wrap, now, timezone, options, true);
  const index = Math.max(0, Math.min(requested, result.count - 1));
  if (requested !== index && !result.retained.has(index)) result = paginate(cards, index, measure, covered, wrap, now, timezone, options, true);
  return { rows: result.retained.get(index) ?? result.pages[0] ?? [], count: result.count, index, retained: result.retained };
}
