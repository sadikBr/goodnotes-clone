// The daftar annossous for the native app: the same steps as the web app's
// daftarSetup.ts and the editor's daftar bar (src/editor/editor.ts), without
// the DOM. Functions that change the open notebook take the editor core and
// return what the canvas has to do next ({ reload, goTo, toast }).
import { api, type Notebook } from "../../src/api";
import {
  addLessonPage,
  classExportPages,
  createDaftar,
  DAYS,
  fillStudents,
  finishDaftarCover,
  holidayDays,
  holidayOn,
  isoDate,
  lessonEntry,
  nationalHolidays,
  nextFreeRow,
  OUTLINES,
  outlineElements,
  parseDaftar,
  parseStudents,
  schoolYear,
  termRange,
  updateDaftar,
  type DaftarMeta,
  type Holiday,
  type Lesson,
  type Term,
} from "../../src/daftar";
import { isLessonTemplate, lessonLayout } from "../../src/editor/daftarPapers";
import { bounds } from "../../src/editor/geometry";
import { nextZ, type El } from "../../src/editor/model";
import { refreshCoverThumbnail, setUpCover } from "../../src/cover";
import type { EditorCore } from "./editorCore";

/** "1AC-2, 1AC-3 ; TC-1" → ["1AC-2", "1AC-3", "TC-1"] (no duplicates). */
export function parseClasses(s: string): string[] {
  return [...new Set(String(s || "").split(/[,;،\n]+/).map((c) => c.trim()).filter(Boolean))].slice(0, 30);
}

export { DAYS, OUTLINES, schoolYear, holidayDays, parseStudents };

export function nationalHolidaysFor(year: string): Holiday[] {
  return nationalHolidays(year || schoolYear());
}

export async function info(notebookId: string): Promise<DaftarMeta | null> {
  return parseDaftar(await api.notebook(notebookId));
}

/** The form's values for a daftar (empty for a new one). */
export function formValues(m: DaftarMeta | null, title: string): Record<string, string> {
  return {
    teacher: m?.teacher ?? "",
    subject: m?.subject ?? "",
    school: m?.school ?? "",
    year: m?.year ?? schoolYear(),
    region: m?.region ?? "",
    directorate: m?.directorate ?? "",
    payroll: m?.payroll ?? "",
    title,
    classes: m?.classes.join(", ") ?? "",
    timetable: JSON.stringify(m?.timetable ?? []),
    holidays: JSON.stringify(m?.holidays ?? []),
  };
}

export function validate(v: Record<string, string>): string | null {
  if (!parseClasses(v.classes ?? "").length) return "Add at least one class.";
  const bad = (JSON.parse(v.timetable || "[]") as Lesson[]).find((l) => !(l.start < l.end));
  if (bad) return `A ${DAYS[bad.day].fr} lesson ends before it starts.`;
  const hd = (JSON.parse(v.holidays || "[]") as Holiday[]).find((x) => !x.from || !x.name.trim());
  if (hd) return hd.from ? "Give each holiday a name." : `Choose the dates of “${hd.name.trim() || "the new holiday"}”.`;
  return null;
}

function metaFrom(r: Record<string, string>): DaftarMeta {
  const classes = parseClasses(r.classes);
  const t = (k: string) => String(r[k] ?? "").trim();
  return {
    kind: "daftar",
    year: t("year"),
    region: t("region"),
    directorate: t("directorate"),
    school: t("school"),
    teacher: t("teacher"),
    payroll: t("payroll"),
    subject: t("subject"),
    classes,
    timetable: (JSON.parse(r.timetable || "[]") as Lesson[]).filter((l) => classes.includes(l.cls)),
    holidays: (JSON.parse(r.holidays || "[]") as Holiday[]).map((x) => ({ name: x.name.trim(), from: x.from, to: x.to >= x.from ? x.to : x.from })),
  };
}

/** Creates a daftar from the form's values; resolves with the notebook. */
export async function create(values: Record<string, string>, folderId: string | null): Promise<Notebook> {
  const meta = metaFrom(values);
  const nb = await createDaftar({ title: String(values.title || "").trim() || "دفتر النصوص", folderId: folderId || null, coverColor: values.cover || "#5b2a5e", meta });
  await setUpCover(nb).catch(() => {});
  await finishDaftarCover(nb, meta);
  const [cover] = await api.pages(nb.id);
  if (cover) await refreshCoverThumbnail(cover).catch(() => {});
  return nb;
}

