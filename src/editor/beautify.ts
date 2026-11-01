// Ink beautification: turns rough hand-drawn strokes into clean geometry.
//
//  1. recognizeShape: line, triangle, rectangle, polygon, ellipse/circle
//  2. detectBrace:    curly braces in any of the four orientations
//  3. smoothPath:     everything else; straight runs become straight lines,
//                     sharp turns stay sharp corners, curved runs become
//                     smooth splines (graphs, arcs, waves, brackets...)
//
// Everything is synchronous so the result can be previewed live while the
// pen is still down.

import type { P2, RecognizedShape } from "../api";

const dist = (a: P2, b: P2) => Math.hypot(a[0] - b[0], a[1] - b[1]);

function segDist(p: P2, a: P2, b: P2): number {
  const dx = b[0] - a[0];
  const dy = b[1] - a[1];
  const len2 = dx * dx + dy * dy;
  if (len2 === 0) return dist(p, a);
  const t = Math.max(0, Math.min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / len2));
  return dist(p, [a[0] + t * dx, a[1] + t * dy]);
}

export function pathLength(pts: P2[]): number {
  let total = 0;
  for (let i = 1; i < pts.length; i++) total += dist(pts[i - 1], pts[i]);
  return total;
}

function bbox(pts: P2[]) {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const [x, y] of pts) {
    x0 = Math.min(x0, x);
    y0 = Math.min(y0, y);
    x1 = Math.max(x1, x);
    y1 = Math.max(y1, y);
  }
  return { x0, y0, x1, y1, w: x1 - x0, h: y1 - y0, diag: Math.hypot(x1 - x0, y1 - y0) };
}

/** Ramer–Douglas–Peucker; returns the indices of kept points. */
function rdpIndices(pts: P2[], eps: number): number[] {
  const n = pts.length;
  if (n < 3) return pts.map((_, i) => i);
  const keep = new Uint8Array(n);
  keep[0] = keep[n - 1] = 1;
  const stack: [number, number][] = [[0, n - 1]];
  while (stack.length) {
    const [s, e] = stack.pop()!;
    let maxD = 0;
    let idx = s;
    for (let i = s + 1; i < e; i++) {
      const d = segDist(pts[i], pts[s], pts[e]);
      if (d > maxD) {
        maxD = d;
        idx = i;
      }
    }
    if (maxD > eps) {
      keep[idx] = 1;
      stack.push([s, idx], [idx, e]);
    }
  }
  const out: number[] = [];
  keep.forEach((k, i) => k && out.push(i));
  return out;
}

/** Resamples to `n` points evenly spaced along the path. */
export function resampleCount(pts: P2[], n: number): P2[] {
  const total = pathLength(pts);
  if (pts.length < 2 || total === 0 || n < 2) return pts.slice();
  return resampleSpacing(pts, total / (n - 1)).slice(0, n);
}

/** Resamples to points `step` apart along the path (endpoints kept). */
export function resampleSpacing(pts: P2[], step: number): P2[] {
  if (pts.length < 2 || step <= 0) return pts.slice();
  const out: P2[] = [pts[0]];
  let acc = 0;
  let prev = pts[0];
  let i = 1;
  while (i < pts.length) {
    const cur = pts[i];
    const d = dist(prev, cur);
    if (d > 0 && acc + d >= step) {
      const t = (step - acc) / d;
      const q: P2 = [prev[0] + t * (cur[0] - prev[0]), prev[1] + t * (cur[1] - prev[1])];
      out.push(q);
      prev = q;
      acc = 0;
    } else {
      acc += d;
      prev = cur;
      i++;
    }
  }
  const last = pts[pts.length - 1];
  if (dist(out[out.length - 1], last) > step * 0.25) out.push(last);
  else out[out.length - 1] = last;
  return out;
}

/** Interior angle at b, in degrees (180 = straight). */
function angleAt(a: P2, b: P2, c: P2): number {
  const v1x = a[0] - b[0], v1y = a[1] - b[1];
  const v2x = c[0] - b[0], v2y = c[1] - b[1];
  const n = Math.hypot(v1x, v1y) * Math.hypot(v2x, v2y);
  if (n === 0) return 180;
  return (Math.acos(Math.max(-1, Math.min(1, (v1x * v2x + v1y * v2y) / n))) * 180) / Math.PI;
}

// ------------------------------------------------------- basic shapes

