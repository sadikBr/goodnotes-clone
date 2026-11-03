// The "New daftar" dialog: school details, classes, and an optional weekly
// timetable that pre-dates each class's rows.

import { createDaftar, DAYS, finishDaftarCover, holidayDays, nationalHolidays, schoolYear, updateDaftar, type DaftarMeta, type Holiday, type Lesson } from "./daftar";
import { type Notebook } from "./api";
import { formDialog, h, icon, reportError, toast, type CustomFieldContext, type Field } from "./ui";
import { refreshCoverThumbnail, setUpCover } from "./cover";
import { api } from "./api";

/** "1AC-2, 1AC-3 ; TC-1" → ["1AC-2", "1AC-3", "TC-1"] (no duplicates). */
export function parseClasses(s: string): string[] {
  return [...new Set(s.split(/[,;،\n]+/).map((c) => c.trim()).filter(Boolean))].slice(0, 30);
}

const SCHOOL_DAYS = [1, 2, 3, 4, 5, 6];

/** An editable list of weekly lessons (day, start, end, class). */
function timetableField(ctx: CustomFieldContext): HTMLElement {
  let lessons: Lesson[] = (() => {
    try {
      return JSON.parse(ctx.values.timetable || "[]") as Lesson[];
    } catch {
      return [];
    }
  })();
  const list = h("div", { class: "tt-list" });
  const empty = h("p", { class: "tt-empty" }, "No lessons yet — without a timetable you write the dates yourself.");
  const save = () => ctx.set(JSON.stringify(lessons));
  const classes = () => {
    const c = ctx.values.classes ? ctx.values.classes.split(/[,;،\n]+/).map((x) => x.trim()).filter(Boolean) : [];
    return [...new Set(c)];
  };
  const draw = () => {
    const cls = classes();
    list.replaceChildren(
      ...lessons.map((l, i) => {
        const day = h("select", { "aria-label": "Day" }, ...SCHOOL_DAYS.map((d) => h("option", { value: String(d), selected: l.day === d }, `${DAYS[d].fr} · ${DAYS[d].ar}`)));
        day.addEventListener("change", () => ((l.day = Number(day.value)), save()));
        const start = h("input", { type: "time", value: l.start, "aria-label": "Start", step: "300" });
        start.addEventListener("change", () => ((l.start = start.value || l.start), save()));
        const end = h("input", { type: "time", value: l.end, "aria-label": "End", step: "300" });
        end.addEventListener("change", () => ((l.end = end.value || l.end), save()));
        const options = cls.includes(l.cls) || !l.cls ? cls : [l.cls, ...cls];
        const klass = h("select", { "aria-label": "Class" }, ...options.map((c) => h("option", { value: c, selected: c === l.cls }, c)));
        if (!l.cls && cls[0]) l.cls = cls[0];
        klass.addEventListener("change", () => ((l.cls = klass.value), save()));
        const remove = h("button", { type: "button", class: "icon-btn small", "aria-label": "Remove lesson", title: "Remove" }, icon("close"));
        remove.addEventListener("click", () => {
          lessons.splice(i, 1);
          save();
          draw();
        });
        return h("div", { class: "tt-row" }, day, start, h("span", { class: "tt-dash" }, "–"), end, klass, remove);
      }),
    );
    empty.hidden = lessons.length > 0;
  };
  const add = h("button", { type: "button", class: "btn ghost tt-add" }, icon("plus"), "Add a lesson");
  add.addEventListener("click", () => {
    const cls = classes();
    if (!cls.length) return toast("Add your classes first", "error");
    const prev = lessons[lessons.length - 1];
    // Suggest the next slot: same class, two hours later (or the next day).
    const next: Lesson = prev
      ? (() => {
          const [hh, mm] = prev.end.split(":").map(Number);
          const later = hh + 2 <= 18;
          return { day: later ? prev.day : Math.min(6, prev.day + 1), start: later ? prev.end : "08:00", end: later ? `${String(hh + 2).padStart(2, "0")}:${String(mm).padStart(2, "0")}` : "10:00", cls: prev.cls };
        })()
      : { day: 1, start: "08:00", end: "10:00", cls: cls[0] };
    lessons.push(next);
    save();
    draw();
  });
  ctx.onChange(() => {
    // Keep class choices in step with the classes field.
    const cls = classes();
    lessons = lessons.map((l) => (cls.includes(l.cls) || !cls.length ? l : { ...l, cls: cls[0] }));
    const json = JSON.stringify(lessons);
    if (ctx.values.timetable !== json) ctx.set(json);
    if (document.activeElement?.closest(".tt-list") == null) draw();
  });
  draw();
  return h("div", { class: "tt" }, list, empty, add);
}

