// Daftar annossous (دفتر النصوص / cahier de textes): a teacher's lesson log,
// laid out like the printed books (see editor/daftarPapers.ts). One notebook:
// the front pages (title, teacher's card, timetable, holidays, diagnostic
// report), then for each class its student list and lesson pages (a colour
// per class), then memos and absences. "New lesson" writes today's date, the
// class and the lesson's time (from the timetable) on the next free line.

import { api, type Notebook, type Page, type Template } from "./api";
import { fromStored, newId, toStored, type El, type StickyEl, type TextEl } from "./editor/model";
import { zelligePanel } from "./editor/covers";
import { daftarSlots, holidaysLayout, isLessonTemplate, lessonLayout, lessonTemplateFor, studentsLayout, timetableLayout, type Slot } from "./editor/daftarPapers";

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
  /** School holidays and vacations (dates YYYY-MM-DD, inclusive). */
  holidays: Holiday[];
}

export interface Holiday {
  name: string;
  from: string;
  to: string;
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
    return { year: "", region: "", directorate: "", school: "", teacher: "", payroll: "", subject: "", timetable: [], holidays: [], ...m } as DaftarMeta;
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
  if (template === "daftar-holidays") out.push(...holidayElements(meta, w, h, out.length));
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
      return { id: newId(), z: 50 + i, kind: "sticky", role: "daftar-field", x: col.x0 + 4, y: y0 + 3, w: col.x1 - col.x0 - 8, h: y1 - y0 - 6, text: `${l.cls}\n${l.start}–${l.end}`, color, fontSize: 12, bold: true, align: "center" };
    })
    .filter((x): x is StickyEl => x !== null);
}

/**
 * The cover's details under its title: school, subject, teacher and
 * classes — the Arabic label, then the value in the middle.
 */
export function coverElements(meta: DaftarMeta, w: number, h: number): TextEl[] {
  const p = zelligePanel(w, h);
  const out: TextEl[] = [];
  let z = 10;
  const base = { kind: "text" as const, role: "daftar-field" as const, font: "sans" as const };
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

// --- holidays ---------------------------------------------------------------------------

/** Days from `from` to `to`, both included. */
export function holidayDays(hd: Holiday): number {
  const d = (s: string) => Date.UTC(+s.slice(0, 4), +s.slice(5, 7) - 1, +s.slice(8, 10));
  return Math.max(1, Math.round((d(hd.to) - d(hd.from)) / 86400000) + 1);
}

const frIso = (s: string) => (s ? `${s.slice(8, 10)}/${s.slice(5, 7)}/${s.slice(0, 4)}` : "");

/** The holiday a day falls in, if any. */
export function holidayOn(meta: DaftarMeta, d = new Date()): Holiday | null {
  const day = isoDate(d);
  return meta.holidays.find((x) => x.from <= day && day <= (x.to || x.from)) ?? null;
}

/**
 * Morocco's fixed-date public holidays in a school year ("2026-2027").
 * Religious holidays follow the moon and the ministry sets the vacations,
 * so those are added by hand.
 */
export function nationalHolidays(year: string): Holiday[] {
  const m = /^(\d{4})/.exec(year);
  const y = m ? Number(m[1]) : new Date().getFullYear();
  const day = (yy: number, mm: number, dd: number): Holiday["from"] => `${yy}-${pad(mm)}-${pad(dd)}`;
  const one = (name: string, yy: number, mm: number, dd: number): Holiday => ({ name, from: day(yy, mm, dd), to: day(yy, mm, dd) });
  return [
    one("ذكرى المسيرة الخضراء", y, 11, 6),
    one("عيد الاستقلال", y, 11, 18),
    one("رأس السنة الميلادية", y + 1, 1, 1),
    one("ذكرى تقديم وثيقة الاستقلال", y + 1, 1, 11),
    one("رأس السنة الأمازيغية", y + 1, 1, 14),
    one("عيد الشغل", y + 1, 5, 1),
  ];
}

/** The holiday list, one row per holiday (in date order). */
export function holidayElements(meta: DaftarMeta, w: number, h: number, z0 = 0): TextEl[] {
  const T = holidaysLayout(w, h);
  const out: TextEl[] = [];
  const list = [...meta.holidays].sort((a, b) => a.from.localeCompare(b.from)).slice(0, T.rows);
  list.forEach((hd, r) => {
    const cells = [null, hd.name, frIso(hd.from), frIso(hd.to || hd.from), String(holidayDays(hd))];
    cells.forEach((v, c) => {
      if (!v) return;
      const col = T.cols[c];
      const size = c === 1 ? Math.max(8, Math.min(11.5, ((col.x1 - col.x0) / v.length) * 1.85)) : 11;
      out.push({ id: newId(), z: z0 + out.length, kind: "text", x: col.x0 + 3, y: T.top + r * T.rowH + (T.rowH - size * 1.3) / 2, w: col.x1 - col.x0 - 6, text: v, fontSize: Math.round(size * 2) / 2, color: VALUE, bold: c === 1, align: "center", font: "sans", role: "daftar-field" });
    });
  });
  return out;
}

// --- students ---------------------------------------------------------------------------

export interface Student {
  /** رقم مسار (e.g. D130045678), or "". */
  code: string;
  name: string;
}

const MASSAR = /^[A-Za-z]\d{6,12}$/;
const HEADER = /^(n°|no|nom|prénom|prenom|name|code|massar|élève|eleve|sexe|genre|الاسم|النسب|الإسم|رمز|رقم|الرقم|النوع|الجنس|تاريخ|مكان|القسم|اسم)/i;
const NOT_NAME = /^(ذكر|أنثى|انثى|m|f|h|g|masculin|féminin|feminin|garçon|fille)$/i;
const DATE = /\d{1,4}[/.-]\d{1,2}[/.-]\d{1,4}/;

/**
 * Reads a class list pasted from Massar or a spreadsheet: one student per
 * line, cells separated by tabs (or ; and ,). The Massar code goes to the
 * national number; the first two name cells (surname, first name) to the name.
 * Headings, numbers, dates and gender columns are skipped.
 */
export function parseStudents(text: string): Student[] {
  const out: Student[] = [];
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line) continue;
    let cells = line.split(/\t|;/).map((c) => c.trim()).filter(Boolean);
    if (cells.length === 1 && line.includes(",")) cells = line.split(",").map((c) => c.trim()).filter(Boolean);
    if (cells.length === 1) {
      // "D130045678 Alaoui Mohamed": the code inside the line.
      const m = /(^|\s)([A-Za-z]\d{6,12})(\s|$)/.exec(line);
      cells = m ? [m[2], line.replace(m[2], "").replace(/^\s*\d+[.)-]?\s+/, "").trim()] : [line.replace(/^\d+[.)-]?\s+/, "")];
    }
    const code = cells.find((c) => MASSAR.test(c)) ?? "";
    const names = cells.filter((c) => c !== code && /\p{L}{2}/u.test(c) && !DATE.test(c) && !NOT_NAME.test(c));
    if (!names.length) continue;
    if (!code && names.every((c) => HEADER.test(c))) continue;
    out.push({ code, name: names.slice(0, 2).join(" ") });
  }
  return out.slice(0, 300);
}

