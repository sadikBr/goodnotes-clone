import type { P2 } from "../api";
import type { El, Pt } from "./model";

export interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

export function distToSegment(px: number, py: number, ax: number, ay: number, bx: number, by: number): number {
  const dx = bx - ax;
  const dy = by - ay;
  const len2 = dx * dx + dy * dy;
  let t = len2 === 0 ? 0 : ((px - ax) * dx + (py - ay) * dy) / len2;
  t = Math.max(0, Math.min(1, t));
  return Math.hypot(px - (ax + t * dx), py - (ay + t * dy));
}

function distToPolyline(px: number, py: number, pts: P2[], closed: boolean): number {
  if (pts.length === 1) return Math.hypot(px - pts[0][0], py - pts[0][1]);
  let best = Infinity;
  const n = closed ? pts.length : pts.length - 1;
  for (let i = 0; i < n; i++) {
    const a = pts[i];
    const b = pts[(i + 1) % pts.length];
    best = Math.min(best, distToSegment(px, py, a[0], a[1], b[0], b[1]));
  }
  return best;
}

export function pointInPolygon(x: number, y: number, poly: P2[]): boolean {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, yi] = poly[i];
    const [xj, yj] = poly[j];
    if (yi > y !== yj > y && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}

function rectOfPoints(pts: ArrayLike<ArrayLike<number>>, pad = 0): Rect {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (let i = 0; i < pts.length; i++) {
    const p = pts[i];
    x0 = Math.min(x0, p[0]);
    y0 = Math.min(y0, p[1]);
    x1 = Math.max(x1, p[0]);
    y1 = Math.max(y1, p[1]);
  }
  if (!isFinite(x0)) return { x: 0, y: 0, w: 0, h: 0 };
  return { x: x0 - pad, y: y0 - pad, w: x1 - x0 + 2 * pad, h: y1 - y0 + 2 * pad };
}

/** Approximate text box height from line count. */
export function textHeight(text: string, fontSize: number): number {
  return Math.max(1, text.split("\n").length) * fontSize * 1.35;
}

export function bounds(el: El): Rect {
  switch (el.kind) {
    case "stroke":
      return rectOfPoints(el.points, el.width / 2);
    case "shape": {
      const s = el.shape;
      if (s.kind === "ellipse") return { x: s.cx - s.rx - el.width / 2, y: s.cy - s.ry - el.width / 2, w: 2 * s.rx + el.width, h: 2 * s.ry + el.width };
      return rectOfPoints(s.points, el.width / 2);
    }
    case "text":
      return { x: el.x, y: el.y, w: el.w, h: textHeight(el.text, el.fontSize) };
    case "sticky":
    case "image":
      return { x: el.x, y: el.y, w: el.w, h: el.h };
  }
}

export function unionRects(rects: Rect[]): Rect | null {
  if (rects.length === 0) return null;
  const x0 = Math.min(...rects.map((r) => r.x));
  const y0 = Math.min(...rects.map((r) => r.y));
  const x1 = Math.max(...rects.map((r) => r.x + r.w));
  const y1 = Math.max(...rects.map((r) => r.y + r.h));
  return { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
}

export function rectContains(r: Rect, x: number, y: number): boolean {
  return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

/** Whether a point (with radius) touches an element, for the eraser. */
export function hitTest(el: El, x: number, y: number, radius: number): boolean {
  switch (el.kind) {
    case "stroke":
      return distToPolyline(x, y, el.points.map((p) => [p[0], p[1]] as P2), false) <= radius + el.width / 2;
    case "shape": {
      const s = el.shape;
      if (s.kind === "ellipse") {
        // Distance to the ellipse outline, approximated in normalised space.
        const nx = (x - s.cx) / Math.max(s.rx, 1e-6);
        const ny = (y - s.cy) / Math.max(s.ry, 1e-6);
        const d = Math.abs(Math.hypot(nx, ny) - 1) * Math.min(s.rx, s.ry);
        return d <= radius + el.width / 2 || (el.fill !== null && Math.hypot(nx, ny) <= 1);
      }
      const closed = s.kind !== "line";
      if (distToPolyline(x, y, s.points, closed) <= radius + el.width / 2) return true;
      return closed && el.fill !== null && pointInPolygon(x, y, s.points);
    }
    default: {
      const r = bounds(el);
      return rectContains({ x: r.x - radius, y: r.y - radius, w: r.w + 2 * radius, h: r.h + 2 * radius }, x, y);
    }
  }
}

/** Lasso selection: strokes need most of their points inside; boxes their centre. */
export function insideLasso(el: El, lasso: P2[]): boolean {
  if (lasso.length < 3) return false;
  const sample = (pts: ArrayLike<ArrayLike<number>>) => {
    let inside = 0;
    for (let i = 0; i < pts.length; i++) if (pointInPolygon(pts[i][0], pts[i][1], lasso)) inside++;
    return pts.length > 0 && inside / pts.length >= 0.6;
  };
  if (el.kind === "stroke") return sample(el.points);
  if (el.kind === "shape" && el.shape.kind !== "ellipse") return sample(el.shape.points);
  const r = bounds(el);
  return pointInPolygon(r.x + r.w / 2, r.y + r.h / 2, lasso);
}

export function translate(el: El, dx: number, dy: number): El {
  switch (el.kind) {
    case "stroke":
      return { ...el, points: el.points.map(([x, y, p]) => [x + dx, y + dy, p] as Pt) };
    case "shape": {
      const s = el.shape;
      if (s.kind === "ellipse") return { ...el, shape: { ...s, cx: s.cx + dx, cy: s.cy + dy } };
      return { ...el, shape: { ...s, points: s.points.map(([x, y]) => [x + dx, y + dy] as P2) } };
    }
    default:
      return { ...el, x: el.x + dx, y: el.y + dy };
  }
}

/** Stroke width at a point, scaled by pressure for the pen. */
export function pressureWidth(base: number, pressure: number): number {
  const p = Math.max(0.05, Math.min(1, pressure));
  return base * (0.45 + 0.85 * p);
}

/** Whether the pointer sat (nearly) still for the last `ms` of the stroke. */
export function isHeld(times: number[], pts: Pt[], ms: number, tolerance: number): boolean {
  if (pts.length < 2 || times.length !== pts.length) return false;
  const end = times[times.length - 1];
  const last = pts[pts.length - 1];
  for (let i = pts.length - 1; i >= 0; i--) {
    if (Math.hypot(pts[i][0] - last[0], pts[i][1] - last[1]) > tolerance) return end - times[i] >= ms;
  }
  return end - times[0] >= ms;
}