/** An editable list of holidays: name, first day, last day. */
function holidaysField(ctx: CustomFieldContext): HTMLElement {
  let items: Holiday[] = (() => {
    try {
      return JSON.parse(ctx.values.holidays || "[]") as Holiday[];
    } catch {
      return [];
    }
  })();
  const list = h("div", { class: "tt-list" });
  const empty = h("p", { class: "tt-empty" }, "No holidays yet. New lesson warns you when today is one.");
  const save = () => ctx.set(JSON.stringify(items));
  const draw = () => {
    items.sort((a, b) => (a.from || "9").localeCompare(b.from || "9"));
    list.replaceChildren(
      ...items.map((hd, i) => {
        const name = h("input", { value: hd.name, "aria-label": "Holiday", placeholder: "عيد الفطر · Aïd al-Fitr" });
        name.addEventListener("input", () => ((hd.name = name.value), save()));
        const from = h("input", { type: "date", value: hd.from, "aria-label": "From" });
        from.addEventListener("change", () => {
          hd.from = from.value;
          if (!hd.to || hd.to < hd.from) hd.to = hd.from;
          save();
          draw();
        });
        const to = h("input", { type: "date", value: hd.to, "aria-label": "To" });
        to.addEventListener("change", () => ((hd.to = to.value), save(), draw()));
        const remove = h("button", { type: "button", class: "icon-btn small", "aria-label": "Remove holiday", title: "Remove" }, icon("close"));
        remove.addEventListener("click", () => {
          items.splice(i, 1);
          save();
          draw();
        });
        const days = hd.from && hd.to >= hd.from ? `${holidayDays(hd)} d` : "";
        return h("div", { class: "hd-row" }, name, from, h("span", { class: "tt-dash" }, "–"), to, h("span", { class: "hd-days" }, days), remove);
      }),
    );
    empty.hidden = items.length > 0;
  };
  const add = h("button", { type: "button", class: "btn ghost tt-add" }, icon("plus"), "Add a holiday");
  add.addEventListener("click", () => {
    items.push({ name: "", from: "", to: "" });
    save();
    draw();
    (list.lastElementChild?.querySelector("input") as HTMLInputElement | null)?.focus();
  });
  const national = h("button", { type: "button", class: "btn ghost tt-add" }, icon("star"), "Add national holidays");
  national.title = "Fixed-date public holidays for the school year (religious holidays and vacations are added by hand)";
  national.addEventListener("click", () => {
    const have = new Set(items.map((x) => x.from));
    const fresh = nationalHolidays(ctx.values.year || schoolYear()).filter((x) => !have.has(x.from));
    if (!fresh.length) return toast("The national holidays are already in the list");
    items.push(...fresh);
    save();
    draw();
  });
  draw();
  return h("div", { class: "tt" }, list, empty, h("div", { class: "hd-actions" }, add, national));
}

/** The daftar's fields, filled from `m` (or empty for a new one). */
function daftarFields(m: DaftarMeta | null, title: string): Field[] {
  const fields: Field[] = [
    { name: "teacher", label: "الأستاذ(ة) · Teacher", value: m?.teacher ?? "", half: true },
    { name: "subject", label: "المادة · Subject", value: m?.subject ?? "", half: true },
    { name: "school", label: "المؤسسة · School", value: m?.school ?? "", half: true },
    { name: "year", label: "الموسم الدراسي · School year", value: m?.year ?? schoolYear(), half: true },
    { name: "region", label: "الجهة / الأكاديمية · Region", value: m?.region ?? "", half: true },
    { name: "directorate", label: "المديرية الإقليمية · Directorate", value: m?.directorate ?? "", half: true },
    { name: "payroll", label: "رقم التأجير · Payroll number (optional)", value: m?.payroll ?? "", half: true },
    { name: "title", label: "Title", value: title, half: true },
    { name: "classes", label: "الأقسام · Classes — separate with commas, e.g. 3/6, 3/7, 2/1", value: m?.classes.join(", ") ?? "" },
    { name: "timetable", label: "استعمال الزمن · Weekly timetable (optional: gives each new lesson its time)", type: "custom", value: JSON.stringify(m?.timetable ?? []), render: timetableField },
    { name: "holidays", label: "العطل · Holidays and vacations (optional)", type: "custom", value: JSON.stringify(m?.holidays ?? []), render: holidaysField },
  ];
  if (m) fields.splice(fields.findIndex((f) => f.name === "title"), 1);
  return fields;
}

