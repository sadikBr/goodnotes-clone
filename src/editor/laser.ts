// Laser pointer for presenting and explaining. In trail mode everything
// drawn stays visible while you keep going; shortly after the pen lifts it
// all fades away. Dot mode just shows a pointer. Nothing is ever added to
// the page.
//
// Look: a saturated tube with a thin white core and a soft glow. It is drawn
// on three stacked layers (glow, tube, core), each a single opaque colour,
// so new bits of curve can be added incrementally: each frame draws only the
// new segments, whatever has been drawn before, and pieces join without
// seams. The glow layer gets its transparency from the layer's CSS opacity,
// so overlaps never darken. Fading is a CSS transition on the layers.

export type LaserMode = "trail" | "dot";

/** How long the drawing stays after the last stroke ends, then the fades (ms). */
export const LASER_HOLD_MS = 900;
/** The colour (glow and tube) fades first, leaving the white core... */
export const LASER_COLOR_FADE_MS = 300;
/** ...which is gone shortly after. */
export const LASER_CORE_FADE_MS = 480;

/** Widths in screen pixels. */
const GLOW_PX = 11;
const TUBE_PX = 6;
const CORE_PX = 1.8;

type P = [number, number];
/** x, y and the width factor at that point (from drawing speed). */
type LP = [number, number, number];

interface Stroke {
  pts: LP[];
  /** Curve segments already drawn (segment i is controlled by pts[i]). */
  drawn: number;
  /** The last bit, to the final point, is drawn once the stroke ends. */
  finished: boolean;
  tailDrawn: boolean;
}

/** The three layers' contexts, all with the same page-unit transform. */
export interface LaserLayers {
  glow: CanvasRenderingContext2D;
  tube: CanvasRenderingContext2D;
  core: CanvasRenderingContext2D;
}

export class Laser {
  mode: LaserMode = "trail";
  color = "#ff1f1f";
  private strokes: Stroke[] = [];
  private current: Stroke | null = null;
  /** Pointer position in page units, or null when it isn't over the page. */
  dot: P | null = null;
  /** Width factor from speed: about 1.15 when slow, 0.6 when fast. */
  private widthLevel = 1;
  private lastT = 0;

  pointAt(p: P | null) {
    this.dot = p ? [p[0], p[1]] : null;
  }

  begin(p: P) {
    this.dot = [p[0], p[1]];
    if (this.mode !== "trail") return;
    this.widthLevel = 1;
    this.lastT = performance.now();
    this.current = { pts: [[p[0], p[1], this.widthLevel]], drawn: 0, finished: false, tailDrawn: false };
    this.strokes.push(this.current);
  }

  /**
   * Adds a point; returns true if there is something new to draw. `t` is the
   * event time and `zoom` turns page distances into screen speed.
   */
  extend(p: P, minStep: number, t = performance.now(), zoom = 1): boolean {
    this.dot = [p[0], p[1]];
    const cur = this.current;
    if (!cur) return false;
    const last = cur.pts[cur.pts.length - 1];
    const dist = Math.hypot(p[0] - last[0], p[1] - last[1]);
    if (dist < minStep) return false;
    // Screen speed (px/ms) → width: slow is fuller, fast is slimmer.
    const dt = Math.max(1, t - this.lastT);
    const v = (dist * zoom) / dt;
    const k = Math.min(1, v / 2.2);
    const target = 1.15 - 0.55 * k * k * (3 - 2 * k);
    this.widthLevel += (target - this.widthLevel) * Math.min(1, 0.1 * dt ** 0.5);
    this.lastT = t;
    cur.pts.push([p[0], p[1], this.widthLevel]);
    return true;
  }

  end() {
    if (this.current) this.current.finished = true;
    this.current = null;
  }

  get drawing(): boolean {
    return this.current !== null;
  }

  /** Removes the drawing (after it has faded out), keeping a stroke in progress. */
  clearStrokes() {
    this.strokes = this.current ? [this.current] : [];
  }

  clear() {
    this.strokes = [];
    this.current = null;
    this.dot = null;
  }

  get strokeCount(): number {
    return this.strokes.length;
  }

  /** Draws only what is new since the last call. */
  drawNew(layers: LaserLayers, zoom: number) {
    const pieces: Piece[] = [];
    for (const st of this.strokes) collect(pieces, st, false);
    if (pieces.length) paint(layers, pieces, zoom, this.color);
  }

