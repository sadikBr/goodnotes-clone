import { describe, expect, it } from "vitest";
import { isRtl } from "./fonts";

describe("isRtl", () => {
  it("is true for Arabic paragraphs", () => {
    expect(isRtl("درس الرياضيات")).toBe(true);
    expect(isRtl("1. التاريخ")).toBe(true); // a list number isn't a strong letter
    expect(isRtl("  ؟ سؤال")).toBe(true);
  });
  it("is false for Latin paragraphs and text without letters", () => {
    expect(isRtl("Leçon 3")).toBe(false);
    expect(isRtl("12/09/2026")).toBe(false);
    expect(isRtl("")).toBe(false);
  });
  it("follows the first strong letter in mixed text", () => {
    expect(isRtl("Date : التاريخ")).toBe(false);
    expect(isRtl("التاريخ / Date")).toBe(true);
  });
});
