// Daftar annossous (دفتر النصوص / cahier de textes): a teacher's lesson log,
// laid out like the printed books (see editor/daftarPapers.ts). One notebook:
// the front pages (title, teacher's card, timetable, holidays, diagnostic
// report), then for each class its student list and lesson pages (a colour
// per class), then memos and absences. "New lesson" writes today's date, the
// class and the lesson's time (from the timetable) on the next free line.

import { api, type Notebook, type Page, type Template } from "./api";
import { fromStored, newId, toStored, type El, type StickyEl, type TextEl } from "./editor/model";
import { zelligePanel } from "./editor/covers";
import { daftarSlots, isLessonTemplate, lessonLayout, lessonTemplateFor, timetableLayout, type Slot } from "./editor/daftarPapers";

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
  /** الجهة / academy. */
  region: string;
  directorate: string;
  school: string;
  teacher: string;
  /** رقم التأجير. */
  payroll: string;
  subject: string;
  classes: string[];
  timetable: Lesson[];
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
    return { year: "", region: "", directorate: "", school: "", teacher: "", payroll: "", subject: "", timetable: [], ...m } as DaftarMeta;
  } catch {
    return null;
  }
}

/** The school year a date falls in, e.g. "2026-2027" (years start in September). */
export function schoolYear(d = new Date()): string {
  const y = d.getMonth() >= 7 ? d.getFullYear() : d.getFullYear() - 1;
  return `${y}-${y + 1}`;
}

const pad = (n: number) => String(n).padStart(2, "0");
export const isoDate = (d: Date) => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
const frDate = (d: Date) => `${pad(d.getDate())}/${pad(d.getMonth() + 1)}/${d.getFullYear()}`;

// --- filling pages in ---------------------------------------------------------------

const VALUE = "#1e3a8a";

function valueBox(slot: Slot, value: string, size = 13, z = 0): TextEl {
  return { id: newId(), z, kind: "text", x: slot.x, y: slot.y, w: slot.w, text: value, fontSize: size, color: VALUE, bold: true, align: "center", font: "sans", role: "daftar-field" };
}

/** The values a page shows, by slot name. */
function values(meta: DaftarMeta, cls: string | null): Record<string, string> {
  return {
    region: meta.region,
    directorate: meta.directorate,
    school: meta.school,
    teacher: meta.teacher,
    payroll: meta.payroll,
    year: meta.year,
    subject: meta.subject,
    classes: meta.classes.join(" - "),
    class: cls ?? "",
    group0: cls ?? "",
    group1: cls ?? "",
  };
}

/** Text boxes for a page's filled-in values (school, class, year…). */
export function pageElements(template: Template, meta: DaftarMeta, cls: string | null, w: number, h: number): El[] {
  const slots = daftarSlots(template, w, h);
  const v = values(meta, cls);
  const out: El[] = [];
  for (const [key, slot] of Object.entries(slots)) {
    if (!v[key]) continue;
    // Long values shrink to fit their line.
    const size = Math.max(8.5, Math.min(13, (slot.w / Math.max(1, v[key].length)) * 1.9));
    out.push(valueBox(slot, v[key], Math.round(size * 2) / 2, out.length));
  }
  if (template === "daftar-timetable") out.push(...timetableElements(meta, w, h));
  return out;
}

/** Pastel note colours for the timetable, one per class. */
const CLASS_COLORS = ["#ead7f0", "#bfdbfe", "#bbf7d0", "#fbcfe8", "#c7d2fe", "#fed7aa", "#a5f3fc", "#e5e7eb"];

/** A sticky note per lesson on the timetable page, coloured by class. */
export function timetableElements(meta: DaftarMeta, w: number, h: number): StickyEl[] {
  const L = timetableLayout(w, h);
  return meta.timetable
    .map((l, i): StickyEl | null => {
      const col = L.days.find((d) => d.day === l.day);
      if (!col) return null;
      const y0 = L.yOf(l.start);
      const y1 = Math.max(y0 + 30, L.yOf(l.end));
      const color = CLASS_COLORS[Math.max(0, meta.classes.indexOf(l.cls)) % CLASS_COLORS.length];
      return { id: newId(), z: 50 + i, kind: "sticky", x: col.x0 + 4, y: y0 + 3, w: col.x1 - col.x0 - 8, h: y1 - y0 - 6, text: `${l.cls}\n${l.start}–${l.end}`, color, fontSize: 12, bold: true, align: "center" };
    })
    .filter((x): x is StickyEl => x !== null);
}

