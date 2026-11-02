import { describe, expect, it } from "vitest";
import { sharpCorners, SMOOTHING, smoothPoints, SpeedInk, speedSensitive, strokeOutline } from "./ink";
import type { Pt } from "./model";

const last = (a: number[]) => a[a.length - 1];

/** Runs the speed model along a straight line at `speed` units/ms. */
function run(speed: number, n = 60, raw: number | null = null): number[] {
  const ink = new SpeedInk();
  const out: number[] = [];
  for (let i = 0; i < n; i++) out.push(ink.next(i * speed * 8, 0, i * 8, raw));
  return out;
}

describe("speed ink", () => {
  it("slow strokes are fuller than fast ones", () => {
    const slow = last(run(0.1));
    const fast = last(run(2.5));
    expect(slow).toBeGreaterThan(0.7);
    expect(fast).toBeLessThan(0.4);
  });

  it("changes gradually, never jumping", () => {
    const ink = new SpeedInk();
    let prev = ink.next(0, 0, 0, null);
    let x = 0;
    for (let i = 1; i < 80; i++) {
      x += i < 40 ? 0.5 : 20; // sudden burst of speed
      const p = ink.next(x, 0, i * 8, null);
      expect(Math.abs(p - prev)).toBeLessThan(0.15);
      prev = p;
    }
  });

  it("with a stylus, pressure leads and speed only adjusts it", () => {
    const light = last(run(0.1, 60, 0.2));
    const firm = last(run(0.1, 60, 0.9));
    expect(firm).toBeGreaterThan(light * 3);
    const firmFast = last(run(2.5, 60, 0.9));
    expect(firmFast).toBeLessThan(firm);
    expect(firmFast).toBeGreaterThan(firm * 0.6);
  });

  it("applies only to the fountain and brush pens", () => {
    expect(speedSensitive("fountain")).toBe(true);
    expect(speedSensitive("brush")).toBe(true);
    expect(speedSensitive(undefined)).toBe(true);
    expect(speedSensitive("ballpoint")).toBe(false);
    expect(speedSensitive("pencil")).toBe(false);
    expect(speedSensitive("calligraphy")).toBe(false);
  });

  it("outlines are closed polygons around the stroke, wider where the pressure is higher", () => {
    const pts: Pt[] = Array.from({ length: 40 }, (_, i) => [i * 5, 0, i < 20 ? 0.2 : 0.9]);
    const [outline] = strokeOutline(pts, "fountain", 4);
    expect(outline.length).toBeGreaterThan(20);
    const halfWidth = (x0: number, x1: number) => Math.max(...outline.filter((p) => p[0] > x0 && p[0] < x1).map((p) => Math.abs(p[1])));
    expect(halfWidth(130, 170)).toBeGreaterThan(halfWidth(30, 70) * 1.5);
  });

  it("finds sharp corners but not gentle curves or jitter", () => {
    const v: Pt[] = [];
    for (let i = 0; i <= 20; i++) v.push([i * 2, -i * 6, 0.5]); // up
    for (let i = 1; i <= 20; i++) v.push([40 + i * 2, -120 + i * 6, 0.5]); // straight back down
    expect(sharpCorners(v, 3)).toEqual([20]);
    const arc: Pt[] = Array.from({ length: 60 }, (_, i) => [Math.cos(i / 20) * 100, Math.sin(i / 20) * 100, 0.5]);
    expect(sharpCorners(arc, 3)).toEqual([]);
    const jitter: Pt[] = Array.from({ length: 60 }, (_, i) => [i * 3, (i % 2) * 0.6, 0.5]);
    expect(sharpCorners(jitter, 3)).toEqual([]);
    expect(strokeOutline(v, "fountain", 3)).toHaveLength(2);
  });
});

describe("smoothing levels", () => {
  const zig: Pt[] = Array.from({ length: 30 }, (_, i) => [i * 3, i % 2 ? 4 : -4, 0.5]);
  const wobble = (pts: Pt[]) => pts.slice(5).reduce((s, p) => s + Math.abs(p[1]), 0);
  it("Off keeps the points exactly as written", () => {
    expect(smoothPoints(zig, [], SMOOTHING.off)).toEqual(zig);
  });
  it("stronger smoothing evens out more of the shake", () => {
    const light = wobble(smoothPoints(zig, [], SMOOTHING.light));
    const normal = wobble(smoothPoints(zig, [], SMOOTHING.normal));
    const strong = wobble(smoothPoints(zig, [], SMOOTHING.strong));
    expect(light).toBeLessThan(wobble(zig));
    expect(normal).toBeLessThan(light);
    expect(strong).toBeLessThan(normal);
  });
});
