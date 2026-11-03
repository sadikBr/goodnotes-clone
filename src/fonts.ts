// Handwriting font presets for text boxes and sticky notes. All fonts are
// bundled (SIL Open Font License, via @fontsource) so the app works offline.

import "@fontsource/inter/400.css";
import "@fontsource/inter/400-italic.css";
import "@fontsource/inter/700.css";
import "@fontsource/inter/700-italic.css";
// Handwriting faces (Latin and Latin Extended subsets: French, Spanish… accents).
import "@fontsource/playpen-sans/latin-400.css";
import "@fontsource/playpen-sans/latin-ext-400.css";
import "@fontsource/playpen-sans/latin-700.css";
import "@fontsource/playpen-sans/latin-ext-700.css";
import "@fontsource/mynerve/latin-400.css";
import "@fontsource/mynerve/latin-ext-400.css";
import "@fontsource/andika/latin-400.css";
import "@fontsource/andika/latin-ext-400.css";
import "@fontsource/andika/latin-700.css";
import "@fontsource/andika/latin-ext-700.css";
import "@fontsource/playwrite-fr-moderne/latin-400.css";
import "@fontsource/shantell-sans/latin-400.css";
import "@fontsource/shantell-sans/latin-ext-400.css";
import "@fontsource/shantell-sans/latin-700.css";
import "@fontsource/shantell-sans/latin-ext-700.css";
import "@fontsource/handlee/latin-400.css";
import "@fontsource/playwrite-fr-trad/latin-400.css";
import "@fontsource/schoolbell/latin-400.css";
import "@fontsource/gochi-hand/latin-400.css";
// A typed serif, with real bold and italic.
import "@fontsource/lora/latin-400.css";
import "@fontsource/lora/latin-ext-400.css";
import "@fontsource/lora/latin-700.css";
import "@fontsource/lora/latin-ext-700.css";
import "@fontsource/lora/latin-400-italic.css";
import "@fontsource/lora/latin-ext-400-italic.css";
import "@fontsource/lora/latin-700-italic.css";
import "@fontsource/lora/latin-ext-700-italic.css";
// Arabic: every preset falls back to Noto Sans Arabic for Arabic letters
// (only its Arabic subset, so Latin text keeps each preset's own look).
import "@fontsource/noto-sans-arabic/arabic-400.css";
import "@fontsource/noto-sans-arabic/arabic-700.css";

export type FontId = "sans" | "book" | "playpen" | "mynerve" | "school" | "cursive-fr" | "cursive-trad" | "marker" | "chalk" | "quick" | "neat";

export interface FontPreset {
  id: FontId;
  label: string;
  /** CSS font-family list, usable in both CSS and canvas `ctx.font`. */
  family: string;
  /** Primary face to wait for before drawing. */
  face: string | null;
  /** The face has a real bold (others are emboldened by the browser). */
  bold?: boolean;
  /** The face has a real italic (others are slanted by the browser). */
  italic?: boolean;
}

/** Arabic fallback face, listed after each preset's own face. */
export const ARABIC_FACE = "Noto Sans Arabic";
const AR = `"${ARABIC_FACE}"`;
const SANS = `"Inter", ${AR}, "Segoe UI", system-ui, -apple-system, sans-serif`;
/**
 * After a handwriting face: Arabic, then Inter for any other character the
 * face lacks (², ½, ×…), so the screen and PDF export fill the gap alike.
 */
const HAND = `${AR}, "Inter", cursive`;
/** Every style, for faces with real bold and italic. */
const INTER_STYLES = ["", "bold ", "italic ", "italic bold "];

export const FONT_PRESETS: FontPreset[] = [
  { id: "sans", label: "Clean", family: SANS, face: "Inter", bold: true, italic: true },
  { id: "book", label: "Book", family: `"Lora", ${AR}, "Inter", serif`, face: "Lora", bold: true, italic: true },
  { id: "playpen", label: "Friendly", family: `"Playpen Sans", ${HAND}`, face: "Playpen Sans", bold: true },
  { id: "mynerve", label: "Natural", family: `"Mynerve", ${HAND}`, face: "Mynerve" },
  { id: "school", label: "School print", family: `"Andika", ${HAND}`, face: "Andika", bold: true },
  { id: "cursive-fr", label: "French cursive", family: `"Playwrite FR Moderne", ${HAND}`, face: "Playwrite FR Moderne" },
  { id: "cursive-trad", label: "Classic cursive", family: `"Playwrite FR Trad", ${HAND}`, face: "Playwrite FR Trad" },
  { id: "marker", label: "Marker", family: `"Shantell Sans", ${HAND}`, face: "Shantell Sans", bold: true },
  { id: "chalk", label: "Chalkboard", family: `"Schoolbell", ${HAND}`, face: "Schoolbell" },
  { id: "quick", label: "Quick notes", family: `"Gochi Hand", ${HAND}`, face: "Gochi Hand" },
  { id: "neat", label: "Neat", family: `"Handlee", ${HAND}`, face: "Handlee" },
];

/** Earlier presets (their fonts were replaced): each shows as the closest new one. */
const LEGACY: Record<string, FontId> = {
  caveat: "quick",
  "patrick-hand": "neat",
  kalam: "playpen",
  "indie-flower": "playpen",
  shadows: "neat",
  dancing: "cursive-trad",
  architect: "school",
  edu: "school",
};

export function fontPreset(id: string | undefined): FontPreset {
  const key = id && LEGACY[id] ? LEGACY[id] : id;
  return FONT_PRESETS.find((f) => f.id === key) ?? FONT_PRESETS[0];
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
    const preset = fontPreset(id);
    const face = preset.face;
    if (!face || loaded.has(face)) continue;
    const styles = preset.italic ? INTER_STYLES : preset.bold ? ["", "bold "] : [""];
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
