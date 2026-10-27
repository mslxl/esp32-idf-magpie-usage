import { element as h } from "../dom";
import { MAX_PASSWORD_BYTES, MAX_SLOTS, MAX_SSID_BYTES, PasswordMode, type PasswordModeValue, type ScanResult } from "../protocol";
import { canAddSlot, isLocked, scanTarget, signalBars, sortedScans, type Slot } from "../state";
import { button, field, input, option, select, type ViewContext } from "./helpers";

function scanList(ctx: ViewContext): HTMLElement | [] {
  const { state, t, actions } = ctx;
  const scans = sortedScans(state.network.scans);
  if (!scans.length) return [];
  const locked = isLocked(state);
  const item = (scan: ScanResult) => {
    const bars = signalBars(scan.rssi);
    return h("li", { "data-key": `scan-${scan.ssid}` },
      h("button", { type: "button", class: "scan-item", disabled: locked, onClick: () => actions.pickScan(scan.ssid),
        "aria-label": t("network.scanPick", { ssid: scan.ssid, rssi: scan.rssi, security: t(scan.secure ? "network.secured" : "network.open") }) },
        h("span", { class: "signal", "data-bars": bars, "aria-hidden": "true" },
          [1, 2, 3, 4].map((level) => h("i", { class: level <= bars ? "on" : false }))),
        h("span", { class: "scan-ssid" }, scan.ssid),
        scan.secure ? h("span", { class: "lock", "aria-hidden": "true", title: t("network.secured") }) : [],
        h("span", { class: "scan-rssi", "aria-hidden": "true" }, `${scan.rssi} dBm`)));
  };
  return h("section", { class: "scan-results", "aria-labelledby": "scan-results-title", "data-key": "scan-results" },
    h("h2", { id: "scan-results-title" }, t("network.scanResults")),
    h("p", { class: "hint" }, t("network.scanHint", { index: scanTarget(state.network) + 1 })),
    h("ul", { class: "scan-list" }, scans.map(item)));
}

function slotCard(ctx: ViewContext, slot: Slot, index: number): HTMLElement {
  const { state, t, actions } = ctx;
  const errors = state.network.errors;
  const n = index + 1;
  return h("section", { class: "slot", "data-key": `slot-${index}`, "aria-labelledby": `slot-${index}-title` },
    h("h2", { id: `slot-${index}-title` }, t("network.slotTitle", { index: n })),
    field(ctx, { id: `ssid-${index}`, label: t("network.ssid"), error: errors[`ssid-${index}`],
      control: h("input", { type: "text", list: "wifi-scan-results", maxlength: MAX_SSID_BYTES, autocomplete: "off",
        spellcheck: "false", placeholder: t("network.ssidPlaceholder"), value: slot.ssid,
        onInput: (event: Event) => actions.editSsid(index, (event.currentTarget as HTMLInputElement).value),
        onFocus: () => actions.focusSlot(index) }) }),
    slot.ssid ? field(ctx, { id: `password-mode-${index}`, label: t("network.passwordMode"),
      control: select(slot.passwordMode, [
        option(PasswordMode.Unchanged, t("network.passwordKeep"), !slot.passwordPresent),
        option(PasswordMode.Set, t("network.passwordSet")),
        option(PasswordMode.Clear, t("network.passwordClear")),
      ], (value) => actions.editSlot(index, { passwordMode: Number(value) as PasswordModeValue, password: "" })) }) : [],
    slot.ssid && slot.passwordMode === PasswordMode.Set ? field(ctx, { id: `password-${index}`, key: "password",
      label: t("network.newPassword"), error: errors[`password-${index}`],
      control: input({ type: "password", autocomplete: "new-password", maxlength: MAX_PASSWORD_BYTES, value: slot.password },
        (node) => actions.editSlot(index, { password: node.value })) }) : [],
    slot.ssid && slot.passwordMode === PasswordMode.Unchanged && slot.passwordPresent
      ? h("p", { class: "hint", "data-key": "stored" }, t("network.passwordStored")) : []);
}

export function networkPanel(ctx: ViewContext): HTMLElement {
  const { state, t, actions } = ctx;
  const locked = isLocked(state);
  const network = state.network;
  const scanning = state.ui.busy === "scanning" || network.scanning;
  const saving = state.ui.busy === "savingNetwork";
  const add = canAddSlot(network);
  return h("section", { id: "network-panel", role: "tabpanel", "aria-labelledby": "network-tab", tabindex: "0",
    hidden: state.ui.tab !== "network", "data-key": "network", class: "panel" },
    button(t(scanning ? "busy.scanning" : "network.scan"), actions.scan, { disabled: locked || scanning, busy: scanning }),
    scanList(ctx),
    h("datalist", { id: "wifi-scan-results" }, sortedScans(network.scans).map((scan) => option(scan.ssid, `${scan.rssi} dBm`))),
    h("p", { class: "hint" }, t("network.priority")),
    h("fieldset", { disabled: locked, class: "slots" },
      network.slots.slice(0, network.visible).map((slot, index) => slotCard(ctx, slot, index)),
      h("div", { class: "add-row", "data-key": "add-row" },
        button(`+ ${t("network.add")}`, actions.addSlot, { disabled: !add.ok, props: { class: "btn btn-secondary add-slot",
          "aria-describedby": add.ok ? false : "add-slot-hint" } }),
        add.ok ? [] : h("p", { id: "add-slot-hint", class: "hint" }, t(add.reason, { max: MAX_SLOTS })))),
    button(t(saving ? "busy.saving" : "network.save"), actions.saveNetwork, { variant: "primary", disabled: locked, busy: saving,
      props: { "data-action": "save-network" } }));
}
