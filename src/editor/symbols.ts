// Hand-drawn maths symbols (∫ Σ √ ∞ = ≡ ≠ ⇒ ⇔ → ↔ ≤ ≥ ± ∀ ∃), recognised
// with the $P point-cloud recogniser (Vatavu, Anthony & Wobbrock, 2012): the
// drawing's points are compared with a clean model of each symbol whatever
// the stroke order or direction, so multi-stroke symbols work too. The same
// models then draw the clean symbol in the space the strokes took.

import type { P2, RecognizedShape } from "../api";

export type SymbolName =
  | "integral"
  | "sum"
  | "sqrt"
  | "infinity"
  | "equals"
  | "identical"
  | "notEqual"
  | "implies"
  | "iff"
  | "arrow"
  | "leftRightArrow"
  | "lessEqual"
  | "greaterEqual"
  | "plusMinus"
  | "forAll"
  | "exists";

export const SYMBOL_GLYPHS: Record<SymbolName, string> = {
  integral: "∫",
  sum: "Σ",
  sqrt: "√",
  infinity: "∞",
  equals: "=",
  identical: "≡",
  notEqual: "≠",
  implies: "⇒",
  iff: "⇔",
  arrow: "→",
  leftRightArrow: "↔",
  lessEqual: "≤",
  greaterEqual: "≥",
  plusMinus: "±",
  forAll: "∀",
  exists: "∃",
};

// --- models -----------------------------------------------------------------------

function cubic(p0: P2, p1: P2, p2: P2, p3: P2, n = 12): P2[] {
  const out: P2[] = [];
  for (let i = 0; i <= n; i++) {
    const t = i / n;
    const u = 1 - t;
    out.push([
      u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
      u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1],
    ]);
  }
  return out;
}

const join = (...parts: P2[][]): P2[] => parts.flatMap((p, i) => (i ? p.slice(1) : p));

function lemniscate(n = 48): P2[] {
  const out: P2[] = [];
  for (let i = 0; i <= n; i++) {
    const t = (i / n) * Math.PI * 2;
    const d = 1 + Math.sin(t) ** 2;
    out.push([1 + Math.cos(t) / d, 0.4 + (Math.sin(t) * Math.cos(t)) / d]);
  }
  return out;
}

const chevron = (x0: number, x1: number, y0: number, y1: number): P2[] => [
  [x0, y0],
  [x1, (y0 + y1) / 2],
  [x0, y1],
];

/** Clean models: polylines in a box, y down. */
const MODELS: Record<SymbolName, P2[][]> = {
  integral: [
    join(
      cubic([0.42, 0.1], [0.4, -0.02], [0.27, -0.02], [0.25, 0.14]),
      cubic([0.25, 0.14], [0.24, 0.4], [0.21, 0.6], [0.2, 0.86]),
      cubic([0.2, 0.86], [0.18, 1.02], [0.05, 1.02], [0.03, 0.9]),
    ),
  ],
  sum: [
    [
      [0.82, 0.1],
      [0.82, 0],
      [0, 0],
      [0.45, 0.5],
      [0, 1],
      [0.85, 1],
      [0.85, 0.9],
    ],
  ],
  sqrt: [
    [
      [0, 0.6],
      [0.15, 0.52],
      [0.4, 1],
      [0.65, 0],
      [1.4, 0],
    ],
  ],
  infinity: [lemniscate()],
  equals: [
    [
      [0, 0],
      [1.2, 0],
    ],
    [
      [0, 0.45],
      [1.2, 0.45],
    ],
  ],
  identical: [
    [
      [0, 0],
      [1.2, 0],
    ],
    [
      [0, 0.4],
      [1.2, 0.4],
    ],
    [
      [0, 0.8],
      [1.2, 0.8],
    ],
  ],
  notEqual: [
    [
      [0, 0.3],
      [1.2, 0.3],
    ],
    [
      [0, 0.75],
      [1.2, 0.75],
    ],
    [
      [0.85, 0],
      [0.35, 1.05],
    ],
  ],
  implies: [
    [
      [0, 0.32],
      [1.35, 0.32],
    ],
    [
      [0, 0.72],
      [1.35, 0.72],
    ],
    chevron(1.15, 1.6, 0, 1.04),
  ],
  iff: [
    [
      [0.2, 0.32],
      [1.8, 0.32],
    ],
    [
      [0.2, 0.72],
      [1.8, 0.72],
    ],
    chevron(1.6, 2.05, 0, 1.04),
    chevron(0.45, 0, 0, 1.04),
  ],
  arrow: [
    [
      [0, 0.5],
      [1.6, 0.5],
    ],
    chevron(1.25, 1.6, 0.12, 0.88),
  ],
  leftRightArrow: [
    [
      [0, 0.5],
      [1.9, 0.5],
    ],
    chevron(1.55, 1.9, 0.12, 0.88),
    chevron(0.35, 0, 0.12, 0.88),
  ],
  lessEqual: [chevron(0.9, 0, 0, 0.75), [[0, 1], [0.9, 1]]],
  greaterEqual: [chevron(0, 0.9, 0, 0.75), [[0, 1], [0.9, 1]]],
  plusMinus: [
    [
      [0.45, 0],
      [0.45, 0.72],
    ],
    [
      [0, 0.36],
      [0.9, 0.36],
    ],
    [
      [0, 1],
      [0.9, 1],
    ],
  ],
  forAll: [
    [
      [0, 0],
      [0.45, 1],
      [0.9, 0],
    ],
    [
      [0.18, 0.42],
      [0.72, 0.42],
    ],
  ],
  exists: [
    [
      [0, 0],
      [0.75, 0],
      [0.75, 1],
      [0, 1],
    ],
    [
      [0.12, 0.5],
      [0.75, 0.5],
    ],
  ],
};

