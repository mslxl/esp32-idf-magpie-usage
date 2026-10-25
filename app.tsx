import { createSignal, For, Show } from "solid-js";
import { getOps } from "@pocketjs/framework/host";
import { quotaLineHeight } from "./quota-fonts";
import { createBakedFontCoverage } from "./third_party/pocketjs/framework/src/font-coverage";
import { quotaPage, type QuotaCard, type QuotaPage } from "./quota-pages";
import { onFrame, onButtonPress } from "@pocketjs/framework/solid/lifecycle";
import { BTN } from "@pocketjs/framework/input";
import { Image, Modal, Text, View } from "@pocketjs/framework/solid/components";

type Status = { state: string; slot: number; round: number; storageError?: boolean; pairing: boolean; passkey: number;
  magpiePhase: number; magpieResult: number; magpieErrorPage: boolean; magpieReading: boolean; magpieRetrySeconds: number; timezone: string; layout: string; adaptiveFont: boolean };
type Note4 = {
  status: Status;
  quotas: { info(): { available: boolean; version: number; count: number }; card(index: number): QuotaCard; visible(index: number): boolean; rank(index: number): number };
  ble: { name: string; requestAction(action: number): void };
  display: { setMode(mode: "1bpp" | "4bpp"): void };
};
const native = (globalThis as typeof globalThis & { note4: Note4 }).note4;
const initial: Status = { state: "boot", slot: 0, round: 1, pairing: false, passkey: 0, magpiePhase: 0, magpieResult: 0, magpieErrorPage: false, magpieReading: false, magpieRetrySeconds: 0, timezone: "", layout: "A", adaptiveFont: true };

function providerIcon(provider: string): string {
  const key = provider.toLowerCase().replace(/[ _-]/g, "");
  return `note4-${key.includes("codex") ? "codex" : key.includes("claude") ? "claude" : key.includes("copilot") ? "copilot" : key.includes("deepseek") ? "deepseek" : "generic"}`;
}
function inSetup(status: Status): boolean {
  return ["configuring", "discard", "applying"].includes(status.state);
}
function accessTitle(status: Status): string {
  if (status.magpiePhase !== 6) return "";
  return ["", "MAGPIE CONFIGURATION REQUIRED", "MAGPIE LOGIN EXPIRED", "ACCESS DENIED", "CERTIFICATE ERROR"][status.magpieResult] || "";
}
function title(status: Status): string {
  if (status.storageError) return "STORAGE UNAVAILABLE";
  if (status.pairing) return "BLUETOOTH PAIRING";
  if (!inSetup(status) && accessTitle(status)) return accessTitle(status);
  if (status.magpieErrorPage && !["configuring", "discard", "applying"].includes(status.state) &&
      ![1, 2, 3, 4].includes(status.magpieResult)) return "QUOTA READING FAILED";
  if (status.state === "connected") {
    if (status.magpiePhase === 1) return "SYNCING CLOCK";
    if (status.magpiePhase === 2) return "MAGPIE SIGN IN";
    if (status.magpiePhase === 3) return "MAGPIE SIGNED IN";
    if (status.magpiePhase === 4) return "READING QUOTAS";
    if (status.magpiePhase === 6) {
      if (status.magpieResult === 2) return "MAGPIE LOGIN EXPIRED";
      if (status.magpieResult === 3) return "ACCESS DENIED";
      if (status.magpieResult === 4) return "CERTIFICATE ERROR";
      return "MAGPIE CONNECTION ERROR";
    }
  }
  switch (status.state) {
    case "configuring": case "discard": return "DEVICE SETTINGS";
    case "applying": return "SAVING CONFIGURATION";
    case "connecting": return "CONNECTING WI-FI";
    case "connected": return "WI-FI CONNECTED";
    case "failure": return "CONNECTION FAILED";
    default: return "Magpie Usage";
  }
}

