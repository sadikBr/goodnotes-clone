import { describe, expect, it } from "vitest";
import { classExportPages, holidayDays, holidayElements, holidayOn, lessonEntry, nationalHolidays, nextFreeRow, outlineElements, pageElements, parseDaftar, parseStudents, schoolYear, studentElements, termRange, todaysLesson, type DaftarMeta } from "./daftar";
import { daftarSlots, lessonLayout, lessonTemplateFor, LESSON_TEMPLATES, DAFTAR_PAGES } from "./editor/daftarPapers";

const W = 794;
const H = 1123;

const meta = (over: Partial<DaftarMeta> = {}): DaftarMeta => ({
  kind: "daftar",
  year: "2026-2027",
  region: "الرباط سلا القنيطرة",
  directorate: "تمارة الصخيرات",
  school: "ماء العينين",
  teacher: "لبنى أرسلان",
  payroll: "2320866",
  subject: "اللغة العربية",
  classes: ["3/6", "3/7"],
  holidays: [],
  // 3/6: Monday 08:00–10:00 and 14:00–16:00; 3/7: Tuesday 10:00–12:00.
  timetable: [
    { day: 1, start: "14:00", end: "16:00", cls: "3/6" },
    { day: 1, start: "08:00", end: "10:00", cls: "3/6" },
    { day: 2, start: "10:00", end: "12:00", cls: "3/7" },
  ],
  ...over,
});

const monday = (hh: number, mm = 0) => new Date(2026, 8, 14, hh, mm); // 14 Sept 2026 is a Monday

describe("today's lesson", () => {
  it("picks the lesson under way, else the nearest one that day", () => {
    expect(todaysLesson(meta(), "3/6", monday(9))?.start).toBe("08:00");
    expect(todaysLesson(meta(), "3/6", monday(15, 30))?.start).toBe("14:00");
    expect(todaysLesson(meta(), "3/6", monday(12, 30))?.start).toBe("14:00");
    expect(todaysLesson(meta(), "3/6", monday(7))?.start).toBe("08:00");
  });
  it("skips a lesson already logged today", () => {
    expect(todaysLesson(meta(), "3/6", monday(9), new Set(["08:00"]))?.start).toBe("14:00");
    // Both logged: the nearest again.
    expect(todaysLesson(meta(), "3/6", monday(9), new Set(["08:00", "14:00"]))?.start).toBe("08:00");
  });
  it("is null when the class has no lesson that day", () => {
    expect(todaysLesson(meta(), "3/7", monday(10))).toBeNull();
    expect(todaysLesson(meta({ timetable: [] }), "3/6", monday(10))).toBeNull();
  });
});

describe("lesson entries", () => {
  const L = lessonLayout(W, H);
  it("starts on the first row of an empty page", () => {
    expect(nextFreeRow([], W, H)).toBe(0);
  });
  it("goes below what is written, leaving one empty row", () => {
    const y = L.bodyTop + 4.2 * L.step; // ink reaching into row 4
    expect(nextFreeRow([{ x0: L.x0 + 50, x1: L.x0 + 200, y0: L.bodyTop + 5, y1: y }], W, H)).toBe(6);
  });
  it("ignores things outside the table (header fields, notes)", () => {
    expect(nextFreeRow([{ x0: L.x0, x1: L.x1, y0: 10, y1: 60 }], W, H)).toBe(0);
    expect(nextFreeRow([{ x0: L.x0, x1: L.x1, y0: L.bodyBottom + 30, y1: L.bodyBottom + 50 }], W, H)).toBe(0);
  });
  it("reports a full page", () => {
    expect(nextFreeRow([{ x0: L.x0 + 50, x1: L.x0 + 200, y0: L.bodyTop, y1: L.bodyBottom - L.step }], W, H)).toBeNull();
  });
  it("writes the date, class and time in their columns on that row", () => {
    const entry = lessonEntry(meta(), "3/6", 3, W, H, monday(9, 5));
    expect(entry.map((e) => e.text)).toEqual(["14/09/2026", "3/6", "08:00-10:00"]);
    const col = (k: string) => L.cols.find((c) => c.key === k)!;
    expect(entry[0].x).toBeGreaterThanOrEqual(col("date").x0);
    expect(entry[1].x).toBeGreaterThanOrEqual(col("class").x0);
    expect(entry[2].x).toBeGreaterThanOrEqual(col("time").x0);
    for (const e of entry) expect(e.y).toBeCloseTo(L.bodyTop + 3 * L.step + 2, 5);
    // No timetable lesson today: date and class only.
    expect(lessonEntry(meta(), "3/7", 0, W, H, monday(9)).map((e) => e.text)).toEqual(["14/09/2026", "3/6".replace("3/6", "3/7")]);
  });
});

