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
  if (dist(first, last) / length > 0.94) return { kind: "line", points: [first, snapToAxis(first, last)] };
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

// ------------------------------------------------------- helpers

/** Snaps the direction a→b to exactly horizontal/vertical when within 4°. */
export function snapToAxis(a: P2, b: P2, toleranceDeg = 4): P2 {
  const dx = b[0] - a[0];
  const dy = b[1] - a[1];
  const ang = Math.abs((Math.atan2(dy, dx) * 180) / Math.PI);
  const offH = Math.min(ang, 180 - ang);
  const offV = Math.abs(90 - ang);
  if (offH <= toleranceDeg) return [b[0], a[1]];
  if (offV <= toleranceDeg) return [a[0], b[1]];
  return b;
}

/**
 * Taubin smoothing (alternating shrink / inflate Laplacian steps): removes
 * hand jitter without shrinking curves the way plain averaging does.
 * The first and last points stay fixed.
 */
export function taubin(pts: P2[], iterations: number): P2[] {
  let p = pts.map((q) => [q[0], q[1]] as P2);
  const n = p.length;
  if (n < 3) return p;
  for (let k = 0; k < iterations; k++) {
    for (const f of [0.5, -0.53]) {
      const q = p.map((v) => [v[0], v[1]] as P2);
      for (let i = 1; i < n - 1; i++) {
        q[i][0] = p[i][0] + f * ((p[i - 1][0] + p[i + 1][0]) / 2 - p[i][0]);
        q[i][1] = p[i][1] + f * ((p[i - 1][1] + p[i + 1][1]) / 2 - p[i][1]);
      }
      p = q;
    }
  }
  return p;
}

function maxDeviation(seg: P2[]): number {
  const a = seg[0];
  const z = seg[seg.length - 1];
  return seg.reduce((m, p) => Math.max(m, segDist(p, a, z)), 0);
}

/** Least-squares circle (Kåsa fit). Returns null for degenerate input. */
function fitCircle(pts: P2[]): { cx: number; cy: number; r: number } | null {
  const n = pts.length;
  if (n < 5) return null;
  const mx = pts.reduce((s, p) => s + p[0], 0) / n;
  const my = pts.reduce((s, p) => s + p[1], 0) / n;
  let sxx = 0, syy = 0, sxy = 0, sxz = 0, syz = 0, sz = 0;
  for (const [px, py] of pts) {
    const x = px - mx;
    const y = py - my;
    const z = x * x + y * y;
    sxx += x * x;
    syy += y * y;
    sxy += x * y;
    sxz += x * z;
    syz += y * z;
    sz += z;
  }
  // With centred data the normal equations reduce to a 2x2 system.
  const det = sxx * syy - sxy * sxy;
  if (Math.abs(det) < 1e-9) return null;
  const a = (sxz * syy - syz * sxy) / det;
  const b = (syz * sxx - sxz * sxy) / det;
  const cx = a / 2;
  const cy = b / 2;
  const r = Math.sqrt(cx * cx + cy * cy + sz / n);
  if (!isFinite(r) || r <= 0) return null;
  return { cx: cx + mx, cy: cy + my, r };
}

