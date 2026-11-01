import { describe, expect, it } from "vitest";
import type { P2 } from "../api";
import { beautify, bracePath, detectBrace, recognizeShape, smoothPath } from "./beautify";
import { isClosedShape, transformShape } from "./geometry";

/** Deterministic wobble so tests mimic a slightly shaky hand. */
const wobble = (i: number, amp: number) => (((Math.sin(i * 12.9898) * 43758.5453) % 1) + 1) % 1 * amp;

function polyline(vertices: P2[], perEdge: number, closed: boolean, amp: number): P2[] {
  const out: P2[] = [];
  const edges = closed ? vertices.length : vertices.length - 1;
  let k = 0;
  for (let e = 0; e < edges; e++) {
    const a = vertices[e];
    const b = vertices[(e + 1) % vertices.length];
    for (let s = 0; s < perEdge; s++) {
      const t = s / perEdge;
      out.push([a[0] + t * (b[0] - a[0]) + wobble(k, amp), a[1] + t * (b[1] - a[1]) + wobble(k + 7, amp)]);
      k++;
    }
  }
  const end = closed ? vertices[0] : vertices[vertices.length - 1];
  out.push([end[0] + 2, end[1] - 1.5]);
  return out;
}

function circle(cx: number, cy: number, rx: number, ry: number, n: number, amp: number): P2[] {
  const out: P2[] = [];
  for (let i = 0; i <= n; i++) {
    const t = (i / n) * Math.PI * 2 * 0.97;
    out.push([cx + rx * Math.cos(t) + wobble(i, amp), cy + ry * Math.sin(t) + wobble(i + 3, amp)]);
  }
  return out;
}

/** Largest change of direction between consecutive segments, in degrees. */
function maxTurn(pts: P2[]): number {
  let max = 0;
  for (let i = 2; i < pts.length; i++) {
    const a = Math.atan2(pts[i - 1][1] - pts[i - 2][1], pts[i - 1][0] - pts[i - 2][0]);
    const b = Math.atan2(pts[i][1] - pts[i - 1][1], pts[i][0] - pts[i - 1][0]);
    let d = Math.abs(b - a);
    if (d > Math.PI) d = 2 * Math.PI - d;
    max = Math.max(max, (d * 180) / Math.PI);
  }
  return max;
}

describe("recognizeShape", () => {
  it("recognises lines, rectangles, triangles and circles", () => {
    expect(recognizeShape(polyline([[0, 0], [200, 40]], 30, false, 2))?.kind).toBe("line");
    const rect = recognizeShape(polyline([[10, 10], [210, 12], [208, 130], [12, 128]], 25, true, 2.5));
    expect(rect?.kind).toBe("rectangle");
    if (rect?.kind === "rectangle") expect(rect.points[0][1]).toBe(rect.points[1][1]);
    expect(recognizeShape(polyline([[0, 200], [100, 0], [200, 200]], 30, true, 2))?.kind).toBe("triangle");
    const c = recognizeShape(circle(100, 100, 80, 76, 80, 2));
    expect(c?.kind).toBe("ellipse");
    if (c?.kind === "ellipse") expect(c.rx).toBe(c.ry);
  });

  it("leaves scribbles and tiny marks alone", () => {
    const scribble: P2[] = Array.from({ length: 60 }, (_, i) => [i * 3, i % 2 ? 40 : 0]);
    expect(recognizeShape(scribble)).toBeNull();
    expect(recognizeShape([[0, 0], [2, 2], [3, 1], [4, 4]])).toBeNull();
  });
});

describe("detectBrace", () => {
  // A rough "{": ends on the right, sharp point on the left half-way down.
  const roughBrace = (): P2[] => {
    const pts: P2[] = [];
    for (let i = 0; i <= 60; i++) {
      const t = i / 60;
      const y = t * 200;
      const fromTip = Math.abs(t - 0.5);
      const x = fromTip < 0.08 ? 10 + (fromTip / 0.08) * 12 : t < 0.08 || t > 0.92 ? 22 + (Math.min(t, 1 - t) < 0.04 ? 14 : 6) : 22;
      pts.push([x + wobble(i, 1.5), y]);
    }
    return pts;
  };

  it("recognises an opening brace and redraws it cleanly", () => {
    const b = detectBrace(roughBrace());
    expect(b?.kind).toBe("path");
    if (b?.kind !== "path") return;
    expect(b.closed).toBe(false);
    const xs = b.points.map((p) => p[0]);
    const ys = b.points.map((p) => p[1]);
    // Tip on the left at mid-height; ends on the right at top and bottom.
    const tip = b.points[xs.indexOf(Math.min(...xs))];
    expect(tip[1]).toBeGreaterThan(70);
    expect(tip[1]).toBeLessThan(130);
    expect(b.points[0][0]).toBeCloseTo(Math.max(...xs), 0);
    expect(Math.min(...ys)).toBeCloseTo(0, 0);
    expect(Math.max(...ys)).toBeCloseTo(200, 0);
  });

  it("recognises closing and sideways braces", () => {
    const closing = roughBrace().map(([x, y]) => [60 - x, y] as P2);
    const c = detectBrace(closing);
    expect(c?.kind).toBe("path");
    if (c?.kind === "path") expect(Math.max(...c.points.map((p) => p[0]))).toBeGreaterThan(c.points[0][0]);
    const under = roughBrace().map(([x, y]) => [y, x] as P2);
    expect(detectBrace(under)?.kind).toBe("path");
  });

  it("does not mistake a parenthesis or a straight line for a brace", () => {
    const paren: P2[] = Array.from({ length: 50 }, (_, i) => {
      const t = i / 49;
      return [30 - Math.sin(t * Math.PI) * 20, t * 200];
    });
    expect(detectBrace(paren)).toBeNull();
    expect(detectBrace(polyline([[0, 0], [0, 200]], 30, false, 1))).toBeNull();
  });

  it("builds a symmetric brace template", () => {
    const pts = bracePath(0, 100, 50, 0, 20);
    expect(pts[0]).toEqual([0, 20]);
    expect(pts[pts.length - 1][0]).toBeCloseTo(100);
  });
});

