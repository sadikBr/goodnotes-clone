import { describe, expect, it } from "vitest";
import { nextSessions, parseDaftar, schoolYear, type DaftarMeta } from "./daftar";

const meta = (over: Partial<DaftarMeta> = {}): DaftarMeta => ({
  kind: "daftar",
  year: "2026/2027",
  school: "",
  teacher: "",
  subject: "Maths",
  classes: ["1AC-2", "TC-3"],
  // 1AC-2: Monday 08:00 and Wednesday 10:00; TC-3: Monday 14:00.
  timetable: [
    { day: 3, start: "10:00", end: "11:00", cls: "1AC-2" },
    { day: 1, start: "08:00", end: "10:00", cls: "1AC-2" },
    { day: 1, start: "14:00", end: "16:00", cls: "TC-3" },
  ],
  start: "2026-09-14", // a Monday
  dated: {},
  ...over,
});

describe("daftar sessions", () => {
  it("lists a class's sessions in date order from the start date", () => {
    const s = nextSessions(meta(), "1AC-2", null, 4);
    expect(s.map((x) => x.key)).toEqual(["2026-09-14 08:00", "2026-09-16 10:00", "2026-09-21 08:00", "2026-09-23 10:00"]);
    expect(s[0]).toMatchObject({ day: 1, start: "08:00", end: "10:00" });
  });
  it("continues after the last dated session", () => {
    expect(nextSessions(meta(), "1AC-2", "2026-09-16 10:00", 2).map((x) => x.key)).toEqual(["2026-09-21 08:00", "2026-09-23 10:00"]);
    // Same day, later lesson.
    const two = meta({ timetable: [{ day: 1, start: "08:00", end: "09:00", cls: "A" }, { day: 1, start: "11:00", end: "12:00", cls: "A" }], classes: ["A"] });
    expect(nextSessions(two, "A", "2026-09-14 08:00", 1)[0].key).toBe("2026-09-14 11:00");
  });
  it("gives nothing for a class without lessons", () => {
    expect(nextSessions(meta(), "2AC-1", null, 5)).toEqual([]);
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
    expect(schoolYear(new Date(2026, 8, 1))).toBe("2026/2027");
    expect(schoolYear(new Date(2027, 2, 1))).toBe("2026/2027");
  });
});
