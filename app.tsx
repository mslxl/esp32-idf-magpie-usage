import { createSignal, Show } from "solid-js";
import { onFrame, onButtonPress } from "@pocketjs/framework/solid/lifecycle";
import { BTN } from "@pocketjs/framework/input";
import { Modal, Text, View } from "@pocketjs/framework/solid/components";

type Status = { state: string; slot: number; round: number; pairing: boolean; passkey: number };
type Note4 = {
  status: Status;
  ble: { name: string; requestAction(action: number): void };
  display: { setMode(mode: "1bpp" | "4bpp"): void };
};
const native = (globalThis as typeof globalThis & { note4: Note4 }).note4;
const initial: Status = { state: "boot", slot: 0, round: 1, pairing: false, passkey: 0 };

function title(status: Status): string {
  if (status.pairing) return "BLUETOOTH PAIRING";
  switch (status.state) {
    case "configuring": case "discard": return "SET UP WI-FI";
    case "applying": return "SAVING CONFIGURATION";
    case "connecting": return "CONNECTING WI-FI";
    case "connected": return "WI-FI CONNECTED";
    case "failure": return "CONNECTION FAILED";
    default: return "Magpie Usage";
  }
}

function detail(status: Status): string {
  if (status.pairing) return `PAIRING CODE: ${String(status.passkey).padStart(6, "0")}`;
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
  onFrame(() => {
    const next = native.status;
    if (next.state && (next.state !== status().state || next.slot !== status().slot ||
        next.round !== status().round || next.pairing !== status().pairing || next.passkey !== status().passkey)) {
      setStatus(next);
    }
  });
  onButtonPress(BTN.CIRCLE, () => native.ble.requestAction(3), { allowWhenBlocked: true });
  onButtonPress(BTN.UP | BTN.DOWN, () => {
    if (status().state === "discard") native.ble.requestAction(4);
  }, { allowWhenBlocked: true });
  return (
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
          ? "OK: retry / Hold DOWN: setup" : "Hold DOWN: Wi-Fi setup"}
      </Text>
      <Modal open={() => status().state === "discard"}
        panelClass="flex-col gap-3 w-[352] p-4 bg-white border-2 border-black">
        <Text class="text-xl font-bold" style={{ textColor: "#000000" }}>Discard changes?</Text>
        <Text class="text-base" style={{ textColor: "#000000" }}>Unsaved Wi-Fi changes will be lost.</Text>
        <Text class="text-base" style={{ textColor: "#000000" }}>OK: discard / UP or DOWN: keep editing</Text>
      </Modal>
    </View>
  );
}
