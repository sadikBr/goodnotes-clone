// Ink dynamics and stroke outlines.
//
// Speed: for the fountain pen and brush pen, moving fast makes the line
// thinner and slowing down makes it fuller, like real ink. The effect is
// computed while drawing (from real timings) and stored in each point's
// pressure value, so a saved stroke looks exactly as it did on screen at
// any zoom and in exports. With a stylus, its real pressure still leads and
// speed is blended in gently.
//
// Outlines: pens are drawn as one smooth filled outline (perfect-freehand)
// instead of many short line pieces, so there are no steps, beads or
// jagged joins, and width changes are gradual.

import { getStroke, type StrokeOptions } from "perfect-freehand";
import type { PenStyle, Pt } from "./model";

/** Pen styles whose width responds to drawing speed. */
export function speedSensitive(style: PenStyle | undefined): boolean {
  return style === undefined || style === "fountain" || style === "brush";
}

/**
 * Speed (page units/ms) → pressure-like value. Calibrated on handwriting:
 * ~0.4 units/ms (about 100 mm/s) is ordinary writing and gives the chosen
 * width (0.5); slow, careful lines approach 0.8, quick flicks 0.3.
 */
function speedLevel(v: number): number {
  return 0.3 + 0.5 / (1 + (v / 0.38) ** 1.5);
}

/**
 * Tracks drawing speed and turns it into a pressure-like value (0..1):
 * about 0.8 when slow, 0.3 when fast, smoothed so it never jumps.
 */
export class SpeedInk {
  private level = 0.55;
  private last: { x: number; y: number; t: number } | null = null;

  /**
   * The value to store for a new point. `raw` is the stylus pressure, or
   * null for mouse/touch (speed alone then sets the width).
   */
  next(x: number, y: number, t: number, raw: number | null): number {
    if (this.last) {
      const dt = Math.max(1, t - this.last.t);
      const v = Math.hypot(x - this.last.x, y - this.last.y) / dt;
      const target = speedLevel(v);
      // Ease towards the target: quick enough to follow, slow enough to look natural.
      this.level += (target - this.level) * Math.min(1, 0.08 * dt ** 0.5);
    }
    this.last = { x, y, t };
    if (raw === null) return this.level;
    // Stylus: real pressure leads, speed adjusts it by up to about ±25%.
    return Math.max(0.02, Math.min(1, raw * (0.75 + 0.6 * (this.level - 0.55))));
  }
}

/** perfect-freehand settings per pen style (size is the stroke's nominal width). */
function options(style: PenStyle | undefined, width: number, last: boolean): StrokeOptions {
  const base = { smoothing: 0.62, streamline: 0.38, simulatePressure: false, last };
  switch (style ?? "fountain") {
    case "ballpoint":
      return { ...base, size: Math.max(0.8, width * 0.8), thinning: 0 };
    case "pencil":
      return { ...base, size: Math.max(0.8, width * 0.78), thinning: 0.15 };
    case "brush":
      return { ...base, size: width * 1.25, thinning: 0.82, start: { taper: width * 3 }, end: { taper: width * 4 } };
    default:
      // Fountain: at medium pressure the width matches the size chosen.
      return { ...base, size: width * 0.875, thinning: 0.5 };
  }
}

/**
 * Indices where the stroke turns sharply (more than ~80° within a short
 * distance), e.g. the tip of a "V". Measured over a little distance on each
 * side so hand jitter doesn't count.
 */
export function sharpCorners(points: Pt[], width: number): number[] {
  const reach = Math.max(3, width * 1.2);
  const n = points.length;
  const turn: number[] = new Array(n).fill(0);
  for (let i = 1; i < n - 1; i++) {
    let a = i - 1;
    while (a > 0 && Math.hypot(points[i][0] - points[a][0], points[i][1] - points[a][1]) < reach) a--;
    let b = i + 1;
    while (b < n - 1 && Math.hypot(points[b][0] - points[i][0], points[b][1] - points[i][1]) < reach) b++;
    const u = [points[i][0] - points[a][0], points[i][1] - points[a][1]];
    const v = [points[b][0] - points[i][0], points[b][1] - points[i][1]];
    const lu = Math.hypot(u[0], u[1]);
    const lv = Math.hypot(v[0], v[1]);
    if (lu < reach * 0.5 || lv < reach * 0.5) continue;
    turn[i] = Math.acos(Math.max(-1, Math.min(1, (u[0] * v[0] + u[1] * v[1]) / (lu * lv))));
  }
  const corners: number[] = [];
  const threshold = (80 * Math.PI) / 180;
  for (let i = 2; i < n - 2; i++) {
    // The sharpest point of each turn (a local maximum).
    if (turn[i] > threshold && turn[i] >= turn[i - 1] && turn[i] > turn[i + 1]) {
      if (!corners.length || i - corners[corners.length - 1] > 2) corners.push(i);
    }
  }
  return corners;
}

