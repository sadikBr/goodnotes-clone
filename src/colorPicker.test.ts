import { describe, expect, it } from "vitest";
import { hexToHsv, hsvToHex, normalizeHex } from "./colorPicker";

describe("colour picker maths", () => {
  it("normalises hex input", () => {
    expect(normalizeHex("#ABC")).toBe("#aabbcc");
    expect(normalizeHex(" 1d4ed8 ")).toBe("#1d4ed8");
    expect(normalizeHex("#12345")).toBeNull();
    expect(normalizeHex("red")).toBeNull();
  });

  it("round-trips colours through HSV", () => {
    for (const c of ["#000000", "#ffffff", "#ff0000", "#00ff00", "#0000ff", "#1d4ed8", "#facc15", "#7f7f7f", "#be123c"]) {
      expect(hsvToHex(hexToHsv(c))).toBe(c);
    }
    expect(hexToHsv("#ff0000")).toEqual({ h: 0, s: 1, v: 1 });
    expect(hexToHsv("#00ffff").h).toBe(180);
  });
});