/** If the run is close to a circular arc, returns that arc (endpoints kept). */
export function fitArc(seg: P2[]): P2[] | null {
  const c = fitCircle(seg);
  if (!c) return null;
  const length = pathLength(seg);
  if (c.r > length * 2.5) return null; // too flat to call an arc
  const resid = seg.reduce((s, p) => s + Math.abs(Math.hypot(p[0] - c.cx, p[1] - c.cy) - c.r), 0) / seg.length;
  if (resid > Math.max(1.2, 0.035 * c.r)) return null;
  // Sweep along the run, which must turn steadily in one direction.
  let total = 0;
  let prev = Math.atan2(seg[0][1] - c.cy, seg[0][0] - c.cx);
  const a0 = prev;
  let back = 0;
  for (let i = 1; i < seg.length; i++) {
    const a = Math.atan2(seg[i][1] - c.cy, seg[i][0] - c.cx);
    let d = a - prev;
    if (d > Math.PI) d -= 2 * Math.PI;
    if (d < -Math.PI) d += 2 * Math.PI;
    if (total !== 0 && Math.sign(d) !== Math.sign(total) && Math.abs(d) > 0.02) back++;
    total += d;
    prev = a;
  }
  const sweep = Math.abs(total) * (180 / Math.PI);
  if (sweep < 35 || sweep > 330 || back > seg.length * 0.1) return null;
  const steps = Math.max(8, Math.ceil(sweep / 4));
  const out: P2[] = [];
  for (let i = 0; i <= steps; i++) {
    const t = a0 + (total * i) / steps;
    out.push([c.cx + Math.cos(t) * c.r, c.cy + Math.sin(t) * c.r]);
  }
  out[0] = seg[0];
  out[out.length - 1] = seg[seg.length - 1];
  return out;
}

/** Cubic B-spline basis values for clamped uniform knots (Cox–de Boor). */
function bsplineBasis(t: number, m: number): Float64Array {
  // m control points, degree 3: knots 0,0,0,0, 1/(m-3) … , 1,1,1,1
  const k = 3;
  const nk = m + k + 1;
  const knots = new Float64Array(nk);
  for (let i = 0; i < nk; i++) knots[i] = Math.min(1, Math.max(0, (i - k) / (m - k)));
  let N = new Float64Array(nk - 1);
  const tt = Math.min(t, 1 - 1e-9);
  for (let i = 0; i < nk - 1; i++) N[i] = knots[i] <= tt && tt < knots[i + 1] ? 1 : 0;
  for (let d = 1; d <= k; d++) {
    const next = new Float64Array(nk - 1 - d);
    for (let i = 0; i < next.length; i++) {
      const a = knots[i + d] - knots[i];
      const b = knots[i + d + 1] - knots[i + 1];
      next[i] = (a > 0 ? ((tt - knots[i]) / a) * N[i] : 0) + (b > 0 ? ((knots[i + d + 1] - tt) / b) * N[i + 1] : 0);
    }
    N = next;
  }
  return N; // length m
}

/** Solves a small dense linear system (Gaussian elimination, partial pivoting). */
function solve(A: number[][], b: number[]): number[] | null {
  const n = b.length;
  const M = A.map((row, i) => [...row, b[i]]);
  for (let c = 0; c < n; c++) {
    let piv = c;
    for (let r = c + 1; r < n; r++) if (Math.abs(M[r][c]) > Math.abs(M[piv][c])) piv = r;
    if (Math.abs(M[piv][c]) < 1e-12) return null;
    [M[c], M[piv]] = [M[piv], M[c]];
    for (let r = c + 1; r < n; r++) {
      const f = M[r][c] / M[c][c];
      if (f) for (let j = c; j <= n; j++) M[r][j] -= f * M[c][j];
    }
  }
  const x = new Array(n).fill(0);
  for (let r = n - 1; r >= 0; r--) {
    let acc = M[r][n];
    for (let j = r + 1; j < n; j++) acc -= M[r][j] * x[j];
    x[r] = acc / M[r][r];
  }
  return x;
}

/**
 * Fits a smooth cubic B-spline through a run by least squares (ends pinned,
 * with a light bending penalty), so wobbles shorter than the control-point
 * spacing disappear while the overall curve is kept. Sampled densely.
 */
