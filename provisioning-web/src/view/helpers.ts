import { element as h } from "../dom";
import type { Identity, DisplayPrefs } from "../display-prefs";
import type { Language, MessageKey, Params, Text } from "../i18n";
import type { MagpieDraft, AppState, Slot, Tab } from "../state";

export type Actions = {
  setLanguage(language: Language): void;
  connect(): void;
  reconnect(): void;
  selectTab(tab: Tab, focus?: boolean): void;
  scan(): void;
  pickScan(ssid: string): void;
  focusSlot(index: number): void;
  editSsid(index: number, ssid: string): void;
  editSlot(index: number, patch: Partial<Slot>): void;
  addSlot(): void;
  saveNetwork(): void;
  editMagpie(patch: Partial<MagpieDraft>): void;
  saveMagpie(): void;
  loadDirectory(): void;
  editPrefs(patch: Partial<DisplayPrefs>): void;
  moveGroup(identity: Identity, direction: -1 | 1): void;
  toggleHidden(identity: Identity): void;
  cleanupMissing(): void;
  savePrefs(): void;
  refresh(): void;
  requestExit(): void;
  confirmExit(): void;
  cancelExit(): void;
};

export type ViewContext = {
  state: AppState;
  t: (key: MessageKey, params?: Params) => string;
  text: (value: Text) => string;
  actions: Actions;
};

export type Variant = "primary" | "secondary" | "danger" | "ghost";

export function button(label: string, onClick: () => unknown, options: {
  variant?: Variant; disabled?: boolean; busy?: boolean; props?: Record<string, unknown>;
} = {}): HTMLElement {
  const { variant = "secondary", disabled = false, busy = false, props = {} } = options;
  return h("button", { type: "button", class: `btn btn-${variant}`, disabled, "aria-busy": busy ? "true" : false,
    onClick, ...props }, label);
}

export const option = (value: string | number, label: string, disabled = false) =>
  h("option", { value: String(value), disabled }, label);

export function input(props: Record<string, unknown>, onInput: (field: HTMLInputElement) => void): HTMLInputElement {
  return h("input", { ...props, onInput: (event: Event) => onInput(event.currentTarget as HTMLInputElement) }) as HTMLInputElement;
}

export function select(value: string | number, options: HTMLElement[], onChange: (value: string) => void,
  props: Record<string, unknown> = {}): HTMLSelectElement {
  const node = h("select", { ...props, onChange: (event: Event) => onChange((event.currentTarget as HTMLSelectElement).value) }, options) as HTMLSelectElement;
  node.value = String(value);
  return node;
}

/**
 * A labelled control with an optional hint and inline error. The control gets the id, aria-invalid
 * and aria-describedby that tie it to the visible label and messages.
 */
export function field(ctx: ViewContext, options: { id: string; label: string; control: HTMLElement; error?: Text;
  hint?: string; key?: string }): HTMLElement {
  const { id, label, control, error, hint, key } = options;
  const hintId = `${id}-hint`, errorId = `${id}-error`;
  control.id = id;
  if (error) control.setAttribute("aria-invalid", "true");
  const describedBy = [hint ? hintId : "", error ? errorId : ""].filter(Boolean).join(" ");
  if (describedBy) control.setAttribute("aria-describedby", describedBy);
  return h("div", { class: error ? "field field-invalid" : "field", "data-key": key ?? `field-${id}` },
    h("label", { for: id }, label),
    hint ? h("p", { id: hintId, class: "hint" }, hint) : [],
    control,
    error ? h("p", { id: errorId, class: "field-error" }, ctx.text(error)) : []);
}

export function checkbox(ctx: ViewContext, options: { id: string; label: string; checked: boolean;
  onChange: (checked: boolean) => void; error?: Text; props?: Record<string, unknown> }): HTMLElement {
  const { id, label, checked, onChange, error, props = {} } = options;
  const errorId = `${id}-error`;
  return h("div", { class: error ? "field field-check field-invalid" : "field field-check", "data-key": `check-${id}` },
    h("label", {},
      h("input", { type: "checkbox", id, checked, "aria-invalid": error ? "true" : false,
        "aria-describedby": error ? errorId : false, ...props,
        onChange: (event: Event) => onChange((event.currentTarget as HTMLInputElement).checked) }),
      h("span", {}, label)),
    error ? h("p", { id: errorId, class: "field-error" }, ctx.text(error)) : []);
}

export const identityLabel = (ctx: ViewContext, identity: Identity) =>
  `${identity.provider} · ${identity.user || ctx.t("directory.emptyUser")}`;
