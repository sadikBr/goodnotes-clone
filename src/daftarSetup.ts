// The "New daftar" dialog: school details, classes, and an optional weekly
// timetable that pre-dates each class's rows.

import { createDaftar, DAYS, finishDaftarCover, schoolYear, type DaftarMeta, type Lesson } from "./daftar";
import { type Notebook } from "./api";
import { formDialog, h, icon, reportError, toast, type CustomFieldContext } from "./ui";
import { refreshCoverThumbnail, setUpCover } from "./cover";
import { api } from "./api";

/** "1AC-2, 1AC-3 ; TC-1" → ["1AC-2", "1AC-3", "TC-1"] (no duplicates). */
export function parseClasses(s: string): string[] {
  return [...new Set(s.split(/[,;،\n]+/).map((c) => c.trim()).filter(Boolean))].slice(0, 30);
}

const SCHOOL_DAYS = [1, 2, 3, 4, 5, 6];

/** An editable list of weekly lessons (day, start, end, class). */
function timetableField(ctx: CustomFieldContext): HTMLElement {
  let lessons: Lesson[] = [];
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

/** Asks for the daftar's details and creates it. */
export async function newDaftarDialog(folderId: string | null): Promise<Notebook | null> {
  const r = await formDialog(
    "New daftar annossous · دفتر النصوص",
    [
      { name: "teacher", label: "الأستاذ(ة) · Teacher", value: "", half: true },
      { name: "subject", label: "المادة · Subject", value: "", half: true },
      { name: "school", label: "المؤسسة · School", value: "", half: true },
      { name: "year", label: "الموسم الدراسي · School year", value: schoolYear(), half: true },
      { name: "region", label: "الجهة / الأكاديمية · Region", value: "", half: true },
      { name: "directorate", label: "المديرية الإقليمية · Directorate", value: "", half: true },
      { name: "payroll", label: "رقم التأجير · Payroll number (optional)", value: "", half: true },
      { name: "title", label: "Title", value: "دفتر النصوص", half: true },
      { name: "classes", label: "الأقسام · Classes — separate with commas, e.g. 3/6, 3/7, 2/1", value: "" },
      { name: "timetable", label: "استعمال الزمن · Weekly timetable (optional: gives each new lesson its time)", type: "custom", value: "[]", render: timetableField },
      { name: "cover", label: "Cover colour", type: "color-swatches", value: "#5b2a5e", colors: [ { color: "#5b2a5e", label: "Plum" }, { color: "#1f7a4a", label: "Green" }, { color: "#1f6fd1", label: "Blue" }, { color: "#2d2f72", label: "Navy" }, { color: "#d1245f", label: "Raspberry" }, { color: "#e0612a", label: "Orange" }, { color: "#0d9488", label: "Teal" }, { color: "#475569", label: "Slate" } ] },
    ],
    "Create daftar",
    {
      width: 640,
      twoColumns: true,
      validate: (v) => {
        if (!parseClasses(v.classes ?? "").length) return "Add at least one class.";
        const bad = (JSON.parse(v.timetable || "[]") as Lesson[]).find((l) => !(l.start < l.end));
        if (bad) return `A ${DAYS[bad.day].fr} lesson ends before it starts.`;
        return null;
      },
    },
  );
  if (!r) return null;
  try {
    const classes = parseClasses(r.classes);
    const meta: DaftarMeta = {
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
    };
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