// --- $P recogniser ------------------------------------------------------------------

const N = 40;

interface CloudPt {
  x: number;
  y: number;
}

function pathLen(stroke: P2[]): number {
  let d = 0;
  for (let i = 1; i < stroke.length; i++) d += Math.hypot(stroke[i][0] - stroke[i - 1][0], stroke[i][1] - stroke[i - 1][1]);
  return d;
}

/** N points spread evenly along all strokes (no points across the gaps). */
function resample(strokes: P2[][], n: number): CloudPt[] {
  const total = strokes.reduce((s, st) => s + pathLen(st), 0);
  if (total === 0) return Array.from({ length: n }, () => ({ x: strokes[0][0][0], y: strokes[0][0][1] }));
  const step = total / (n - 1);
  const out: CloudPt[] = [];
  let acc = 0;
  for (const st of strokes) {
    if (!st.length) continue;
    let prev = st[0];
    if (!out.length) out.push({ x: prev[0], y: prev[1] });
    for (let i = 1; i < st.length; i++) {
      let cur = st[i];
      let d = Math.hypot(cur[0] - prev[0], cur[1] - prev[1]);
      while (acc + d >= step && d > 0) {
        const t = (step - acc) / d;
        const q: P2 = [prev[0] + t * (cur[0] - prev[0]), prev[1] + t * (cur[1] - prev[1])];
        out.push({ x: q[0], y: q[1] });
        prev = q;
        d = Math.hypot(cur[0] - prev[0], cur[1] - prev[1]);
        acc = 0;
      }
      acc += d;
      prev = cur;
      cur = st[i];
    }
  }
  const last = strokes[strokes.length - 1];
  while (out.length < n) out.push({ x: last[last.length - 1][0], y: last[last.length - 1][1] });
  return out.slice(0, n);
}

/** Scale (keeping proportions) to a unit box and centre on the centroid. */
function normalise(pts: CloudPt[]): CloudPt[] {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const p of pts) {
    x0 = Math.min(x0, p.x);
    y0 = Math.min(y0, p.y);
    x1 = Math.max(x1, p.x);
    y1 = Math.max(y1, p.y);
  }
  const s = Math.max(x1 - x0, y1 - y0) || 1;
  const scaled = pts.map((p) => ({ x: (p.x - x0) / s, y: (p.y - y0) / s }));
  const cx = scaled.reduce((a, p) => a + p.x, 0) / scaled.length;
  const cy = scaled.reduce((a, p) => a + p.y, 0) / scaled.length;
  return scaled.map((p) => ({ x: p.x - cx, y: p.y - cy }));
}

function cloudDistance(a: CloudPt[], b: CloudPt[], start: number): number {
  const matched = new Array(b.length).fill(false);
  let sum = 0;
  let i = start;
  do {
    let best = Infinity;
    let index = -1;
    for (let j = 0; j < b.length; j++) {
      if (matched[j]) continue;
      const d = Math.hypot(a[i].x - b[j].x, a[i].y - b[j].y);
      if (d < best) {
        best = d;
        index = j;
      }
    }
    matched[index] = true;
    const weight = 1 - ((i - start + a.length) % a.length) / a.length;
    sum += weight * best;
    i = (i + 1) % a.length;
  } while (i !== start);
  return sum;
}

function greedyMatch(a: CloudPt[], b: CloudPt[]): number {
  const step = Math.floor(Math.sqrt(a.length));
  let min = Infinity;
  for (let i = 0; i < a.length; i += step) min = Math.min(min, cloudDistance(a, b, i), cloudDistance(b, a, i));
  return min;
}

