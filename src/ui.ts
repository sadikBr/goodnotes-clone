// Small DOM helpers: element builder, modal dialogs, toasts and icons.

type Child = Node | string | null | undefined | false;

export function h<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  props: Record<string, unknown> = {},
  ...children: Child[]
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(props)) {
    if (value === undefined || value === null || value === false) continue;
    if (key === "class") node.className = String(value);
    else if (key === "style" && typeof value === "object") Object.assign(node.style, value);
    else if (key.startsWith("on") && typeof value === "function") node.addEventListener(key.slice(2).toLowerCase(), value as EventListener);
    else if (key === "html") node.innerHTML = String(value);
    else if (value === true) node.setAttribute(key, "");
    else node.setAttribute(key, String(value));
  }
  for (const c of children) if (c !== null && c !== undefined && c !== false) node.append(c);
  return node;
}

export function icon(name: keyof typeof ICONS, label?: string): HTMLSpanElement {
  return h("span", { class: "icon", "aria-hidden": label ? undefined : "true", title: label, html: ICONS[name] });
}

const svg = (body: string) =>
  `<svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">${body}</svg>`;

export const ICONS = {
  pen: svg('<path d="M4 20l4-1 11-11-3-3L5 16l-1 4z"/><path d="M14 6l3 3"/>'),
  highlighter: svg('<path d="M9 15l-3 5h6l1-2"/><path d="M8 14l8-10 4 4-10 8z"/>'),
  ruler: svg('<path d="M3 16.5L16.5 3 21 7.5 7.5 21z"/><path d="M7 13l1.5 1.5M9.5 10.5l2.5 2.5M12 8l1.5 1.5M14.5 5.5l2.5 2.5"/>'),
  laser: svg('<circle cx="7" cy="17" r="2.6" fill="currentColor"/><path d="M9 15l9-9"/><path d="M14.5 4.5l1-2M19.5 9.5l2-1M18 3.5l1.5-1.5M20.5 6l1.5-.5"/>'),
  eraser: svg('<path d="M7 20h12"/><path d="M4 15l9-9 6 6-7 7H8z"/><path d="M9 10l6 6"/>'),
  lasso: svg('<ellipse cx="12" cy="9" rx="8" ry="5" stroke-dasharray="3 3"/><path d="M8 13c-1 3 1 5 3 4s0 3-2 4"/>'),
  shape: svg('<rect x="3" y="11" width="9" height="9"/><circle cx="16.5" cy="7.5" r="4.5"/>'),
  text: svg('<path d="M5 6V4h14v2"/><path d="M12 4v16"/><path d="M9 20h6"/>'),
  sticky: svg('<path d="M4 4h16v11l-5 5H4z"/><path d="M15 20v-5h5"/>'),
  image: svg('<rect x="3" y="5" width="18" height="14" rx="2"/><circle cx="9" cy="10" r="2"/><path d="M21 16l-5-5-8 8"/>'),
  hand: svg('<path d="M8 13V5.5a1.5 1.5 0 013 0V12"/><path d="M11 11V4.5a1.5 1.5 0 013 0V12"/><path d="M14 11.5V6a1.5 1.5 0 013 0v8a6 6 0 01-6 6h-1a6 6 0 01-5-2.7L3 13.5a1.5 1.5 0 012.4-1.8L8 14"/>'),
  undo: svg('<path d="M9 14L4 9l5-5"/><path d="M4 9h11a5 5 0 010 10h-3"/>'),
  redo: svg('<path d="M15 14l5-5-5-5"/><path d="M20 9H9a5 5 0 000 10h3"/>'),
  back: svg('<path d="M15 18l-6-6 6-6"/>'),
  plus: svg('<path d="M12 5v14M5 12h14"/>'),
  zoomIn: svg('<circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3M11 8v6M8 11h6"/>'),
  zoomOut: svg('<circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3M8 11h6"/>'),
  fit: svg('<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/>'),
  more: svg('<circle cx="5" cy="12" r="1.3"/><circle cx="12" cy="12" r="1.3"/><circle cx="19" cy="12" r="1.3"/>'),
  folder: svg('<path d="M3 7a2 2 0 012-2h4l2 2h8a2 2 0 012 2v8a2 2 0 01-2 2H5a2 2 0 01-2-2z"/>'),
  book: svg('<path d="M5 4h11a3 3 0 013 3v13H8a3 3 0 01-3-3z"/><path d="M5 17a3 3 0 013-3h11"/>'),
  star: svg('<path d="M12 3l2.8 5.7 6.2.9-4.5 4.4 1 6.2L12 17.3 6.5 20.2l1-6.2L3 9.6l6.2-.9z"/>'),
  trash: svg('<path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/>'),
  search: svg('<circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3"/>'),
  page: svg('<path d="M6 3h9l4 4v14H6z"/><path d="M14 3v5h5"/>'),
  download: svg('<path d="M12 4v11M7 10l5 5 5-5M5 20h14"/>'),
  copy: svg('<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V5a1 1 0 00-1-1H5a1 1 0 00-1 1v10a1 1 0 001 1h3"/>'),
  up: svg('<path d="M6 15l6-6 6 6"/>'),
  down: svg('<path d="M6 9l6 6 6-6"/>'),
  moon: svg('<path d="M20 14.5A8 8 0 019.5 4 8 8 0 1020 14.5z"/>'),
  close: svg('<path d="M6 6l12 12M18 6L6 18"/>'),
  stylus: svg('<path d="M15 4l5 5-9.5 9.5-6 1.5 1.5-6z"/><path d="M13 6l5 5"/><path d="M3 21l2-2"/>'),
  fountain: svg('<path d="M12 3l5 7-5 11-5-11z"/><path d="M12 10v5"/><circle cx="12" cy="9" r="1"/>'),
  ballpoint: svg('<path d="M9 3h6v12l-3 5-3-5z"/><path d="M9 15h6"/>'),
  pencil: svg('<path d="M9 3h6v12l-3 6-3-6z"/><path d="M9 15h6"/><path d="M11 19h2"/><path d="M9 6h6"/>'),
  calligraphy: svg('<path d="M14 3l5 5-8 8-4 1 1-4z"/><path d="M4 21c3-1 5-2 7-5"/>'),
  brush: svg('<path d="M14 4c2-1 5 2 4 4l-6 6-3-3z"/><path d="M9 11c-3 0-4 2-4 4s-1 3-2 4c3 1 7 0 8-3"/>'),
  sAuto: svg('<path d="M5 19c2-6 5-9 9-10"/><path d="M15 3l1.2 2.8L19 7l-2.8 1.2L15 11l-1.2-2.8L11 7l2.8-1.2z"/>'),
  sLine: svg('<path d="M5 19L19 5"/>'),
  sArrow: svg('<path d="M5 19L19 5"/><path d="M11 5h8v8"/>'),
  sRectangle: svg('<rect x="4" y="6" width="16" height="12" rx="1"/>'),
  sEllipse: svg('<ellipse cx="12" cy="12" rx="8.5" ry="6.5"/>'),
  sTriangle: svg('<path d="M12 4l9 16H3z"/>'),
  sDiamond: svg('<path d="M12 3l9 9-9 9-9-9z"/>'),
  sStar: svg('<path d="M12 3l2.6 5.6 6.1.7-4.5 4.2 1.2 6L12 16.6 6.6 19.5l1.2-6-4.5-4.2 6.1-.7z"/>'),
  listBullet: svg('<circle cx="5" cy="7" r="1.3" fill="currentColor"/><circle cx="5" cy="12" r="1.3" fill="currentColor"/><circle cx="5" cy="17" r="1.3" fill="currentColor"/><path d="M10 7h10M10 12h10M10 17h10"/>'),
  listNumber: svg('<path d="M4 5h2v5M4 10h3M4 14h3l-3 4h3"/><path d="M11 7h9M11 12h9M11 17h9"/>'),
  alignLeft: svg('<path d="M4 6h16M4 10h10M4 14h16M4 18h10"/>'),
  alignCenter: svg('<path d="M4 6h16M7 10h10M4 14h16M7 18h10"/>'),
  alignRight: svg('<path d="M4 6h16M10 10h10M4 14h16M10 18h10"/>'),
  fill: svg('<rect x="4" y="4" width="16" height="16" rx="2" fill="currentColor" fill-opacity="0.35"/>'),
};