/** Applies changed details. */
export async function update(notebookId: string, values: Record<string, string>): Promise<{ removed: string[] }> {
  const nb = await api.notebook(notebookId);
  const before = parseDaftar(nb);
  if (!before) return { removed: [] };
  const next = metaFrom(values);
  await updateDaftar(nb, before, next);
  return { removed: before.classes.filter((c) => !next.classes.includes(c)) };
}

// ------------------------------------------------------------- in the editor

type CorePage = { meta: { id: string; width: number; height: number; template: string; section?: string | null } };

const isField = (e: El) => e.kind === "text" && e.role === "daftar-field";

function inkRects(els: El[]) {
  return els.filter((e) => !isField(e)).map((e) => {
    const b = bounds(e);
    return { x0: b.x, y0: b.y, x1: b.x + b.w, y1: b.y + b.h };
  });
}

/** The class of the page being edited (or the first class). */
export function currentClass(core: EditorCore, meta: DaftarMeta): string | null {
  const s = (core.page as CorePage["meta"]).section;
  return s && meta.classes.includes(s) ? s : (meta.classes[0] ?? null);
}

/** Section headings for the page list: [index, class] where a section starts. */
export function sections(core: EditorCore): { index: number; label: string }[] {
  const out: { index: number; label: string }[] = [];
  const pages = core.pages as unknown as CorePage[];
  pages.forEach((p, i) => {
    const s = p.meta.section;
    if (s && s !== pages[i - 1]?.meta.section) out.push({ index: i, label: s });
  });
  return out;
}

/** A class's lesson page in use: the last with writing on it (else the first). */
function classPage(core: EditorCore, cls: string): number | null {
  const pages = core.pages as unknown as CorePage[];
  let current: number | null = null;
  pages.forEach((p, i) => {
    if (p.meta.section !== cls || !isLessonTemplate(p.meta.template as never)) return;
    if (current === null) current = i;
    if (core.load(i).some((e) => !isField(e))) current = i;
  });
  return current;
}

export function goToClass(core: EditorCore, cls: string) {
  return { goTo: classPage(core, cls) };
}

export function holidayToday(meta: DaftarMeta): string | null {
  return holidayOn(meta)?.name ?? null;
}

/**
 * A new lesson on the next free line of the class's current lesson page (a
 * new page when it's full). Two steps when a page is needed: this adds the
 * page and asks for a reload; the canvas then calls it again.
 */
export async function newLesson(core: EditorCore, notebookId: string, cls: string) {
  const nb = await api.notebook(notebookId);
  const d = parseDaftar(nb);
  if (!d) return {};
  core.finishTextEdit();
  const here = core.page as CorePage["meta"];
  let target: number | null = classPage(core, cls);
  if (here.section === cls && isLessonTemplate(here.template as never)) target = core.active;
  let row: number | null = null;
  if (target !== null) {
    const p = (core.pages[target] as unknown as CorePage).meta;
    row = nextFreeRow(inkRects(core.load(target)), p.width, p.height);
  }
  if (target === null || row === null) {
    const page = await addLessonPage(nb, d, cls);
    return { reload: true, again: true, pageId: page.id };
  }
  const p = (core.pages[target] as unknown as CorePage).meta;
  core.activate(target);
  const today = isoDate(new Date());
  const logged = new Set(core.elements.flatMap((e) => (e.kind === "text" && e.role === "daftar-session" && e.session?.startsWith(today) ? [e.session.slice(11)] : [])));
  const entry = lessonEntry(d, cls, row, p.width, p.height, new Date(), logged);
  const c = core as unknown as { snapshot(): void; changed(): void };
  c.snapshot();
  let z = nextZ(core.elements);
  core.elements = core.elements.concat(entry.map((el) => ({ ...el, z: z++ })));
  c.changed();
  const L = lessonLayout(p.width, p.height);
  const time = entry[2]?.text;
  return { goTo: target, y: L.bodyTop + row * L.step, toast: `${cls} · ${DAYS[new Date().getDay()].ar} ${entry[0].text}${time ? ` · ${time}` : ""}` };
}

