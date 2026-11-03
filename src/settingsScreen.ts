// The Settings screen: writing, stylus and touch, and appearance in one
// place. Changes apply at once.

import { getPref, setPref } from "./prefs";
import { getSetting, notifySettings, PEN_ONLY_KEY, setSetting, type Settings } from "./settings";
import { setTheme, themeMode } from "./theme";
import { h, icon } from "./ui";
import { backupSection } from "./backupSettings";

type Choice<T extends string> = { value: T; label: string };

function segmented<T extends string>(label: string, choices: Choice<T>[], current: T, onPick: (v: T) => void): HTMLElement {
  const group = h("div", { class: "seg", role: "radiogroup", "aria-label": label });
  const draw = (value: T) =>
    group.replaceChildren(
      ...choices.map((c) =>
        h(
          "button",
          {
            type: "button",
            role: "radio",
            "aria-checked": String(c.value === value),
            class: c.value === value ? "on" : "",
            onclick: () => {
              onPick(c.value);
              draw(c.value);
            },
          },
          c.label,
        ),
      ),
    );
  draw(current);
  return group;
}

function toggle(label: string, on: boolean, onChange: (v: boolean) => void): HTMLElement {
  const sw = h("button", { type: "button", class: "switch-btn", role: "switch", "aria-checked": String(on), "aria-label": label }, h("span", { class: "switch" }, h("span", {})));
  sw.addEventListener("click", () => {
    const next = sw.getAttribute("aria-checked") !== "true";
    sw.setAttribute("aria-checked", String(next));
    onChange(next);
  });
  return sw;
}

function row(title: string, help: string, control: HTMLElement): HTMLElement {
  return h("div", { class: "set-row" }, h("div", { class: "set-text" }, h("div", { class: "set-title" }, title), h("div", { class: "set-help" }, help)), control);
}

function section(iconName: Parameters<typeof icon>[0], title: string, ...rows: HTMLElement[]): HTMLElement {
  return h("section", { class: "set-section" }, h("h3", {}, icon(iconName), title), ...rows);
}

const pick = <K extends keyof Settings>(k: K) => (v: Settings[K]) => setSetting(k, v);

/** Opens the Settings screen (closing it is Esc, the backdrop or Done). */
export function openSettings() {
  if (document.querySelector(".settings-modal")) return;
  const close = () => {
    backdrop.remove();
    document.removeEventListener("keydown", onKey, true);
  };
  const onKey = (e: KeyboardEvent) => {
    if (e.key === "Escape") {
      e.stopPropagation();
      close();
    }
  };
  const body = h(
    "div",
    { class: "set-body" },
    section(
      "pen",
      "Writing",
      row("Smoothing", "Evens out shaky lines as you write. Off draws exactly what you write.", segmented("Smoothing", [{ value: "off", label: "Off" }, { value: "light", label: "Light" }, { value: "normal", label: "Normal" }, { value: "strong", label: "Strong" }], getSetting("smoothing"), pick("smoothing"))),
      row("Pressure", "How hard you press for a full line. Light: a gentle touch is enough. Firm: press harder for thick lines.", segmented("Pressure", [{ value: "light", label: "Light" }, { value: "normal", label: "Normal" }, { value: "firm", label: "Firm" }], getSetting("pressure"), pick("pressure"))),
      row("Cursor under the pen", "Shown for the pen, highlighter and shapes.", segmented("Cursor", [{ value: "cross", label: "Cross" }, { value: "dot", label: "Dot" }, { value: "none", label: "Hidden" }], getSetting("penCursor"), pick("penCursor"))),
    ),
    section(
      "stylus",
      "Stylus and touch",
      row("Side button", "Hold the pen's side button while writing to use this tool instead.", segmented("Side button", [{ value: "eraser", label: "Eraser" }, { value: "lasso", label: "Lasso" }, { value: "none", label: "Nothing" }], getSetting("sideButton"), pick("sideButton"))),
      row("Eraser end", "Turning the pen over erases (on pens that have an eraser end).", toggle("Eraser end", getSetting("eraserEnd"), pick("eraserEnd"))),
      row("Palm rejection", "Ignores your hand resting on the screen while you use the pen.", toggle("Palm rejection", getSetting("palmRejection"), pick("palmRejection"))),
      row(
        "Pen-only mode",
        "Only the pen writes; touch and the mouse scroll and zoom. Turns on by itself the first time you use a pen.",
        toggle("Pen-only mode", getPref<string>(PEN_ONLY_KEY, "false") === "true", (v) => {
          setPref(PEN_ONLY_KEY, String(v));
          notifySettings();
        }),
      ),
    ),
    backupSection(),
    section("sun", "Appearance", row("Theme", "System follows your computer's light or dark setting.", segmented("Theme", [{ value: "light", label: "Light" }, { value: "dark", label: "Dark" }, { value: "system", label: "System" }], themeMode(), (v) => setTheme(v)))),
  );
  const backdrop = h(
    "div",
    { class: "modal-backdrop", onpointerdown: (e: Event) => e.target === backdrop && close() },
    h(
      "div",
      { class: "modal settings-modal", role: "dialog", "aria-modal": "true", "aria-label": "Settings" },
      h("div", { class: "set-head" }, h("h2", {}, "Settings"), h("button", { class: "btn primary", onclick: close }, "Done")),
      body,
    ),
  );
  document.body.append(backdrop);
  document.addEventListener("keydown", onKey, true);
  (backdrop.querySelector(".seg button.on") as HTMLElement | null)?.focus();
}