let toastTimer: number | undefined;
export function toast(message: string, kind: "info" | "error" = "info") {
  let el = document.getElementById("toast");
  if (!el) {
    el = h("div", { id: "toast", role: "status", "aria-live": "polite" });
    document.body.append(el);
  }
  el.textContent = message;
  el.className = `toast show ${kind}`;
  window.clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => el!.classList.remove("show"), kind === "error" ? 5000 : 2600);
}

export function reportError(e: unknown) {
  console.error(e);
  toast(e instanceof Error ? e.message : String(e), "error");
}

export interface CustomFieldContext {
  /** Current values of all fields. */
  values: Record<string, string>;
  /** Sets this field's value. */
  set(value: string): void;
  /** Called whenever any field changes (e.g. to redraw previews). */
  onChange(fn: () => void): void;
}

export interface Field {
  name: string;
  label: string;
  type?: "text" | "select" | "color-swatches" | "custom";
  /** Builds a "custom" field. */
  render?: (ctx: CustomFieldContext) => HTMLElement;
  value?: string;
  /** Options for a "select" field; consecutive options sharing a group go in one <optgroup>. */
  options?: { value: string; label: string; group?: string }[];
  /** Colours for a "color-swatches" field (defaults to cover colours). */
  colors?: { color: string; label: string }[];
}

