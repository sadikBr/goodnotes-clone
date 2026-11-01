import type { P2, RecognizedShape } from "../api";
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

/**
 * Counts wrapped lines for a text box. The renderer installs a measurer based
 * on real font metrics; without one (e.g. in unit tests) only explicit line
 * breaks are counted.
 */
type LineCounter = (text: string, fontSize: number, width: number, font: string | undefined) => number;
let countLines: LineCounter = (text) => text.split("\n").length;

export function setLineCounter(counter: LineCounter) {
  countLines = counter;
}

/** Text box height including word-wrapped lines. */
export function textHeight(text: string, fontSize: number, width = Infinity, font?: string): number {
  return Math.max(1, countLines(text, fontSize, width, font)) * fontSize * 1.35;
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
      return { x: el.x, y: el.y, w: el.w, h: textHeight(el.text, el.fontSize, el.w, el.font) };
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
      const closed = isClosedShape(s);
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

export type ShapeKind = "auto" | "line" | "arrow" | "rectangle" | "ellipse" | "triangle" | "diamond" | "star";

/**
 * Builds the chosen shape from a drag between `a` and `b`. With `constrain`
 * (Shift), boxes become squares/circles and lines snap to 45° steps.
 * Returns null for "auto" (recognition) or a drag too small to see.
 */
export function buildShape(kind: ShapeKind, a: P2, b: P2, constrain = false): RecognizedShape | null {
  let [x1, y1] = b;
  const [x0, y0] = a;
  if (kind === "auto") return null;
  if (kind === "line" || kind === "arrow") {
    if (constrain) {
      const len = Math.hypot(x1 - x0, y1 - y0);
      const step = Math.PI / 4;
      const ang = Math.round(Math.atan2(y1 - y0, x1 - x0) / step) * step;
      x1 = x0 + Math.cos(ang) * len;
      y1 = y0 + Math.sin(ang) * len;
    }
    if (Math.hypot(x1 - x0, y1 - y0) < 3) return null;
    return { kind, points: [[x0, y0], [x1, y1]] };
  }
  let w = x1 - x0;
  let h = y1 - y0;
  if (constrain) {
    const side = Math.max(Math.abs(w), Math.abs(h));
    w = Math.sign(w || 1) * side;
    h = Math.sign(h || 1) * side;
  }
  if (Math.abs(w) < 3 || Math.abs(h) < 3) return null;
  const left = Math.min(x0, x0 + w);
  const top = Math.min(y0, y0 + h);
  const right = left + Math.abs(w);
  const bottom = top + Math.abs(h);
  const cx = (left + right) / 2;
  const cy = (top + bottom) / 2;
  switch (kind) {
    case "rectangle":
      return { kind: "rectangle", points: [[left, top], [right, top], [right, bottom], [left, bottom]] };
    case "ellipse":
      return { kind: "ellipse", cx, cy, rx: (right - left) / 2, ry: (bottom - top) / 2 };
    case "triangle":
      return { kind: "triangle", points: [[cx, top], [right, bottom], [left, bottom]] };
    case "diamond":
      return { kind: "polygon", points: [[cx, top], [right, cy], [cx, bottom], [left, cy]] };
    case "star": {
      const rx = (right - left) / 2;
      const ry = (bottom - top) / 2;
      const points: P2[] = [];
      for (let i = 0; i < 10; i++) {
        const r = i % 2 === 0 ? 1 : 0.42;
        const t = -Math.PI / 2 + (i * Math.PI) / 5;
        points.push([cx + Math.cos(t) * rx * r, cy + Math.sin(t) * ry * r]);
      }
      return { kind: "polygon", points };
    }
  }
}

// ------------------------------------------------------- editing handles

export type Handle = "nw" | "n" | "ne" | "e" | "se" | "s" | "sw" | "w" | "p0" | "p1";

/** Elements that can be reshaped with handles. */
export function isResizable(el: El): boolean {
  return el.kind === "shape" || el.kind === "image" || el.kind === "sticky";
}

function isSegment(el: El): el is Extract<El, { kind: "shape" }> & { shape: { kind: "line" | "arrow"; points: P2[] } } {
  return el.kind === "shape" && (el.shape.kind === "line" || el.shape.kind === "arrow");
}

/** Geometric box of an element (without stroke width). */
export function geomBox(el: El): Rect {
  if (el.kind === "shape") {
    const s = el.shape;
    if (s.kind === "ellipse") return { x: s.cx - s.rx, y: s.cy - s.ry, w: 2 * s.rx, h: 2 * s.ry };
    return rectOfPoints(s.points);
  }
  return bounds(el);
}

export function handlesFor(el: El): { id: Handle; x: number; y: number }[] {
  if (!isResizable(el)) return [];
  if (isSegment(el)) {
    const [a, b] = [el.shape.points[0], el.shape.points[el.shape.points.length - 1]];
    return [
      { id: "p0", x: a[0], y: a[1] },
      { id: "p1", x: b[0], y: b[1] },
    ];
  }
  const r = geomBox(el);
  const [x0, y0, x1, y1, cx, cy] = [r.x, r.y, r.x + r.w, r.y + r.h, r.x + r.w / 2, r.y + r.h / 2];
  return [
    { id: "nw", x: x0, y: y0 },
    { id: "n", x: cx, y: y0 },
    { id: "ne", x: x1, y: y0 },
    { id: "e", x: x1, y: cy },
    { id: "se", x: x1, y: y1 },
    { id: "s", x: cx, y: y1 },
    { id: "sw", x: x0, y: y1 },
    { id: "w", x: x0, y: cy },
  ];
}

export function handleAt(el: El, x: number, y: number, radius: number): Handle | null {
  for (const hdl of handlesFor(el)) if (Math.hypot(hdl.x - x, hdl.y - y) <= radius) return hdl.id;
  return null;
}

export const HANDLE_CURSORS: Record<Handle, string> = {
  nw: "nwse-resize",
  se: "nwse-resize",
  ne: "nesw-resize",
  sw: "nesw-resize",
  n: "ns-resize",
  s: "ns-resize",
  e: "ew-resize",
  w: "ew-resize",
  p0: "move",
  p1: "move",
};

const MIN_SIZE = 4;

/**
 * Returns `el` reshaped by dragging `handle` to `p`. Box handles keep the
 * opposite side fixed; `keepAspect` (Shift) scales proportionally from a
 * corner. Segment handles move one endpoint (Shift snaps to 45°).
 */
export function resizeElement(el: El, handle: Handle, p: P2, keepAspect = false): El {
  if (isSegment(el)) {
    const pts = el.shape.points.map((q) => [q[0], q[1]] as P2);
    const idx = handle === "p0" ? 0 : pts.length - 1;
    const other = pts[handle === "p0" ? pts.length - 1 : 0];
    let [x, y] = p;
    if (keepAspect) {
      const len = Math.hypot(x - other[0], y - other[1]);
      const step = Math.PI / 4;
      const ang = Math.round(Math.atan2(y - other[1], x - other[0]) / step) * step;
      x = other[0] + Math.cos(ang) * len;
      y = other[1] + Math.sin(ang) * len;
    }
    pts[idx] = [x, y];
    return { ...el, shape: { ...el.shape, points: pts } };
  }
  if (!isResizable(el) || handle === "p0" || handle === "p1") return el;
  const box = geomBox(el);
  let x0 = box.x;
  let y0 = box.y;
  let x1 = box.x + box.w;
  let y1 = box.y + box.h;
  // Move the dragged edges, never past the opposite edge.
  if (handle.includes("w")) x0 = Math.min(p[0], x1 - MIN_SIZE);
  if (handle.includes("e")) x1 = Math.max(p[0], x0 + MIN_SIZE);
  if (handle.includes("n")) y0 = Math.min(p[1], y1 - MIN_SIZE);
  if (handle.includes("s")) y1 = Math.max(p[1], y0 + MIN_SIZE);
  if (keepAspect && handle.length === 2 && box.w > 0 && box.h > 0) {
    const scale = Math.max((x1 - x0) / box.w, (y1 - y0) / box.h);
    const w = box.w * scale;
    const hh = box.h * scale;
    if (handle.includes("w")) x0 = x1 - w;
    else x1 = x0 + w;
    if (handle.includes("n")) y0 = y1 - hh;
    else y1 = y0 + hh;
  }
  const sx = box.w > 0 ? (x1 - x0) / box.w : 1;
  const sy = box.h > 0 ? (y1 - y0) / box.h : 1;
  const map = ([x, y]: P2): P2 => [x0 + (x - box.x) * sx, y0 + (y - box.y) * sy];
  if (el.kind === "shape") {
    const s = el.shape;
    if (s.kind === "ellipse") return { ...el, shape: { ...s, cx: (x0 + x1) / 2, cy: (y0 + y1) / 2, rx: (x1 - x0) / 2, ry: (y1 - y0) / 2 } };
    return { ...el, shape: { ...s, points: s.points.map(map) } };
  }
  if (el.kind === "image" || el.kind === "sticky") return { ...el, x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
  return el;
}

/** Hit test for tap-to-select: closed shapes also count when tapped inside. */
export function hitForSelect(el: El, x: number, y: number, radius: number): boolean {
  if (hitTest(el, x, y, radius)) return true;
  if (el.kind !== "shape") return false;
  const s = el.shape;
  if (s.kind === "ellipse") return Math.hypot((x - s.cx) / Math.max(s.rx, 1e-6), (y - s.cy) / Math.max(s.ry, 1e-6)) <= 1;
  if (!isClosedShape(s)) return false;
  return pointInPolygon(x, y, s.points);
}

/** Re-creates a shape as another kind inside the same box (line ↔ arrow keep their ends). */
export function convertShape(el: El, kind: ShapeKind): El {
  if (el.kind !== "shape" || kind === "auto") return el;
  if (isSegment(el)) {
    if (kind === "line" || kind === "arrow") return { ...el, shape: { kind, points: el.shape.points } };
    return el;
  }
  if (kind === "line" || kind === "arrow") return el;
  const r = geomBox(el);
  const shape = buildShape(kind, [r.x, r.y], [r.x + r.w, r.y + r.h]);
  return shape ? { ...el, shape } : el;
}

/** Whether a shape outline is closed (can be filled and tapped inside). */
export function isClosedShape(s: RecognizedShape): boolean {
  if (s.kind === "line" || s.kind === "arrow") return false;
  if (s.kind === "path") return s.closed;
  return true;
}

/**
 * Rotates by `angle` (radians) and scales by `scale` around `pivot`, then
 * translates by (dx, dy). Rotated non-circular ellipses become polygons so
 * they stay editable with the box handles.
 */
export function transformShape(s: RecognizedShape, pivot: P2, angle: number, scale: number, dx = 0, dy = 0): RecognizedShape {
  const cos = Math.cos(angle) * scale;
  const sin = Math.sin(angle) * scale;
  const map = ([x, y]: P2): P2 => [pivot[0] + (x - pivot[0]) * cos - (y - pivot[1]) * sin + dx, pivot[1] + (x - pivot[0]) * sin + (y - pivot[1]) * cos + dy];
  if (s.kind === "ellipse") {
    const [cx, cy] = map([s.cx, s.cy]);
    const circle = Math.abs(s.rx - s.ry) < 0.5;
    if (circle || Math.abs(angle) < 0.035) return { ...s, cx, cy, rx: s.rx * scale, ry: s.ry * scale };
    const points: P2[] = [];
    for (let i = 0; i < 72; i++) {
      const t = (i / 72) * Math.PI * 2;
      points.push(map([s.cx + Math.cos(t) * s.rx, s.cy + Math.sin(t) * s.ry]));
    }
    return { kind: "path", points: [...points, points[0]], closed: true };
  }
  return { ...s, points: s.points.map(map) } as RecognizedShape;
}