/** Students per list page (two tables of 30). */
export const STUDENTS_PER_PAGE = 60;

/** The names on one student list page: students[offset …] in its two tables. */
export function studentElements(students: Student[], offset: number, w: number, h: number): TextEl[] {
  const tables = studentsLayout(w, h);
  const out: TextEl[] = [];
  const per = tables[0].rows;
  students.slice(offset, offset + STUDENTS_PER_PAGE).forEach((st, i) => {
    const T = tables[Math.floor(i / per)];
    const r = i % per;
    const cell = (c: number, v: string, size: number) => {
      const col = T.cols[c];
      const fs = Math.round(Math.max(7.5, Math.min(size, ((col.x1 - col.x0 - 6) / Math.max(1, v.length)) * 1.9)) * 2) / 2;
      out.push({ id: newId(), z: 100 + out.length, kind: "text", x: col.x0 + 3, y: T.top + r * T.rowH + (T.rowH - fs * 1.3) / 2 + 2, w: col.x1 - col.x0 - 6, text: v, fontSize: fs, color: VALUE, align: "center", font: "sans", role: "daftar-student" });
    };
    if (st.code) cell(1, st.code, 10.5);
    cell(2, st.name, 11.5);
  });
  return out;
}

// --- lesson outlines -------------------------------------------------------------------

export interface Outline {
  name: string;
  lines: string[];
}

export const OUTLINES: Outline[] = [
  { name: "درس", lines: ["التمهيد", "النشاط 1", "النشاط 2", "النشاط 3", "الخلاصة", "التقويم", "الواجب المنزلي"] },
  { name: "تمارين", lines: ["تصحيح الواجب", "تمرين 1", "تمرين 2", "تمرين 3"] },
  { name: "فرض محروس", lines: ["فرض محروس رقم …", "المدة: …", "المحاور: …"] },
  { name: "تصحيح الفرض", lines: ["تصحيح الفرض المحروس رقم …", "الأخطاء الشائعة", "الدعم"] },
  { name: "دعم ومعالجة", lines: ["التشخيص", "أنشطة الدعم", "التقويم"] },
];

/** An outline written down the activities column from row `row`, one line per row. */
export function outlineElements(lines: string[], row: number, w: number, h: number): TextEl[] {
  const L = lessonLayout(w, h);
  const col = L.cols.find((c) => c.key === "activities")!;
  return lines.slice(0, Math.max(0, L.rows - row)).map((line, i) => ({ id: newId(), z: 0, kind: "text", x: col.x0 + 6, y: L.bodyTop + (row + i) * L.step + 2, w: col.x1 - col.x0 - 12, text: line, fontSize: 11.5, color: "#1f1d24", font: "sans" }));
}

// --- exporting a class -----------------------------------------------------------------

export type Term = "year" | "s1" | "s2" | "month";