export const COVER_COLORS = ["#2563eb", "#0d9488", "#16a34a", "#ca8a04", "#ea580c", "#dc2626", "#db2777", "#7c3aed", "#475569", "#111827"];

/** Shows a modal form; resolves with field values or null when cancelled. */
export function formDialog(title: string, fields: Field[], submitLabel = "Save"): Promise<Record<string, string> | null> {
  return new Promise((resolve) => {
    const values: Record<string, string> = {};
    const listeners: (() => void)[] = [];
    const changed = () => listeners.forEach((fn) => fn());
    const form = h("form", { class: "dialog-form" });
    const backdrop = h("div", { class: "modal-backdrop" }, h("div", { class: "modal", role: "dialog", "aria-modal": "true", "aria-label": title }, h("h2", {}, title), form));
    const close = (result: Record<string, string> | null) => {
      backdrop.remove();
      document.removeEventListener("keydown", onKey, true);
      resolve(result);
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        e.stopPropagation();
        close(null);
      }
    };
    for (const f of fields) {
      values[f.name] = f.value ?? "";
      if (f.type === "select") {
        const option = (o: { value: string; label: string }) => h("option", { value: o.value, selected: o.value === f.value }, o.label);
        const items: HTMLElement[] = [];
        for (const o of f.options ?? []) {
          if (!o.group) items.push(option(o));
          else {
            let grp = items[items.length - 1];
            if (!(grp instanceof HTMLOptGroupElement) || grp.label !== o.group) items.push((grp = h("optgroup", { label: o.group })));
            grp.append(option(o));
          }
        }
        const select = h("select", { name: f.name }, ...items);
        select.addEventListener("change", () => {
          values[f.name] = select.value;
          changed();
        });
        if (!f.value && f.options?.length) values[f.name] = f.options[0].value;
        form.append(h("label", {}, h("span", {}, f.label), select));
      } else if (f.type === "color-swatches") {
        const row = h("div", { class: "swatches", role: "radiogroup", "aria-label": f.label });
        const palette = f.colors ?? COVER_COLORS.map((c) => ({ color: c, label: c }));
        if (!values[f.name]) values[f.name] = palette[0].color;
        for (const { color: c, label } of palette) {
          const b = h("button", { type: "button", class: "swatch", style: { background: c }, "aria-label": label, title: label, role: "radio", "aria-checked": String(c === values[f.name]) });
          b.addEventListener("click", () => {
            values[f.name] = c;
            row.querySelectorAll(".swatch").forEach((s) => s.setAttribute("aria-checked", "false"));
            b.setAttribute("aria-checked", "true");
            changed();
          });
          row.append(b);
        }
        form.append(h("div", { class: "field" }, h("span", {}, f.label), row));
      } else if (f.type === "custom" && f.render) {
        const el = f.render({
          values,
          set: (v) => {
            values[f.name] = v;
            changed();
          },
          onChange: (fn) => listeners.push(fn),
        });
        // A custom field without a label only carries a value (or stands alone).
        form.append(f.label ? h("div", { class: "field" }, h("span", {}, f.label), el) : el);
      } else {
        const input = h("input", { name: f.name, value: f.value ?? "", autocomplete: "off", maxlength: "200" });
        input.addEventListener("input", () => {
          values[f.name] = input.value;
          changed();
        });
        form.append(h("label", {}, h("span", {}, f.label), input));
      }
    }
    form.append(
      h(
        "div",
        { class: "dialog-actions" },
        h("button", { type: "button", class: "btn", onclick: () => close(null) }, "Cancel"),
        h("button", { type: "submit", class: "btn primary" }, submitLabel),
      ),
    );
    form.addEventListener("submit", (e) => {
      e.preventDefault();
      close({ ...values });
    });
    backdrop.addEventListener("pointerdown", (e) => e.target === backdrop && close(null));
    document.addEventListener("keydown", onKey, true);
    document.body.append(backdrop);
    (form.querySelector("input, select") as HTMLElement | null)?.focus();
    (form.querySelector("input") as HTMLInputElement | null)?.select();
  });
}

