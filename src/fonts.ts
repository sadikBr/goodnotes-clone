// Handwriting font presets for text boxes and sticky notes. All fonts are
// bundled (SIL Open Font License, via @fontsource) so the app works offline.

import "@fontsource/caveat/400.css";
import "@fontsource/patrick-hand/400.css";
import "@fontsource/kalam/400.css";
import "@fontsource/indie-flower/400.css";
import "@fontsource/shadows-into-light/400.css";
import "@fontsource/dancing-script/400.css";
import "@fontsource/architects-daughter/400.css";

export type FontId = "sans" | "caveat" | "patrick-hand" | "kalam" | "indie-flower" | "shadows" | "dancing" | "architect";

export interface FontPreset {
  id: FontId;
  label: string;
  /** CSS font-family list, usable in both CSS and canvas `ctx.font`. */
  family: string;
  /** Primary face to wait for before drawing (null for system fonts). */
  face: string | null;
}

const SANS = '"Inter", "Segoe UI", system-ui, -apple-system, sans-serif';

export const FONT_PRESETS: FontPreset[] = [
  { id: "sans", label: "Clean", family: SANS, face: null },
  { id: "caveat", label: "Quick notes", family: `"Caveat", cursive`, face: "Caveat" },
  { id: "patrick-hand", label: "Neat print", family: `"Patrick Hand", cursive`, face: "Patrick Hand" },
  { id: "kalam", label: "Casual", family: `"Kalam", cursive`, face: "Kalam" },
  { id: "indie-flower", label: "Bubbly", family: `"Indie Flower", cursive`, face: "Indie Flower" },
  { id: "shadows", label: "Light pencil", family: `"Shadows Into Light", cursive`, face: "Shadows Into Light" },
  { id: "dancing", label: "Cursive", family: `"Dancing Script", cursive`, face: "Dancing Script" },
  { id: "architect", label: "Architect", family: `"Architects Daughter", cursive`, face: "Architects Daughter" },
];

export function fontPreset(id: string | undefined): FontPreset {
  return FONT_PRESETS.find((f) => f.id === id) ?? FONT_PRESETS[0];
}

/** CSS / canvas font string, e.g. `24px "Caveat", cursive`. */
export function fontCss(id: string | undefined, sizePx: number, style?: { bold?: boolean; italic?: boolean }): string {
  return `${style?.italic ? "italic " : ""}${style?.bold ? "bold " : ""}${sizePx}px ${fontPreset(id).family}`;
}

const loaded = new Set<string>();

/**
 * Resolves once the given presets' font files are ready, so canvas text is
 * never drawn with a fallback face. Resolves `true` if anything had to load.
 */
export async function ensureFonts(ids: Iterable<string | undefined>): Promise<boolean> {
  if (typeof document === "undefined" || !("fonts" in document)) return false;
  const pending: Promise<unknown>[] = [];
  for (const id of new Set(ids)) {
    const face = fontPreset(id).face;
    if (!face || loaded.has(face)) continue;
    pending.push(
      document.fonts
        .load(`24px "${face}"`, "Aa")
        .then(() => loaded.add(face))
        .catch(() => loaded.add(face)), // fall back silently rather than retrying forever
    );
  }
  if (pending.length === 0) return false;
  await Promise.all(pending);
  return true;
}