export const MODELS_FOR_TESTS = MODELS;

// --- shape features: corners and proportions ------------------------------------------

function bboxOf(strokes: P2[][]) {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const st of strokes)
    for (const [x, y] of st) {
      x0 = Math.min(x0, x);
      y0 = Math.min(y0, y);
      x1 = Math.max(x1, x);
      y1 = Math.max(y1, y);
    }
  return { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
}

/** Evenly spaced points along one stroke. */
function spaced(stroke: P2[], step: number): P2[] {
  const out: P2[] = [stroke[0]];
  let acc = 0;
  let prev = stroke[0];
  for (let i = 1; i < stroke.length; i++) {
    let cur = stroke[i];
    let d = Math.hypot(cur[0] - prev[0], cur[1] - prev[1]);
    while (acc + d >= step && d > 0) {
      const t = (step - acc) / d;
      prev = [prev[0] + t * (cur[0] - prev[0]), prev[1] + t * (cur[1] - prev[1])];
      out.push(prev);
      d = Math.hypot(cur[0] - prev[0], cur[1] - prev[1]);
      acc = 0;
      cur = stroke[i];
    }
    acc += d;
    prev = stroke[i];
  }
  if (out.length < 2 || Math.hypot(out[out.length - 1][0] - prev[0], out[out.length - 1][1] - prev[1]) > step * 0.3) out.push(prev);
  return out;
}

/**
 * Sharp corners in a stroke: places where its direction turns by more than
 * 58° over about an eighth of the symbol's size (gentle curves and tremor don't count).
 */
/** Direction changes along a stroke (radians), measured over about an eighth of the symbol. */
function turns(stroke: P2[], size: number): number[] {
  const raw = spaced(stroke, Math.max(size * 0.04, 1e-6));
  // Light smoothing first, so tremor doesn't read as corners.
  const pts = raw.map((p, i) => {
    const a = raw[Math.max(0, i - 1)];
    const b = raw[Math.min(raw.length - 1, i + 1)];
    return [(a[0] + 2 * p[0] + b[0]) / 4, (a[1] + 2 * p[1] + b[1]) / 4] as P2;
  });
  const k = 3;
  const out: number[] = [];
  for (let i = k; i < pts.length - k; i++) {
    const a = Math.atan2(pts[i][1] - pts[i - k][1], pts[i][0] - pts[i - k][0]);
    const b = Math.atan2(pts[i + k][1] - pts[i][1], pts[i + k][0] - pts[i][0]);
    let turn = Math.abs(b - a);
    if (turn > Math.PI) turn = 2 * Math.PI - turn;
    out.push(turn);
  }
  return out;
}

export function cornerCount(stroke: P2[], size: number): number {
  const t = turns(stroke, size);
  let corners = 0;
  for (let i = 0; i < t.length; i++) {
    if (t[i] > (58 * Math.PI) / 180) {
      corners++;
      i += 5; // one corner, not several around it
    }
  }
  return corners;
}

/** The sharpest turn in a stroke (radians): near 0 for a straight line. */
function sharpest(stroke: P2[], size: number): number {
  return Math.max(0, ...turns(stroke, size));
}

/** Each stroke's corner count and sharpest turn, ordered by corners (stroke order doesn't matter). */
function cornerSignature(strokes: P2[][]): { corners: number; sharpest: number }[] {
  const b = bboxOf(strokes);
  const size = Math.max(b.w, b.h);
  return strokes.map((st) => ({ corners: cornerCount(st, size), sharpest: sharpest(st, size) })).sort((a, b) => a.corners - b.corners || a.sharpest - b.sharpest);
}

const TEMPLATES = (Object.keys(MODELS) as SymbolName[]).map((name) => {
  const b = bboxOf(MODELS[name]);
  // Strokes that are plain straight lines in the model.
  const straight = MODELS[name].filter((st) => st.length === 2).length;
  return { name, strokes: MODELS[name].length, straight, cloud: normalise(resample(MODELS[name], N)), corners: cornerSignature(MODELS[name]), aspect: b.w / Math.max(b.h, 1e-6) };
});

/**
 * Per symbol, the $P distance under which a drawing counts: just above its
 * sloppiest hand-drawn samples and below the closest letters and digits
 * (calibrated on generated samples; see symbols.test.ts).
 */
const THRESHOLDS: Record<SymbolName, number> = {
  integral: 1.6,
  sum: 2.1,
  sqrt: 1.9,
  infinity: 1.4,
  equals: 1.5,
  identical: 1.65,
  notEqual: 1.6,
  implies: 1.6,
  iff: 1.6,
  arrow: 1.25,
  leftRightArrow: 1.5,
  lessEqual: 1.5,
  greaterEqual: 1.5,
  plusMinus: 1.7,
  forAll: 1.7,
  exists: 1.75,
};

