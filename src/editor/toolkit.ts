// Compact tool options: each tool keeps a few favourite colours and sizes
// (set once, remembered), and the full choice is one tap away in a popover.
// Pen nibs and shape kinds are single chips that open a list with previews.

import { getPref, setPref } from "../prefs";
import { h, icon } from "../ui";
import { openColorPicker } from "../colorPicker";
import { sizeControl, type SizeControlOptions } from "../ui";

// --- favourites -------------------------------------------------------------------

export type FavTool = "pen" | "highlighter" | "shape" | "text" | "sticky" | "laser" | "eraser";

interface ToolFavs {
  colors: string[];
  color: number;
  sizes: number[];
  size: number;
}

const INK = ["#111827", "#2563eb", "#dc2626", "#16a34a", "#9333ea"];
const DEFAULTS: Record<FavTool, ToolFavs> = {
  pen: { colors: INK, color: 0, sizes: [1.5, 3, 6], size: 1 },
  highlighter: { colors: ["#facc15", "#4ade80", "#f472b6", "#60a5fa", "#fb923c"], color: 0, sizes: [10, 18, 28], size: 1 },
  shape: { colors: INK, color: 0, sizes: [2, 3, 6], size: 1 },
  text: { colors: ["#111827", "#2563eb", "#dc2626", "#16a34a", "#6b7280"], color: 0, sizes: [16, 24, 36], size: 1 },
  sticky: { colors: ["#fef08a", "#bbf7d0", "#bfdbfe", "#fbcfe8", "#fed7aa"], color: 0, sizes: [14, 18, 24], size: 1 },
  laser: { colors: ["#ff1f1f", "#12c95a", "#1f7bff", "#b44dff", "#ff8a00"], color: 0, sizes: [], size: 0 },
  eraser: { colors: [], color: 0, sizes: [8, 16, 32], size: 1 },
};

/** The fuller palette offered in the colour popover, per kind of tool. */
export const PALETTES: Record<"ink" | "highlighter" | "sticky" | "laser", string[]> = {
  ink: [
    "#111827", "#374151", "#6b7280", "#9ca3af", "#ffffff", "#7c2d12",
    "#1e3a8a", "#2563eb", "#0891b2", "#0d9488", "#16a34a", "#65a30d",
    "#ca8a04", "#ea580c", "#dc2626", "#be123c", "#db2777", "#9333ea",
  ],
  highlighter: ["#facc15", "#fde047", "#a3e635", "#4ade80", "#2dd4bf", "#38bdf8", "#60a5fa", "#c084fc", "#f472b6", "#fb7185", "#fb923c", "#e5e7eb"],
  sticky: ["#fef08a", "#fde68a", "#bbf7d0", "#a7f3d0", "#bfdbfe", "#c7d2fe", "#fbcfe8", "#fecaca", "#fed7aa", "#e5e7eb", "#ddd6fe", "#99f6e4"],
  laser: ["#ff1f1f", "#ff8a00", "#ffd400", "#12c95a", "#00c2d1", "#1f7bff", "#b44dff", "#ff3fa4", "#ffffff"],
};

export function paletteFor(tool: FavTool): string[] {
  return tool === "highlighter" ? PALETTES.highlighter : tool === "sticky" ? PALETTES.sticky : tool === "laser" ? PALETTES.laser : PALETTES.ink;
}

const KEY = "inkleaf-favourites";

/** Per-tool favourite colours and sizes, with the one in use; remembered. */
export class Favourites {
  private data: Record<FavTool, ToolFavs>;

  constructor() {
    this.data = structuredClone(DEFAULTS);
    try {
      const saved = JSON.parse(getPref(KEY, "{}")) as Partial<Record<FavTool, Partial<ToolFavs>>>;
      for (const t of Object.keys(DEFAULTS) as FavTool[]) {
        const s = saved[t];
        if (!s) continue;
        const d = this.data[t];
        if (Array.isArray(s.colors) && s.colors.length === d.colors.length && s.colors.every((c) => typeof c === "string")) d.colors = s.colors;
        if (Array.isArray(s.sizes) && s.sizes.length === d.sizes.length && s.sizes.every((v) => typeof v === "number" && v > 0)) d.sizes = s.sizes;
        if (typeof s.color === "number" && s.color >= 0 && s.color < d.colors.length) d.color = s.color;
        if (typeof s.size === "number" && s.size >= 0 && s.size < d.sizes.length) d.size = s.size;
      }
    } catch {
      /* defaults */
    }
  }

  get(tool: FavTool): ToolFavs {
    return this.data[tool];
  }