describe("daftar pages", () => {
  it("gives each class a lesson colour", () => {
    expect(lessonTemplateFor(0)).toBe("daftar");
    expect(lessonTemplateFor(1)).toBe("daftar-blue");
    expect(lessonTemplateFor(LESSON_TEMPLATES.length)).toBe("daftar");
  });
  it("fills in the details each page asks for", () => {
    const texts = (t: Parameters<typeof pageElements>[0], cls: string | null = null) => pageElements(t, meta(), cls, W, H).map((e) => ("text" in e ? e.text : ""));
    expect(texts("daftar-title")).toEqual(expect.arrayContaining(["الرباط سلا القنيطرة", "تمارة الصخيرات", "ماء العينين", "لبنى أرسلان", "3/6 - 3/7", "2320866", "2026-2027"]));
    expect(texts("daftar", "3/6")).toEqual(["اللغة العربية", "3/6"]);
    expect(texts("daftar-students", "3/7")).toEqual(["3/7", "3/7"]);
    expect(texts("daftar-profile")).toEqual(expect.arrayContaining(["2320866", "3/6 - 3/7", "2026-2027", "ماء العينين"]));
    // The timetable page carries a note per lesson.
    expect(pageElements("daftar-timetable", meta(), null, W, H).filter((e) => e.kind === "sticky")).toHaveLength(3);
  });
  it("keeps every slot on the page", () => {
    for (const t of [...DAFTAR_PAGES, ...LESSON_TEMPLATES]) {
      for (const s of Object.values(daftarSlots(t, W, H))) {
        expect(s.x).toBeGreaterThanOrEqual(0);
        expect(s.x + s.w).toBeLessThanOrEqual(W);
        expect(s.w).toBeGreaterThan(20);
        expect(s.y).toBeGreaterThan(0);
        expect(s.y).toBeLessThan(H);
      }
    }
  });
});

describe("daftar settings", () => {
  it("reads only daftar settings", () => {
    expect(parseDaftar({ meta: null })).toBeNull();
    expect(parseDaftar({ meta: "{oops" })).toBeNull();
    expect(parseDaftar({ meta: JSON.stringify({ kind: "other" }) })).toBeNull();
    expect(parseDaftar({ meta: JSON.stringify({ kind: "daftar", classes: ["A"] }) })?.classes).toEqual(["A"]);
  });
  it("school years start in September", () => {
    expect(schoolYear(new Date(2026, 8, 1))).toBe("2026-2027");
    expect(schoolYear(new Date(2027, 2, 1))).toBe("2026-2027");
  });
});