/**
 * The stroke's outline polygons, in page units. The stroke is split at sharp
 * corners so each part gets a clean outline (one outline around a sharp
 * reversal bulges and hooks); fill them together with the non-zero rule.
 */
/** Smoothing levels (perfect-freehand "streamline"): how much the path lags behind the pen to even out shakes. */
export const SMOOTHING = { off: 0, light: 0.2, normal: 0.38, strong: 0.62 } as const;
export type SmoothingLevel = keyof typeof SMOOTHING;
export const DEFAULT_STREAMLINE = SMOOTHING.normal;

/**
 * The stroke's points after streamline smoothing (each point eases toward
 * the pen from the previous one). Pass `into` to extend an earlier result
 * as points are added, for a stroke being drawn.
 */
export function smoothPoints(points: Pt[], into: Pt[] = [], streamline: number = DEFAULT_STREAMLINE): Pt[] {
  // How far each point moves toward the pen (1 = all the way: no smoothing).
  const STREAMLINE_T = 0.15 + (1 - streamline) * 0.85;
  for (let i = into.length; i < points.length; i++) {
    const r = points[i];
    if (i === 0) {
      into.push(r);
      continue;
    }
    const p = into[i - 1];
    into.push([p[0] + (r[0] - p[0]) * STREAMLINE_T, p[1] + (r[1] - p[1]) * STREAMLINE_T, r[2]]);
  }
  return into;
}

/**
 * `part` draws a piece of a longer stroke: a piece that isn't the stroke's
 * real start or end gets a plain round cap there instead of a taper, so
 * pieces join seamlessly (used to draw a long stroke incrementally).
 */
export function strokeOutline(points: Pt[], style: PenStyle | undefined, width: number, last = true, part?: { first: boolean; last: boolean }, streamline: number = DEFAULT_STREAMLINE): [number, number][][] {
  const opts = options(style, width, last);
  if (part && !part.first) opts.start = { cap: true, taper: 0 };
  if (part && !part.last) opts.end = { cap: true, taper: 0 };
  // Smoothing ("streamline") is applied here, over the whole stroke, rather
  // than inside perfect-freehand: then a piece of a stroke (`part`, given
  // already-smoothed points) lines up exactly with the rest of it.
  if (!part) {
    const raw = points;
    points = smoothPoints(raw, [], streamline);
    if (last && raw.length > 1) points[points.length - 1] = raw[raw.length - 1];
  }
  opts.streamline = 0;
  const corners = points.length > 6 ? sharpCorners(points, width) : [];
  if (!corners.length) return [getStroke(points, opts) as [number, number][]];
  const out: [number, number][][] = [];
  const cuts = [0, ...corners, points.length - 1];
  for (let k = 0; k < cuts.length - 1; k++) {
    const part = points.slice(cuts[k], cuts[k + 1] + 1);
    // Tapers belong to the stroke's real start and end only; parts meet with round caps.
    const partOpts: StrokeOptions = {
      ...opts,
      start: k === 0 ? opts.start : { cap: true, taper: 0 },
      end: k === cuts.length - 2 ? opts.end : { cap: true, taper: 0 },
      last: k === cuts.length - 2 ? last : true,
    };
    out.push(getStroke(part, partOpts) as [number, number][]);
  }
  return out;
}

/** Adds a closed, smooth path through an outline polygon (quadratics through midpoints). */
export function outlinePath(ctx: CanvasRenderingContext2D, outline: [number, number][]) {
  const n = outline.length;
  if (n < 3) return;
  const mid = (a: [number, number], b: [number, number]) => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2] as const;
  const m0 = mid(outline[n - 1], outline[0]);
  ctx.moveTo(m0[0], m0[1]);
  for (let i = 0; i < n; i++) {
    const p = outline[i];
    const m = mid(p, outline[(i + 1) % n]);
    ctx.quadraticCurveTo(p[0], p[1], m[0], m[1]);
  }
  ctx.closePath();
}