describe("smoothPath", () => {
  it("turns hand-drawn graph axes into two straight lines with a sharp corner", () => {
    const axes = polyline([[0, 0], [3, 200], [260, 204]], 40, false, 2.5);
    const { points } = smoothPath(axes);
    expect(points.length).toBeLessThanOrEqual(4);
    expect(points[0][0]).toBeCloseTo(axes[0][0], 0);
  });

  it("smooths a jagged curve", () => {
    const jagged: P2[] = Array.from({ length: 120 }, (_, i) => [i * 3, 100 - 60 * Math.sin(i / 20) + wobble(i, 5)]);
    const { points } = smoothPath(jagged);
    expect(maxTurn(points)).toBeLessThan(maxTurn(jagged) / 2);
    // Keeps the overall shape: endpoints stay put.
    expect(points[0][0]).toBeCloseTo(jagged[0][0], 0);
    expect(points[points.length - 1][0]).toBeCloseTo(jagged[jagged.length - 1][0], 0);
  });

  it("closes loops that end where they started", () => {
    const blob: P2[] = Array.from({ length: 80 }, (_, i) => {
      const t = (i / 79) * Math.PI * 2;
      return [100 + Math.cos(t) * (60 + 20 * Math.sin(3 * t)), 100 + Math.sin(t) * 60];
    });
    expect(smoothPath(blob).closed).toBe(true);
  });
});

describe("beautify", () => {
  it("prefers basic shapes, then braces, then smoothing", () => {
    expect(beautify(circle(100, 100, 80, 76, 80, 2))?.kind).toBe("ellipse");
    const wave: P2[] = Array.from({ length: 80 }, (_, i) => [i * 4, 50 + Math.sin(i / 8) * 30 + wobble(i, 3)]);
    expect(beautify(wave)?.kind).toBe("path");
    expect(beautify([[0, 0], [1, 1]])).toBeNull();
  });
});

describe("transformShape", () => {
  it("rotates and scales around a pivot", () => {
    const line = { kind: "line" as const, points: [[0, 0], [10, 0]] as P2[] };
    const t = transformShape(line, [0, 0], Math.PI / 2, 2);
    if (t.kind !== "line") throw new Error("kind changed");
    expect(t.points[1][0]).toBeCloseTo(0);
    expect(t.points[1][1]).toBeCloseTo(20);
  });

  it("keeps circles as circles and turns rotated ellipses into closed paths", () => {
    const c = transformShape({ kind: "ellipse", cx: 0, cy: 0, rx: 10, ry: 10 }, [5, 0], 1, 1.5);
    expect(c).toMatchObject({ kind: "ellipse", rx: 15, ry: 15 });
    const e = transformShape({ kind: "ellipse", cx: 0, cy: 0, rx: 20, ry: 10 }, [0, 0], 0.6, 1);
    expect(e.kind).toBe("path");
    expect(isClosedShape(e)).toBe(true);
  });

  it("translates", () => {
    const r = transformShape({ kind: "rectangle", points: [[0, 0], [1, 0], [1, 1], [0, 1]] }, [0, 0], 0, 1, 5, 7);
    if (r.kind === "rectangle") expect(r.points[0]).toEqual([5, 7]);
  });
});

describe("smoothPath on shaky curves", () => {
  it("does not turn hand wobble into corners", () => {
    const shaky: P2[] = Array.from({ length: 91 }, (_, i) => {
      const t = i / 90;
      return [i * 4.2 + wobble(i, 2), 300 - 160 * (0.45 * Math.sin(t * 3.4) + 0.7 * t * t) + wobble(i + 9, 4) + (i % 7 === 0 ? 4 : 0)];
    });
    const { points } = smoothPath(shaky);
    expect(maxTurn(points)).toBeLessThan(25);
  });
});