export function fitSmoothCurve(seg: P2[], spacing: number): P2[] {
  const L = pathLength(seg);
  const m = Math.max(4, Math.min(48, Math.round(L / spacing) + 3));
  const n = seg.length;
  if (n < m + 2) return seg.slice();
  // Arc-length parameter for each sample.
  const t: number[] = [0];
  for (let i = 1; i < n; i++) t.push(t[i - 1] + dist(seg[i - 1], seg[i]));
  const B = t.map((v) => bsplineBasis(v / L, m));
  const free = m - 2; // first and last control points are the run's ends
  const AtA = Array.from({ length: free }, () => new Array(free).fill(0));
  const rx = new Array(free).fill(0);
  const ry = new Array(free).fill(0);
  const p0 = seg[0];
  const pn = seg[n - 1];
  for (let s = 0; s < n; s++) {
    const row = B[s];
    const fx = seg[s][0] - row[0] * p0[0] - row[m - 1] * pn[0];
    const fy = seg[s][1] - row[0] * p0[1] - row[m - 1] * pn[1];
    for (let i = 1; i < m - 1; i++) {
      if (!row[i]) continue;
      rx[i - 1] += row[i] * fx;
      ry[i - 1] += row[i] * fy;
      for (let j = 1; j < m - 1; j++) if (row[j]) AtA[i - 1][j - 1] += row[i] * row[j];
    }
  }
  // Bending penalty on second differences of the control polygon.
  const lambda = 0.02 * (n / m);
  for (let i = 0; i < m - 2; i++) {
    const idx = [i, i + 1, i + 2];
    const w = [1, -2, 1];
    for (let a = 0; a < 3; a++) {
      for (let b = 0; b < 3; b++) {
        const ia = idx[a] - 1;
        const ib = idx[b] - 1;
        const v = lambda * w[a] * w[b];
        if (ia >= 0 && ia < free && ib >= 0 && ib < free) AtA[ia][ib] += v;
        else if (ia >= 0 && ia < free) {
          const fixed = idx[b] === 0 ? p0 : pn;
          rx[ia] -= v * fixed[0];
          ry[ia] -= v * fixed[1];
        }
      }
    }
  }
  const cx = solve(AtA, rx);
  const cy = solve(AtA, ry);
  if (!cx || !cy) return seg.slice();
  const ctrl: P2[] = [p0, ...cx.map((x, i) => [x, cy[i]] as P2), pn];
  const samples = Math.max(16, Math.min(400, Math.round(L / 2.5)));
  const out: P2[] = [];
  for (let i = 0; i <= samples; i++) {
    const basis = bsplineBasis(i / samples, m);
    let x = 0;
    let y = 0;
    for (let j = 0; j < m; j++) {
      x += basis[j] * ctrl[j][0];
      y += basis[j] * ctrl[j][1];
    }
    out.push([x, y]);
  }
  out[0] = p0;
  out[out.length - 1] = pn;
  return out;
}

/** Turning angle (degrees, 180 = straight) at each point, over ±w points. */
function turning(pts: P2[], w: number): number[] {
  return pts.map((p, i) => (i < w || i >= pts.length - w ? 180 : angleAt(pts[i - w], p, pts[i + w])));
}

/** Indices of sharp corners: clear local minima of the turning angle. */
function findCorners(pts: P2[], w: number, maxAngle: number): number[] {
  const ang = turning(pts, w);
  const out: number[] = [];
  for (let i = w; i < pts.length - w; i++) {
    if (ang[i] >= maxAngle) continue;
    let isMin = true;
    for (let j = Math.max(0, i - w); j <= Math.min(pts.length - 1, i + w); j++) {
      if (ang[j] < ang[i] || (ang[j] === ang[i] && j < i)) {
        isMin = false;
        break;
      }
    }
    if (isMin) out.push(i);
  }
  return out;
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
  const half = Math.abs(endV - spine);
  // Curl radius: proportional to the brace's width, but never more than a
  // fifth of each half so the straight spine stays visible.
  const q = Math.max(1, Math.min(half * 1.4, (tipU - u0) * 0.4, (u1 - tipU) * 0.4));
  const out: P2[] = [[u0, endV]];
  quad([u0, endV], [u0, spine], [u0 + q, spine], 12, out);
  out.push([tipU - q, spine]);
  quad([tipU - q, spine], [tipU, spine], [tipU, tipV], 12, out);
  quad([tipU, tipV], [tipU, spine], [tipU + q, spine], 12, out);
  out.push([u1 - q, spine]);
  quad([u1 - q, spine], [u1, spine], [u1, endV], 12, out);
  return out;
}