function validateDaftar(v: Record<string, string>): string | null {
  if (!parseClasses(v.classes ?? "").length) return "Add at least one class.";
  const bad = (JSON.parse(v.timetable || "[]") as Lesson[]).find((l) => !(l.start < l.end));
  if (bad) return `A ${DAYS[bad.day].fr} lesson ends before it starts.`;
  const hd = (JSON.parse(v.holidays || "[]") as Holiday[]).find((x) => !x.from || !x.name.trim());
  if (hd) return hd.from ? "Give each holiday a name." : `Choose the dates of “${hd.name.trim() || "the new holiday"}”.`;
  return null;
}

function metaFrom(r: Record<string, string>): DaftarMeta {
  const classes = parseClasses(r.classes);
  return {
    kind: "daftar",
    year: r.year.trim(),
    region: r.region.trim(),
    directorate: r.directorate.trim(),
    school: r.school.trim(),
    teacher: r.teacher.trim(),
    payroll: r.payroll.trim(),
    subject: r.subject.trim(),
    classes,
    timetable: (JSON.parse(r.timetable || "[]") as Lesson[]).filter((l) => classes.includes(l.cls)),
    holidays: (JSON.parse(r.holidays || "[]") as Holiday[]).map((x) => ({ name: x.name.trim(), from: x.from, to: x.to >= x.from ? x.to : x.from })),
  };
}

/**
 * Changes a daftar's details after it was made. Every page's filled-in
 * values are written again; new classes get a section. Resolves with the
 * new details (null when cancelled).
 */
export async function editDaftarDialog(nb: Notebook, meta: DaftarMeta): Promise<DaftarMeta | null> {
  const r = await formDialog("Daftar settings · إعدادات الدفتر", daftarFields(meta, nb.title), "Save", { width: 640, twoColumns: true, validate: validateDaftar });
  if (!r) return null;
  const next = metaFrom(r);
  const removed = meta.classes.filter((c) => !next.classes.includes(c));
  toast(removed.length ? `Updating… The pages of ${removed.join(", ")} are kept.` : "Updating your daftar…");
  await updateDaftar(nb, meta, next);
  return next;
}

/** Asks for the daftar's details and creates it. */
export async function newDaftarDialog(folderId: string | null): Promise<Notebook | null> {
  const r = await formDialog(
    "New daftar annossous · دفتر النصوص",
    [
      ...daftarFields(null, "دفتر النصوص"),
      { name: "cover", label: "Cover colour", type: "color-swatches", value: "#5b2a5e", colors: [ { color: "#5b2a5e", label: "Plum" }, { color: "#1f7a4a", label: "Green" }, { color: "#1f6fd1", label: "Blue" }, { color: "#2d2f72", label: "Navy" }, { color: "#d1245f", label: "Raspberry" }, { color: "#e0612a", label: "Orange" }, { color: "#0d9488", label: "Teal" }, { color: "#475569", label: "Slate" } ] },
    ],
    "Create daftar",
    { width: 640, twoColumns: true, validate: validateDaftar },
  );
  if (!r) return null;
  try {
    const meta = metaFrom(r);
    toast("Preparing your daftar…");
    const nb = await createDaftar({ title: r.title.trim() || "دفتر النصوص", folderId, coverColor: r.cover, meta });
    await setUpCover(nb).catch(reportError);
    await finishDaftarCover(nb, meta);
    const [cover] = await api.pages(nb.id);
    if (cover) await refreshCoverThumbnail(cover).catch(() => {});
    return nb;
  } catch (e) {
    reportError(e);
    return null;
  }
}