/**
 * The cover's details under its title: "Cahier de textes", then school,
 * subject, teacher and classes — Arabic label right, French left, value in
 * the middle.
 */
export function coverElements(meta: DaftarMeta, w: number, h: number): TextEl[] {
  const p = zelligePanel(w, h);
  const out: TextEl[] = [];
  let z = 10;
  const base = { kind: "text" as const, role: "daftar-field" as const, font: "sans" as const };
  out.push({ ...base, id: newId(), z: z++, x: p.x + 30, y: p.y + 186, w: p.w - 60, text: "Cahier de textes", fontSize: 20, color: "#6b7280", align: "center" });
  const rows = [
    { ar: "المؤسسة", fr: "Établissement", value: meta.school },
    { ar: "المادة", fr: "Matière", value: meta.subject },
    { ar: "الأستاذ(ة)", fr: "Enseignant(e)", value: meta.teacher },
    { ar: "الأقسام", fr: "Classes", value: meta.classes.join("  ·  ") },
  ].filter((r) => r.value);
  const x = p.x + 44;
  const rw = p.w - 88;
  const top = p.y + Math.max(250, p.h * 0.36);
  const step = Math.min(78, (p.y + p.h - 100 - top) / Math.max(1, rows.length));
  rows.forEach((r, i) => {
    const y = top + i * step;
    out.push({ ...base, id: newId(), z: z++, x, y, w: rw, text: r.ar, fontSize: 13, color: "#6b7280" });
    out.push({ ...base, id: newId(), z: z++, x, y, w: rw, text: r.fr, fontSize: 12, color: "#6b7280", align: "left" });
    out.push({ ...base, id: newId(), z: z++, x, y: y + 22, w: rw, text: r.value, fontSize: 19, color: "#1f2937", bold: true, align: "center" });
  });
  return out;
}

// --- lessons ----------------------------------------------------------------------------

/**
 * The class's lesson today closest to `now` (or null: no lesson today, or no
 * timetable), skipping lessons already logged (`logged` holds "HH:MM" starts).
 */
export function todaysLesson(meta: DaftarMeta, cls: string, now = new Date(), logged: Set<string> = new Set()): Lesson | null {
  const all = meta.timetable.filter((l) => l.cls === cls && l.day === now.getDay());
  const fresh = all.filter((l) => !logged.has(l.start));
  const today = fresh.length ? fresh : all;
  if (!today.length) return null;
  const mins = (s: string) => {
    const [hh, mm] = s.split(":").map(Number);
    return hh * 60 + (mm || 0);
  };
  const t = now.getHours() * 60 + now.getMinutes();
  // The lesson under way, else the nearest one.
  return [...today].sort((a, b) => {
    const da = t >= mins(a.start) && t <= mins(a.end) ? -1 : Math.min(Math.abs(t - mins(a.start)), Math.abs(t - mins(a.end)));
    const db = t >= mins(b.start) && t <= mins(b.end) ? -1 : Math.min(Math.abs(t - mins(b.start)), Math.abs(t - mins(b.end)));
    return da - db;
  })[0];
}

/**
 * The first free grid row (0-based) on a lesson page below what's written
 * there, leaving one empty row after earlier entries; null when fewer than
 * `need` rows remain. `ink` holds the bounds of everything on the page.
 */
export function nextFreeRow(ink: { y0: number; y1: number; x0: number; x1: number }[], w: number, h: number, need = 3): number | null {
  const L = lessonLayout(w, h);
  let bottom = -Infinity;
  for (const r of ink) {
    if (r.x1 < L.x0 || r.x0 > L.x1 || r.y1 < L.bodyTop + 1 || r.y0 > L.bodyBottom) continue;
    bottom = Math.max(bottom, r.y1);
  }
  const row = bottom === -Infinity ? 0 : Math.ceil((bottom - L.bodyTop) / L.step - 0.15) + 1;
  return row + need <= L.rows ? row : null;
}

