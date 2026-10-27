import { element as h } from "../dom";
import { LANGUAGES, type Language } from "../i18n";
import { isLocked, tabDirty, type Tab } from "../state";
import { exitDialog } from "./exit-dialog";
import { button, type ViewContext } from "./helpers";
import { magpiePanel } from "./magpie-panel";
import { networkPanel } from "./network-panel";

const TABS: readonly Tab[] = ["network", "magpie"];

function header(ctx: ViewContext): HTMLElement {
  const { state, t, actions } = ctx;
  const languageButton = (language: Language) => h("button", { type: "button", class: "lang-option",
    lang: language === "zh" ? "zh-CN" : "en", "aria-pressed": String(state.ui.language === language),
    onClick: () => actions.setLanguage(language) }, t(`lang.${language}`));
  return h("header", { class: "page-header" },
    h("h1", {}, t("app.title")),
    h("div", { class: "lang-switch", role: "group", "aria-label": t("lang.label") }, LANGUAGES.map(languageButton)));
}

function notices(ctx: ViewContext): HTMLElement {
  const { state, text } = ctx;
  // Both live regions stay mounted so screen readers announce changes to their content.
  return h("div", { class: "notices" },
    h("div", { role: "status", "aria-live": "polite", class: state.ui.status ? "notice notice-status" : "notice notice-status empty" },
      state.ui.status ? text(state.ui.status) : ""),
    h("div", { role: "alert", class: state.ui.alert ? "notice notice-alert" : "notice notice-alert empty" },
      state.ui.alert ? text(state.ui.alert) : ""));
}

function connection(ctx: ViewContext): HTMLElement {
  const { state, t, actions } = ctx;
  const { session, ui } = state;
  if (!session.supported) {
    return h("section", { class: "unsupported", "aria-labelledby": "unsupported-title", "data-key": "unsupported" },
      h("h2", { id: "unsupported-title" }, t("unsupported.title")),
      h("p", {}, t("unsupported.body")),
      button(t("connection.connect"), actions.connect, { variant: "primary", disabled: true }));
  }
  const connecting = ui.busy === "connecting", reconnecting = ui.busy === "reconnecting";
  if (session.phase === "connected") {
    return h("p", { class: "device", "data-key": "device" }, h("span", { class: "dot online", "aria-hidden": "true" }),
      t("connection.device", { name: session.deviceName }));
  }
  if (session.phase === "lost") {
    return h("div", { class: "device-lost", "data-key": "device-lost" },
      h("p", { class: "device" }, h("span", { class: "dot offline", "aria-hidden": "true" }), t("connection.lost", { name: session.deviceName })),
      h("div", { class: "button-row" },
        button(t(reconnecting ? "busy.reconnecting" : "connection.reconnect"), actions.reconnect,
          { variant: "primary", disabled: Boolean(ui.busy), busy: reconnecting }),
        button(t(connecting ? "busy.connecting" : "connection.chooseOther"), actions.connect,
          { disabled: Boolean(ui.busy), busy: connecting })));
  }
  return button(t(connecting ? "busy.connecting" : "connection.connect"), actions.connect,
    { variant: "primary", disabled: Boolean(ui.busy), busy: connecting, props: { "data-key": "connect" } });
}

// Optional sections keep a hidden placeholder so later siblings (the dialog) keep their position.
const placeholder = (key: string) => h("div", { "data-key": key, hidden: true });

function wifiProgress(ctx: ViewContext): HTMLElement {
  const { state, t } = ctx;
  const wifi = state.session.wifi;
  if (!wifi) return placeholder("no-wifi");
  const ssid = (slot: number) => state.network.slots[slot]?.ssid || "—";
  const current = wifi.result === "applying" ? t("wifi.applying")
    : wifi.result === "connecting" ? t("wifi.trying", { index: wifi.slot + 1, ssid: ssid(wifi.slot), round: wifi.round })
    : wifi.result === "connected" ? t("wifi.connected") : t("wifi.failed");
  return h("section", { class: `wifi-progress wifi-${wifi.result}`, "aria-labelledby": "wifi-title", "data-key": "wifi" },
    h("h2", { id: "wifi-title" }, t("wifi.title")),
    h("ul", {}, wifi.failedSlots.map((slot) => h("li", { class: "failed" }, t("wifi.slotFailed", { index: slot + 1, ssid: ssid(slot) })))),
    h("p", { class: "wifi-current", "aria-busy": wifi.result === "connecting" || wifi.result === "applying" ? "true" : false }, current));
}

function tabs(ctx: ViewContext): HTMLElement {
  const { state, t, actions } = ctx;
  const tab = (name: Tab, index: number) => {
    const selected = state.ui.tab === name, dirty = tabDirty(state, name);
    return h("button", { type: "button", id: `${name}-tab`, role: "tab", class: "tab", "aria-selected": String(selected),
      "aria-controls": `${name}-panel`, tabindex: selected ? "0" : "-1", "data-tab": name,
      onClick: () => actions.selectTab(name),
      onKeydown: (event: KeyboardEvent) => {
        const next = event.key === "ArrowRight" ? TABS[(index + 1) % TABS.length]
          : event.key === "ArrowLeft" ? TABS[(index - 1 + TABS.length) % TABS.length]
          : event.key === "Home" ? TABS[0] : event.key === "End" ? TABS[TABS.length - 1] : undefined;
        if (!next) return;
        event.preventDefault();
        actions.selectTab(next, true);
      } },
      t(`tab.${name}`),
      dirty ? h("span", { class: "unsaved-dot", "aria-hidden": "true" }) : [],
      dirty ? h("span", { class: "visually-hidden" }, ` ${t("tab.unsaved")}`) : []);
  };
  return h("div", { class: "tabs", role: "tablist", "aria-label": t("tabs.label") }, TABS.map(tab));
}

function footer(ctx: ViewContext): HTMLElement {
  const { state, t, actions } = ctx;
  const locked = isLocked(state);
  const refreshing = state.ui.busy === "refreshing";
  return h("footer", { class: "page-footer", "data-key": "footer" },
    button(t(refreshing ? "busy.refreshing" : "actions.refresh"), actions.refresh, { disabled: locked, busy: refreshing,
      props: { "data-action": "refresh" } }),
    button(t("actions.exit"), actions.requestExit, { variant: "danger", disabled: locked, props: { "data-action": "exit" } }));
}

export function page(ctx: ViewContext): HTMLElement {
  const { state } = ctx;
  const showSettings = state.session.phase === "connected" && (state.session.editing || !state.session.wifi);
  return h("main", {},
    header(ctx),
    notices(ctx),
    connection(ctx),
    wifiProgress(ctx),
    showSettings ? h("div", { class: "settings-area", "data-key": "settings" },
      tabs(ctx), networkPanel(ctx), magpiePanel(ctx), footer(ctx)) : placeholder("no-settings"),
    exitDialog(ctx));
}
