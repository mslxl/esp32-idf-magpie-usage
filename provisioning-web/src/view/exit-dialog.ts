import { element as h } from "../dom";
import { button, type ViewContext } from "./helpers";

export const EXIT_DIALOG_ID = "exit-dialog";

/** Native modal confirmation; App opens it with showModal() and Esc/close sync back to state. */
export function exitDialog(ctx: ViewContext): HTMLElement {
  const { state, t, actions } = ctx;
  const exiting = state.ui.busy === "exiting";
  return h("dialog", { id: EXIT_DIALOG_ID, class: "dialog", "aria-labelledby": "exit-title", "aria-describedby": "exit-body",
    "data-key": "exit-dialog", onClose: () => actions.cancelExit() },
    h("h2", { id: "exit-title" }, t("exit.title")),
    h("p", { id: "exit-body" }, t("exit.body")),
    h("div", { class: "dialog-actions" },
      button(t("exit.cancel"), actions.cancelExit, { disabled: exiting, props: { autofocus: true } }),
      button(t(exiting ? "busy.exiting" : "exit.confirm"), actions.confirmExit, { variant: "danger", disabled: exiting, busy: exiting })));
}

/** Opens or closes the dialog element to match the state after a render. */
export function syncDialog(root: ParentNode, open: boolean): void {
  const dialog = root.querySelector<HTMLDialogElement>(`#${EXIT_DIALOG_ID}`);
  if (!dialog || dialog.hasAttribute("open") === open) return;
  if (open) {
    if (typeof dialog.showModal === "function") dialog.showModal();
    else dialog.setAttribute("open", "");
  } else if (typeof dialog.close === "function") dialog.close();
  else dialog.removeAttribute("open");
}