/** The text boxes of a new lesson entry on row `row`: date, class and time. */
export function lessonEntry(meta: DaftarMeta, cls: string, row: number, w: number, h: number, now = new Date(), logged: Set<string> = new Set()): TextEl[] {
  const L = lessonLayout(w, h);
  const lesson = todaysLesson(meta, cls, now, logged);
  const y = L.bodyTop + row * L.step + 2;
  const col = (k: string) => L.cols.find((c) => c.key === k)!;
  const cell = (k: string, s: string): TextEl => {
    const c = col(k);
    return { id: newId(), z: 0, kind: "text", x: c.x0 + 2, y, w: c.x1 - c.x0 - 4, text: s, fontSize: 11.5, color: VALUE, bold: true, align: "center", font: "sans", role: "daftar-session", session: `${isoDate(now)} ${lesson?.start ?? ""}`.trim() };
  };
  const out = [cell("date", frDate(now)), cell("class", cls)];
  if (lesson) out.push(cell("time", `${lesson.start}-${lesson.end}`));
  return out;
}

// --- creating ---------------------------------------------------------------------------

async function addFilledPage(nb: Notebook, at: number, template: Template, meta: DaftarMeta, cls: string | null): Promise<Page> {
  const page = await api.addPage(nb.id, at, template);
  // New pages copy their neighbour's paper and section: set both.
  await api.setPageTemplate(page.id, template, "#ffffff");
  await api.setPageSection(page.id, cls);
  await api.saveElements(page.id, pageElements(template, meta, cls, page.width, page.height).map(toStored));
  return page;
}

/** Adds a lesson page at the end of a class's section; returns it. */
export async function addLessonPage(nb: Notebook, meta: DaftarMeta, cls: string): Promise<Page> {
  const pages = await api.pages(nb.id);
  const inClass = pages.filter((p) => p.section === cls);
  const at = inClass.length ? inClass[inClass.length - 1].position + 1 : pages.length;
  return addFilledPage(nb, at, lessonTemplateFor(Math.max(0, meta.classes.indexOf(cls))), meta, cls);
}

/** Lesson pages each class starts with. */
const LESSON_PAGES = 3;

/**
 * Creates a daftar laid out like the printed books, with the details filled
 * in wherever the pages ask for them.
 */
export async function createDaftar(input: { title: string; folderId: string | null; coverColor: string; meta: DaftarMeta }): Promise<Notebook> {
  const meta = input.meta;
  const nb = await api.createNotebook({ title: input.title, folderId: input.folderId, coverColor: input.coverColor, template: "daftar-title", paperColor: "#ffffff", coverPage: true });
  const [cover, title] = await api.pages(nb.id);
  await api.setPageTemplate(cover.id, "cover-zellige", input.coverColor);
  await api.saveElements(title.id, pageElements("daftar-title", meta, null, title.width, title.height).map(toStored));
  let at = 2;
  const add = async (t: Template, cls: string | null) => {
    await addFilledPage(nb, at, t, meta, cls);
    at++;
  };
  for (const t of ["daftar-profile", "daftar-profile-2", "daftar-timetable", "daftar-holidays", "daftar-diagnostic"] as Template[]) await add(t, null);
  for (const [i, cls] of meta.classes.entries()) {
    await add("daftar-students", cls);
    for (let k = 0; k < LESSON_PAGES; k++) await add(lessonTemplateFor(i), cls);
  }
  await add("daftar-memos", null);
  await add("daftar-absence", null);
  await api.setNotebookMeta(nb.id, JSON.stringify(meta));
  return api.notebook(nb.id);
}

/** Fills in the cover once its title and date are placed (after setUpCover). */
export async function finishDaftarCover(nb: Notebook, meta: DaftarMeta) {
  const [cover] = await api.pages(nb.id);
  if (!cover) return;
  const els: El[] = fromStored(await api.elements(cover.id)).map((e) => (e.kind === "text" && e.role === "cover-date" && meta.year ? { ...e, text: meta.year } : e));
  await api.saveElements(cover.id, [...els, ...coverElements(meta, cover.width, cover.height)].map(toStored));
}

export { isLessonTemplate };
