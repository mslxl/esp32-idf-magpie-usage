import { element as h, update } from "./dom";
import {
  decodeStatusPayload,
  decodeWebSettings,
  encodeWebSettings,
  canonicalEndpoint,
  encodeBegin,
  SUPPORTED_TIMEZONES,
  type WebSettings,
  Opcode,
  PasswordMode,
  encodeSetSlotPayload,
  MAX_SLOTS,
  type DeviceStatus,
  type PasswordModeValue,
} from "./protocol";
import { Note4Transport } from "./transport";
import { readAccountDirectory, type AccountDirectory } from "./account-directory";
import { defaultPrefs, identityKey, uploadDisplayPrefs, type DisplayPrefs, type Identity } from "./display-prefs";

const transport = new Note4Transport();

type Slot = {
  ssid: string;
  password: string;
  passwordPresent: boolean;
  passwordMode: PasswordModeValue;
};
type Scan = { ssid: string; rssi: number; channel: number; secure: boolean };

function emptySlot(): Slot {
  return { ssid: "", password: "", passwordPresent: false, passwordMode: PasswordMode.Clear };
}

export default function mountApp(root: HTMLElement): () => void {
  let mounted = true;
  let ready = false;
  function createState<T>(initial: T): [() => T, (next: T | ((old: T) => T)) => void];
  function createState<T>(): [() => T | undefined, (next: T | undefined | ((old: T | undefined) => T | undefined)) => void];
  function createState<T>(initial?: T) {
    let value = initial;
    return [() => value, (next: T | undefined | ((old: T | undefined) => T | undefined)) => {
      value = typeof next === "function" ? (next as (old: T | undefined) => T | undefined)(value) : next;
      if (ready && mounted) paint();
    }];
  }
  const [connected, setConnected] = createState(false);
  const [deviceName, setDeviceName] = createState("");
  const [message, setMessage] = createState("请先连接 Magpie Usage");
  const [slots, setSlots] = createState<Slot[]>(Array.from({ length: MAX_SLOTS }, emptySlot));
  const [visibleSlots, setVisibleSlots] = createState(1);
  const [scans, setScans] = createState<Scan[]>([]);
  const [applying, setApplying] = createState(false);
  const [busy, setBusy] = createState(false);
  const [editing, setEditing] = createState(false);
  const [tab, setTab] = createState<"network" | "magpie">("network");
  const [magpie, setMagpie] = createState<WebSettings>({ endpoint: "", key: "", keyPresent: false,
    keyMode: PasswordMode.Clear, revision: 0, allowPlainHttp: false, intervalSeconds: 30,
    timezone: "", layout: "A", adaptiveFont: true });
  const [revision, setRevision] = createState(0);
  const [savedEndpoint, setSavedEndpoint] = createState("");
  const [magpieReady, setMagpieReady] = createState(false);
  const [networkDirty, setNetworkDirty] = createState(false);
  const [magpieDirty, setMagpieDirty] = createState(false);
  const [confirmExit, setConfirmExit] = createState(false);
  const [directory, setDirectory] = createState<AccountDirectory>();
  const [prefs, setPrefs] = createState<DisplayPrefs>(defaultPrefs());
  const [prefsDirty, setPrefsDirty] = createState(false);

  function clearSecrets(): void {
    setSlots((items) => items.map((slot) => ({ ...slot, password: "" })));
    setMagpie((value) => ({ ...value, key: "" }));
  }

  function stopEditing(): void {
    setDirectory(undefined);
    setPrefsDirty(false);
    setEditing(false);
    setApplying(false);
    setConfirmExit(false);
    setNetworkDirty(false);
    setMagpieDirty(false);
    clearSecrets();
  }

  const unsubscribe = transport.onEvent((frame) => {
    if (frame.opcode === Opcode.Status) {
      try {
        const status = decodeStatusPayload(frame.payload);
        const canEdit = status.state === 1 || status.state === 2;
        if (!canEdit) {
          stopEditing();
          applyStatus(status);
          setMessage("设备已退出设置模式；长按设备下键可重新进入");
        } else {
          if (!editing() && !busy()) {
            applyStatus(status);
            setMagpieReady(false);
            void transport.send(Opcode.GetWebSettings).catch(() => setMessage("设备固件不支持 Magpie 配置，请升级固件"));
          } else if (!editing() || (!busy() && !networkDirty())) applyStatus(status);
          setEditing(true);
          if (!busy()) {
            setApplying(false);
            setMessage("已读取设备配置；密码不会从设备传回网页");
          }
        }
      } catch {
        setMessage("设备配置无法解析");
      }
    } else if (frame.opcode === Opcode.WebSettings) {
      try {
        const metadata = decodeWebSettings(frame.payload);
        setRevision(metadata.revision);
        setSavedEndpoint(metadata.endpoint);
        if (!magpieDirty()) setMagpie({ ...metadata, timezone: metadata.timezone || Intl.DateTimeFormat().resolvedOptions().timeZone,
          key: "", keyMode: metadata.keyPresent ? PasswordMode.Unchanged : PasswordMode.Clear });
        setMagpieReady(true);
        if (!applying()) setMessage("已读取设备配置；密码和 Key 不会从设备传回网页");
      } catch {
        setMessage("Magpie 配置无法解析");
      }
    } else if (frame.opcode === Opcode.ScanResult) {
      try {
        const length = frame.payload[0];
        if (!length || frame.payload.length !== length + 4) throw new Error("invalid scan result");
        const ssid = new TextDecoder().decode(frame.payload.slice(1, length + 1));
        const rawRssi = frame.payload[length + 1];
        const rssi = rawRssi > 127 ? rawRssi - 256 : rawRssi;
        const channel = frame.payload[length + 2];
        const secure = frame.payload[length + 3] !== 0;
        setScans((items) => [...items.filter((item) => item.ssid !== ssid), {
          ssid,
          rssi: Number(rssi),
          channel: Number(channel),
          secure,
        }]);
      } catch {
        setMessage("扫描结果无法解析");
      }
    } else if (frame.opcode === Opcode.ScanDone) {
      setMessage("扫描完成");
    } else if (frame.opcode === Opcode.Applying) {
      setApplying(true);
      setMessage("设备正在应用配置");
    } else if (frame.opcode === Opcode.Connecting) {
      stopEditing();
      setMessage(`设备正在尝试网络 ${frame.slot + 1}，第 ${frame.payload[0] ?? 1} / 3 轮`);
    } else if (frame.opcode === Opcode.Connected) {
      stopEditing();
      setMessage("设备已联网");
    } else if (frame.opcode === Opcode.Failure) {
      setApplying(false);
      setMessage("所有网络尝试失败，可在设备上按确定重试");
    } else if (frame.opcode === Opcode.Nack && frame.requestId === 0) {
      setConnected(false);
      stopEditing();
      setSlots((items) => items.map((slot) => ({ ...slot, password: "" })));
      setMessage("Magpie Usage 蓝牙连接已断开");
    }
  });

  const canAddSlot = () => visibleSlots() < MAX_SLOTS &&
    slots().slice(0, visibleSlots()).every((slot) => {
      if (!slot.ssid) return false;
      try {
        encodeSetSlotPayload(slot);
        return true;
      } catch {
        return false;
      }
    });

  function addSlot(): void {
    if (!busy() && !applying() && editing() && canAddSlot()) {
      setVisibleSlots((count) => count + 1);
      setNetworkDirty(true);
    }
  }

  function applyStatus(status: DeviceStatus): void {
    const lastConfigured = status.slots.reduce((last, slot, index) => slot.ssid ? index : last, -1);
    setVisibleSlots(Math.max(1, lastConfigured + 1));
    setSlots(status.slots.map((slot) => ({
      ssid: slot.ssid,
      password: "",
      passwordPresent: slot.passwordPresent,
      passwordMode: slot.ssid ? PasswordMode.Unchanged : PasswordMode.Clear,
    })));
  }

  const updateSlot = (index: number, patch: Partial<Slot>) => {
    setNetworkDirty(true);
    setSlots((items) => items.map((item, position) => position === index ? { ...item, ...patch } : item));
  };

  function updateMagpie(patch: Partial<WebSettings>): void {
    setMagpieDirty(true);
    setMagpie((value) => ({ ...value, ...patch }));
  }

  function updateSsid(index: number, ssid: string): void {
    const previous = slots()[index];
    updateSlot(index, {
      ssid,
      passwordPresent: ssid === previous.ssid && previous.passwordPresent,
      passwordMode: ssid && ssid === previous.ssid && previous.passwordPresent
        ? PasswordMode.Unchanged
        : PasswordMode.Clear,
      password: "",
    });
  }

  async function connect() {
    if (busy()) return;
    setBusy(true);
    try {
      setDeviceName(await transport.connect());
      setConnected(true);
      setEditing(false);
      setMagpieReady(false);
      setNetworkDirty(false);
      setMagpieDirty(false);
      clearSecrets();
      setMessage("已连接，正在读取配置");
      await transport.send(Opcode.GetStatus);
      try {
        await transport.send(Opcode.GetWebSettings);
      } catch {
        setMessage("设备固件不支持 Magpie 配置，请升级固件");
      }
    } catch (error) {
      setConnected(false);
      setMessage(error instanceof Error ? error.message : "连接失败");
    } finally {
      setBusy(false);
    }
  }

  async function scan() {
    if (busy() || applying() || !editing()) return;
    setBusy(true);
    try {
      setScans([]);
      setMessage("设备正在扫描 2.4G Wi‑Fi");
      await transport.send(Opcode.Scan);
    } catch (error) {
      setMessage(error instanceof Error ? error.message : "扫描失败");
    } finally {
      setBusy(false);
    }
  }

  async function commit() {
    if (busy() || applying() || !editing()) return;
    if (!magpieReady()) { setMessage("请先读取 Magpie 配置；旧版固件需要升级"); return; }
    const selected = tab();
    const current = slots();
    const currentMagpie = magpie();
    setBusy(true);
    try {
      let payloads: Uint8Array[];
      if (selected === "network") {
        if (!current.some((slot) => slot.ssid.length > 0)) throw new Error("至少配置一个 Wi‑Fi");
        const names = current.filter((slot) => slot.ssid).map((slot) => slot.ssid);
        if (new Set(names).size !== names.length) throw new Error("不能配置重复的 SSID");
        payloads = current.map(encodeSetSlotPayload);
      } else payloads = [encodeWebSettings({ ...currentMagpie, revision: revision() }, savedEndpoint())];
      setApplying(true);
      setMessage("正在发送配置");
      await transport.send(Opcode.Begin, encodeBegin(revision()));
      for (let index = 0; index < payloads.length; index += 1) {
        await transport.send(selected === "network" ? Opcode.SetSlot : Opcode.SetWebSettings, payloads[index], index);
      }
      await transport.send(Opcode.Commit);
      setRevision((value) => value + 1);
      if (!editing()) throw new Error("设备已退出设置模式，请重新进入后编辑");
      if (selected === "network") {
        setSlots(current.map((slot) => ({ ...slot, password: "",
          passwordPresent: slot.ssid.length > 0 && (slot.passwordMode === PasswordMode.Set ||
            (slot.passwordMode === PasswordMode.Unchanged && slot.passwordPresent)),
          passwordMode: slot.ssid ? PasswordMode.Unchanged : PasswordMode.Clear,
        })));
        setNetworkDirty(false);
      } else {
        const keyPresent = currentMagpie.keyMode === PasswordMode.Set ||
          (currentMagpie.keyMode === PasswordMode.Unchanged && currentMagpie.keyPresent);
        setMagpie({ ...currentMagpie, key: "", keyPresent, keyMode: keyPresent ? PasswordMode.Unchanged : PasswordMode.Clear });
        const endpoint = canonicalEndpoint(currentMagpie.endpoint);
        if (endpoint !== canonicalEndpoint(savedEndpoint())) {
          setDirectory(undefined); setPrefs(defaultPrefs()); setPrefsDirty(false);
        }
        setSavedEndpoint(endpoint);
        setMagpieDirty(false);
      }
      setApplying(false);
      setMessage("设置已保存，仍处于设置模式");
    } catch (error) {
      setApplying(false);
      const detail = error instanceof Error ? error.message : "配置提交失败";
      setMessage(detail.includes("错误码 10") ? "设备配置已改变；草稿已保留。重新读取版本并检查后再次保存。" : detail);
    } finally {
      setBusy(false);
    }
  }

  async function loadDirectory() {
    if (busy() || !editing()) return;
    setBusy(true);
    try {
      const next = await readAccountDirectory(transport);
      if (!editing()) throw new Error("设备已退出设置模式");
      const previous = directory();
      const instanceChanged = previous !== undefined && previous.instanceRevision !== next.instanceRevision;
      setDirectory(next);
      if (!prefsDirty() || instanceChanged) { setPrefs(next.prefs); setPrefsDirty(false); }
      setMessage("已读取完整账号目录");
    } catch (error) {
      const detail = error instanceof Error ? error.message : "目录读取失败";
      setMessage(detail.includes("错误码 11") ? "设备尚未取得完整额度；退出设置并成功读取后再进入。" : detail);
    } finally { setBusy(false); }
  }

  const directoryGroups = () => {
    const unique = new Map<string, Identity>();
    for (const card of directory()?.cards ?? []) unique.set(identityKey(card), { provider: card.provider, user: card.user });
    return [...unique.values()];
  };
  const customGroups = () => {
    const saved = prefs().custom, keys = new Set(saved.map(identityKey));
    return [...saved, ...directoryGroups().filter(item => !keys.has(identityKey(item)))];
  };
  function updatePrefs(patch: Partial<DisplayPrefs>) { setPrefs(value => ({ ...value, ...patch })); setPrefsDirty(true); }
  function moveGroup(identity: Identity, direction: number) {
    const ordered = customGroups(), at = ordered.findIndex(item => identityKey(item) === identityKey(identity));
    if (at + direction < 0 || at + direction >= ordered.length) return;
    [ordered[at], ordered[at + direction]] = [ordered[at + direction], ordered[at]];
    updatePrefs({ custom: ordered });
  }
  const missingGroups = () => {
    const current = new Set(directoryGroups().map(identityKey)), missing = new Map<string, Identity>();
    for (const item of [...prefs().hidden, ...prefs().custom]) if (!current.has(identityKey(item))) missing.set(identityKey(item), item);
    return [...missing.values()];
  };
  function cleanupMissing() {
    const keys = new Set(directoryGroups().map(identityKey));
    updatePrefs({ hidden: prefs().hidden.filter(item => keys.has(identityKey(item))), custom: prefs().custom.filter(item => keys.has(identityKey(item))) });
  }
  function toggleHidden(identity: Identity) {
    const key = identityKey(identity);
    setPrefs(value => ({ ...value, hidden: value.hidden.some(item => identityKey(item) === key)
      ? value.hidden.filter(item => identityKey(item) !== key) : [...value.hidden, { provider: identity.provider, user: identity.user }] }));
    setPrefsDirty(true);
  }
  async function savePrefs() {
    if (busy() || !editing() || !directory()) return;
    setBusy(true);
    try {
      await transport.send(Opcode.Begin, encodeBegin(revision()));
      const candidate = { ...prefs(), custom: customGroups() };
      await uploadDisplayPrefs(transport, revision(), candidate);
      await transport.send(Opcode.Commit);
      if (!editing()) throw new Error("设备已退出设置模式，请重新进入后编辑");
      setPrefs(candidate); setRevision(value => value + 1); setPrefsDirty(false);
      setMessage("账号显示偏好已保存，仍处于设置模式");
    } catch (error) {
      const detail = error instanceof Error ? error.message : "偏好保存失败";
      setMessage(detail.includes("错误码 10") ? "设备配置已改变；偏好草稿已保留。重新读取版本后重试。" : detail);
    } finally { setBusy(false); }
  }

  async function exitSetup() {
    if (busy() || applying() || !editing()) return;
    if ((networkDirty() || magpieDirty() || prefsDirty()) && !confirmExit()) { setConfirmExit(true); return; }
    setBusy(true);
    try {
      await transport.send(Opcode.ExitSetup);
      stopEditing();
      setNetworkDirty(false);
      setMagpieDirty(false);
      setMessage("已退出设置模式，设备将使用已保存的配置");
    } catch (error) {
      setMessage(error instanceof Error ? error.message : "退出设置失败");
    } finally {
      setBusy(false);
    }
  }

  const button = (text: string, action: () => unknown, disabled = false, props = {}) =>
    h("button", { type: "button", disabled, onClick: action, ...props }, text);
  const option = (value: string | number, text: string, disabled = false) => h("option", { value: String(value), disabled }, text);
  const label = (text: string, control: HTMLElement) => h("label", {}, text, control);
  const input = (props: Record<string, unknown>, action: (input: HTMLInputElement) => void) =>
    h("input", { ...props, onInput: (event: Event) => action(event.currentTarget as HTMLInputElement) });
  const select = (value: string | number, options: HTMLElement[], action: (value: string) => void, props = {}) => {
    const node = h("select", { ...props, onChange: (event: Event) => action((event.currentTarget as HTMLSelectElement).value) }, options) as HTMLSelectElement;
    node.value = String(value); return node;
  };
  const checkbox = (text: string, checked: boolean, action: (checked: boolean) => void, props = {}) =>
    label(text, h("input", { type: "checkbox", checked, ...props, onChange: (event: Event) => action((event.currentTarget as HTMLInputElement).checked) }));
  const passwordOptions = (present: boolean, noun: string) => [option(PasswordMode.Unchanged, `保持设备中的${noun}`, !present),
    option(PasswordMode.Set, `设置新${noun}`), option(PasswordMode.Clear, noun === "密码" ? "清除密码（开放网络）" : "清除 Key")];
  function directoryView(): HTMLElement | undefined {
    const value = directory(); if (!value) return;
    return h("section", { "aria-label": "完整账号目录", "data-key": "directory" },
      h("p", {}, `${value.groups} 个偏好组 · ${value.cards.length} 张额度卡片`),
      h("fieldset", { disabled: busy() || !editing() },
        label("显示顺序", select(prefs().order, [option("original", "原顺序"), option("custom", "自定义顺序"), option("smart", "智能顺序")], value => updatePrefs({ order: value as DisplayPrefs["order"] }), { "aria-label": "显示顺序" })),
        label("厂商范围", select(prefs().providerMode, [option("all", "全部厂商（含新厂商）"), option("selected", "指定厂商")], value => updatePrefs({ providerMode: value as DisplayPrefs["providerMode"] }), { "aria-label": "厂商范围" })),
        prefs().providerMode === "selected" ? [...new Set([...value.cards.map(card => card.provider), ...prefs().providers])].map(provider =>
          checkbox(provider, prefs().providers.includes(provider), checked => updatePrefs({ providers: checked ? [...prefs().providers, provider] : prefs().providers.filter(item => item !== provider) }))) : [],
        ([[1, "subscription"], [2, "plan"], [4, "balance"], [8, "unknown"]] as const).map(([bit, name]) =>
          checkbox(name, !!(prefs().kinds & bit), checked => updatePrefs({ kinds: checked ? prefs().kinds | bit : prefs().kinds & ~bit }), { "aria-label": `显示类型 ${name}` })),
        label("名称筛选", input({ "aria-label": "名称筛选", value: prefs().query }, field => updatePrefs({ query: field.value }))),
        prefs().order === "custom" ? customGroups().map(identity => h("div", {},
          h("span", {}, `${identity.provider} · ${identity.user || "（空账号标识）"}${directoryGroups().some(item => identityKey(item) === identityKey(identity)) ? "" : " · 当前不存在"}`),
          button("上移", () => moveGroup(identity, -1)), button("下移", () => moveGroup(identity, 1)))) : [],
        h("p", {}, `当前不存在的偏好：${missingGroups().length}`),
        h("ul", { "aria-label": "当前不存在的账号偏好" }, missingGroups().map(identity => h("li", {}, `${identity.provider} · ${identity.user || "（空账号标识）"}`))),
        button("清理当前不存在的账号偏好", cleanupMissing)),
      value.cards.length === 0 ? h("p", {}, "Magpie 暂无账号额度") : [],
      value.cards.map((card, index) => h("article", { class: "slot", "data-key": `card-${index}` },
        h("strong", {}, `${card.provider} · ${card.user || "（空账号标识）"}`),
        h("p", {}, [card.name, card.alias, card.seat, card.from, card.kind || "unknown", card.plan].filter(Boolean).join(" · ")),
        h("small", {}, `偏好组 ${card.group + 1} · 相同厂商与账号标识的卡片共用显示偏好`),
        checkbox("隐藏此偏好组", prefs().hidden.some(item => identityKey(item) === identityKey(card)), () => toggleHidden(card),
          { "aria-label": `隐藏 ${card.provider} ${card.user} ${card.name}`, disabled: busy() || !editing() }))),
      button("保存账号显示偏好", savePrefs, busy() || !editing() || !prefsDirty()));
  }
  function paint(): void {
    const locked = busy() || applying() || !editing();
    update(root, h("main", {},
      h("h1", {}, "Magpie Usage 设置"), h("p", {}, message()),
      connected() ? h("strong", {}, deviceName()) : button("连接设备", connect, busy()),
      connected() ? [
        h("div", { class: "tabs", role: "tablist", "aria-label": "设备设置" },
          button("网络", () => setTab("network"), false, { id: "network-tab", role: "tab", "aria-selected": String(tab() === "network"), "aria-controls": "network-panel" }),
          button("Magpie 配置", () => setTab("magpie"), false, { id: "magpie-tab", role: "tab", "aria-selected": String(tab() === "magpie"), "aria-controls": "magpie-panel" })),
        h("section", { id: "network-panel", role: "tabpanel", "aria-labelledby": "network-tab", hidden: tab() !== "network", "data-key": "network" },
          button("扫描 Wi‑Fi", scan, locked),
          h("fieldset", { disabled: locked }, slots().slice(0, visibleSlots()).map((slot, index) =>
            h("section", { class: "slot", "data-key": `slot-${index}` },
              h("h2", {}, `网络 ${index + 1}`),
              h("datalist", { id: `wifi-scan-results-${index}` }, scans().map(network => option(network.ssid, `${network.rssi} dBm`))),
              input({ type: "text", list: `wifi-scan-results-${index}`, "aria-label": `网络 ${index + 1} SSID`, placeholder: "选择扫描到的网络或输入 SSID", value: slot.ssid }, field => updateSsid(index, field.value)),
              slot.ssid ? label("密码处理", select(slot.passwordMode, passwordOptions(slot.passwordPresent, "密码"), value => updateSlot(index, { passwordMode: Number(value) as PasswordModeValue, password: "" }))) : [],
              slot.ssid && slot.passwordMode === PasswordMode.Set ? input({ type: "password", placeholder: "输入新密码", value: slot.password, "data-key": "password" }, field => updateSlot(index, { password: field.value })) : [],
              slot.ssid && slot.passwordMode === PasswordMode.Unchanged && slot.passwordPresent ? h("small", {}, "设备已有密码，网页不会显示密码内容。") : [])),
            canAddSlot() ? button("+", addSlot, false, { class: "add-slot", "aria-label": "添加 Wi-Fi" }) : [])),
        h("section", { id: "magpie-panel", role: "tabpanel", "aria-labelledby": "magpie-tab", hidden: tab() !== "magpie", "data-key": "magpie" },
          h("p", {}, "填写 Magpie 管理网页地址。Web key 只填密钥值，不含 ?k= 或完整链接。"),
          h("fieldset", { disabled: locked || !magpieReady() },
            label("Magpie endpoint", input({ type: "url", "aria-label": "Magpie endpoint", placeholder: "https://magpie.example.com", value: magpie().endpoint }, field => updateMagpie({ endpoint: field.value }))),
            label("Key 处理", select(magpie().keyMode, passwordOptions(magpie().keyPresent, " Key"), value => updateMagpie({ keyMode: Number(value) as PasswordModeValue, key: "" }), { "aria-label": "Key 处理" })),
            magpie().keyMode === PasswordMode.Set ? h("label", { "data-key": "key-input" }, "Magpie Key", input({ type: "password", "aria-label": "Magpie Key", autocomplete: "new-password", value: magpie().key }, field => updateMagpie({ key: field.value }))) : [],
            magpie().keyPresent ? h("small", {}, "设备已设置 Web key，网页不会读取或显示已有凭据。") : [],
            checkbox("允许明文 HTTP", magpie().allowPlainHttp, checked => updateMagpie({ allowPlainHttp: checked }), { "data-key": "http" }),
            label("额度读取间隔（秒）", input({ type: "number", "aria-label": "额度读取间隔", min: "30", max: "3600", value: magpie().intervalSeconds }, field => updateMagpie({ intervalSeconds: Number(field.value) }))),
            label("显示时区", input({ type: "text", "aria-label": "显示时区", list: "supported-timezones", value: magpie().timezone }, field => updateMagpie({ timezone: field.value }))),
            h("datalist", { id: "supported-timezones" }, SUPPORTED_TIMEZONES.map(zone => option(zone, ""))),
            label("额度布局", select(magpie().layout, [option("A", "A · 单列紧凑卡片"), option("B", "B · 双列紧凑卡片")], value => updateMagpie({ layout: value as "A" | "B" }), { "aria-label": "额度布局" })),
            checkbox("自适应字号", magpie().adaptiveFont, checked => updateMagpie({ adaptiveFont: checked }))),
          button("读取完整账号目录", loadDirectory, locked), directoryView()),
        button(applying() ? "应用中…" : "应用设置", commit, locked || !magpieReady()),
        button("重新读取配置版本", async () => {
          setBusy(true);
          try { await transport.send(Opcode.GetStatus); await transport.send(Opcode.GetWebSettings); }
          catch (error) { setMessage(error instanceof Error ? error.message : "读取失败"); }
          finally { setBusy(false); }
        }, locked),
        button("退出设置模式", exitSetup, locked),
        confirmExit() ? h("section", { role: "dialog", "aria-modal": "true", "aria-label": "退出设置模式", class: "slot" },
          h("p", {}, "未应用的设置将丢失，已保存的设置会保留。"), button("确认退出", exitSetup, busy()), button("继续编辑", () => setConfirmExit(false), busy())) : []
      ] : []));
  }
  ready = true; paint();
  return () => { mounted = false; unsubscribe(); clearSecrets(); root.replaceChildren(); };
}
