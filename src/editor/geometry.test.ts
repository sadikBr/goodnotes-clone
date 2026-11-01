import { describe, expect, it } from "vitest";
import { bounds, buildShape, convertShape, eraseElement, geomBox, handleAt, handlesFor, hitForSelect, hitTest, resizeElement, setLineCounter, insideLasso, isHeld, pointInPolygon, pressureWidth, translate, unionRects } from "./geometry";
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

  it("sizes text boxes by wrapped lines when a line counter is installed", () => {
    const text: El = { id: "t", z: 0, kind: "text", x: 0, y: 0, w: 100, text: "one two three four", fontSize: 20, color: "#000" };
    expect(bounds(text).h).toBeCloseTo(27);
    setLineCounter((t, _size, width) => Math.ceil((t.length * 10) / width));
    expect(bounds(text).h).toBeCloseTo(2 * 27);
    setLineCounter((t) => t.split("\n").length);
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

describe("buildShape", () => {
  it("normalises boxes dragged in any direction", () => {
    const r = buildShape("rectangle", [100, 100], [20, 40]);
    expect(r).toEqual({ kind: "rectangle", points: [[20, 40], [100, 40], [100, 100], [20, 100]] });
  });

  it("constrains to squares, circles and 45° lines", () => {
    const c = buildShape("ellipse", [0, 0], [100, 40], true);
    expect(c).toMatchObject({ kind: "ellipse", rx: 50, ry: 50 });
    const l = buildShape("line", [0, 0], [100, 8], true);
    expect(l && l.kind === "line" && Math.abs(l.points[1][1])).toBeLessThan(1e-9);
  });

  it("builds stars, diamonds, triangles and arrows", () => {
    expect(buildShape("star", [0, 0], [100, 100])).toMatchObject({ kind: "polygon" });
    expect((buildShape("star", [0, 0], [100, 100]) as { points: unknown[] }).points).toHaveLength(10);
    expect((buildShape("diamond", [0, 0], [100, 50]) as { points: unknown[] }).points).toHaveLength(4);
    expect(buildShape("triangle", [0, 0], [100, 50])).toMatchObject({ kind: "triangle" });
    expect(buildShape("arrow", [0, 0], [50, 50])).toMatchObject({ kind: "arrow" });
  });

  it("ignores taps and auto mode", () => {
    expect(buildShape("rectangle", [0, 0], [1, 1])).toBeNull();
    expect(buildShape("auto", [0, 0], [100, 100])).toBeNull();
  });
});

describe("shape editing", () => {
  const rect: El = { id: "r", z: 0, kind: "shape", color: "#000", width: 2, fill: null, shape: { kind: "rectangle", points: [[0, 0], [100, 0], [100, 50], [0, 50]] } };
  const line: El = { id: "l", z: 0, kind: "shape", color: "#000", width: 2, fill: null, shape: { kind: "arrow", points: [[0, 0], [100, 0]] } };

  it("offers 8 box handles for shapes and 2 for lines", () => {
    expect(handlesFor(rect)).toHaveLength(8);
    expect(handlesFor(line).map((h) => h.id)).toEqual(["p0", "p1"]);
    expect(handleAt(rect, 101, 51, 4)).toBe("se");
    expect(handleAt(rect, 50, 25, 4)).toBeNull();
  });

  it("resizes from a corner keeping the opposite corner fixed", () => {
    const r = resizeElement(rect, "se", [200, 150]);
    expect(geomBox(r)).toEqual({ x: 0, y: 0, w: 200, h: 150 });
    const w = resizeElement(rect, "w", [50, 999]);
    expect(geomBox(w)).toEqual({ x: 50, y: 0, w: 50, h: 50 });
  });

  it("keeps proportions with Shift and never inverts", () => {
    const r = resizeElement(rect, "se", [300, 60], true);
    expect(geomBox(r)).toEqual({ x: 0, y: 0, w: 300, h: 150 });
    const tiny = resizeElement(rect, "e", [-500, 0]);
    expect(geomBox(tiny).w).toBe(4);
  });

  it("resizes ellipses, images and sticky notes", () => {
    const e: El = { id: "e", z: 0, kind: "shape", color: "#000", width: 2, fill: null, shape: { kind: "ellipse", cx: 50, cy: 50, rx: 50, ry: 25 } };
    expect(resizeElement(e, "e", [200, 0])).toMatchObject({ shape: { cx: 100, rx: 100, ry: 25 } });
    const img: El = { id: "i", z: 0, kind: "image", x: 10, y: 10, w: 100, h: 100, src: "x" };
    expect(resizeElement(img, "nw", [0, 0])).toMatchObject({ x: 0, y: 0, w: 110, h: 110 });
  });

  it("moves line endpoints, snapping with Shift", () => {
    const moved = resizeElement(line, "p1", [80, 60]);
    expect(moved).toMatchObject({ shape: { points: [[0, 0], [80, 60]] } });
    const snapped = resizeElement(line, "p1", [100, 8], true) as { shape: { points: [number, number][] } };
    expect(Math.abs(snapped.shape.points[1][1])).toBeLessThan(1e-9);
  });

  it("selects closed shapes by tapping inside", () => {
    expect(hitForSelect(rect, 50, 25, 2)).toBe(true);
    expect(hitForSelect(line, 50, 25, 2)).toBe(false);
  });

  it("converts shapes to other kinds in the same box", () => {
    const e = convertShape(rect, "ellipse");
    expect(e).toMatchObject({ shape: { kind: "ellipse", cx: 50, cy: 25, rx: 50, ry: 25 } });
    expect(convertShape(line, "line")).toMatchObject({ shape: { kind: "line", points: [[0, 0], [100, 0]] } });
    expect(convertShape(line, "star")).toBe(line);
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

describe("partial erasing", () => {
  it("cuts a stroke in two where the eraser touches it", () => {
    const s = stroke([[0, 0], [100, 0]], 2); // only two points: segments are subdivided
    const pieces = eraseElement(s, 50, 0, 5);
    expect(pieces).toHaveLength(2);
    const [left, right] = pieces as StrokeEl[];
    expect(left.id).toBe("s");
    expect(right.id).not.toBe("s");
    expect(Math.max(...left.points.map((p) => p[0]))).toBeLessThan(45);
    expect(Math.min(...right.points.map((p) => p[0]))).toBeGreaterThan(55);
    expect(left.points[0]).toEqual([0, 0, 0.5]); // the untouched end stays exactly where it was
  });

  it("trims an end, removes fully covered strokes, and ignores far-away ones", () => {
    const s = stroke([[0, 0], [100, 0]], 2);
    const trimmed = eraseElement(s, 100, 0, 10) as StrokeEl[];
    expect(trimmed).toHaveLength(1);
    expect(Math.max(...trimmed[0].points.map((p) => p[0]))).toBeLessThan(90);
    expect(eraseElement(stroke([[0, 0], [3, 0]], 2), 1, 0, 10)).toEqual([]);
    expect(eraseElement(s, 50, 40, 5)).toBeNull();
  });

  it("opens up shape outlines and keeps an arrow's head on the tip piece", () => {
    const rect: El = { id: "r", z: 0, kind: "shape", color: "#000", width: 2, fill: "#00000033", shape: { kind: "rectangle", points: [[0, 0], [100, 0], [100, 50], [0, 50]] } };
    const pieces = eraseElement(rect, 50, 0, 4);
    expect(pieces?.length).toBeGreaterThanOrEqual(1);
    expect(pieces?.every((p) => p.kind === "shape" && p.shape.kind === "path" && p.fill === null)).toBe(true);
    const arrow: El = { id: "a", z: 0, kind: "shape", color: "#000", width: 2, fill: null, shape: { kind: "arrow", points: [[0, 0], [100, 0]] } };
    const cut = eraseElement(arrow, 50, 0, 4) as Extract<El, { kind: "shape" }>[];
    expect(cut.map((p) => p.shape.kind)).toEqual(["path", "arrow"]);
  });

  it("leaves text alone (handled as whole objects)", () => {
    const text: El = { id: "t", z: 0, kind: "text", x: 0, y: 0, w: 100, text: "hi", fontSize: 20, color: "#000" };
    expect(eraseElement(text, 5, 5, 10)).toBeNull();
  });
});
