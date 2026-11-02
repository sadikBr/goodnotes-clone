// The keyboard shortcuts sheet (press ? in a notebook).

import { h } from "../ui";

const GROUPS: { title: string; items: [string, string[]][] }[] = [
  {
    title: "Tools",
    items: [
      ["Pen", ["P"]],
      ["Highlighter", ["H"]],
      ["Eraser", ["E"]],
      ["Lasso select", ["L"]],
      ["Shapes", ["S"]],
      ["Text box", ["T"]],
      ["Sticky note", ["N"]],
      ["Laser pointer", ["K"]],
      ["Pan (hold)", ["Space"]],
    ],
  },
  {
    title: "Editing",
    items: [
      ["Undo", ["Ctrl", "Z"]],
      ["Redo", ["Ctrl", "Shift", "Z"]],
      ["Copy / paste", ["Ctrl", "C / V"]],
      ["Duplicate", ["Ctrl", "D"]],
      ["Select all", ["Ctrl", "A"]],
      ["Delete selection", ["Del"]],
      ["Bold / italic (typing)", ["Ctrl", "B / I"]],
      ["Move freely (no guides)", ["Alt"]],
    ],
  },
  {
    title: "View",
    items: [
      ["Zoom in / out", ["Ctrl", "+ / −"]],
      ["Fit width", ["Ctrl", "0"]],
      ["Next / previous page", ["→ / ←"]],
      ["Ruler & instruments", ["R"]],
      ["Pages sidebar", ["["]],
      ["Focus mode", ["F"]],
      ["This list", ["?"]],
    ],
  },
];

export function shortcutsSheet() {
  if (document.querySelector(".shortcuts-modal")) return;
  const close = () => {
    backdrop.remove();
    document.removeEventListener("keydown", onKey, true);
  };
  const onKey = (e: KeyboardEvent) => {
    if (e.key === "Escape" || e.key === "?") {
      e.preventDefault();
      e.stopPropagation();
      close();
    }
  };
  const groups = GROUPS.map((g) =>
    h(
      "section",
      { class: "shortcut-group" },
      h("h3", {}, g.title),
      ...g.items.map(([label, keys]) => h("div", { class: "shortcut-row" }, h("span", {}, label), h("span", { class: "keys" }, ...keys.map((k) => h("kbd", {}, k))))),
    ),
  );
  const backdrop = h(
    "div",
    { class: "modal-backdrop", onclick: (e: Event) => e.target === backdrop && close() },
    h(
      "div",
      { class: "modal shortcuts-modal", role: "dialog", "aria-modal": "true", "aria-label": "Keyboard shortcuts" },
      h("div", { class: "shortcuts-head" }, h("h2", {}, "Keyboard shortcuts"), h("button", { class: "btn ghost", onclick: close }, "Done")),
      h("div", { class: "shortcut-groups" }, ...groups),
    ),
  );
  document.body.append(backdrop);
  document.addEventListener("keydown", onKey, true);
}