/** Writes an outline down the activities column of the current lesson. */
export function insertOutline(core: EditorCore, meta: DaftarMeta, lines: string[]) {
  const cls = currentClass(core, meta);
  if (!cls) return {};
  core.finishTextEdit();
  if (!isLessonTemplate((core.page as CorePage["meta"]).template as never)) {
    const i = classPage(core, cls);
    if (i === null) return {};
    core.activate(i);
  }
  const p = core.page as CorePage["meta"];
  const L = lessonLayout(p.width, p.height);
  const act = L.cols.find((c) => c.key === "activities")!;
  const ink = inkRects(core.elements);
  const sessionRows = core.elements.flatMap((e) => (e.kind === "text" && e.role === "daftar-session" ? [Math.round((e.y - 2 - L.bodyTop) / L.step)] : []));
  let row: number | null = null;
  if (sessionRows.length) {
    const last = Math.max(...sessionRows);
    const top = L.bodyTop + last * L.step + 1;
    const used = ink.some((r) => r.x1 > act.x0 + 1 && r.x0 < act.x1 - 1 && r.y1 > top);
    if (!used && last + lines.length <= L.rows) row = last;
  }
  if (row === null) row = nextFreeRow(ink, p.width, p.height, lines.length);
  if (row === null) return { error: "No room left for this outline on the page. Start a new lesson for a new page." };
  const c = core as unknown as { snapshot(): void; changed(): void };
  c.snapshot();
  let z = nextZ(core.elements);
  core.elements = core.elements.concat(outlineElements(lines, row, p.width, p.height).map((el) => ({ ...el, z: z++ })));
  c.changed();
  return { goTo: core.active, y: L.bodyTop + row * L.step };
}

/** How many students a pasted list holds, for the dialog. */
export function studentsSummary(text: string): string {
  const n = parseStudents(text || "");
  return n.length ? `${n.length} student${n.length > 1 ? "s" : ""} found · ${n.filter((x) => x.code).length} with a Massar code. First: ${n[0].name}` : "No names found yet.";
}

export async function pasteStudents(notebookId: string, cls: string, text: string) {
  const nb = await api.notebook(notebookId);
  const d = parseDaftar(nb);
  if (!d) return {};
  const students = parseStudents(text);
  await fillStudents(nb, d, cls, students);
  const pages = await api.pages(nb.id);
  const first = pages.find((p) => p.section === cls && p.template === "daftar-students");
  return { reloadAll: true, pageId: first?.id ?? null, toast: `${students.length} students written for ${cls}` };
}

/** The page indexes to export for a class and period (null when it has no lessons then). */
export function exportPages(core: EditorCore, meta: DaftarMeta, cls: string, term: Term): { pages: number[]; name: string } | null {
  const pages = (core.pages as unknown as CorePage[]).map((p, i) => ({ ...p.meta, i }));
  const sessions = (p: { i: number }) => core.load(p.i).flatMap((e) => (e.kind === "text" && e.role === "daftar-session" && e.session ? [e.session] : []));
  const range = termRange(meta, term);
  const picked = classExportPages(pages as never[], cls, sessions as never, range) as unknown as { i: number; template: string }[];
  if (!picked.some((p) => isLessonTemplate(p.template as never))) return null;
  const period = ({ year: meta.year, s1: "S1", s2: "S2", month: isoDate(new Date()).slice(0, 7) } as Record<string, string>)[term] ?? "";
  return { pages: picked.map((p) => p.i), name: `${cls.replace(/[\\/]/g, "-")}${period ? ` - ${period}` : ""}` };
}

/** "Add page" in a daftar: a lesson page at the end of the current class's section. */
export async function addClassPage(core: EditorCore, notebookId: string) {
  const nb = await api.notebook(notebookId);
  const d = parseDaftar(nb);
  const s = (core.page as CorePage["meta"]).section;
  if (!d || !s || !d.classes.includes(s)) return { plain: true };
  const page = await addLessonPage(nb, d, s);
  return { reload: true, pageId: page.id };
}
