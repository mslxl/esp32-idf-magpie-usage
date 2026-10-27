import { describe, expect, it } from "vitest";
import { DeviceError, ProtocolDataError, toUserMessage, TransportError, ValidationError } from "./errors";
import { loadLanguage, MESSAGE_KEYS, saveLanguage, translate, LANGUAGE_STORAGE_KEY } from "./i18n";
import { encodeWebSettings, PasswordMode } from "./protocol";

describe("dictionary", () => {
  it("uses the same placeholders in both languages", () => {
    for (const key of MESSAGE_KEYS) {
      const names = (text: string) => [...text.matchAll(/\{(\w+)\}/g)].map((match) => match[1]).sort();
      expect(names(translate("zh", key)), key).toEqual(names(translate("en", key)));
      expect(translate("zh", key).length, key).toBeGreaterThan(0);
    }
  });

  it("defaults to English and persists the choice", () => {
    const values = new Map<string, string>();
    const storage = { getItem: (key: string) => values.get(key) ?? null, setItem: (key: string, value: string) => { values.set(key, value); } };
    expect(loadLanguage(storage)).toBe("en");
    expect(loadLanguage(undefined)).toBe("en");
    expect(loadLanguage({ getItem: () => { throw new Error("denied"); } })).toBe("en");
    saveLanguage(storage, "zh");
    expect(values.get(LANGUAGE_STORAGE_KEY)).toBe("zh");
    expect(loadLanguage(storage)).toBe("zh");
  });
});

describe("toUserMessage", () => {
  it("localizes chooser, device, transport and validation errors", () => {
    expect(toUserMessage(new DOMException("User cancelled the requestDevice() chooser.", "NotFoundError"), "en")).toBe("No device selected.");
    expect(toUserMessage(new DOMException("Bluetooth adapter not available.", "NotFoundError"), "en")).toContain("Bluetooth is turned off");
    expect(toUserMessage(new DOMException("blocked", "SecurityError"), "zh")).toContain("浏览器阻止了蓝牙访问");
    expect(toUserMessage(new DeviceError(10), "en")).toContain("configuration changed");
    expect(toUserMessage(new DeviceError(11), "zh")).toContain("尚未取得完整额度");
    expect(toUserMessage(new DeviceError(7), "zh")).toContain("错误码 7");
    expect(toUserMessage(new DeviceError(99), "en")).toBe("The device rejected the request. (code 99)");
    expect(toUserMessage(new TransportError("timeout"), "zh")).toBe("设备响应超时，请重试。");
    expect(toUserMessage(new ProtocolDataError("invalid crc"), "en")).toContain("invalid crc");
    expect(toUserMessage(new Error("boom"), "en")).toBe("Something went wrong: boom");
  });

  it("localizes protocol validation errors and names the field", () => {
    const value = { endpoint: "http://192.168.1.2", key: "", keyPresent: false, keyMode: PasswordMode.Clear, revision: 0,
      allowPlainHttp: false, intervalSeconds: 30, timezone: "", layout: "A" as const, adaptiveFont: true, language: "en" as const };
    const error = (() => { try { encodeWebSettings(value, ""); } catch (caught) { return caught; } })();
    expect(error).toBeInstanceOf(ValidationError);
    expect((error as ValidationError).field).toBe("http");
    expect(toUserMessage(error, "zh")).toBe("使用 http:// 地址前需要允许明文 HTTP。");
    expect(toUserMessage(error, "en")).toBe("Allow plain HTTP to use an http:// address.");
  });
});
