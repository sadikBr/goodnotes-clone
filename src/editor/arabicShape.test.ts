import { describe, expect, it } from "vitest";
import { bidiRuns, shapeArabic } from "./arabicShape";

const hex = (s: string) => [...s].map((c) => c.codePointAt(0)!.toString(16)).join(" ");

describe("Arabic joining", () => {
  it("picks initial, medial and final forms", () => {
    // محمد: meem initial, hah medial, meem medial, dal final.
    expect(hex(shapeArabic("محمد"))).toBe("fee3 fea4 fee4 feaa");
  });
  it("breaks the join after right-joining letters", () => {
    // دار: dal, alef and reh all isolated (dal and alef never join the next letter).
    expect(hex(shapeArabic("دار"))).toBe("fea9 fe8d fead");
    // يا: yeh initial, alef final.
    expect(hex(shapeArabic("يا"))).toBe("fef3 fe8e");
  });
  it("makes lam-alef one ligature, isolated or final", () => {
    expect(hex(shapeArabic("لا"))).toBe("fefb");
    expect(hex(shapeArabic("سلام"))).toBe("feb3 fefc fee1");
  });
  it("ignores harakat when joining and keeps them", () => {
    expect(hex(shapeArabic("مَد"))).toBe("fee3 64e feaa");
  });
  it("leaves other text alone", () => {
    expect(shapeArabic("Classe 3/6")).toBe("Classe 3/6");
  });
});

describe("bidi runs", () => {
  it("orders an Arabic line with numbers right to left, numbers kept left to right", () => {
    // Runs keep logical order inside; the Arabic run is drawn reversed, so its space ends up on its left.
    expect(bidiRuns("القسم 3/6", true)).toEqual([
      { text: "3/6", rtl: false },
      { text: "القسم ", rtl: true },
    ]);
  });
  it("keeps a French line with an Arabic word in order", () => {
    expect(bidiRuns("Classe القسم ok", false)).toEqual([
      { text: "Classe ", rtl: false },
      { text: "القسم", rtl: true },
      { text: " ok", rtl: false },
    ]);
  });
  it("mirrors brackets inside right-to-left runs", () => {
    expect(bidiRuns("الأستاذ(ة)", true)).toEqual([{ text: "الأستاذ)ة(", rtl: true }]);
  });
});
