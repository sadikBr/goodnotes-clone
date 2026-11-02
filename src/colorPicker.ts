// Custom colour picker: a rainbow swatch next to the preset colours that
// opens a panel with a saturation/brightness square, a hue slider, a hex
// field, recently used colours and (where supported) an eyedropper.

import { getPref, setPref } from "./prefs";
import { h } from "./ui";

const RECENT_KEY = "inkleaf.recentColors";
const MAX_RECENT = 8;

// --- colour maths ----------------------------------------------------------

export interface Hsv {
  h: number; // 0..360
  s: number; // 0..1
  v: number; // 0..1
}

/** Normalises "#abc" / "#AABBCC" / "aabbcc" to "#aabbcc"; null if invalid. */
export function normalizeHex(input: string): string | null {
  let s = input.trim().toLowerCase();
  if (s.startsWith("#")) s = s.slice(1);
  if (/^[0-9a-f]{3}$/.test(s)) s = [...s].map((c) => c + c).join("");
  return /^[0-9a-f]{6}$/.test(s) ? `#${s}` : null;
}

export function hexToHsv(hex: string): Hsv {
  const n = normalizeHex(hex) ?? "#000000";
  const r = parseInt(n.slice(1, 3), 16) / 255;
  const g = parseInt(n.slice(3, 5), 16) / 255;
  const b = parseInt(n.slice(5, 7), 16) / 255;
  const max = Math.max(r, g, b);
  const d = max - Math.min(r, g, b);
  let hue = 0;
  if (d > 0) {
    if (max === r) hue = ((g - b) / d) % 6;
    else if (max === g) hue = (b - r) / d + 2;
    else hue = (r - g) / d + 4;
    hue *= 60;
    if (hue < 0) hue += 360;
  }
  return { h: hue, s: max === 0 ? 0 : d / max, v: max };
}

export function hsvToHex({ h: hue, s, v }: Hsv): string {
  const f = (n: number) => {
    const k = (n + hue / 60) % 6;
    return v - v * s * Math.max(0, Math.min(k, 4 - k, 1));
  };
  return `#${[f(5), f(3), f(1)].map((x) => Math.round(x * 255).toString(16).padStart(2, "0")).join("")}`;
}

// --- recent colours ----------------------------------------------------------

export function recentColors(): string[] {
  try {
    const list = JSON.parse(getPref(RECENT_KEY, "[]"));
    return Array.isArray(list) ? list.map((c) => normalizeHex(String(c))).filter((c): c is string => !!c).slice(0, MAX_RECENT) : [];
  } catch {
    return [];
  }
}

function rememberColor(hex: string) {
  const list = [hex, ...recentColors().filter((c) => c !== hex)].slice(0, MAX_RECENT);
  setPref(RECENT_KEY, JSON.stringify(list));
}

// --- swatch button -----------------------------------------------------------

export interface CustomColorOptions {
  /** The colour currently in use. */
  value: string | null;
  /** Preset colours shown next to this button (it is "selected" when the value isn't one). */
  presets: string[];
  onPick: (hex: string) => void;
  small?: boolean;
}

/** The rainbow swatch that opens the picker. Shows the custom colour when one is in use. */
export function customColorButton(opts: CustomColorOptions): HTMLButtonElement {
  const value = opts.value ? normalizeHex(opts.value) : null;
  const isCustom = !!value && !opts.presets.some((p) => normalizeHex(p) === value);
  const btn = h("button", {
    class: `swatch swatch-custom${opts.small === false ? "" : " small"}${isCustom ? " has-colour" : ""}`,
    role: "radio",
    "aria-checked": String(isCustom),
    "aria-label": isCustom ? `Custom colour ${value}` : "Custom colour",
    title: "Custom colour…",
    "aria-haspopup": "dialog",
  });
  if (isCustom) btn.style.setProperty("--custom", value);
  btn.addEventListener("click", () => openColorPicker(btn, value ?? "#2563eb", opts.onPick));
  return btn;
}

// --- picker panel --------------------------------------------------------------

interface EyeDropperCtor {
  new (): { open(): Promise<{ sRGBHex: string }> };
}

