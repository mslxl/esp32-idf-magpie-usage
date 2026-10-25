import { For, Show, createSignal } from "solid-js";
import {
  decodeStatusPayload,
  Opcode,
  PasswordMode,
  encodeSetSlotPayload,
  type DeviceStatus,
  type PasswordModeValue,
} from "./protocol";
import { Note4Transport } from "./transport";

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

export default function App() {
  const [connected, setConnected] = createSignal(false);
  const [deviceName, setDeviceName] = createSignal("");
  const [message, setMessage] = createSignal("请先连接 Magpie Usage");
  const [slots, setSlots] = createSignal<Slot[]>(Array.from({ length: 5 }, emptySlot));
  const [scans, setScans] = createSignal<Scan[]>([]);
  const [applying, setApplying] = createSignal(false);
  const [busy, setBusy] = createSignal(false);
  const [editing, setEditing] = createSignal(false);

  transport.onEvent((frame) => {
    if (frame.opcode === Opcode.Status) {
      try {
        const status = decodeStatusPayload(frame.payload);
        if (frame.requestId === 0) {
          const canEdit = status.state === 1 || status.state === 2;
          setEditing(canEdit);
          if (canEdit) setApplying(false);
        }
        if (!applying()) {
          const canEdit = status.state === 1 || status.state === 2;
          if (frame.requestId !== 0 || !editing() || !canEdit) applyStatus(status);
          setEditing(canEdit);
          if (!canEdit) setApplying(false);
          setMessage(canEdit ? "已读取设备配置；密码不会从设备传回网页"
            : "设备已退出配网；长按设备下键可重新进入");
        }
      } catch {
        setMessage("设备配置无法解析");
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
      setEditing(false);
      setApplying(true);
      setMessage("设备正在应用配置");
    } else if (frame.opcode === Opcode.Connecting) {
      setEditing(false);
      setApplying(false);
      setMessage(`设备正在尝试网络 ${frame.slot + 1}，第 ${frame.payload[0] ?? 1} / 3 轮`);
    } else if (frame.opcode === Opcode.Connected) {
      setEditing(false);
      setApplying(false);
      setMessage("设备已联网");
    } else if (frame.opcode === Opcode.Failure) {
      setApplying(false);
      setMessage("所有网络尝试失败，可在设备上按确定重试");
    } else if (frame.opcode === Opcode.Nack && frame.requestId === 0) {
      setConnected(false);
      setEditing(false);
      setApplying(false);
      setSlots((items) => items.map((slot) => ({ ...slot, password: "" })));
      setMessage("Magpie Usage 蓝牙连接已断开");
    }
  });

  function applyStatus(status: DeviceStatus): void {
    setSlots(status.slots.map((slot) => ({
      ssid: slot.ssid,
      password: "",
      passwordPresent: slot.passwordPresent,
      passwordMode: slot.ssid ? PasswordMode.Unchanged : PasswordMode.Clear,
    })));
  }

  const updateSlot = (index: number, patch: Partial<Slot>) => setSlots((items) => items.map((item, position) =>
    position === index ? { ...item, ...patch } : item));

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
      setMessage("已连接，正在读取配置");
      await transport.send(Opcode.Begin);
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
    const current = slots();
    if (!current.some((slot) => slot.ssid.length > 0)) {
      setMessage("至少配置一个 Wi‑Fi");
      return;
    }
    setBusy(true);
    try {
      setApplying(true);
      setMessage("正在发送配置");
      await transport.send(Opcode.Begin);
      for (let index = 0; index < current.length; index += 1) {
        const slot = current[index];
        await transport.send(Opcode.SetSlot, encodeSetSlotPayload({
          ssid: slot.ssid,
          passwordMode: slot.passwordMode,
          password: slot.password,
        }), index);
      }
      await transport.send(Opcode.Commit);
      setSlots(current.map((slot) => ({ ...slot, password: "",
        passwordPresent: slot.ssid.length > 0 && (slot.passwordMode === PasswordMode.Set ||
          (slot.passwordMode === PasswordMode.Unchanged && slot.passwordPresent)),
        passwordMode: slot.ssid ? PasswordMode.Unchanged : PasswordMode.Clear,
      })));
      setEditing(false);
      setApplying(false);
      setMessage("配置已提交，设备正在连接 Wi‑Fi");
    } catch (error) {
      setApplying(false);
      setMessage(error instanceof Error ? error.message : "配置提交失败");
    } finally {
      setBusy(false);
    }
  }

  async function abort() {
    if (busy() || applying() || !editing()) return;
    setBusy(true);
    try {
      await transport.send(Opcode.Abort);
      setSlots((items) => items.map((slot) => ({ ...slot, password: "" })));
      setEditing(false);
      setMessage("已放弃未应用的配置");
    } catch (error) {
      setMessage(error instanceof Error ? error.message : "放弃配置失败");
    } finally {
      setBusy(false);
    }
  }

  return (
    <main>
      <h1>Magpie Usage 配网</h1>
      <p>{message()}</p>
      <Show when={!connected()} fallback={<strong>{deviceName()}</strong>}>
        <button disabled={busy()} onClick={connect}>连接设备</button>
      </Show>
      <Show when={connected()}>
        <button disabled={busy() || applying() || !editing()} onClick={scan}>扫描 Wi‑Fi</button>
        <fieldset disabled={busy() || applying() || !editing()}>
        <For each={slots()}>{(slot, index) => (
          <section class="slot">
            <h2>网络 {index() + 1}</h2>
            <select value={slot.ssid} onChange={(event) => updateSsid(index(), event.currentTarget.value)}>
              <option value="">选择扫描到的 SSID</option>
              <For each={scans()}>{(network) => <option value={network.ssid}>{network.ssid} ({network.rssi} dBm)</option>}</For>
            </select>
            <input
              type="text"
              placeholder="SSID（也可手动输入）"
              value={slot.ssid}
              onInput={(event) => updateSsid(index(), event.currentTarget.value)}
            />
            <Show when={slot.ssid}>
              <label>
                密码处理
                <select
                  value={String(slot.passwordMode)}
                  onChange={(event) => updateSlot(index(), { passwordMode: Number(event.currentTarget.value) as PasswordModeValue, password: "" })}
                >
                  <option value={PasswordMode.Unchanged} disabled={!slot.passwordPresent}>保持设备中的密码</option>
                  <option value={PasswordMode.Set}>设置新密码</option>
                  <option value={PasswordMode.Clear}>清除密码（开放网络）</option>
                </select>
              </label>
              <Show when={slot.passwordMode === PasswordMode.Set}>
                <input
                  type="password"
                  placeholder="输入新密码"
                  value={slot.password}
                  onInput={(event) => updateSlot(index(), { password: event.currentTarget.value })}
                />
              </Show>
              <Show when={slot.passwordMode === PasswordMode.Unchanged && slot.passwordPresent}>
                <small>设备已有密码，网页不会显示密码内容。</small>
              </Show>
            </Show>
          </section>
        )}</For>
        </fieldset>
        <button disabled={busy() || applying() || !editing()} onClick={commit}>{applying() ? "应用中…" : "确认并应用"}</button>
        <button disabled={busy() || applying() || !editing()} onClick={abort}>放弃修改</button>
      </Show>
    </main>
  );
}
