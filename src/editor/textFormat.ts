// List formatting for text boxes. Lists are stored as plain-text markers
// ("• " and "1. ") so they show while typing and in search results; the
// renderer adds a hanging indent for wrapped lines.

export type ListKind = "bullet" | "number";

/** Matches a list marker at the start of a paragraph. */
export const LIST_MARKER = /^(•|◦|-|\*|\d{1,3}[.)])\s+/;
const BULLET = /^•\s+/;
const NUMBER = /^\d{1,3}[.)]\s+/;

export function listKindOf(paragraph: string): ListKind | null {
  if (BULLET.test(paragraph)) return "bullet";
  if (NUMBER.test(paragraph)) return "number";
  return null;
}

/** The list kind shared by every non-empty paragraph, if any. */
export function currentList(text: string): ListKind | null {
  const paras = text.split("\n").filter((p) => p.trim());
  if (!paras.length) return null;
  const first = listKindOf(paras[0]);
  return first && paras.every((p) => listKindOf(p) === first) ? first : null;
}

function strip(paragraph: string): string {
  return paragraph.replace(LIST_MARKER, "");
}

/**
 * Turns a list on for every non-empty paragraph, renumbering numbered lists,
 * or off when the whole text already is that kind of list.
 */
export function toggleList(text: string, kind: ListKind): string {
  const paras = text.split("\n");
  // Starting a list in an empty box: put the first marker in place to type after.
  if (!text.trim()) return kind === "bullet" ? "• " : "1. ";
  if (currentList(text) === kind) return paras.map(strip).join("\n");
  let n = 0;
  return paras
    .map((p) => {
      if (!p.trim()) return p;
      const body = strip(p);
      return kind === "bullet" ? `• ${body}` : `${++n}. ${body}`;
    })
    .join("\n");
}

/**
 * Enter inside a list: starts the next item ("• " or the next number). On an
 * empty item it ends the list instead. Returns null when not in a list, so
 * the textarea's normal Enter applies.
 */
export function continueList(value: string, caret: number): { value: string; caret: number } | null {
  const lineStart = value.lastIndexOf("\n", caret - 1) + 1;
  const lineEndIdx = value.indexOf("\n", caret);
  const lineEnd = lineEndIdx === -1 ? value.length : lineEndIdx;
  const line = value.slice(lineStart, lineEnd);
  const m = LIST_MARKER.exec(line);
  if (!m) return null;
  if (!line.slice(m[0].length).trim()) {
    // Empty item: remove the marker and leave the list.
    const next = value.slice(0, lineStart) + value.slice(lineStart + m[0].length);
    return { value: next, caret: lineStart };
  }
  const num = /^(\d{1,3})([.)])/.exec(m[0]);
  const marker = num ? `${Number(num[1]) + 1}${num[2]} ` : `${m[1]} `;
  const insert = `\n${marker}`;
  return { value: value.slice(0, caret) + insert + value.slice(caret), caret: caret + insert.length };
}