/** Opens the picker below (or above) `anchor`. `onPick` runs when a colour is chosen. */
export function openColorPicker(anchor: HTMLElement, initial: string, onPick: (hex: string) => void) {
  document.querySelector(".color-popover")?.remove();
  const hsv = hexToHsv(initial);
  let committed = normalizeHex(initial);

  const svKnob = h("div", { class: "cp-sv-knob" });
  const sv = h("div", { class: "cp-sv", tabindex: "0", role: "slider", "aria-label": "Saturation and brightness" }, svKnob);
  const hueKnob = h("div", { class: "cp-hue-knob" });
  const hue = h("div", { class: "cp-hue", tabindex: "0", role: "slider", "aria-label": "Hue", "aria-valuemin": "0", "aria-valuemax": "360" }, hueKnob);
  const preview = h("div", { class: "cp-preview" });
  const hex = h("input", { class: "cp-hex", type: "text", maxlength: "7", spellcheck: "false", "aria-label": "Hex colour" });

  const render = (syncHex = true) => {
    const c = hsvToHex(hsv);
    sv.style.backgroundColor = hsvToHex({ h: hsv.h, s: 1, v: 1 });
    svKnob.style.left = `${hsv.s * 100}%`;
    svKnob.style.top = `${(1 - hsv.v) * 100}%`;
    svKnob.style.background = c;
    hueKnob.style.left = `${(hsv.h / 360) * 100}%`;
    hue.setAttribute("aria-valuenow", String(Math.round(hsv.h)));
    preview.style.background = c;
    if (syncHex) hex.value = c;
  };

  const commit = (c = hsvToHex(hsv)) => {
    if (c === committed) return;
    committed = c;
    onPick(c);
  };

  // Drag on the square and the slider; the colour is applied on release.
  const drag = (el: HTMLElement, update: (x: number, y: number) => void) => {
    el.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      el.focus();
      el.setPointerCapture(e.pointerId);
      const move = (ev: PointerEvent) => {
        const r = el.getBoundingClientRect();
        update(Math.min(1, Math.max(0, (ev.clientX - r.left) / r.width)), Math.min(1, Math.max(0, (ev.clientY - r.top) / r.height)));
        render();
      };
      const up = () => {
        el.removeEventListener("pointermove", move);
        el.removeEventListener("pointerup", up);
        el.removeEventListener("pointercancel", up);
        commit();
      };
      move(e);
      el.addEventListener("pointermove", move);
      el.addEventListener("pointerup", up);
      el.addEventListener("pointercancel", up);
    });
  };
  drag(sv, (x, y) => {
    hsv.s = x;
    hsv.v = 1 - y;
  });
  drag(hue, (x) => {
    hsv.h = Math.min(359.9, x * 360);
  });

  // Keyboard: arrows adjust, the colour applies when the key is released.
  sv.addEventListener("keydown", (e) => {
    const step = e.shiftKey ? 0.1 : 0.02;
    const moves: Record<string, () => void> = {
      ArrowLeft: () => (hsv.s = Math.max(0, hsv.s - step)),
      ArrowRight: () => (hsv.s = Math.min(1, hsv.s + step)),
      ArrowUp: () => (hsv.v = Math.min(1, hsv.v + step)),
      ArrowDown: () => (hsv.v = Math.max(0, hsv.v - step)),
    };
    if (moves[e.key]) {
      e.preventDefault();
      moves[e.key]();
      render();
    }
  });
  hue.addEventListener("keydown", (e) => {
    const step = e.shiftKey ? 15 : 3;
    if (e.key === "ArrowLeft" || e.key === "ArrowDown") hsv.h = Math.max(0, hsv.h - step);
    else if (e.key === "ArrowRight" || e.key === "ArrowUp") hsv.h = Math.min(359.9, hsv.h + step);
    else return;
    e.preventDefault();
    render();
  });
  for (const el of [sv, hue]) el.addEventListener("keyup", (e) => e.key.startsWith("Arrow") && commit());

  hex.addEventListener("input", () => {
    const c = normalizeHex(hex.value);
    hex.classList.toggle("invalid", !c);
    if (c) {
      Object.assign(hsv, hexToHsv(c));
      render(false);
    }
  });
  const applyHex = () => {
    const c = normalizeHex(hex.value);
    if (c) commit(c);
    else render();
    hex.classList.remove("invalid");
  };
  hex.addEventListener("change", applyHex);
  hex.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      applyHex();
      close();
    }
  });

  const setColor = (c: string) => {
    Object.assign(hsv, hexToHsv(c));
    render();
    commit(c);
  };

  const tools: HTMLElement[] = [preview, hex];
  const Dropper = (window as unknown as { EyeDropper?: EyeDropperCtor }).EyeDropper;
  if (Dropper) {
    tools.push(
      h(
        "button",
        {
          class: "icon-btn small cp-dropper",
          "aria-label": "Pick a colour from the screen",
          title: "Pick from screen",
          onclick: async () => {
            try {
              const r = await new Dropper().open();
              const c = normalizeHex(r.sRGBHex);
              if (c) setColor(c);
            } catch {
              /* cancelled */
            }
          },
          html: '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M14.5 5.5l4 4"/><path d="M17 3a2.1 2.1 0 013 3l-2.5 2.5-3-3z"/><path d="M14.5 5.5L5 15l-1 5 5-1 9.5-9.5"/></svg>',
        },
      ),
    );
  }

  const recent = recentColors();
  const panel = h(
    "div",
    { class: "color-popover", role: "dialog", "aria-label": "Custom colour" },
    sv,
    hue,
    h("div", { class: "cp-row" }, ...tools),
    recent.length
      ? h(
          "div",
          { class: "cp-recent" },
          h("div", { class: "cp-label" }, "Recent"),
          h(
            "div",
            { class: "swatches" },
            ...recent.map((c) => h("button", { class: "swatch small", style: { background: c }, "aria-label": c, title: c, onclick: () => setColor(c) })),
          ),
        )
      : null,
  );

  const close = () => {
    panel.remove();
    anchor.setAttribute("aria-expanded", "false");
    document.removeEventListener("pointerdown", onOutside, true);
    document.removeEventListener("keydown", onKey, true);
    if (committed && committed !== normalizeHex(initial)) rememberColor(committed);
  };
  const onOutside = (e: Event) => {
    const t = e.target as Node;
    if (!panel.contains(t) && !anchor.contains(t)) close();
  };
  const onKey = (e: KeyboardEvent) => {
    if (e.key === "Escape") {
      e.stopPropagation();
      close();
      if (anchor.isConnected) anchor.focus();
    }
  };

  document.body.append(panel);
  render();
  const r = anchor.getBoundingClientRect();
  const below = r.bottom + 8;
  const top = below + panel.offsetHeight > window.innerHeight - 8 ? Math.max(8, r.top - 8 - panel.offsetHeight) : below;
  panel.style.top = `${top}px`;
  panel.style.left = `${Math.max(8, Math.min(r.left + r.width / 2 - panel.offsetWidth / 2, window.innerWidth - panel.offsetWidth - 8))}px`;
  anchor.setAttribute("aria-expanded", "true");
  setTimeout(() => {
    document.addEventListener("pointerdown", onOutside, true);
    document.addEventListener("keydown", onKey, true);
  });
  sv.focus();
}
