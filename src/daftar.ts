// Daftar annossous (دفتر النصوص / cahier de textes): a teacher's lesson log.
// One notebook with a section of pages per class. When a weekly timetable is
// given, each row of a class's pages is pre-dated with that class's next
// session; pages are added as the year goes on (see extendSection).

import { api, type Notebook, type Page } from "./api";
import { measureText } from "./editor/render";
import { daftarLayout } from "./editor/papers";
import { newId, toStored, type TextEl } from "./editor/model";

/** One weekly lesson: day (1 = Monday … 6 = Saturday, 0 = Sunday), times "HH:MM", class. */
export interface Lesson {
  day: number;
  start: string;
  end: string;
  cls: string;
}

export interface DaftarMeta {
  kind: "daftar";
  year: string;
  school: string;
  teacher: string;
  subject: string;
  classes: string[];
  timetable: Lesson[];
  /** First day of lessons (YYYY-MM-DD): dating starts here. */
  start: string;
  /** Per class: the last session already written on its pages (YYYY-MM-DD HH:MM). */
  dated: Record<string, string>;
}

export const DAYS: { ar: string; fr: string }[] = [
  { ar: "الأحد", fr: "Dimanche" },
  { ar: "الإثنين", fr: "Lundi" },
  { ar: "الثلاثاء", fr: "Mardi" },
  { ar: "الأربعاء", fr: "Mercredi" },
  { ar: "الخميس", fr: "Jeudi" },
  { ar: "الجمعة", fr: "Vendredi" },
  { ar: "السبت", fr: "Samedi" },
];

export function parseDaftar(nb: Pick<Notebook, "meta"> | null | undefined): DaftarMeta | null {
  if (!nb?.meta) return null;
  try {
    const m = JSON.parse(nb.meta) as Partial<DaftarMeta>;
    if (m.kind !== "daftar" || !Array.isArray(m.classes)) return null;
    return { year: "", school: "", teacher: "", subject: "", timetable: [], start: isoDate(new Date()), dated: {}, ...m } as DaftarMeta;
  } catch {
    return null;
  }
}

/** The school year a date falls in, e.g. "2026/2027" (years start in September). */
export function schoolYear(d = new Date()): string {
  const y = d.getMonth() >= 7 ? d.getFullYear() : d.getFullYear() - 1;
  return `${y}/${y + 1}`;
}

const pad = (n: number) => String(n).padStart(2, "0");
export const isoDate = (d: Date) => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
const parseIso = (s: string) => {
  const [y, m, d] = s.split("-").map(Number);
  return new Date(y, (m || 1) - 1, d || 1);
};

export interface Session {
  /** "YYYY-MM-DD HH:MM", sortable. */
  key: string;
  date: string;
  day: number;
  start: string;
  end: string;
}

/** The class's lessons in weekly order. */
function weekly(meta: DaftarMeta, cls: string): Lesson[] {
  return meta.timetable.filter((l) => l.cls === cls).sort((a, b) => a.day - b.day || a.start.localeCompare(b.start));
}

/** The class's next `count` sessions after `afterKey` (exclusive; null: from the start date). */
export function nextSessions(meta: DaftarMeta, cls: string, afterKey: string | null, count: number): Session[] {
  const lessons = weekly(meta, cls);
  const out: Session[] = [];
  if (!lessons.length || count <= 0) return out;
  const d = parseIso(afterKey ? afterKey.slice(0, 10) : meta.start);
  // A year of days at most (guards against a timetable with no valid day).
  for (let i = 0; i < 400; i++, d.setDate(d.getDate() + 1)) {
    const date = isoDate(d);
    for (const l of lessons) {
      if (l.day !== d.getDay()) continue;
      const key = `${date} ${l.start}`;
      if (afterKey && key <= afterKey) continue;
      out.push({ key, date, day: l.day, start: l.start, end: l.end });
      if (out.length >= count) return out;
    }
  }
  return out;
}

const INK = "#1e3a8a";
const frDate = (iso: string) => `${iso.slice(8, 10)}/${iso.slice(5, 7)}/${iso.slice(0, 4)}`;

