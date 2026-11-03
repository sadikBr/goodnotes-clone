// Handwriting font presets for text boxes and sticky notes. All fonts are
// bundled (SIL Open Font License, via @fontsource) so the app works offline.

import "@fontsource/inter/400.css";
import "@fontsource/inter/400-italic.css";
import "@fontsource/inter/700.css";
import "@fontsource/inter/700-italic.css";
import "@fontsource/caveat/400.css";
import "@fontsource/patrick-hand/400.css";
import "@fontsource/kalam/400.css";
import "@fontsource/indie-flower/400.css";
import "@fontsource/shadows-into-light/400.css";
import "@fontsource/dancing-script/400.css";
import "@fontsource/architects-daughter/400.css";
// Arabic: every preset falls back to Noto Sans Arabic for Arabic letters
// (only its Arabic subset, so Latin text keeps each preset's own look).
import "@fontsource/noto-sans-arabic/arabic-400.css";
import "@fontsource/noto-sans-arabic/arabic-700.css";

export type FontId = "sans" | "caveat" | "patrick-hand" | "kalam" | "indie-flower" | "shadows" | "dancing" | "architect";

export interface FontPreset {
  id: FontId;
  label: string;
  /** CSS font-family list, usable in both CSS and canvas `ctx.font`. */
  family: string;
  /** Primary face to wait for before drawing. */
  face: string | null;
}

/** Arabic fallback face, listed after each preset's own face. */
export const ARABIC_FACE = "Noto Sans Arabic";
const AR = `"${ARABIC_FACE}"`;
const SANS = `"Inter", ${AR}, "Segoe UI", system-ui, -apple-system, sans-serif`;
/** Faces of Inter that are bundled (handwriting fonts only have a regular face). */
const INTER_STYLES = ["", "bold ", "italic ", "italic bold "];

export const FONT_PRESETS: FontPreset[] = [
  { id: "sans", label: "Clean", family: SANS, face: "Inter" },
  { id: "caveat", label: "Quick notes", family: `"Caveat", ${AR}, cursive`, face: "Caveat" },
  { id: "patrick-hand", label: "Neat print", family: `"Patrick Hand", ${AR}, cursive`, face: "Patrick Hand" },
  { id: "kalam", label: "Casual", family: `"Kalam", ${AR}, cursive`, face: "Kalam" },
  { id: "indie-flower", label: "Bubbly", family: `"Indie Flower", ${AR}, cursive`, face: "Indie Flower" },
  { id: "shadows", label: "Light pencil", family: `"Shadows Into Light", ${AR}, cursive`, face: "Shadows Into Light" },
  { id: "dancing", label: "Cursive", family: `"Dancing Script", ${AR}, cursive`, face: "Dancing Script" },
  { id: "architect", label: "Architect", family: `"Architects Daughter", ${AR}, cursive`, face: "Architects Daughter" },
];

export function fontPreset(id: string | undefined): FontPreset {
  return FONT_PRESETS.find((f) => f.id === id) ?? FONT_PRESETS[0];
}

/**
 * A family list with the Arabic face first. Used for right-to-left text: the
 * spaces and punctuation between Arabic words then come from the Arabic
 * face too, which keeps every word on one baseline (WebKitGTK puts runs from
 * different fonts at different heights).
 */
export function arabicFirst(family: string): string {
  return `${AR}, ${family.replace(`, ${AR}`, "")}`;
}

/** CSS / canvas font string, e.g. `24px "Caveat", cursive` (`rtl`: Arabic face first). */
export function fontCss(id: string | undefined, sizePx: number, style?: { bold?: boolean; italic?: boolean; rtl?: boolean }): string {
  const family = fontPreset(id).family;
  return `${style?.italic ? "italic " : ""}${style?.bold ? "bold " : ""}${sizePx}px ${style?.rtl ? arabicFirst(family) : family}`;
}

const loaded = new Set<string>();

/**
 * Resolves once the given presets' font files are ready, so canvas text is
 * never drawn with a fallback face. Resolves `true` if anything had to load.
 */
export async function ensureFonts(ids: Iterable<string | undefined>): Promise<boolean> {
  if (typeof document === "undefined" || !("fonts" in document)) return false;
  const pending: Promise<unknown>[] = [];
  if (!loaded.has(ARABIC_FACE)) {
    pending.push(
      Promise.all(["", "bold "].map((st) => document.fonts.load(`${st}24px "${ARABIC_FACE}"`, "بسم")))
        .then(() => loaded.add(ARABIC_FACE))
        .catch(() => loaded.add(ARABIC_FACE)),
    );
  }
  for (const id of new Set(ids)) {
    const face = fontPreset(id).face;
    if (!face || loaded.has(face)) continue;
    const styles = face === "Inter" ? INTER_STYLES : [""];
    pending.push(
      Promise.all(styles.map((st) => document.fonts.load(`${st}24px "${face}"`, "Aa")))
        .then(() => loaded.add(face))
        .catch(() => loaded.add(face)), // fall back silently rather than retrying forever
    );
  }
  if (pending.length === 0) return false;
  await Promise.all(pending);
  return true;
}

const RTL_CHAR = /[\u0590-\u08FF\uFB1D-\uFDFF\uFE70-\uFEFF]/;
const STRONG = /[\u0590-\u08FF\uFB1D-\uFDFF\uFE70-\uFEFFA-Za-z\u00C0-\u024F\u0370-\u03FF\u0400-\u04FF]/;

/** True when a paragraph reads right to left: its first strong letter is Arabic (or Hebrew). */
export function isRtl(text: string): boolean {
  const m = STRONG.exec(text);
  return !!m && RTL_CHAR.test(m[0]);
}