export function confirmDialog(title: string, message: string, confirmLabel = "Delete"): Promise<boolean> {
  return new Promise((resolve) => {
    const close = (v: boolean) => {
      backdrop.remove();
      resolve(v);
    };
    const ok = h("button", { class: "btn danger", onclick: () => close(true) }, confirmLabel);
    const backdrop = h(
      "div",
      { class: "modal-backdrop" },
      h(
        "div",
        { class: "modal", role: "alertdialog", "aria-modal": "true", "aria-label": title },
        h("h2", {}, title),
        h("p", {}, message),
        h("div", { class: "dialog-actions" }, h("button", { class: "btn", onclick: () => close(false) }, "Cancel"), ok),
      ),
    );
    backdrop.addEventListener("keydown", (e) => e.key === "Escape" && close(false));
    document.body.append(backdrop);
    ok.focus();
  });
}

export interface MenuItem {
  label: string;
  action: () => void;
  danger?: boolean;
  /** Optional CSS font-family, e.g. to preview a font in its own face. */
  fontFamily?: string;
  checked?: boolean;
}

/** Opens a small popup menu anchored to an element. */
export function popupMenu(anchor: HTMLElement, items: MenuItem[]) {
  document.querySelectorAll(".popup-menu").forEach((m) => m.remove());
  const menu = h("div", { class: "popup-menu", role: "menu" });
  for (const item of items) {
    menu.append(
      h(
        "button",
        {
          role: item.checked === undefined ? "menuitem" : "menuitemradio",
          "aria-checked": item.checked === undefined ? undefined : String(item.checked),
          style: item.fontFamily ? { fontFamily: item.fontFamily, fontSize: "18px" } : undefined,
          class: item.danger ? "danger" : "",
          onclick: (e: Event) => {
            e.stopPropagation();
            menu.remove();
            item.action();
          },
        },
        item.label,
      ),
    );
  }
  document.body.append(menu);
  const r = anchor.getBoundingClientRect();
  const mw = menu.offsetWidth;
  const mh = menu.offsetHeight;
  menu.style.left = `${Math.max(8, Math.min(r.left, window.innerWidth - mw - 8))}px`;
  menu.style.top = `${r.bottom + mh + 8 > window.innerHeight ? Math.max(8, r.top - mh - 4) : r.bottom + 4}px`;
  const dismiss = (e: Event) => {
    if (!menu.contains(e.target as Node)) {
      menu.remove();
      document.removeEventListener("pointerdown", dismiss, true);
    }
  };
  setTimeout(() => document.addEventListener("pointerdown", dismiss, true));
  (menu.querySelector("button") as HTMLButtonElement | null)?.focus();
}