function closedCorners(pts: P2[], eps: number, minGap: number): P2[] {
  const start = pts[0];
  let far = 0;
  pts.forEach((p, i) => dist(p, start) > dist(pts[far], start) && (far = i));
  const first = pts.slice(0, far + 1);
  const second = [...pts.slice(far), start];
  const corners = rdpIndices(first, eps).map((i) => first[i]);
  corners.pop();
  corners.push(...rdpIndices(second, eps).map((i) => second[i]));
  corners.pop();
  const merged: P2[] = [];
  for (const c of corners) if (!merged.length || dist(merged[merged.length - 1], c) >= minGap) merged.push(c);
  while (merged.length > 2 && dist(merged[0], merged[merged.length - 1]) < minGap) merged.pop();
  for (;;) {
    const n = merged.length;
    if (n <= 3) break;
    const i = merged.findIndex((_, k) => angleAt(merged[(k + n - 1) % n], merged[k], merged[(k + 1) % n]) > 160);
    if (i < 0) break;
    merged.splice(i, 1);
  }
  return merged;
}

function ellipseFit(pts: P2[]) {
  const b = bbox(pts);
  const cx = (b.x0 + b.x1) / 2;
  const cy = (b.y0 + b.y1) / 2;
  const rx = Math.max(b.w / 2, 1e-6);
  const ry = Math.max(b.h / 2, 1e-6);
  const err = pts.reduce((s, [x, y]) => s + Math.abs(Math.hypot((x - cx) / rx, (y - cy) / ry) - 1), 0) / pts.length;
  return { err, cx, cy, rx, ry };
}

function regulariseRectangle(c: P2[]): P2[] {
  const edge: P2 = [c[1][0] - c[0][0], c[1][1] - c[0][1]];
  const ang = (((Math.atan2(edge[1], edge[0]) * 180) / Math.PI) % 90 + 90) % 90;
  if (ang < 8 || ang > 82) {
    const b = bbox(c);
    return [[b.x0, b.y0], [b.x1, b.y0], [b.x1, b.y1], [b.x0, b.y1]];
  }
  const cx = c.reduce((s, p) => s + p[0], 0) / 4;
  const cy = c.reduce((s, p) => s + p[1], 0) / 4;
  const len = Math.hypot(edge[0], edge[1]);
  const u: P2 = [edge[0] / len, edge[1] / len];
  const v: P2 = [-u[1], u[0]];
  const w = (dist(c[0], c[1]) + dist(c[2], c[3])) / 4;
  const h = (dist(c[1], c[2]) + dist(c[3], c[0])) / 4;
  const cross = (c[1][0] - c[0][0]) * (c[2][1] - c[1][1]) - (c[1][1] - c[0][1]) * (c[2][0] - c[1][0]);
  const s = cross >= 0 ? 1 : -1;
  const corner = (a: number, b: number): P2 => [cx + a * w * u[0] + b * s * h * v[0], cy + a * w * u[1] + b * s * h * v[1]];
  return [corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1)];
}

/** Recognises a rough stroke as a basic geometric shape, or returns null. */
export function recognizeShape(raw: P2[]): RecognizedShape | null {
  if (raw.length < 4) return null;
  const b = bbox(raw);
  if (b.diag < 12) return null;
  const length = pathLength(raw);
  const first = raw[0];
  const last = raw[raw.length - 1];
  if (dist(first, last) / length > 0.94) return { kind: "line", points: [first, last] };
  const closed = dist(first, last) < 0.22 * b.diag && length > 2 * b.diag * 0.9;
  if (!closed) return null;
  const pts = resampleCount(raw, 96);
  const corners = closedCorners(pts, 0.06 * b.diag, 0.12 * b.diag);
  const e = ellipseFit(pts);
  if (corners.length === 3) return { kind: "triangle", points: corners };
  if (corners.length === 4) {
    const right = corners.every((_, i) => Math.abs(angleAt(corners[(i + 3) % 4], corners[i], corners[(i + 1) % 4]) - 90) < 22);
    if (right) return { kind: "rectangle", points: regulariseRectangle(corners) };
    if (e.err < 0.06) return { kind: "ellipse", cx: e.cx, cy: e.cy, rx: e.rx, ry: e.ry };
    return { kind: "polygon", points: corners };
  }
  if (e.err < 0.12) {
    if (Math.abs(e.rx - e.ry) / Math.max(e.rx, e.ry) < 0.12) {
      const r = (e.rx + e.ry) / 2;
      return { kind: "ellipse", cx: e.cx, cy: e.cy, rx: r, ry: r };
    }
    return { kind: "ellipse", cx: e.cx, cy: e.cy, rx: e.rx, ry: e.ry };
  }
  if (corners.length >= 5 && corners.length <= 8) return { kind: "polygon", points: corners };
  return null;
}

