import { describe, expect, it } from "vitest";
import { continueList, currentList, toggleList } from "./textFormat";

describe("lists", () => {
  it("turns bullets on and off, skipping blank lines", () => {
    const on = toggleList("Cells\n\nTissues", "bullet");
    expect(on).toBe("• Cells\n\n• Tissues");
    expect(currentList(on)).toBe("bullet");
    expect(toggleList(on, "bullet")).toBe("Cells\n\nTissues");
  });

  it("numbers items in order and switches between list kinds", () => {
    const numbered = toggleList("• a\n• b\n• c", "number");
    expect(numbered).toBe("1. a\n2. b\n3. c");
    expect(currentList(numbered)).toBe("number");
    expect(currentList("1. a\nplain")).toBeNull();
  });

  it("starts a list in an empty box with the first marker", () => {
    expect(toggleList("", "bullet")).toBe("• ");
    expect(toggleList("", "number")).toBe("1. ");
  });

  it("continues bullets and numbers on Enter", () => {
    const text = "• first";
    expect(continueList(text, text.length)).toEqual({ value: "• first\n• ", caret: 10 });
    const n = "1. one";
    expect(continueList(n, n.length)).toEqual({ value: "1. one\n2. ", caret: 10 });
  });

  it("ends the list on an empty item and ignores plain text", () => {
    const text = "• a\n• ";
    expect(continueList(text, text.length)).toEqual({ value: "• a\n", caret: 4 });
    expect(continueList("plain", 5)).toBeNull();
  });
});
