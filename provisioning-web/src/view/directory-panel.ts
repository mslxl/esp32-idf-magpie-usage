import { element as h } from "../dom";
import { identityKey, type DisplayPrefs, type Identity } from "../display-prefs";
import type { MessageKey } from "../i18n";
import { customGroups, directoryGroups, isLocked, missingGroups } from "../state";
import { button, checkbox, field, identityLabel, input, option, select, type ViewContext } from "./helpers";

/** Identifies a move button so focus can follow the moved item after re-rendering. */
export const moveFocusId = (identity: Identity, direction: -1 | 1) => `move:${direction}:${identityKey(identity)}`;

const KINDS: ReadonlyArray<readonly [number, MessageKey]> = [[1, "directory.kind.subscription"], [2, "directory.kind.plan"],
  [4, "directory.kind.balance"], [8, "directory.kind.unknown"]];

export function directoryPanel(ctx: ViewContext): HTMLElement {
  const { state, t, actions } = ctx;
  const locked = isLocked(state);
  const loading = state.ui.busy === "loadingDirectory";
  const value = state.directory.value;
  return h("div", { class: "directory", "data-key": "directory-wrap" },
    button(t(loading ? "busy.loading" : "directory.load"), actions.loadDirectory, { disabled: locked, busy: loading,
      props: { "data-action": "load-directory" } }),
    value ? loadedDirectory(ctx, value) : []);
}

function loadedDirectory(ctx: ViewContext, value: NonNullable<ViewContext["state"]["directory"]["value"]>): HTMLElement {
  const { state, t, actions } = ctx;
  const { prefs, dirty } = state.directory;
  const disabled = Boolean(state.ui.busy) || !state.session.editing;
  const saving = state.ui.busy === "savingPrefs";
  const present = new Set(directoryGroups(state.directory).map(identityKey));
  const ordered = customGroups(state.directory);
  const missing = missingGroups(state.directory);
  const providers = [...new Set([...value.cards.map((card) => card.provider), ...prefs.providers])];

  const moveRow = (identity: Identity, index: number) => {
    const name = identityLabel(ctx, identity);
    return h("li", { class: "move-row", "data-key": `order-${index}` },
      h("span", {}, present.has(identityKey(identity)) ? name : `${name} · ${t("directory.notPresent")}`),
      h("span", { class: "move-buttons" },
        button(t("directory.up"), () => actions.moveGroup(identity, -1), { disabled: index === 0,
          props: { "aria-label": t("directory.moveUp", { name }), "data-focus-id": moveFocusId(identity, -1) } }),
        button(t("directory.down"), () => actions.moveGroup(identity, 1), { disabled: index === ordered.length - 1,
          props: { "aria-label": t("directory.moveDown", { name }), "data-focus-id": moveFocusId(identity, 1) } })));
  };

  return h("section", { class: "directory-body", "aria-labelledby": "directory-title", "data-key": "directory" },
    h("h2", { id: "directory-title" }, t("directory.region")),
    h("p", {}, t("directory.summary", { groups: value.groups, cards: value.cards.length })),
    h("fieldset", { disabled, class: "settings" },
      field(ctx, { id: "display-order", label: t("directory.order"),
        control: select(prefs.order, [option("original", t("directory.orderOriginal")), option("custom", t("directory.orderCustom")),
          option("smart", t("directory.orderSmart"))], (order) => actions.editPrefs({ order: order as DisplayPrefs["order"] })) }),
      field(ctx, { id: "provider-mode", label: t("directory.providerMode"),
        control: select(prefs.providerMode, [option("all", t("directory.providersAll")), option("selected", t("directory.providersSelected"))],
          (mode) => actions.editPrefs({ providerMode: mode as DisplayPrefs["providerMode"] })) }),
      prefs.providerMode === "selected" ? h("fieldset", { class: "group", "data-key": "providers" },
        h("legend", {}, t("directory.providerMode")),
        providers.map((provider, index) => checkbox(ctx, { id: `provider-${index}`, label: provider, checked: prefs.providers.includes(provider),
          onChange: (checked) => actions.editPrefs({ providers: checked ? [...prefs.providers, provider] : prefs.providers.filter((item) => item !== provider) }) }))) : [],
      h("fieldset", { class: "group", "data-key": "kinds" },
        h("legend", {}, t("directory.kinds")),
        KINDS.map(([bit, key]) => checkbox(ctx, { id: `kind-${bit}`, label: t(key), checked: Boolean(prefs.kinds & bit),
          onChange: (checked) => actions.editPrefs({ kinds: checked ? prefs.kinds | bit : prefs.kinds & ~bit }) }))),
      field(ctx, { id: "name-filter", label: t("directory.query"),
        control: input({ type: "text", autocomplete: "off", value: prefs.query }, (node) => actions.editPrefs({ query: node.value })) }),
      prefs.order === "custom" ? h("ol", { class: "move-list", "data-key": "move-list" }, ordered.map(moveRow)) : [],
      h("p", {}, t("directory.missingCount", { count: missing.length })),
      h("ul", { "aria-label": t("directory.missingList") }, missing.map((identity) => h("li", {}, identityLabel(ctx, identity)))),
      button(t("directory.cleanup"), actions.cleanupMissing, { disabled: missing.length === 0 })),
    value.cards.length === 0 ? h("p", {}, t("directory.empty")) : [],
    value.cards.map((card, index) => h("article", { class: "slot", "data-key": `card-${index}` },
      h("strong", {}, identityLabel(ctx, card)),
      h("p", {}, [card.name, card.alias, card.seat, card.from, card.kind || "unknown", card.plan].filter(Boolean).join(" · ")),
      h("small", {}, t("directory.groupNote", { group: card.group + 1 })),
      checkbox(ctx, { id: `hide-${index}`, label: t("directory.hide"),
        checked: prefs.hidden.some((item) => identityKey(item) === identityKey(card)),
        onChange: () => actions.toggleHidden(card),
        props: { "aria-label": t("directory.hideLabel", { provider: card.provider, user: card.user, name: card.name }), disabled } }))),
    button(t(saving ? "busy.saving" : "directory.save"), actions.savePrefs, { variant: "primary",
      disabled: disabled || !dirty, busy: saving, props: { "data-action": "save-prefs" } }));
}