  color(tool: FavTool): string | undefined {
    const d = this.data[tool];
    return d.colors[d.color];
  }

  size(tool: FavTool): number | undefined {
    const d = this.data[tool];
    return d.sizes[d.size];
  }

  selectColor(tool: FavTool, i: number) {
    this.data[tool].color = i;
    this.save();
  }

  setColor(tool: FavTool, i: number, c: string) {
    this.data[tool].colors[i] = c;
    this.data[tool].color = i;
    this.save();
  }

  selectSize(tool: FavTool, i: number) {
    this.data[tool].size = i;
    this.save();
  }

  setSize(tool: FavTool, i: number, v: number) {
    this.data[tool].sizes[i] = v;
    this.data[tool].size = i;
    this.save();
  }

  private save() {
    setPref(KEY, JSON.stringify(this.data));
  }
}

// --- popovers -------------------------------------------------------------------------

/** A small panel under (or above) `anchor`; closes on outside click or Escape. */
let openPopover: { anchor: HTMLElement; close(): void } | null = null;

export function popover(anchor: HTMLElement, content: HTMLElement, cls = ""): () => void {
  // Clicking the button of an open popover closes it.
  const wasOpen = openPopover?.anchor === anchor && openPopover;
  openPopover?.close();
  document.querySelectorAll(".tool-popover").forEach((p) => p.remove());
  if (wasOpen) return () => {};
  const panel = h("div", { class: `tool-popover ${cls}`.trim(), role: "dialog" }, content);
  const close = () => {
    panel.remove();
    anchor.setAttribute("aria-expanded", "false");
    document.removeEventListener("pointerdown", onOutside, true);
    document.removeEventListener("keydown", onKey, true);
    if (openPopover?.anchor === anchor) openPopover = null;
  };
  const onOutside = (e: Event) => {
    const t = e.target as Node;
    if (!panel.contains(t) && !anchor.contains(t) && !(t as Element).closest?.(".color-popover")) close();
  };
  const onKey = (e: KeyboardEvent) => {
    if (e.key === "Escape") {
      e.stopPropagation();
      close();
      anchor.focus();
    }
  };
  document.body.append(panel);
  const r = anchor.getBoundingClientRect();
  const below = r.bottom + 10 + panel.offsetHeight < window.innerHeight;
  panel.style.top = `${below ? r.bottom + 10 : r.top - 10 - panel.offsetHeight}px`;
  panel.style.left = `${Math.max(8, Math.min(window.innerWidth - panel.offsetWidth - 8, r.left + r.width / 2 - panel.offsetWidth / 2))}px`;
  anchor.setAttribute("aria-expanded", "true");
  openPopover = { anchor, close };
  setTimeout(() => {
    document.addEventListener("pointerdown", onOutside, true);
    document.addEventListener("keydown", onKey, true);
  });
  return close;
}

// --- colour favourites ----------------------------------------------------------------

/**
 * Five favourite colours and a palette button. Tapping a favourite uses it;
 * tapping the one in use (or the palette button) opens the palette, and the
 * pick replaces that favourite.
 */
export function colorFavourites(o: { colors: string[]; index: number; palette: string[]; onSelect(i: number): void; onEdit(i: number, c: string): void }): HTMLElement {
  const openPalette = (anchor: HTMLElement, i: number) => {
    const current = o.colors[i].toLowerCase();
    let close = () => {};
    const grid = h(
      "div",
      { class: "palette-grid", role: "radiogroup", "aria-label": "Colours" },
      ...o.palette.map((c) =>
        h("button", {
          class: "swatch",
          role: "radio",
          "aria-label": c,
          "aria-checked": String(c.toLowerCase() === current),
          style: { background: c },
          onclick: () => {
            close();
            o.onEdit(i, c);
          },
        }),
      ),
    );
    const custom = h(
      "button",
      {
        class: "btn ghost palette-custom",
        onclick: () => {
          close();
          openColorPicker(anchor, o.colors[i], (c) => o.onEdit(i, c));
        },
      },
      h("span", { class: "rainbow-dot" }),
      "Custom colour…",
    );
    close = popover(anchor, h("div", { class: "palette" }, h("div", { class: "popover-title" }, "Favourite colour"), grid, custom, h("p", { class: "popover-note" }, "Your pick replaces the selected favourite.")));
  };
  const slots = o.colors.map((c, i) => {
    const b = h("button", {
      class: "fav-swatch",
      role: "radio",
      "aria-checked": String(i === o.index),
      "aria-label": `Colour ${i + 1}: ${c}`,
      title: i === o.index ? "Tap again to change this favourite" : c,
      style: { "--c": c },
    });
    b.style.setProperty("--c", c);
    b.addEventListener("click", () => (i === o.index ? openPalette(b, i) : o.onSelect(i)));
    b.addEventListener("dblclick", () => openPalette(b, i));
    return b;
  });
  const more = h("button", { class: "palette-btn", "aria-label": "All colours", title: "All colours", "aria-haspopup": "dialog" });
  more.addEventListener("click", () => openPalette(more, o.index));
  return h("div", { class: "fav-colors", role: "radiogroup", "aria-label": "Colour" }, ...slots, more);
}

