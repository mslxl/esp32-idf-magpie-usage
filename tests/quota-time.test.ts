import { expect, test } from "bun:test";
import { absoluteTime, readingTime, parseInstant, zonedParts } from "../quota-time";

test("pinned IANA rules cross spring and autumn DST transitions", () => {
  expect(zonedParts(Date.parse("2026-03-08T06:59:00Z"), "America/New_York")?.hour).toBe(1);
  expect(zonedParts(Date.parse("2026-03-08T07:00:00Z"), "America/New_York")?.hour).toBe(3);
  expect(zonedParts(Date.parse("2026-11-01T05:59:00Z"), "America/New_York")?.hour).toBe(1);
  expect(zonedParts(Date.parse("2026-11-01T06:00:00Z"), "America/New_York")?.hour).toBe(1);
  expect(zonedParts(Date.parse("2026-01-01T00:00:00Z"), "Australia/Sydney")?.hour).toBe(11);
  expect(zonedParts(Date.parse("2026-07-01T00:00:00Z"), "Australia/Sydney")?.hour).toBe(10);
});
test("Magpie date categories use the configured local day and week", () => {
  const now = Date.parse("2026-10-25T16:00:00Z"); // Monday midnight in Shanghai
  expect(absoluteTime("2026-10-25T17:23:00Z", now, "Asia/Shanghai")).toBe("今天 01:23");
  expect(absoluteTime("2026-10-26T17:23:00Z", now, "Asia/Shanghai")).toBe("明天 01:23");
  expect(absoluteTime("2026-10-28T17:23:00Z", now, "Asia/Shanghai")).toBe("星期四 01:23");
  expect(absoluteTime("2026-11-02T17:23:00Z", now, "Asia/Shanghai")).toBe("11月3日 01:23");
  expect(absoluteTime("2027-01-01T00:00:00Z", now, "Asia/Shanghai")).toBe("2027年1月1日 08:00");
});
test("reading time distinguishes cached deferred future invalid and missing readings", () => {
  const now = Date.parse("2026-10-25T00:10:00Z");
  expect(readingTime({ readAt: "2026-10-25T00:09:31Z" }, now)).toBe("刚刚");
  expect(readingTime({ asOf: "2026-10-25T00:00:00Z", readAt: "2026-10-25T00:09:59Z" }, now)).toBe("缓存 · 10分钟前");
  expect(readingTime({ readDeferred: true, readAt: "2026-10-25T00:00:00Z" }, now)).toBe("未刷新 · 10分钟前");
  expect(readingTime({ readAt: "2026-10-26T00:00:00Z" }, now)).toBe("读取时间在未来");
  expect(readingTime({}, now)).toBe("读取时间未知");
  expect(parseInstant("2026-02-30T00:00:00Z")).toBeNull();
  expect(parseInstant("2026-10-25T08:00:00+08:00")).toBe(parseInstant("2026-10-25T00:00:00Z"));
  expect(absoluteTime("1999-01-01T00:00:00Z", now, "UTC")).toBe("时间超出支持范围");
});