export interface SizeControlOptions {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  unit: string;
  presets: number[];
  /** "dot" previews line thickness, "letter" previews text size. */
  preview: "dot" | "letter";
  onChange(value: number): void;
}

/**
 * Size picker: one-tap presets plus a − / value / + stepper showing the
 * exact number, which can also be typed in.
 */
export function sizeControl(o: SizeControlOptions): HTMLElement {
  let value = o.value;
  const decimals = o.step < 1 ? 1 : 0;
  const clamp = (v: number) => Math.min(o.max, Math.max(o.min, Math.round(v / o.step) * o.step));
  const input = h("input", {
    type: "number",
    class: "size-value",
    min: String(o.min),
    max: String(o.max),
    step: String(o.step),
    "aria-label": `${o.label} (${o.unit})`,
  });
  const maxPreset = Math.max(...o.presets);
  const presetBtns = o.presets.map((p) => {
    const mark =
      o.preview === "dot"
        ? h("span", { class: "size-dot", style: { width: `${4 + (p / maxPreset) * 14}px`, height: `${4 + (p / maxPreset) * 14}px` } })
        : h("span", { class: "size-letter", style: { fontSize: `${10 + (p / maxPreset) * 12}px` } }, "A");
    const b = h("button", { class: "size-preset", type: "button", title: `${p} ${o.unit}`, "aria-label": `${o.label} ${p} ${o.unit}` }, mark);
    b.addEventListener("click", () => set(p));
    return b;
  });
  const sync = () => {
    input.value = value.toFixed(decimals);
    presetBtns.forEach((b, i) => b.setAttribute("aria-pressed", String(Math.abs(o.presets[i] - value) < 1e-6)));
  };
  const set = (v: number) => {
    const next = clamp(v);
    if (!Number.isFinite(next)) return sync();
    value = next;
    sync();
    o.onChange(value);
  };
  const stepBy = (dir: number) => {
    // Small steps for fine sizes, larger ones further up the range.
    const delta = value >= 20 ? Math.max(o.step, 2) : value >= 8 ? Math.max(o.step, 1) : o.step;
    set(value + dir * delta);
  };
  input.addEventListener("change", () => set(Number(input.value)));
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      set(Number(input.value));
      input.blur();
    }
  });
  sync();
  return h(
    "div",
    { class: "size-control", role: "group", "aria-label": o.label },
    presetBtns.length ? h("div", { class: "size-presets" }, ...presetBtns) : null,
    h(
      "div",
      { class: "size-stepper" },
      h("button", { type: "button", class: "step-btn", "aria-label": `Smaller ${o.label.toLowerCase()}`, onclick: () => stepBy(-1) }, "−"),
      input,
      h("span", { class: "size-unit" }, o.unit),
      h("button", { type: "button", class: "step-btn", "aria-label": `Larger ${o.label.toLowerCase()}`, onclick: () => stepBy(1) }, "+"),
    ),
  );
}

export function relativeTime(ms: number): string {
  const diff = Date.now() - ms;
  const min = Math.round(diff / 60000);
  if (min < 1) return "just now";
  if (min < 60) return `${min} min ago`;
  const hours = Math.round(min / 60);
  if (hours < 24) return `${hours} h ago`;
  return new Date(ms).toLocaleDateString();
}