function distToPolyline(p: P2, line: P2[]): number {
  let best = Infinity;
  for (let i = 1; i < line.length; i++) best = Math.min(best, segDist(p, line[i - 1], line[i]));
  return best;
}

/**
 * Detects a curly brace ({, }, or the same turned sideways) by fitting clean
 * brace templates to the stroke and keeping the best match if it is close
 * enough. Tolerant of thin, lopsided or quickly drawn braces.
 */
export function detectBrace(raw: P2[]): RecognizedShape | null {
  if (raw.length < 6) return null;
  // A brace's two ends sit on the same side, so the line between them gives
  // its axis even when it is drawn at a slant. Work in that rotated frame:
  // u runs along the brace, v across it.
  const first = raw[0];
  const last = raw[raw.length - 1];
  const axisLen = dist(first, last);
  if (axisLen < 30) return null;
  const ang = Math.atan2(last[1] - first[1], last[0] - first[0]);
  const cos = Math.cos(ang);
  const sin = Math.sin(ang);
  const toUV = ([x, y]: P2): P2 => [(x - first[0]) * cos + (y - first[1]) * sin, -(x - first[0]) * sin + (y - first[1]) * cos];
  const fromUV = ([u, v]: P2): P2 => [first[0] + u * cos - v * sin, first[1] + u * sin + v * cos];
  const pts = taubin(resampleCount(raw.map(toUV), 90), 4);
  const lb = bbox(pts);
  const L = lb.w;
  const W = lb.h;
  if (W < 4 || L < 1.8 * W) return null;
  // Must travel from one end to the other without doubling back much.
  const travel = pts[pts.length - 1][0] - pts[0][0];
  if (Math.abs(travel) < 0.75 * L) return null;
  const dir = Math.sign(travel);
  let back = 0;
  for (let i = 1; i < pts.length; i++) if ((pts[i][0] - pts[i - 1][0]) * dir < -0.25) back++;
  if (back > pts.length * 0.15) return null;

  let best: { err: number; path: P2[] } | null = null;
  for (const tipLow of [true, false]) {
    const tipV = tipLow ? lb.y0 : lb.y1;
    const endV = tipLow ? lb.y1 : lb.y0;
    // Tip position: the most extreme point in the middle part of the stroke.
    let tip = -1;
    pts.forEach((p, i) => {
      const pos = (p[0] - lb.x0) / L;
      if (pos < 0.2 || pos > 0.8) return;
      if (tip < 0 || (tipLow ? p[1] < pts[tip][1] : p[1] > pts[tip][1])) tip = i;
    });
    if (tip < 0) continue;
    // The tip must stick out past the spine, and both ends curl the other way.
    const reach = Math.abs(pts[tip][1] - endV);
    if (reach < 0.75 * W) continue;
    const ends = [pts[0], pts[pts.length - 1]];
    if (ends.some((e) => Math.abs(e[1] - endV) > 0.55 * W)) continue;
    const path = bracePath(lb.x0, lb.x1, pts[tip][0], tipV, endV);
    // Symmetric average distance between stroke and template, relative to width.
    const d1 = pts.reduce((s, p) => s + distToPolyline(p, path), 0) / pts.length;
    const dense = resampleCount(path, 90);
    const d2 = dense.reduce((s, p) => s + distToPolyline(p, pts), 0) / dense.length;
    const err = (d1 + d2) / 2 / W;
    // A plain "V" (or an L seen along its diagonal) also has two ends on one
    // side and a point in the middle. A real brace has a flat spine and curled
    // ends, so it must fit the brace template clearly better than two lines.
    const vee: P2[] = [pts[0], pts[tip], pts[pts.length - 1]];
    const v1 = pts.reduce((acc, p) => acc + distToPolyline(p, vee), 0) / pts.length;
    const veeDense = resampleCount(vee, 60);
    const v2 = veeDense.reduce((acc, p) => acc + distToPolyline(p, pts), 0) / veeDense.length;
    const veeErr = (v1 + v2) / 2 / W;
    if (err > veeErr * 0.75) continue;
    if (!best || err < best.err) best = { err, path };
  }
  if (!best || best.err > 0.17) return null;
  return { kind: "path", points: best.path.map(fromUV), closed: false };
}