// ------------------------------------------------------------- braces

function quad(a: P2, c: P2, b: P2, steps: number, out: P2[]) {
  for (let i = 1; i <= steps; i++) {
    const t = i / steps;
    const m = 1 - t;
    out.push([m * m * a[0] + 2 * m * t * c[0] + t * t * b[0], m * m * a[1] + 2 * m * t * c[1] + t * t * b[1]]);
  }
}

/**
 * Builds a clean brace path. Coordinates are (u, v): u runs along the brace,
 * v across it. `tipV` is where the point sticks out, `endV` where the ends
 * curl to, `tipU` the position of the point along the brace.
 */
export function bracePath(u0: number, u1: number, tipU: number, tipV: number, endV: number): P2[] {
  const spine = (tipV + endV) / 2;
  const q = Math.min(Math.abs(endV - spine) * 1.6, (u1 - u0) * 0.12);
  const out: P2[] = [[u0, endV]];
  quad([u0, endV], [u0, spine], [u0 + q, spine], 10, out);
  out.push([Math.max(u0 + q, tipU - q), spine]);
  quad([tipU - q, spine], [tipU, spine], [tipU, tipV], 10, out);
  quad([tipU, tipV], [tipU, spine], [tipU + q, spine], 10, out);
  out.push([Math.min(u1 - q, tipU + q), spine], [u1 - q, spine]);
  quad([u1 - q, spine], [u1, spine], [u1, endV], 10, out);
  return out;
}

/** Detects a curly brace ({, }, or the same turned sideways) and redraws it. */
export function detectBrace(raw: P2[]): RecognizedShape | null {
  if (raw.length < 6) return null;
  const b = bbox(raw);
  const vertical = b.h >= 2.2 * b.w && b.h > 30;
  const horizontal = b.w >= 2.2 * b.h && b.w > 30;
  if (!vertical && !horizontal) return null;
  // Local coordinates: u along the brace, v across it.
  const toUV = ([x, y]: P2): P2 => (vertical ? [y, x] : [x, y]);
  const fromUV = ([u, v]: P2): P2 => (vertical ? [v, u] : [u, v]);
  const pts = resampleCount(raw.map(toUV), 80);
  const lb = bbox(pts);
  const L = lb.w;
  const span = lb.h;
  if (span < 4) return null;
  // Travels along u steadily from one end to the other.
  const travel = pts[pts.length - 1][0] - pts[0][0];
  if (Math.abs(travel) < 0.8 * L) return null;
  const dir = Math.sign(travel);
  let back = 0;
  for (let i = 1; i < pts.length; i++) if ((pts[i][0] - pts[i - 1][0]) * dir < -0.2) back++;
  if (back > pts.length * 0.12) return null;
  // Both ends on one side; the tip sticks out on the other side mid-way.
  const vMid = (lb.y0 + lb.y1) / 2;
  const endsHigh = pts[0][1] > vMid && pts[pts.length - 1][1] > vMid;
  const endsLow = pts[0][1] < vMid && pts[pts.length - 1][1] < vMid;
  if (!endsHigh && !endsLow) return null;
  let tip = 0;
  pts.forEach((p, i) => {
    if (endsHigh ? p[1] < pts[tip][1] : p[1] > pts[tip][1]) tip = i;
  });
  const tipPos = (pts[tip][0] - lb.x0) / L;
  if (tipPos < 0.28 || tipPos > 0.72) return null;
  // The tip must be a sharp point, not the round belly of a parenthesis.
  const k = Math.max(2, Math.round(pts.length * 0.08));
  if (tip - k < 0 || tip + k >= pts.length) return null;
  if (angleAt(pts[tip - k], pts[tip], pts[tip + k]) > 135) return null;
  const tipV = endsHigh ? lb.y0 : lb.y1;
  const endV = endsHigh ? lb.y1 : lb.y0;
  return { kind: "path", points: bracePath(lb.x0, lb.x1, pts[tip][0], tipV, endV).map(fromUV), closed: false };
}

// ---------------------------------------------------------- smoothing

function movingAverage(pts: P2[], radius: number): P2[] {
  if (pts.length < 3 || radius < 1) return pts.slice();
  return pts.map((p, i) => {
    if (i === 0 || i === pts.length - 1) return p;
    const r = Math.min(radius, i, pts.length - 1 - i);
    let sx = 0, sy = 0;
    for (let j = i - r; j <= i + r; j++) {
      sx += pts[j][0];
      sy += pts[j][1];
    }
    return [sx / (2 * r + 1), sy / (2 * r + 1)];
  });
}