function detail(status: Status): string {
  if (status.storageError) return "Settings storage could not start. Press OK to retry. Existing storage is preserved.";
  if (status.pairing) return `PAIRING CODE: ${String(status.passkey).padStart(6, "0")}`;
  if (!inSetup(status)) {
    if (accessTitle(status)) return ["", "Hold DOWN to configure Magpie Web", "Hold DOWN and save a valid Web key",
      "Magpie refused access. Check server permissions.", "Check certificate, hostname and device time"][status.magpieResult];
    if (status.magpieErrorPage && status.state !== "connected") return "Offline. Reconnecting to Wi-Fi. Hold DOWN for setup.";
  }
  if (status.state === "connected") {
    if (status.magpiePhase === 1) return "Setting time before HTTPS verification";
    if (status.magpiePhase === 2) return "Signing in to Magpie Web";
    if (status.magpiePhase === 3) return "Signed in. No quota reading received yet.";
    if (status.magpiePhase === 6) {
      if (status.magpieErrorPage && ![1, 2, 3, 4].includes(status.magpieResult))
        return `Reading unavailable. Retry in ${status.magpieRetrySeconds}s. Hold DOWN for setup.`;
      if (status.magpieResult === 1) return "Hold DOWN to configure Magpie Web";
      if (status.magpieResult === 2) return "Hold DOWN and save a valid Web key";
      if (status.magpieResult === 3) return "Magpie refused access. Check server permissions.";
      if (status.magpieResult === 4) return "Check certificate, hostname and device time";
      if (status.magpieResult === 6) return "Request timed out. Hold DOWN for setup.";
      return "Could not connect. Hold DOWN for setup.";
    }
  }
  switch (status.state) {
    case "configuring": case "discard": return "Open the setup page and connect magpie usage";
    case "applying": return "Please wait";
    case "connecting": return `Network ${status.slot + 1} / 5    Round ${status.round} / 3`;
    case "connected": return "Your device is online";
    case "failure": return "All configured networks failed in 3 rounds";
    default: return "Starting";
  }
}