// ---------------------------------------------------------- smoothing

/**
 * Smooths a freehand stroke while keeping its intent: jitter is removed
 * without flattening curves, straight runs become exact straight lines
 * (snapped to horizontal/vertical when nearly so), sharp turns stay corners,
 * and near-circular runs become true arcs.
 */
export function smoothPath(raw: P2[]): { points: P2[]; closed: boolean } {
  const b = bbox(raw);
  if (raw.length < 3 || b.diag < 6) return { points: raw.slice(), closed: false };
  const step = Math.max(1, Math.min(3, b.diag / 150));
  let pts = resampleSpacing(raw, step);
  if (pts.length < 4) return { points: raw.slice(), closed: false };
  const total = pathLength(pts);
  const closed = dist(pts[0], pts[pts.length - 1]) < Math.max(6, 0.08 * b.diag) && total > 2.2 * Math.max(b.w, b.h);
  if (closed) pts = [...pts.slice(0, -1), pts[0]];

  // Corners are found on a lightly smoothed copy so jitter doesn't count.
  const w = Math.max(3, Math.round(Math.max(12, 0.035 * b.diag) / step));
  const corners = [0, ...findCorners(taubin(pts, 6), w, 120), pts.length - 1];

  // How strongly to smooth: proportional to the drawing's size. Jitter is
  // filtered first (Taubin), then curves are fitted with control points
  // `spacing` apart, which sets the smallest wiggle that survives.
  const scale = Math.max(2.5, Math.min(12, 0.02 * b.diag));
  const spacing = Math.max(14, Math.min(45, 0.09 * b.diag));
  const iterations = Math.max(6, Math.min(150, Math.round(2 * (scale / step) ** 2)));

  interface Run {
    straight: boolean;
    points: P2[];
  }
  const runs: Run[] = [];
  for (let s = 1; s < corners.length; s++) {
    const seg = pts.slice(corners[s - 1], corners[s] + 1);
    if (seg.length < 2) continue;
    const a = seg[0];
    const z = seg[seg.length - 1];
    const chord = dist(a, z);
    // Hand jitter is a couple of pixels whatever the length, so short runs
    // get a fixed allowance and long ones a proportional one.
    if (seg.length <= 3 || maxDeviation(seg) < Math.max(2.5, 0.035 * chord)) {
      runs.push({ straight: true, points: [a, z] });
      continue;
    }
    const sm = taubin(seg, iterations);
    if (maxDeviation(sm) < Math.max(2, 0.04 * chord)) {
      runs.push({ straight: true, points: [a, z] });
      continue;
    }
    const arc = fitArc(sm);
    if (arc) {
      runs.push({ straight: false, points: arc });
      continue;
    }
    runs.push({ straight: false, points: fitSmoothCurve(sm, spacing) });
  }

  // Assemble, snapping straight runs of open paths to horizontal/vertical and
  // carrying any resulting offset into the following runs.
  const out: P2[] = [runs.length ? runs[0].points[0] : pts[0]];
  let off: P2 = [0, 0];
  for (const run of runs) {
    const shifted = run.points.map(([x, y]) => [x + off[0], y + off[1]] as P2);
    if (run.straight && !closed) {
      const start = out[out.length - 1];
      const end = snapToAxis(start, shifted[1]);
      off = [off[0] + end[0] - shifted[1][0], off[1] + end[1] - shifted[1][1]];
      out.push(end);
    } else {
      out.push(...shifted.slice(1));
    }
  }
  if (closed) out[out.length - 1] = out[0];
  return { points: out, closed };
}

/**
 * Full beautification for the Shape tool's Auto mode: a basic shape, a
 * brace, a single arc, or a smoothed path. Returns null for taps and tiny marks.
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
