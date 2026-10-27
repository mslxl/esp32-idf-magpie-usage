type Child = Node | string | number | false | null | undefined | Child[];
const handlers = new WeakMap<Element, Record<string, EventListener>>();

export function element(tag: string, props: Record<string, unknown> = {}, ...children: Child[]): HTMLElement {
  const node = document.createElement(tag);
  const events: Record<string, EventListener> = {};
  for (const [name, value] of Object.entries(props)) {
    if (name.startsWith("on")) {
      events[name.slice(2).toLowerCase()] = value as EventListener;
    } else if (name === "value" || name === "checked" || name === "disabled" || name === "hidden") {
      (node as unknown as Record<string, unknown>)[name] = value;
    } else if (value !== false && value != null) node.setAttribute(name, String(value));
  }
  handlers.set(node, events);
  function append(child: Child): void {
    if (Array.isArray(child)) { for (const item of child) append(item); }
    else if (child !== false && child != null) node.append(child instanceof Node ? child : String(child));
  }
  for (const child of children) append(child);
  return node;
}

function clearPasswords(node: Node): void {
  if (node instanceof HTMLInputElement && node.type === "password") node.value = "";
  if (node instanceof Element) for (const input of node.querySelectorAll<HTMLInputElement>('input[type="password"]')) input.value = "";
}

// Patch existing controls so typing and asynchronous status updates preserve focus.
export function update(parent: Node, next: Node): void {
  const current = parent.firstChild;
  if (!current) { parent.appendChild(next); activate(next); }
  else patch(current, next);
}
function activate(node: Node): void {
  if (node instanceof Element) for (const [event, handler] of Object.entries(handlers.get(node) ?? {})) node.addEventListener(event, handler);
  for (const child of node.childNodes) activate(child);
}
// A dialog's "open" attribute is managed by showModal()/close(), not by rendering.
const browserOwned = (node: Element, attribute: string) => attribute === "open" && node.nodeName === "DIALOG";

function patch(current: Node, next: Node): void {
  if (current.nodeType !== next.nodeType || current.nodeName !== next.nodeName ||
      (current instanceof Element && next instanceof Element && current.getAttribute("data-key") !== next.getAttribute("data-key"))) {
    clearPasswords(current); current.parentNode!.replaceChild(next, current); activate(next); return;
  }
  if (!(current instanceof Element) || !(next instanceof Element)) {
    if (current.nodeValue !== next.nodeValue) current.nodeValue = next.nodeValue;
    return;
  }
  for (const attribute of [...current.attributes]) {
    if (!next.hasAttribute(attribute.name) && !browserOwned(current, attribute.name)) current.removeAttribute(attribute.name);
  }
  for (const attribute of next.attributes) if (current.getAttribute(attribute.name) !== attribute.value) current.setAttribute(attribute.name, attribute.value);
  for (const [event, handler] of Object.entries(handlers.get(current) ?? {})) current.removeEventListener(event, handler);
  const events = handlers.get(next) ?? {}; handlers.set(current, events);
  for (const [event, handler] of Object.entries(events)) current.addEventListener(event, handler);
  const children = [...next.childNodes];
  for (let i = 0; i < children.length; i++) {
    const old = current.childNodes[i];
    if (old) patch(old, children[i]); else { current.appendChild(children[i]); activate(children[i]); }
  }
  while (current.childNodes.length > children.length) { clearPasswords(current.lastChild!); current.lastChild!.remove(); }
  if (current instanceof HTMLInputElement && next instanceof HTMLInputElement) {
    if (current.value !== next.value) current.value = next.value;
    current.checked = next.checked; current.disabled = next.disabled;
  } else if (current instanceof HTMLSelectElement && next instanceof HTMLSelectElement) current.value = next.value;
  if (current instanceof HTMLElement && next instanceof HTMLElement) current.hidden = next.hidden;
  if ("disabled" in current && "disabled" in next) (current as HTMLButtonElement).disabled = (next as HTMLButtonElement).disabled;
}