export interface SymbolMatch {
  name: SymbolName;
  /** $P distance: lower is closer. */
  distance: number;
}

/**
 * The symbol a drawing looks like, or null. Only symbols with the same
 * number of strokes, similar corners and similar proportions are considered.
 */
export function matchSymbol(strokes: P2[][]): SymbolMatch | null {
  const usable = strokes.filter((s) => s.length > 1);
  if (!usable.length) return null;
  const cloud = normalise(resample(usable, N));
  const corners = cornerSignature(usable);
  const b = bboxOf(usable);
  const aspect = b.w / Math.max(b.h, 1e-6);
  let best: SymbolMatch | null = null;
  for (const t of TEMPLATES) {
    if (!plausible(t, usable.length, corners, aspect)) continue;
    const d = greedyMatch(cloud, t.cloud);
    if (d >= THRESHOLDS[t.name]) continue;
    // Closest relative to its own limit.
    if (!best || d / THRESHOLDS[t.name] < best.distance / THRESHOLDS[best.name]) best = { name: t.name, distance: d };
  }
  return best;
}

/** Same number of strokes, about the same corners per stroke and similar proportions. */
function plausible(t: (typeof TEMPLATES)[number], strokes: number, corners: { corners: number; sharpest: number }[], aspect: number): boolean {
  if (strokes !== t.strokes) return false;
  // Corners may be off by one, but a stroke that must be pointed (a chevron,
  // a V) needs a clear turn somewhere: a plain line isn't an arrowhead.
  const needPointed = t.corners.filter((c) => c.corners >= 1).length;
  const pointed = corners.filter((c) => c.corners >= 1 || c.sharpest > (38 * Math.PI) / 180).length;
  if (pointed < needPointed) return false;
  // …and no more bent strokes than the symbol has curved or pointed ones:
  // its straight bars (=, ≠, ⇒) aren't matched by a chevron.
  if (corners.filter((c) => c.corners >= 1 || c.sharpest > (38 * Math.PI) / 180).length > t.strokes - t.straight) return false;
  // Zigzags (Σ, √) have small corners that are easy to round off: allow more slack there.
  for (let i = 0; i < corners.length; i++) {
    const want = t.corners[i].corners;
    if (Math.abs(corners[i].corners - want) > (want >= 3 ? 2 : 1)) return false;
    // A one-corner stroke (chevron, V) has exactly one: an E's bracket has two.
    if (want === 1 && corners[i].corners > 1) return false;
  }
  const ratio = aspect / t.aspect;
  return ratio > 0.45 && ratio < 2.2;
}

/** The plausible symbols' distances (best first), for tuning and tests. */
export function rankSymbols(strokes: P2[][]): SymbolMatch[] {
  const cloud = normalise(resample(strokes, N));
  const corners = cornerSignature(strokes);
  const b = bboxOf(strokes);
  const aspect = b.w / Math.max(b.h, 1e-6);
  return TEMPLATES.filter((t) => plausible(t, strokes.length, corners, aspect))
    .map((t) => ({ name: t.name, distance: greedyMatch(cloud, t.cloud) }))
    .sort((a, b) => a.distance - b.distance);
}

// --- clean drawing ---------------------------------------------------------------------

/** The clean symbol in a box (page coordinates), as one path with breaks between its strokes. */
export function symbolShape(name: SymbolName, box: { x: number; y: number; w: number; h: number }): RecognizedShape {
  const model = MODELS[name];
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const st of model)
    for (const [x, y] of st) {
      x0 = Math.min(x0, x);
      y0 = Math.min(y0, y);
      x1 = Math.max(x1, x);
      y1 = Math.max(y1, y);
    }
  // Keep the model's proportions, sized to the drawing and centred on it.
  const mw = x1 - x0 || 1;
  const mh = y1 - y0 || 1;
  // A balance of fitting the width and the height.
  const scale = Math.sqrt((box.w / mw) * (box.h / mh));
  const ox = box.x + box.w / 2 - (mw * scale) / 2 - x0 * scale;
  const oy = box.y + box.h / 2 - (mh * scale) / 2 - y0 * scale;
  const points: P2[] = [];
  const breaks: number[] = [];
  for (const st of model) {
    if (points.length) breaks.push(points.length);
    for (const [x, y] of st) points.push([ox + x * scale, oy + y * scale]);
  }
  return { kind: "path", points, closed: false, ...(breaks.length ? { breaks } : {}) };
}
