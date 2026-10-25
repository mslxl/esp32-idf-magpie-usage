import { TIMEZONE_TRANSITIONS, TIMEZONE_START, TIMEZONE_END } from "./timezone-data";
export function parseInstant(value: string): number | null {
  const m = /^(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)(\.\d+)?(Z|[+-]\d\d:\d\d)$/.exec(value);
  if (!m) return null;
  const [year, month, day, hour, minute, second] = m.slice(1, 7).map(Number);
  if (year < 100 || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59 || second > 59) return null;
  const days = new Date(Date.UTC(year, month, 0)).getUTCDate();
  if (day > days) return null;
  let offset = 0;
  if (m[8] !== "Z") {
    const h = Number(m[8].slice(1, 3)), min = Number(m[8].slice(4, 6));
    if (h > 23 || min > 59) return null;
    offset = (h * 60 + min) * (m[8][0] === "+" ? 1 : -1);
  }
  const fraction = Number(`0${m[7] || ".0"}`) * 1000;
  const result = Date.UTC(year, month - 1, day, hour, minute, second) + fraction - offset * 60000;
  return Number.isFinite(result) ? result : null;
}
export function zonedParts(epoch: number, timezone: string) {
  const rules = TIMEZONE_TRANSITIONS[timezone || "UTC"];
  const seconds = epoch / 1000;
  if (!rules || !Number.isFinite(seconds) || seconds < TIMEZONE_START || seconds >= TIMEZONE_END) return null;
  let lo = 0, hi = rules.length;
  while (lo + 1 < hi) { const middle = (lo + hi) >>> 1; if (rules[middle][0] <= seconds) lo = middle; else hi = middle; }
  const date = new Date(epoch + rules[lo][1] * 1000);
  return { year: date.getUTCFullYear(), month: date.getUTCMonth() + 1, day: date.getUTCDate(),
    hour: date.getUTCHours(), minute: date.getUTCMinutes(), weekday: date.getUTCDay(),
    dayNumber: Math.floor(date.getTime() / 86400000) };
}
export function absoluteTime(value: string, now: number, timezone: string): string {
  const epoch = parseInstant(value);
  if (epoch === null) return value ? "时间无效" : "时间未知";
  const target = zonedParts(epoch, timezone), current = zonedParts(now, timezone);
  if (!target || !current) return "时间超出支持范围";
  const clock = `${String(target.hour).padStart(2, "0")}:${String(target.minute).padStart(2, "0")}`;
  const days = target.dayNumber - current.dayNumber;
  if (days === 0) return `今天 ${clock}`;
  if (days === 1) return `明天 ${clock}`;
  const monday = current.dayNumber - (current.weekday + 6) % 7;
  if (days > 1 && target.dayNumber < monday + 7) return `星期${"日一二三四五六"[target.weekday]} ${clock}`;
  return `${target.year !== current.year ? `${target.year}年` : ""}${target.month}月${target.day}日 ${clock}`;
}
export function readingTime(card: { asOf?: string; readAt?: string; readDeferred?: boolean }, now: number): string {
  const value = card.asOf || card.readAt || "";
  const epoch = parseInstant(value);
  if (epoch === null) return value ? "读取时间无效" : "读取时间未知";
  if (!Number.isFinite(now) || now < TIMEZONE_START * 1000) return "设备时间未知";
  const age = (now - epoch) / 1000;
  if (age < 0) return "读取时间在未来";
  const prefix = card.asOf ? "缓存 · " : card.readDeferred ? "未刷新 · " : "";
  if (age < 60) return `${prefix}刚刚`;
  for (const [duration, unit] of [[31536000, "年"], [2592000, "个月"], [604800, "周"], [86400, "天"], [3600, "小时"], [60, "分钟"]] as const)
    if (age >= duration) return `${prefix}${Math.round(age / duration)}${unit}前`;
  return `${prefix}刚刚`;
}