/** The dates a term covers (YYYY-MM-DD, inclusive), or null for everything. */
export function termRange(meta: DaftarMeta, term: Term, now = new Date()): { from: string; to: string } | null {
  if (term === "year") return null;
  if (term === "month") return { from: `${isoDate(now).slice(0, 7)}-01`, to: `${isoDate(now).slice(0, 7)}-31` };
  const y = Number(/^(\d{4})/.exec(meta.year)?.[1] ?? schoolYear(now).slice(0, 4));
  // Semester 1: September to January; semester 2: February to July.
  return term === "s1" ? { from: `${y}-08-15`, to: `${y + 1}-01-31` } : { from: `${y + 1}-02-01`, to: `${y + 1}-07-31` };
}

/**
 * The pages to export for a class: the cover and front pages, its student list, and
 * its lesson pages — with a date range, only those with a lesson in it.
 */
export function classExportPages<T extends { template: Template; section: string | null }>(pages: T[], cls: string, sessionsOf: (p: T) => string[], range: { from: string; to: string } | null): T[] {
  const FRONT = ["daftar-title", "daftar-profile", "daftar-profile-2", "daftar-timetable"];
  return pages.filter((p) => {
    if (FRONT.includes(p.template) || p.template.startsWith("cover")) return true;
    if (p.section !== cls) return false;
    if (p.template === "daftar-students") return true;
    if (!isLessonTemplate(p.template)) return false;
    const s = sessionsOf(p);
    if (!range) return true;
    return s.some((d) => d.slice(0, 10) >= range.from && d.slice(0, 10) <= range.to);
  });
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

/**
 * Applies changed daftar details: every filled-in value (headers, cover,
 * timetable notes, holiday list) is written again, and classes that are new
 * get their own section before the memos. Removed classes keep their pages.
 */
export async function updateDaftar(nb: Notebook, before: DaftarMeta, meta: DaftarMeta): Promise<void> {
  const pages = await api.pages(nb.id);
  const oldNote = new Set(before.timetable.map((l) => `${l.cls}\n${l.start}–${l.end}`));
  for (const [i, p] of pages.entries()) {
    const isCover = i === 0 && p.template.startsWith("cover");
    if (!isCover && !p.template.startsWith("daftar")) continue;
    const els = fromStored(await api.elements(p.id));
    const keep = els
      .filter((e) => !((e.kind === "text" || e.kind === "sticky") && e.role === "daftar-field"))
      // Timetable notes made before they were marked.
      .filter((e) => !(p.template === "daftar-timetable" && e.kind === "sticky" && oldNote.has(e.text)))
      .map((e) => (isCover && e.kind === "text" && e.role === "cover-date" && meta.year ? { ...e, text: meta.year } : e));
    const fresh = isCover ? coverElements(meta, p.width, p.height) : pageElements(p.template, meta, p.section, p.width, p.height);
    const z0 = Math.min(0, ...keep.map((e) => e.z)) - fresh.length - 1;
    await api.saveElements(p.id, [...fresh.map((e, k) => ({ ...e, z: p.template === "daftar-timetable" && e.kind === "sticky" ? e.z : z0 + k })), ...keep].map(toStored));
  }
  const added = meta.classes.filter((c) => !before.classes.includes(c));
  if (added.length) {
    const memos = pages.find((p) => p.template === "daftar-memos");
    let at = memos ? memos.position : pages.length;
    for (const cls of added) {
      await addFilledPage(nb, at++, "daftar-students", meta, cls);
      for (let k = 0; k < LESSON_PAGES; k++) await addFilledPage(nb, at++, lessonTemplateFor(meta.classes.indexOf(cls)), meta, cls);
    }
  }
  await api.setNotebookMeta(nb.id, JSON.stringify(meta));
}

/**
 * Writes a class's student list onto its list pages (replacing the names
 * there), adding list pages when one isn't enough.
 */
export async function fillStudents(nb: Notebook, meta: DaftarMeta, cls: string, students: Student[]): Promise<void> {
  let pages = (await api.pages(nb.id)).filter((p) => p.section === cls && p.template === "daftar-students");
  const need = Math.max(1, Math.ceil(students.length / STUDENTS_PER_PAGE));
  if (pages.length < need) {
    const all = await api.pages(nb.id);
    const inClass = all.filter((p) => p.section === cls);
    let at = pages.length ? pages[pages.length - 1].position + 1 : inClass.length ? inClass[0].position : all.length;
    for (let k = pages.length; k < need; k++) await addFilledPage(nb, at++, "daftar-students", meta, cls);
    pages = (await api.pages(nb.id)).filter((p) => p.section === cls && p.template === "daftar-students");
  }
  for (const [k, p] of pages.entries()) {
    const els = fromStored(await api.elements(p.id)).filter((e) => !(e.kind === "text" && e.role === "daftar-student"));
    await api.saveElements(p.id, [...els, ...studentElements(students, k * STUDENTS_PER_PAGE, p.width, p.height)].map(toStored));
  }
}

export { isLessonTemplate };