/** Text boxes for a page: the header fields, and a session in the date cell of each row. */
export function pageElements(meta: DaftarMeta, cls: string, sessions: Session[], w: number, h: number): TextEl[] {
  const L = daftarLayout(w, h);
  const out: TextEl[] = [];
  let z = 0;
  const text = (t: Partial<TextEl> & Pick<TextEl, "x" | "y" | "w" | "text" | "fontSize">): TextEl => ({ id: newId(), z: z++, kind: "text", color: INK, align: "center", ...t });
  const values: Record<string, string> = { class: cls, subject: meta.subject, teacher: meta.teacher };
  for (const f of L.fields) {
    const value = values[f.key];
    if (!value) continue;
    const labelW = measureText(`${f.ar} / ${f.fr}`, "sans", 12) + 14;
    const x0 = f.x0 + 4;
    const x1 = Math.max(x0 + 40, f.x1 - labelW);
    // Shrink a long value to fit its dotted line (down to 9 px).
    const natural = measureText(value, "sans", 13) * 1.08;
    const size = Math.max(9, Math.min(13, (13 * (x1 - x0 - 6)) / Math.max(1, natural)));
    out.push(text({ x: x0, y: L.fieldsY - 15 + (13 - size) * 0.8, w: x1 - x0, text: value, fontSize: Math.round(size * 2) / 2, bold: true, role: "daftar-field" }));
  }
  const date = L.cols[0];
  sessions.forEach((s, i) => {
    const y0 = L.rows[i];
    const y1 = L.rows[i + 1];
    if (y1 === undefined) return;
    const half = (y1 - y0) / 2;
    const day = DAYS[s.day];
    out.push(text({ x: date.x0 + 4, y: y0 + half / 2 - 14, w: date.x1 - date.x0 - 8, text: `${day.ar} / ${day.fr}\n${frDate(s.date)}`, fontSize: 11, role: "daftar-session", session: s.key }));
    out.push(text({ x: date.x0 + 4, y: y0 + half + half / 2 - 8, w: date.x1 - date.x0 - 8, text: `${s.start} – ${s.end}`, fontSize: 11, role: "daftar-session", session: s.key }));
  });
  return out;
}

/** Rows per daftar page at a page size. */
export const rowsPerPage = (w: number, h: number) => daftarLayout(w, h).rows.length - 1;

async function fillPage(page: Page, meta: DaftarMeta, cls: string, sessions: Session[]) {
  await api.setPageSection(page.id, cls);
  await api.saveElements(page.id, pageElements(meta, cls, sessions, page.width, page.height).map(toStored));
}

/**
 * Adds pages at the end of a class's section until its dated rows reach
 * `through` (YYYY-MM-DD), or adds one page when `oneMore` (continuing the
 * dates). Saves the updated settings. Returns the first new page's index.
 */
export async function extendSection(nb: Notebook, meta: DaftarMeta, cls: string, opts: { through?: string; oneMore?: boolean }): Promise<number | null> {
  const pages = await api.pages(nb.id);
  const inSection = pages.filter((p) => p.section === cls);
  const last = inSection[inSection.length - 1];
  let at = last ? last.position + 1 : pages.length;
  const tmpl = last ?? pages.find((p) => p.template === "daftar");
  let first: number | null = null;
  const per = rowsPerPage(tmpl?.width ?? 794, tmpl?.height ?? 1123);
  for (let guard = 0; guard < 60; guard++) {
    const after = meta.dated[cls] ?? null;
    const hasLessons = meta.timetable.some((l) => l.cls === cls);
    if (!opts.oneMore) {
      if (!hasLessons || !opts.through || (after && after.slice(0, 10) >= opts.through)) break;
    }
    const sessions = hasLessons ? nextSessions(meta, cls, after, per) : [];
    const page = await api.addPage(nb.id, at, "daftar");
    await fillPage(page, meta, cls, sessions);
    if (sessions.length) meta.dated[cls] = sessions[sessions.length - 1].key;
    first ??= at;
    at++;
    if (opts.oneMore) break;
  }
  await api.setNotebookMeta(nb.id, JSON.stringify(meta));
  return first;
}

/**
 * Creates a daftar: cover, then a section per class. Classes with lessons in
 * the timetable get pages dated through a week from today (or the start
 * date); the others get one page to fill in by hand.
 */
export async function createDaftar(input: { title: string; folderId: string | null; coverColor: string; meta: DaftarMeta }): Promise<Notebook> {
  const meta = input.meta;
  const nb = await api.createNotebook({ title: input.title, folderId: input.folderId, coverColor: input.coverColor, template: "daftar", coverPage: true });
  const pages = await api.pages(nb.id);
  // The notebook's first paper page becomes the first class's first page.
  const firstPaper = pages.find((p) => p.template === "daftar")!;
  const per = rowsPerPage(firstPaper.width, firstPaper.height);
  const weekAhead = new Date();
  weekAhead.setDate(weekAhead.getDate() + 7);
  const through = [isoDate(weekAhead), (() => { const d = parseIso(meta.start); d.setDate(d.getDate() + 7); return isoDate(d); })()].sort()[1];
  for (const [i, cls] of meta.classes.entries()) {
    if (i === 0) {
      const sessions = nextSessions(meta, cls, null, per);
      await fillPage(firstPaper, meta, cls, sessions);
      if (sessions.length) meta.dated[cls] = sessions[sessions.length - 1].key;
    } else {
      await extendSection(nb, meta, cls, { oneMore: true });
    }
    await extendSection(nb, meta, cls, { through });
  }
  await api.setNotebookMeta(nb.id, JSON.stringify(meta));
  return api.notebook(nb.id);
}
