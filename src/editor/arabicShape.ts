// Arabic for the vector PDF: letters joined into their contextual forms
// (Unicode presentation forms, which the bundled Noto Sans Arabic has) and a
// line split into left-to-right and right-to-left runs in display order.
// Enough for Arabic and French in a daftar; not a full Unicode bidi.

/** Arabic letters in code-point order with how many forms each has (from U+FE80). */
const LETTERS: [number, 1 | 2 | 4][] = [
  [0x0621, 1], [0x0622, 2], [0x0623, 2], [0x0624, 2], [0x0625, 2], [0x0626, 4], [0x0627, 2], [0x0628, 4],
  [0x0629, 2], [0x062a, 4], [0x062b, 4], [0x062c, 4], [0x062d, 4], [0x062e, 4], [0x062f, 2], [0x0630, 2],
  [0x0631, 2], [0x0632, 2], [0x0633, 4], [0x0634, 4], [0x0635, 4], [0x0636, 4], [0x0637, 4], [0x0638, 4],
  [0x0639, 4], [0x063a, 4], [0x0641, 4], [0x0642, 4], [0x0643, 4], [0x0644, 4], [0x0645, 4], [0x0646, 4],
  [0x0647, 4], [0x0648, 2], [0x0649, 2], [0x064a, 4],
];

/** Letter → first presentation form and number of forms. */
const FORMS = new Map<number, { base: number; n: 1 | 2 | 4 }>();
{
  let next = 0xfe80;
  for (const [cp, n] of LETTERS) {
    FORMS.set(cp, { base: next, n });
    next += n;
  }
}

/** Lam + alef ligatures: alef variant → isolated form (final is the next code). */
const LAM_ALEF = new Map<number, number>([
  [0x0622, 0xfef5],
  [0x0623, 0xfef7],
  [0x0625, 0xfef9],
  [0x0627, 0xfefb],
]);

const TATWEEL = 0x0640;

/** Harakat and other marks that sit on a letter without affecting joining. */
export function isMark(cp: number): boolean {
  return (cp >= 0x064b && cp <= 0x065f) || cp === 0x0670 || (cp >= 0x06d6 && cp <= 0x06ed);
}

type Join = "U" | "R" | "D" | "C" | "T";
function joining(cp: number | undefined): Join {
  if (cp === undefined) return "U";
  if (isMark(cp)) return "T";
  if (cp === TATWEEL) return "C";
  const f = FORMS.get(cp);
  return !f ? "U" : f.n === 4 ? "D" : f.n === 2 ? "R" : "U";
}

/**
 * Replaces Arabic letters (logical order) by their joined forms: isolated,
 * final, initial or medial, with lam-alef as one ligature.
 */
export function shapeArabic(text: string): string {
  const cps = [...text].map((c) => c.codePointAt(0)!);
  const neighbour = (i: number, step: 1 | -1) => {
    for (let j = i + step; j >= 0 && j < cps.length; j += step) if (!isMark(cps[j])) return cps[j];
    return undefined;
  };
  const out: number[] = [];
  for (let i = 0; i < cps.length; i++) {
    const cp = cps[i];
    const f = FORMS.get(cp);
    if (!f) {
      out.push(cp);
      continue;
    }
    const prev = joining(neighbour(i, -1));
    const joinsBefore = prev === "D" || prev === "C";
    if (cp === 0x0644) {
      // Lam followed by an alef: one ligature (marks between them are kept after it).
      let j = i + 1;
      while (j < cps.length && isMark(cps[j])) j++;
      const lig = LAM_ALEF.get(cps[j]);
      if (lig) {
        out.push(lig + (joinsBefore ? 1 : 0));
        for (let k = i + 1; k < j; k++) out.push(cps[k]);
        i = j;
        continue;
      }
    }
    const nextJ = joining(neighbour(i, 1));
    const joinsAfter = f.n === 4 && (nextJ === "D" || nextJ === "R" || nextJ === "C");
    const before = f.n > 1 && joinsBefore;
    // Forms in order: isolated, final, initial, medial.
    const form = before && joinsAfter ? 3 : joinsAfter ? 2 : before ? 1 : 0;
    out.push(f.base + form);
  }
  return String.fromCodePoint(...out);
}

type Dir = "L" | "R" | "N";

function classOf(ch: string): Dir {
  const cp = ch.codePointAt(0)!;
  if ((cp >= 0x0660 && cp <= 0x0669) || (cp >= 0x06f0 && cp <= 0x06f9)) return "L"; // digits read left to right
  if ((cp >= 0x0590 && cp <= 0x08ff) || (cp >= 0xfb1d && cp <= 0xfdff) || (cp >= 0xfe70 && cp <= 0xfefc)) return "R";
  if (/[\p{L}\p{N}]/u.test(ch)) return "L";
  return "N";
}

const MIRROR: Record<string, string> = { "(": ")", ")": "(", "[": "]", "]": "[", "{": "}", "}": "{", "<": ">", ">": "<", "«": "»", "»": "«" };

/**
 * A line's runs in display order (left to right on the page). Spaces and
 * punctuation take the direction of the letters around them, else the
 * paragraph's; brackets in right-to-left runs are mirrored.
 */
export function bidiRuns(text: string, rtl: boolean): { text: string; rtl: boolean }[] {
  const chars = [...text];
  const cls = chars.map(classOf);
  const para: Dir = rtl ? "R" : "L";
  const strongAt = (i: number, step: 1 | -1): Dir | null => {
    for (let j = i + step; j >= 0 && j < cls.length; j += step) if (cls[j] !== "N") return cls[j];
    return null;
  };
  const resolved = cls.map((c, i) => {
    if (c !== "N") return c;
    if (isMark(chars[i].codePointAt(0)!)) return strongAt(i, -1) ?? para;
    const a = strongAt(i, -1);
    const b = strongAt(i, 1);
    return a && a === b ? a : para;
  });
  const runs: { text: string; rtl: boolean }[] = [];
  chars.forEach((ch, i) => {
    const r = resolved[i] === "R";
    const c = r ? (MIRROR[ch] ?? ch) : ch;
    const last = runs[runs.length - 1];
    if (last && last.rtl === r) last.text += c;
    else runs.push({ text: c, rtl: r });
  });
  return rtl ? runs.reverse() : runs;
}

/** True when the text has characters that need shaping or right-to-left layout. */
export function needsBidi(text: string): boolean {
  return /[֐-ࣿיִ-﷿ﹰ-ﻼ]/.test(text);
}