  /** Redraws everything (after scrolling or zooming); the layers must be cleared first. */
  drawAll(layers: LaserLayers, zoom: number) {
    const pieces: Piece[] = [];
    for (const st of this.strokes) collect(pieces, st, true);
    if (pieces.length) paint(layers, pieces, zoom, this.color);
  }
}

/** A run of points (x, y, width factor) along the smoothed curve. */
type Piece = LP[];

const mid = (a: LP, b: LP): LP => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2];

/** Points along the quadratic from `a` to `b` with control `c` (dense enough to look smooth). */
function flatten(a: LP, c: LP, b: LP): LP[] {
  const len = Math.hypot(c[0] - a[0], c[1] - a[1]) + Math.hypot(b[0] - c[0], b[1] - c[1]);
  const n = Math.max(1, Math.min(24, Math.ceil(len / 1.5)));
  const out: LP[] = [];
  for (let k = 1; k <= n; k++) {
    const t = k / n;
    const u = 1 - t;
    out.push([u * u * a[0] + 2 * u * t * c[0] + t * t * b[0], u * u * a[1] + 2 * u * t * c[1] + t * t * b[1], u * u * a[2] + 2 * u * t * c[2] + t * t * b[2]]);
  }
  return out;
}

/**
 * Adds a stroke's undrawn bits (all of them when `restart`) as pieces. The
 * curve is the usual smooth one: quadratics through the midpoints of
 * consecutive points, then a straight bit to the last point.
 */
function collect(out: Piece[], s: Stroke, restart: boolean) {
  if (restart) {
    s.drawn = 0;
    s.tailDrawn = false;
  }
  const pts = s.pts;
  const n = pts.length;
  if (n === 1) {
    if (s.finished && !s.tailDrawn) {
      out.push([pts[0]]);
      s.tailDrawn = true;
    }
    return;
  }
  for (let i = s.drawn + 1; i <= n - 2; i++) {
    const start = i === 1 ? pts[0] : mid(pts[i - 1], pts[i]);
    out.push([start, ...flatten(start, pts[i], mid(pts[i], pts[i + 1]))]);
    s.drawn = i;
  }
  if (s.finished && !s.tailDrawn) {
    out.push([n === 2 ? pts[0] : mid(pts[n - 2], pts[n - 1]), pts[n - 1]]);
    s.tailDrawn = true;
  }
}

/**
 * Adds round-ended capsules along a piece: a circle at each point and the
 * band between neighbours, so the width can change smoothly.
 */
function capsules(path: Path2D, piece: Piece, radius: (w: number) => number) {
  for (let i = 0; i < piece.length; i++) {
    const p = piece[i];
    const r = radius(p[2]);
    path.moveTo(p[0] + r, p[1]);
    path.arc(p[0], p[1], r, 0, Math.PI * 2);
    if (i === 0) continue;
    const q = piece[i - 1];
    const rq = radius(q[2]);
    const dx = p[0] - q[0];
    const dy = p[1] - q[1];
    const len = Math.hypot(dx, dy);
    if (len < 1e-6) continue;
    const nx = -dy / len;
    const ny = dx / len;
    // Traced in the same direction as the circles (clockwise on screen), so
    // the non-zero fill unites them instead of cutting holes where they overlap.
    path.moveTo(q[0] - nx * rq, q[1] - ny * rq);
    path.lineTo(p[0] - nx * r, p[1] - ny * r);
    path.lineTo(p[0] + nx * r, p[1] + ny * r);
    path.lineTo(q[0] + nx * rq, q[1] + ny * rq);
    path.closePath();
  }
}

function paint(layers: LaserLayers, pieces: Piece[], zoom: number, color: string) {
  const fill = (ctx: CanvasRenderingContext2D, style: string, px: number, min: number) => {
    const path = new Path2D();
    for (const piece of pieces) capsules(path, piece, (w) => Math.max(min, (px * w) / 2) / zoom);
    ctx.fillStyle = style;
    ctx.fill(path, "nonzero");
  };
  fill(layers.glow, color, GLOW_PX, 3);
  fill(layers.tube, color, TUBE_PX, 1.5);
  fill(layers.core, "#ffffff", CORE_PX, 0.55);
}
