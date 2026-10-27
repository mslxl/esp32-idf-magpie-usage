import { describe, expect, it } from "vitest";
import { PasswordMode, type WebMetadata } from "./protocol";
import * as S from "./state";

const metadata: WebMetadata = { endpoint: "https://magpie.test", keyPresent: true, revision: 3, allowPlainHttp: false,
  intervalSeconds: 60, timezone: "", layout: "A", adaptiveFont: true, language: "en" };

describe("network state", () => {
  it("explains why another network cannot be added", () => {
    let network = S.initialState("en", true).network;
    expect(S.canAddSlot(network)).toEqual({ ok: false, reason: "network.addHintIncomplete" });
    network = S.editSsid(network, 0, "home");
    expect(S.canAddSlot(network)).toEqual({ ok: true });
    network = S.editSlot(network, 0, { passwordMode: PasswordMode.Set, password: "" });
    expect(S.canAddSlot(network).ok).toBe(false);
    network = { ...network, visible: 5 };
    expect(S.canAddSlot(network)).toEqual({ ok: false, reason: "network.addHintMax" });
  });

  it("reports field errors for missing, duplicate and empty-password networks", () => {
    let network = S.initialState("en", true).network;
    expect(S.validateNetwork(network)).toEqual({ "ssid-0": { key: "validation.networkRequired" } });
    network = { ...S.editSsid(S.editSsid(network, 0, "home"), 1, "home"), visible: 2 };
    expect(S.validateNetwork(network)["ssid-1"]).toEqual({ key: "validation.duplicateSsid" });
    network = S.editSlot(S.editSsid(network, 1, "work"), 1, { passwordMode: PasswordMode.Set });
    expect(S.validateNetwork(network)).toEqual({ "password-1": { key: "validation.passwordEmpty", params: {} } });
    expect(S.editSlot(network, 1, { password: "x" }).errors).toEqual({});
  });

  it("keeps the strongest scan per SSID, sorts by signal and fills the focused or first empty slot", () => {
    let network = S.initialState("en", true).network;
    network = S.addScanResult(network, { ssid: "a", rssi: -70, channel: 1, secure: true });
    network = S.addScanResult(network, { ssid: "a", rssi: -90, channel: 1, secure: true });
    network = S.addScanResult(network, { ssid: "b", rssi: -50, channel: 6, secure: false });
    expect(S.sortedScans(network.scans).map((scan) => [scan.ssid, scan.rssi])).toEqual([["b", -50], ["a", -70]]);
    expect([-50, -60, -70, -90].map(S.signalBars)).toEqual([4, 3, 2, 1]);
    network = S.pickScan({ ...S.editSsid(network, 0, "x"), visible: 2 }, "b");
    expect(network.slots[1].ssid).toBe("b");
    expect(S.pickScan({ ...network, target: 0 }, "a").slots[0].ssid).toBe("a");
  });
});

describe("magpie state", () => {
  it("keeps the raw interval text and validates it only on save", () => {
    let magpie = S.magpieFromDevice(S.initialMagpie(), metadata, "Asia/Shanghai");
    expect(magpie.draft.timezone).toBe("Asia/Shanghai");
    expect(magpie.draft.keyMode).toBe(PasswordMode.Unchanged);
    magpie = S.editMagpie(magpie, { interval: "" });
    expect(magpie.draft.interval).toBe("");
    expect(S.validateMagpie(magpie, 3)).toEqual({ errors: { interval: { key: "validation.interval" } } });
    expect(["29", "3601", "1.5", "abc"].map(S.parseInterval)).toEqual([undefined, undefined, undefined, undefined]);
    expect(S.parseInterval(" 30 ")).toBe(30);
    magpie = S.editMagpie(magpie, { interval: "3600" });
    expect(S.validateMagpie(magpie, 3).payload).toBeInstanceOf(Uint8Array);
  });

  it("maps encoder failures to the field they belong to and clears them on edit", () => {
    let magpie = S.magpieFromDevice(S.initialMagpie(), metadata, "UTC");
    magpie = S.editMagpie(magpie, { endpoint: "http://192.168.1.2" });
    const { errors } = S.validateMagpie(magpie, 3);
    expect(errors).toEqual({ http: { key: "validation.httpNotAllowed", params: {} } });
    magpie = { ...magpie, errors };
    expect(S.editMagpie(magpie, { allowPlainHttp: true }).errors).toEqual({});
  });

  it("keeps a dirty draft when device metadata arrives and reads the device language", () => {
    const zh = S.magpieFromDevice(S.initialMagpie(), { ...metadata, language: "zh" }, "UTC");
    expect(zh.draft.language).toBe("zh");
    const dirty = S.editMagpie(zh, { endpoint: "https://draft.test" });
    expect(S.magpieFromDevice(dirty, metadata, "UTC").draft.endpoint).toBe("https://draft.test");
  });
});

describe("session state", () => {
  it("tracks Wi-Fi connection progress per round", () => {
    let state = S.initialState("en", true);
    state = S.wifiEvent(state, { kind: "connecting", slot: 0, round: 1 });
    state = S.wifiEvent(state, { kind: "slotFailed", slot: 0 });
    state = S.wifiEvent(state, { kind: "connecting", slot: 1, round: 1 });
    expect(state.session.wifi).toEqual({ slot: 1, round: 1, result: "connecting", failedSlots: [0] });
    state = S.wifiEvent(state, { kind: "connecting", slot: 0, round: 2 });
    expect(state.session.wifi!.failedSlots).toEqual([]);
    expect(S.wifiEvent(state, { kind: "connected" }).session.wifi!.result).toBe("connected");
  });

  it("drops drafts and secrets when editing stops and marks a lost link", () => {
    let state = S.initialState("en", true);
    state = S.connected(state, "dev");
    state = { ...state, session: { ...state.session, editing: true },
      network: S.editSlot(state.network, 0, { ssid: "a", password: "secret", passwordMode: PasswordMode.Set }) };
    const lost = S.disconnected(state);
    expect(lost.session.phase).toBe("lost");
    expect(lost.session.editing).toBe(false);
    expect(lost.network.slots[0].password).toBe("");
    expect(lost.network.dirty).toBe(false);
  });
});

describe("directory state", () => {
  it("moves groups in the custom order and cleans up missing preferences", () => {
    const card = (provider: string) => ({ group: 0, provider, user: "u", name: "", alias: "", seat: "", from: "", kind: "", plan: "" });
    let directory: S.DirectoryState = { prefs: { ...S.initialState("en", true).directory.prefs,
      hidden: [{ provider: "gone", user: "u" }] }, dirty: false };
    directory = { ...directory, value: { token: 1, generation: 1, settingsRevision: 0, instanceRevision: 1, groups: 2,
      cards: [card("a"), card("b")], prefs: directory.prefs } };
    expect(S.missingGroups(directory)).toEqual([{ provider: "gone", user: "u" }]);
    directory = S.moveGroup(directory, { provider: "b", user: "u" }, -1);
    expect(S.customGroups(directory).map((item) => item.provider)).toEqual(["b", "a"]);
    expect(S.moveGroup(directory, { provider: "b", user: "u" }, -1)).toBe(directory);
    expect(S.missingGroups(S.cleanupMissing(directory))).toEqual([]);
    expect(S.toggleHidden(directory, { provider: "a", user: "u" }).prefs.hidden).toHaveLength(2);
  });
});