/** Centripetal Catmull–Rom spline through `ctrl`, sampled densely. */
function catmullRom(ctrl: P2[], samples: number): P2[] {
  if (ctrl.length < 3) return ctrl.slice();
  const p = [ctrl[0], ...ctrl, ctrl[ctrl.length - 1]];
  const out: P2[] = [ctrl[0]];
  const tj = (ti: number, a: P2, b: P2) => ti + Math.max(1e-4, Math.sqrt(dist(a, b)));
  for (let i = 1; i < p.length - 2; i++) {
    const [p0, p1, p2, p3] = [p[i - 1], p[i], p[i + 1], p[i + 2]];
    const t0 = 0;
    const t1 = tj(t0, p0, p1);
    const t2 = tj(t1, p1, p2);
    const t3 = tj(t2, p2, p3);
    for (let s = 1; s <= samples; s++) {
      const t = t1 + ((t2 - t1) * s) / samples;
      const lerp = (a: P2, b: P2, ta: number, tb: number): P2 => {
        const w = tb === ta ? 0 : (t - ta) / (tb - ta);
        return [a[0] + (b[0] - a[0]) * w, a[1] + (b[1] - a[1]) * w];
      };
      const a1 = lerp(p0, p1, t0, t1);
      const a2 = lerp(p1, p2, t1, t2);
      const a3 = lerp(p2, p3, t2, t3);
      const b1 = lerp(a1, a2, t0, t2);
      const b2 = lerp(a2, a3, t1, t3);
      out.push(lerp(b1, b2, t1, t2));
    }
  }
  return out;
}

/**
 * Smooths a freehand stroke while keeping its intent: straight runs become
 * exact straight lines, sharp turns stay as corners, and curved runs become
 * smooth splines.
 */
export function smoothPath(raw: P2[]): { points: P2[]; closed: boolean } {
  const b = bbox(raw);
  if (raw.length < 3 || b.diag < 6) return { points: raw.slice(), closed: false };
  const step = Math.max(1.5, Math.min(4, b.diag / 120));
  let pts = movingAverage(resampleSpacing(raw, step), 3);
  const total = pathLength(pts);
  const closed = dist(pts[0], pts[pts.length - 1]) < Math.max(6, 0.08 * b.diag) && total > 2.2 * Math.max(b.w, b.h);
  if (closed) pts = [...pts.slice(0, -1), pts[0]];

  // Corners: key points where the direction turns sharply.
  const key = rdpIndices(pts, Math.max(1.5, 0.02 * b.diag));
  // Judge each turn over a wide stretch so hand wobble isn't mistaken for a corner.
  const win = Math.max(3, Math.round(Math.max(16, 0.05 * b.diag) / step));
  const corners = [0];
  for (const i of key) {
    if (i < win || i > pts.length - 1 - win) continue;
    const a = pts[i - win];
    const c = pts[i + win];
    if (angleAt(a, pts[i], c) < 125 && i - corners[corners.length - 1] > win) corners.push(i);
  }
  corners.push(pts.length - 1);

  const out: P2[] = [pts[0]];
  for (let s = 1; s < corners.length; s++) {
    const seg = pts.slice(corners[s - 1], corners[s] + 1);
    const a = seg[0];
    const z = seg[seg.length - 1];
    const chord = dist(a, z);
    const maxDev = seg.reduce((m, p) => Math.max(m, segDist(p, a, z)), 0);
    if (seg.length <= 2 || maxDev < Math.max(1.5, 0.04 * chord)) {
      out.push(z); // straight run
      continue;
    }
    const ctrlIdx = rdpIndices(seg, Math.max(2, 0.025 * b.diag));
    const curve = catmullRom(
      ctrlIdx.map((i) => seg[i]),
      8,
    );
    out.push(...curve.slice(1));
  }
  return { points: out, closed };
}

/**
 * Full beautification for the Shape tool's Auto mode: a basic shape, a
 * brace, or a smoothed path. Returns null for taps and tiny marks.
 */
export function beautify(raw: P2[]): RecognizedShape | null {
  if (raw.length < 2) return null;
  const shape = recognizeShape(raw);
  if (shape) return shape;
  const brace = detectBrace(raw);
  if (brace) return brace;
  const b = bbox(raw);
  if (b.diag < 6) return null;
  const smooth = smoothPath(raw);
  return { kind: "path", points: smooth.points, closed: smooth.closed };
}