// --- size favourites ------------------------------------------------------------------

/**
 * Three favourite sizes drawn at their relative thickness, plus a chip with
 * the exact value that opens a slider to fine-tune the selected favourite.
 */
export function sizeFavourites(o: { sizes: number[]; index: number; spec: Omit<SizeControlOptions, "value" | "onChange" | "presets" | "unit">; unit: string; onSelect(i: number): void; onEdit(i: number, v: number): void }): HTMLElement {
  const max = Math.max(...o.sizes, 1);
  const decimals = o.spec.step < 1 ? 1 : 0;
  const slots = o.sizes.map((v, i) => {
    const d = 5 + (v / max) * 13;
    const mark =
      o.spec.preview === "dot"
        ? h("span", { class: "size-dot", style: { width: `${d}px`, height: `${d}px` } })
        : h("span", { class: "size-letter", style: { fontSize: `${10 + (v / max) * 10}px` } }, "A");
    const b = h("button", { class: "fav-size", role: "radio", "aria-checked": String(i === o.index), "aria-label": `${o.spec.label} ${v} ${o.unit}`, title: `${v.toFixed(decimals)} ${o.unit}` }, mark);
    b.addEventListener("click", () => o.onSelect(i));
    return b;
  });
  const chip = h("button", { class: "value-chip", title: `Adjust ${o.spec.label.toLowerCase()}`, "aria-haspopup": "dialog" }, `${o.sizes[o.index].toFixed(decimals)}`, h("span", { class: "unit" }, o.unit), icon("down"));
  chip.addEventListener("click", () => {
    let value = o.sizes[o.index];
    const range = h("input", { type: "range", class: "size-range", min: String(o.spec.min), max: String(o.spec.max), step: String(o.spec.step), value: String(value), "aria-label": o.spec.label });
    const stepper = sizeControl({ ...o.spec, value, unit: o.unit, presets: [], onChange: (v) => commit(v, false) });
    const commit = (v: number, fromRange: boolean) => {
      value = v;
      if (!fromRange) range.value = String(v);
      o.onEdit(o.index, v);
    };
    range.addEventListener("input", () => {
      const v = Number(range.value);
      (stepper.querySelector("input") as HTMLInputElement).value = v.toFixed(decimals);
      commit(v, true);
    });
    popover(chip, h("div", { class: "size-pop" }, h("div", { class: "popover-title" }, o.spec.label), range, stepper, h("p", { class: "popover-note" }, "Adjusts the selected size.")));
  });
  return h("div", { class: "fav-sizes" }, h("div", { class: "fav-size-group", role: "radiogroup", "aria-label": o.spec.label }, ...slots), chip);
}

// --- menu chips (pen nib, shape kind) ---------------------------------------------------

export interface ChipItem {
  id: string;
  label: string;
  /** Makes a small picture of the choice (called once per place it is shown). */
  art(): HTMLElement;
}

/** A chip showing the current choice; opens a list with pictures to change it. */
export function choiceChip(o: { label: string; items: ChipItem[]; current: string; onPick(id: string): void; title: string }): HTMLElement {
  const cur = o.items.find((i) => i.id === o.current) ?? o.items[0];
  const chip = h("button", { class: "choice-chip", title: o.title, "aria-haspopup": "menu", "aria-label": `${o.label}: ${cur.label}` }, cur.art(), h("span", { class: "choice-label" }, cur.label), icon("down"));
  chip.addEventListener("click", () => {
    let close = () => {};
    const list = h(
      "div",
      { class: "choice-list", role: "menu" },
      ...o.items.map((it) =>
        h(
          "button",
          {
            class: "choice-item",
            role: "menuitemradio",
            "aria-checked": String(it.id === o.current),
            onclick: () => {
              close();
              o.onPick(it.id);
            },
          },
          it.art(),
          h("span", {}, it.label),
        ),
      ),
    );
    close = popover(chip, h("div", {}, h("div", { class: "popover-title" }, o.label), list));
    (list.querySelector('[aria-checked="true"]') as HTMLElement | null)?.focus();
  });
  return chip;
}