export default function App() {
  const [status, setStatus] = createSignal<Status>(initial);
  const [rows, setRows] = createSignal<QuotaPage>([]);
  const [pageCount, setPageCount] = createSignal(0);
  const [available, setAvailable] = createSignal(false);
  const [filtered, setFiltered] = createSignal(false);
  const [page, setPage] = createSignal(0);
  const covered = createBakedFontCoverage();
  let version = -1, minute = -1, timezone = "", layoutKey = "", renderedPage = -1;
  const banner = (value: Status) => value.state !== "connected" ? "Last data - offline; reconnecting" : value.magpiePhase === 6
    ? `Last data - ${value.magpieResult === 6 ? "request timed out" : "reading failed"}`
    : value.magpieReading ? "Magpie Usage - reading" : "";
  const blocked = () => status().magpiePhase === 6 && [1, 2, 3, 4].includes(status().magpieResult);
  const showQuotas = () => available() && !status().storageError && !inSetup(status()) && !status().pairing && !blocked() && !status().magpieErrorPage;
  onFrame(() => {
    const info = native.quotas.info();
    const next = native.status;
    const now = Date.now();
    const nextLayoutKey = `${next.layout}:${next.adaptiveFont}:${!!banner(next)}`;
    if (info.version !== version || minute !== Math.floor(now / 60000) || timezone !== next.timezone || layoutKey !== nextLayoutKey || page() !== renderedPage) {
      try {
      const indices: { index: number; rank: number }[] = [];
      for (let index = 0; index < info.count; ++index) if (native.quotas.visible(index)) indices.push({ index, rank: native.quotas.rank(index) });
      indices.sort((a, b) => a.rank - b.rank || a.index - b.index);
      const nextFiltered = info.count > 0 && indices.length === 0;
      const host = getOps();
      const result = quotaPage({ length: indices.length, card: index => native.quotas.card(indices[index].index) }, page(),
        (text, slot = 2) => host.measureText(text, slot), (scalar, slot = 2) => covered(scalar, slot),
        host.wrapText ? (text, width, slot = 2) => host.wrapText!(text, slot, width) : undefined, now, next.timezone || "UTC",
        { layout: next.layout, adaptive: next.adaptiveFont, top: banner(next) ? 62 : 38, lineHeight: quotaLineHeight });
      if (JSON.stringify(result.rows) !== JSON.stringify(rows())) setRows(result.rows);
      setFiltered(nextFiltered);
      setPageCount(result.count);
      setPage(result.index); renderedPage = result.index;
      setAvailable(info.available);
      version = info.version;
      minute = Math.floor(now / 60000); timezone = next.timezone; layoutKey = nextLayoutKey;
      } catch {
        // Keep the last complete page; retry materialization on the next UI turn.
      }
    }
    if (next.state && (next.state !== status().state || next.storageError !== status().storageError || next.slot !== status().slot ||
        next.round !== status().round || next.pairing !== status().pairing || next.passkey !== status().passkey ||
        next.magpiePhase !== status().magpiePhase || next.magpieResult !== status().magpieResult ||
        next.magpieErrorPage !== status().magpieErrorPage || next.magpieReading !== status().magpieReading ||
        next.magpieRetrySeconds !== status().magpieRetrySeconds || next.timezone !== status().timezone ||
        next.layout !== status().layout || next.adaptiveFont !== status().adaptiveFont)) {
      setStatus(next);
    }
  });
  onButtonPress(BTN.UP, () => { if (showQuotas()) setPage(current => Math.max(0, current - 1)); });
  onButtonPress(BTN.DOWN, () => { if (showQuotas()) setPage(current => Math.min(Math.max(0, pageCount() - 1), current + 1)); });
  onButtonPress(BTN.CIRCLE, () => native.ble.requestAction(3), { allowWhenBlocked: true });
  onButtonPress(BTN.UP | BTN.DOWN, () => {
    if (status().state === "discard") native.ble.requestAction(4);
  }, { allowWhenBlocked: true });
  return (
    <Show when={showQuotas()} fallback={
    <View class="w-full h-full flex-col p-6 justify-between" style={{ bgColor: "#FFFFFF" }}>
      <Text class="text-lg font-bold" style={{ textColor: "#000000" }}>Magpie Usage</Text>
      <View class="flex-col gap-3">
        <Text class="text-2xl font-bold" style={{ textColor: "#000000" }}>{title(status())}</Text>
        <Text class="text-base" style={{ textColor: "#000000" }}>{detail(status())}</Text>
        <Show when={status().state === "configuring" && !status().pairing}>
          <Text class="text-base" style={{ textColor: "#000000" }}>{`Bluetooth: ${native.ble.name}`}</Text>
          <Text class="text-base" style={{ textColor: "#000000" }}>Wi-Fi: 2.4 GHz</Text>
        </Show>
      </View>
      <Text class="text-base" style={{ textColor: "#000000" }}>
        {status().state === "configuring" ? "OK: exit setup" : status().state === "failure"
          ? "OK: retry / Hold DOWN: setup" : "Hold DOWN: setup"}
      </Text>
      <Modal open={() => status().state === "discard"}
        panelClass="flex-col gap-3 w-[352] p-4 bg-white border-2 border-black">
        <Text class="text-xl font-bold" style={{ textColor: "#000000" }}>Exit setup?</Text>
        <Text class="text-base" style={{ textColor: "#000000" }}>Unsaved changes are lost. Saved settings remain.</Text>
        <Text class="text-base" style={{ textColor: "#000000" }}>OK: discard / UP or DOWN: keep editing</Text>
      </Modal>
    </View>}>
      <View class="w-full h-full" style={{ bgColor: "#FFFFFF" }}>
        <Text class="absolute text-lg" style={{ insetL: 8, insetT:8, width: 384, height: 24, textColor: "#000000" }}>Magpie Usage</Text>
        <Show when={banner(status())}>
          <Text class="absolute text-sm" style={{ insetL: 8, insetT:34, width: 384, height: 22, textColor: "#000000" }}>{banner(status())}</Text>
        </Show>
        <Show when={pageCount() > 0} fallback={
          <Text class="absolute text-base" style={{ insetL: 8, insetT:100, width: 384, height: 24, textColor: "#000000" }}>{filtered() ? "No accounts match display settings" : "No accounts"}</Text>
        }>
          <For each={rows()}>{row => <>
            <Show when={row.icon}><Image src={providerIcon(row.icon!)} class="absolute"
              style={{ insetL: row.x, insetT:row.y, width:18, height:18 }} /></Show>
            <Show when={row.progress !== undefined} fallback={
              <Text class="absolute text-base" style={{ insetL: row.x + (row.header ? 22 : 0), insetT:row.y,
                width: row.width - (row.header ? 22 : 0), height: row.height, fontSlot: row.slot, textColor: "#000000" }}>{row.text}</Text>
            }>
              <View class="absolute border border-black" style={{ insetL:row.x, insetT:row.y + 2, width:row.width, height:6 }}>
                <View style={{ width:Math.round((row.width - 2) * (row.progress ?? 0) / 100), height:4, bgColor:"#000000" }} />
              </View>
            </Show>
          </>}</For>
        </Show>
        <Text class="absolute text-base" style={{ insetL: 8, insetT:274, width: 384, height: 20, textColor: "#000000" }}>
          {`${pageCount() ? page() + 1 : 0} / ${pageCount()}    UP/DOWN: page    Hold DOWN: setup`}
        </Text>
      </View>
    </Show>
  );
}
