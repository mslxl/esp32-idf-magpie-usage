import { element as h } from "../dom";
import { MAX_ENDPOINT_BYTES, MAX_KEY_BYTES, PasswordMode, SUPPORTED_TIMEZONES, type DeviceLanguage, type PasswordModeValue } from "../protocol";
import { isLocked } from "../state";
import { directoryPanel } from "./directory-panel";
import { button, checkbox, field, input, option, select, type ViewContext } from "./helpers";

export function magpiePanel(ctx: ViewContext): HTMLElement {
  const { state, t, actions } = ctx;
  const { draft, errors, ready } = state.magpie;
  const locked = isLocked(state);
  const saving = state.ui.busy === "savingMagpie";
  return h("section", { id: "magpie-panel", role: "tabpanel", "aria-labelledby": "magpie-tab", tabindex: "0",
    hidden: state.ui.tab !== "magpie", "data-key": "magpie", class: "panel" },
    h("p", { class: "hint" }, t("magpie.intro")),
    h("fieldset", { disabled: locked || !ready, class: "settings" },
      field(ctx, { id: "magpie-endpoint", label: t("magpie.endpoint"), error: errors.endpoint,
        control: input({ type: "url", inputmode: "url", autocomplete: "off", spellcheck: "false", maxlength: MAX_ENDPOINT_BYTES,
          placeholder: "https://magpie.example.com", value: draft.endpoint }, (node) => actions.editMagpie({ endpoint: node.value })) }),
      field(ctx, { id: "magpie-keyMode", label: t("magpie.keyMode"), error: errors.keyMode,
        control: select(draft.keyMode, [
          option(PasswordMode.Unchanged, t("magpie.keyKeep"), !draft.keyPresent),
          option(PasswordMode.Set, t("magpie.keySet")),
          option(PasswordMode.Clear, t("magpie.keyClear")),
        ], (value) => actions.editMagpie({ keyMode: Number(value) as PasswordModeValue, key: "" })) }),
      draft.keyMode === PasswordMode.Set ? field(ctx, { id: "magpie-key", key: "key-input", label: t("magpie.key"), error: errors.key,
        control: input({ type: "password", autocomplete: "new-password", maxlength: MAX_KEY_BYTES, spellcheck: "false", value: draft.key },
          (node) => actions.editMagpie({ key: node.value })) }) : [],
      draft.keyPresent ? h("p", { class: "hint", "data-key": "key-stored" }, t("magpie.keyStored")) : [],
      checkbox(ctx, { id: "magpie-http", label: t("magpie.allowHttp"), checked: draft.allowPlainHttp, error: errors.http,
        onChange: (checked) => actions.editMagpie({ allowPlainHttp: checked }) }),
      // The raw text is kept so clearing or partially typing a number never rewrites the field.
      field(ctx, { id: "magpie-interval", label: t("magpie.interval"), hint: t("magpie.intervalHint"), error: errors.interval,
        control: input({ type: "text", inputmode: "numeric", autocomplete: "off", maxlength: 4, value: draft.interval },
          (node) => actions.editMagpie({ interval: node.value })) }),
      field(ctx, { id: "magpie-timezone", label: t("magpie.timezone"), error: errors.timezone,
        control: input({ type: "text", list: "supported-timezones", autocomplete: "off", spellcheck: "false", value: draft.timezone },
          (node) => actions.editMagpie({ timezone: node.value })) }),
      h("datalist", { id: "supported-timezones" }, SUPPORTED_TIMEZONES.map((zone) => option(zone, ""))),
      field(ctx, { id: "magpie-layout", label: t("magpie.layout"), error: errors.layout,
        control: select(draft.layout, [option("A", t("magpie.layoutA")), option("B", t("magpie.layoutB"))],
          (value) => actions.editMagpie({ layout: value as "A" | "B" })) }),
      checkbox(ctx, { id: "magpie-adaptive-font", label: t("magpie.adaptiveFont"), checked: draft.adaptiveFont,
        onChange: (checked) => actions.editMagpie({ adaptiveFont: checked }) }),
      field(ctx, { id: "magpie-language", label: t("magpie.language"), error: errors.language,
        control: select(draft.language, [
          h("option", { value: "en", lang: "en" }, t("magpie.languageEn")),
          h("option", { value: "zh", lang: "zh-CN" }, t("magpie.languageZh")),
        ], (value) => actions.editMagpie({ language: value as DeviceLanguage })) })),
    button(t(saving ? "busy.saving" : "magpie.save"), actions.saveMagpie, { variant: "primary", disabled: locked || !ready,
      busy: saving, props: { "data-action": "save-magpie" } }),
    directoryPanel(ctx));
}