describe("student lists", () => {
  it("reads Massar rows: code to the national number, surname and first name to the name", () => {
    const text = ["رقم التلميذ\tالنسب\tالاسم\tالنوع\tتاريخ الازدياد", "D130045678\tالعلوي\tمحمد\tذكر\t12/03/2012", "1\tR131122334\tBennani\tSalma\tF", "", "  "].join("\n");
    expect(parseStudents(text)).toEqual([
      { code: "D130045678", name: "العلوي محمد" },
      { code: "R131122334", name: "Bennani Salma" },
    ]);
  });
  it("reads plain lines with or without a code, and CSV", () => {
    expect(parseStudents("1. Alaoui Mohamed\nD130000001 Idrissi Sara\nBennani;Omar")).toEqual([
      { code: "", name: "Alaoui Mohamed" },
      { code: "D130000001", name: "Idrissi Sara" },
      { code: "", name: "Bennani Omar" },
    ]);
  });
  it("puts 30 names per table and 60 per page", () => {
    const st = Array.from({ length: 70 }, (_, i) => ({ code: `D${1000000 + i}`, name: `N${i}` }));
    const first = studentElements(st, 0, W, H);
    expect(first.filter((e) => e.text.startsWith("N"))).toHaveLength(60);
    const n0 = first.find((e) => e.text === "N0")!;
    const n30 = first.find((e) => e.text === "N30")!;
    expect(n30.x).toBeLessThan(n0.x); // the second table is to the left
    expect(Math.abs(n30.y - n0.y)).toBeLessThan(0.01);
    expect(studentElements(st, 60, W, H).filter((e) => e.text.startsWith("N"))).toHaveLength(10);
  });
});

describe("holidays", () => {
  const hd = { name: "عطلة", from: "2026-12-20", to: "2026-12-28" };
  it("counts both ends and finds the holiday a day falls in", () => {
    expect(holidayDays(hd)).toBe(9);
    expect(holidayOn(meta({ holidays: [hd] }), new Date(2026, 11, 24))?.name).toBe("عطلة");
    expect(holidayOn(meta({ holidays: [hd] }), new Date(2026, 11, 29))).toBeNull();
  });
  it("suggests the fixed national holidays of the school year", () => {
    const list = nationalHolidays("2026-2027").map((x) => x.from);
    expect(list).toContain("2026-11-18");
    expect(list).toContain("2027-01-11");
    expect(list).toContain("2027-05-01");
  });
  it("writes the holiday list on the holidays page", () => {
    const els = holidayElements(meta({ holidays: [hd] }), W, H);
    expect(els.map((e) => e.text)).toEqual(["عطلة", "20/12/2026", "28/12/2026", "9"]);
    expect(pageElements("daftar-holidays", meta({ holidays: [hd] }), null, W, H).some((e) => e.kind === "text" && e.text === "9")).toBe(true);
  });
});

describe("outlines and class export", () => {
  it("writes one outline line per grid row in the activities column", () => {
    const L = lessonLayout(W, H);
    const els = outlineElements(["النشاط 1", "النشاط 2"], 3, W, H);
    const act = L.cols.find((c) => c.key === "activities")!;
    expect(els).toHaveLength(2);
    expect(els[1].y - els[0].y).toBeCloseTo(L.step);
    expect(els[0].x).toBeGreaterThan(act.x0);
    expect(els[0].x + els[0].w).toBeLessThan(act.x1);
  });
  it("keeps the front pages, the class's list and its lessons in the period", () => {
    const pages = [
      { id: "t", template: "daftar-title", section: null },
      { id: "s", template: "daftar-students", section: "3/6" },
      { id: "a", template: "daftar", section: "3/6" },
      { id: "b", template: "daftar", section: "3/6" },
      { id: "o", template: "daftar-blue", section: "3/7" },
      { id: "m", template: "daftar-memos", section: null },
    ] as const;
    const sessions: Record<string, string[]> = { a: ["2026-10-05 08:00"], b: ["2027-03-01 08:00"] };
    const pick = (term: "year" | "s1" | "s2") => classExportPages([...pages] as { id: string; template: never; section: string | null }[], "3/6", (p) => sessions[p.id] ?? [], termRange(meta(), term)).map((p) => p.id);
    expect(pick("year")).toEqual(["t", "s", "a", "b"]);
    expect(pick("s1")).toEqual(["t", "s", "a"]);
    expect(pick("s2")).toEqual(["t", "s", "b"]);
  });
});
