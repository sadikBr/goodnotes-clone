// Dialog for turning selected handwriting into text in a handwriting font.

import type { HandwritingRecognition } from "../api";
import { ensureFonts, FONT_PRESETS, type FontId } from "../fonts";
import { h } from "../ui";

export interface InkToTextResult {
  text: string;
  font: FontId;
  keepInk: boolean;
}

export function inkToTextDialog(recognition: Promise<HandwritingRecognition>, initialFont: FontId): Promise<InkToTextResult | null> {
  void ensureFonts(FONT_PRESETS.map((f) => f.id));
  return new Promise((resolve) => {
    let font = initialFont;
    const status = h("p", { class: "ink-status", "aria-live": "polite" }, "Reading your handwriting…");
    const suggestions = h("div", { class: "ink-suggestions" });
    const input = h("input", { class: "ink-text", "aria-label": "Text", placeholder: "Type what you wrote", maxlength: "500", autocomplete: "off" });
    const keep = h("input", { type: "checkbox", id: "keep-ink" });
    const apply = h("button", { type: "submit", class: "btn primary", disabled: true }, "Apply font");
    const cards = FONT_PRESETS.map((f) => {
      const preview = h("span", { class: "font-card-preview", style: { fontFamily: f.family } });
      const card = h("button", { type: "button", class: "font-card", role: "radio", "aria-checked": String(f.id === font), "aria-label": f.label }, preview, h("span", { class: "font-card-label" }, f.label));
      card.addEventListener("click", () => {
        font = f.id;
        cards.forEach((c) => c.card.setAttribute("aria-checked", String(c.id === font)));
      });
      return { id: f.id, card, preview };
    });
    const refresh = () => {
      const text = input.value.trim();
      cards.forEach((c) => (c.preview.textContent = text || "Your text"));
      apply.disabled = text.length === 0;
    };
    input.addEventListener("input", refresh);
    refresh();

    const form = h(
      "form",
      { class: "ink-form" },
      status,
      suggestions,
      h("label", { class: "ink-label" }, h("span", {}, "Text"), input),
      h("div", { class: "ink-label" }, h("span", {}, "Font"), h("div", { class: "font-grid", role: "radiogroup", "aria-label": "Font" }, ...cards.map((c) => c.card))),
      h("label", { class: "ink-keep", for: "keep-ink" }, keep, h("span", {}, "Keep my handwriting as well")),
      h("div", { class: "dialog-actions" }, h("button", { type: "button", class: "btn", onclick: () => close(null) }, "Cancel"), apply),
    );
    const backdrop = h(
      "div",
      { class: "modal-backdrop" },
      h("div", { class: "modal ink-modal", role: "dialog", "aria-modal": "true", "aria-label": "Apply a font to handwriting" }, h("h2", {}, "Apply a font to handwriting"), form),
    );
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        e.stopPropagation();
        close(null);
      }
    };
    const close = (result: InkToTextResult | null) => {
      backdrop.remove();
      document.removeEventListener("keydown", onKey, true);
      resolve(result);
    };
    form.addEventListener("submit", (e) => {
      e.preventDefault();
      const text = input.value.trim();
      if (text) close({ text, font, keepInk: keep.checked });
    });
    backdrop.addEventListener("pointerdown", (e) => e.target === backdrop && close(null));
    document.addEventListener("keydown", onKey, true);
    document.body.append(backdrop);
    input.focus();

    recognition
      .then((r) => {
        if (!r.available) {
          status.textContent = "Automatic handwriting recognition isn't available on this computer. Type what you wrote and pick a font.";
          return;
        }
        if (r.candidates.length === 0) {
          status.textContent = "Couldn't read that handwriting. Type what you wrote and pick a font.";
          return;
        }
        status.textContent = r.candidates.length > 1 ? "Recognised. Pick the right reading or edit it:" : "Recognised. Edit the text if needed:";
        if (!input.value) input.value = r.candidates[0];
        if (r.candidates.length > 1) {
          suggestions.replaceChildren(
            ...r.candidates.map((c) =>
              h(
                "button",
                {
                  type: "button",
                  class: "chip",
                  onclick: () => {
                    input.value = c;
                    refresh();
                    input.focus();
                  },
                },
                c,
              ),
            ),
          );
        }
        refresh();
        input.select();
      })
      .catch((e: unknown) => {
        status.textContent = `Recognition failed (${e instanceof Error ? e.message : String(e)}). Type what you wrote and pick a font.`;
      });
  });
}
