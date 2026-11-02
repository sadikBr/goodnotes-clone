import { describe, expect, it } from "vitest";
import { Laser } from "./laser";

describe("laser", () => {
  it("keeps every stroke until cleared, and skips tiny movements", () => {
    const l = new Laser();
    l.begin([0, 0]);
    expect(l.extend([0.2, 0], 0.75)).toBe(false);
    expect(l.extend([20, 0], 0.75)).toBe(true);
    l.end();
    l.begin([0, 10]);
    l.extend([30, 10], 0.75);
    expect(l.drawing).toBe(true);
    // Clearing after a fade keeps a stroke that is still being drawn.
    l.clearStrokes();
    expect(l.strokeCount).toBe(1);
    l.end();
    l.clearStrokes();
    expect(l.strokeCount).toBe(0);
  });

  it("dot mode never records strokes", () => {
    const l = new Laser();
    l.mode = "dot";
    l.begin([0, 0]);
    expect(l.extend([10, 10], 0.75)).toBe(false);
    l.end();
    expect(l.strokeCount).toBe(0);
    expect(l.dot).toEqual([10, 10]);
  });
});
