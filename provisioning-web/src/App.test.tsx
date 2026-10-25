// @vitest-environment jsdom
import { afterEach, expect, it, vi } from "vitest";
import { render } from "solid-js/web";
import { Opcode, type Frame } from "./protocol";

const mock = vi.hoisted(() => ({ listener: undefined as ((frame: Frame) => void) | undefined }));
vi.mock("./transport", () => ({
  Note4Transport: class {
    onEvent(listener: (frame: Frame) => void) { mock.listener = listener; }
    async connect() { return "magpie-usage-test"; }
    async send() { return 1; }
  },
}));
import App from "./App";

let dispose: (() => void) | undefined;
afterEach(() => {
  dispose?.();
  document.body.replaceChildren();
});

it("keeps SSID and password inputs mounted and focused while typing", async () => {
  const root = document.createElement("div");
  document.body.append(root);
  dispose = render(() => <App />, root);
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
