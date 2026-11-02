// Glyph outlines for vector PDF export. The bundled fonts' WOFF files
// (Latin and Latin Extended subsets) are parsed with opentype.js, so text in
// an exported PDF looks exactly like the text on the page.

import { parse, type Font } from "opentype.js";
import { FONT_PRESETS, type FontId } from "../fonts";

/** @fontsource package per preset. */
const PACKAGES: Record<FontId, string> = {
  sans: "inter",
  caveat: "caveat",
  "patrick-hand": "patrick-hand",
  kalam: "kalam",
  "indie-flower": "indie-flower",
  shadows: "shadows-into-light",
  dancing: "dancing-script",
  architect: "architects-daughter",
};

/** Presets with real bold/italic faces bundled (others are synthesized, as on screen). */
const REAL_STYLES = new Set<FontId>(["sans"]);

const FILES = import.meta.glob(["../../node_modules/@fontsource/*/files/*-latin-400-*.woff", "../../node_modules/@fontsource/*/files/*-latin-ext-400-*.woff", "../../node_modules/@fontsource/inter/files/inter-latin-700-*.woff", "../../node_modules/@fontsource/inter/files/inter-latin-ext-700-*.woff"], {
  query: "?url",
  import: "default",
  eager: true,
}) as Record<string, string>;

function fileUrl(pkg: string, subset: "latin" | "latin-ext", weight: 400 | 700, italic: boolean): string | null {
  const name = `/${pkg}/files/${pkg}-${subset}-${weight}-${italic ? "italic" : "normal"}.woff`;
  for (const [path, url] of Object.entries(FILES)) if (path.endsWith(name)) return url;
  return null;
}

export interface FaceSpec {
  font: FontId;
  bold: boolean;
  italic: boolean;
}

/** Fonts for one face, in fallback order, plus what must be synthesized. */
export interface LoadedFace {
  fonts: Font[];
  fakeBold: boolean;
  fakeItalic: boolean;
}

const fontCache = new Map<string, Promise<Font | null>>();
function loadFont(url: string): Promise<Font | null> {
  let f = fontCache.get(url);
  if (!f) {
    f = fetch(url)
      .then((r) => (r.ok ? r.arrayBuffer() : Promise.reject(new Error(`font ${url}: ${r.status}`))))
      .then((buf) => parse(buf))
      .catch(() => null);
    fontCache.set(url, f);
  }
  return f;
}

const faceKey = (s: FaceSpec) => `${s.font}/${s.bold ? "b" : ""}${s.italic ? "i" : ""}`;
const faces = new Map<string, LoadedFace>();

/** Loads the glyph data for the given faces (call before painting). */
export async function loadFaces(specs: Iterable<FaceSpec>): Promise<void> {
  const jobs: Promise<void>[] = [];
  for (const s of specs) {
    const key = faceKey(s);
    if (faces.has(key)) continue;
    const real = REAL_STYLES.has(s.font);
    const weight = real && s.bold ? 700 : 400;
    const italic = real && s.italic;
    const pkg = PACKAGES[s.font] ?? PACKAGES.sans;
    jobs.push(
      (async () => {
        const urls = (["latin", "latin-ext"] as const).map((sub) => fileUrl(pkg, sub, weight, italic)).filter((u): u is string => !!u);
        const fonts = (await Promise.all(urls.map(loadFont))).filter((f): f is Font => !!f);
        faces.set(key, { fonts, fakeBold: s.bold && !real, fakeItalic: s.italic && !real });
      })(),
    );
  }
  await Promise.all(jobs);
}

/** A loaded face, or null if it wasn't loaded (or has no font files). */
export function faceFor(spec: FaceSpec): LoadedFace | null {
  const f = faces.get(faceKey(spec));
  return f && f.fonts.length ? f : null;
}

/** Reads the preset, size and style back out of a canvas font string (see fontCss). */
export function parseFontCss(css: string): (FaceSpec & { size: number }) | null {
  const size = /(\d+(?:\.\d+)?)px/.exec(css);
  if (!size) return null;
  const family = css.slice(size.index + size[0].length).trim();
  const preset = FONT_PRESETS.find((p) => p.family === family);
  if (!preset) return null;
  const head = css.slice(0, size.index);
  return { font: preset.id, size: Number(size[1]), bold: /\bbold\b/.test(head), italic: /\bitalic\b/.test(head) };
}
