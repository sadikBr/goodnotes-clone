import { describe, expect, it } from "vitest";
import { bounds, hitTest, insideLasso, isHeld, pointInPolygon, pressureWidth, translate, unionRects } from "./geometry";
import { fromStored, nextZ, toStored, type El, type StrokeEl } from "./model";

const stroke = (points: [number, number][], width = 4): StrokeEl => ({
  id: "s",
  z: 0,
  kind: "stroke",
  tool: "pen",
  color: "#000",
  width,
  points: points.map(([x, y]) => [x, y, 0.5]),
});

describe("geometry", () => {
  it("computes stroke bounds including half the width", () => {
    expect(bounds(stroke([[10, 10], [30, 20]], 4))).toEqual({ x: 8, y: 8, w: 24, h: 14 });
  });

  it("hit-tests strokes by distance to the polyline", () => {
    const s = stroke([[0, 0], [100, 0]], 2);
    expect(hitTest(s, 50, 5, 5)).toBe(true);
    expect(hitTest(s, 50, 20, 5)).toBe(false);
  });

  it("hit-tests ellipse outlines but not their empty interior", () => {
    const el: El = { id: "e", z: 0, kind: "shape", color: "#000", width: 2, fill: null, shape: { kind: "ellipse", cx: 0, cy: 0, rx: 50, ry: 50 } };
    expect(hitTest(el, 50, 0, 3)).toBe(true);
    expect(hitTest(el, 0, 0, 3)).toBe(false);
  });

  it("detects points in polygons", () => {
    const square: [number, number][] = [[0, 0], [10, 0], [10, 10], [0, 10]];
    expect(pointInPolygon(5, 5, square)).toBe(true);
    expect(pointInPolygon(15, 5, square)).toBe(false);
  });

  it("selects strokes mostly inside the lasso", () => {
    const lasso: [number, number][] = [[0, 0], [100, 0], [100, 100], [0, 100]];
    expect(insideLasso(stroke([[10, 10], [50, 50], [90, 90]]), lasso)).toBe(true);
    expect(insideLasso(stroke([[10, 10], [150, 150], [190, 190]]), lasso)).toBe(false);
  });

  it("translates every element kind", () => {
    const text: El = { id: "t", z: 0, kind: "text", x: 1, y: 2, w: 100, text: "hi", fontSize: 20, color: "#000" };
    expect(translate(text, 5, 5)).toMatchObject({ x: 6, y: 7 });
    const s = translate(stroke([[0, 0]]), 3, 4) as StrokeEl;
    expect(s.points[0]).toEqual([3, 4, 0.5]);
  });

  it("unions rectangles", () => {
    expect(unionRects([])).toBeNull();
    expect(unionRects([{ x: 0, y: 0, w: 10, h: 10 }, { x: 5, y: -5, w: 20, h: 5 }])).toEqual({ x: 0, y: -5, w: 25, h: 15 });
  });

  it("scales pen width with pressure", () => {
    expect(pressureWidth(4, 1)).toBeGreaterThan(pressureWidth(4, 0.2));
  });

  it("detects a held pen at the end of a stroke", () => {
    const pts: [number, number, number][] = [[0, 0, 0.5], [50, 0, 0.5], [100, 0, 0.5], [100.5, 0, 0.5], [100.6, 0, 0.5]];
    expect(isHeld([0, 100, 200, 500, 900], pts, 600, 2)).toBe(true);
    expect(isHeld([0, 100, 200, 250, 300], pts, 600, 2)).toBe(false);
  });
});

describe("model", () => {
  it("round-trips elements through storage and drops unknown kinds", () => {
    const s = stroke([[1, 2]]);
    const rows = [toStored({ ...s, z: 3 }), { id: "x", kind: "hologram", z: 0, data: {} }];
    const back = fromStored(rows);
    expect(back).toHaveLength(1);
    expect(back[0]).toMatchObject({ id: "s", kind: "stroke", z: 3 });
    expect(nextZ(back)).toBe(4);
    expect(nextZ([])).toBe(0);
  });
});
