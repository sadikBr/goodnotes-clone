import { describe, expect, it } from "vitest";
import { Laser, LASER_FADE_MS, LASER_HOLD_MS } from "./laser";

describe("laser", () => {
  it("keeps everything while writing, fades together a second after the pen stops", () => {
    const l = new Laser();
    l.begin([0, 0], 0);
    l.extend([50, 0], 5000, 0.5); // a long stroke: nothing fades while drawing
    expect(l.opacity(5000)).toBe(1);
    l.end(5000);
    l.begin([0, 20], 5600); // second stroke within the second
    l.extend([40, 20], 5700, 0.5);
    l.end(5700);
    expect(l.strokeCount).toBe(2);
    expect(l.opacity(5700 + LASER_HOLD_MS - 1)).toBe(1);
    expect(l.opacity(5700 + LASER_HOLD_MS + LASER_FADE_MS / 2)).toBeCloseTo(0.5, 5);
    expect(l.animating(5700 + LASER_HOLD_MS + LASER_FADE_MS / 2)).toBe(true);
    // Fully faded: cleared.
    expect(l.animating(5700 + LASER_HOLD_MS + LASER_FADE_MS + 1)).toBe(false);
    expect(l.strokeCount).toBe(0);
  });

  it("drawing again during the fade brings everything back", () => {
    const l = new Laser();
    l.begin([0, 0], 0);
    l.extend([10, 0], 100, 0.5);
    l.end(100);
    const midFade = 100 + LASER_HOLD_MS + LASER_FADE_MS / 2;
    l.begin([0, 10], midFade);
    expect(l.opacity(midFade)).toBe(1);
    expect(l.strokeCount).toBe(2);
  });

  it("dot mode never records strokes", () => {
    const l = new Laser();
    l.mode = "dot";
    l.begin([0, 0], 0);
    l.extend([10, 10], 10, 0.5);
    l.end(20);
    expect(l.strokeCount).toBe(0);
    expect(l.animating(30)).toBe(false);
  });
});
