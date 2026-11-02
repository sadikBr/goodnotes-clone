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

interface Stroke {
  pts: P[];
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

  pointAt(p: P | null) {
    this.dot = p ? [p[0], p[1]] : null;
  }

  begin(p: P) {
    this.dot = [p[0], p[1]];
    if (this.mode !== "trail") return;
    this.current = { pts: [[p[0], p[1]]], drawn: 0, finished: false, tailDrawn: false };
    this.strokes.push(this.current);
  }

  /** Adds a point; returns true if there is something new to draw. */
  extend(p: P, minStep: number): boolean {
    this.dot = [p[0], p[1]];
    const cur = this.current;
    if (!cur) return false;
    const last = cur.pts[cur.pts.length - 1];
    if (Math.hypot(p[0] - last[0], p[1] - last[1]) < minStep) return false;
    cur.pts.push([p[0], p[1]]);
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
    const path = new Path2D();
    let any = false;
    for (const s of this.strokes) any = addSegments(path, s, false) || any;
    if (any) paint(layers, path, zoom, this.color);
  }

  /** Redraws everything (after scrolling or zooming); the layers must be cleared first. */
  drawAll(layers: LaserLayers, zoom: number) {
    const path = new Path2D();
    let any = false;
    for (const s of this.strokes) any = addSegments(path, s, true) || any;
    if (any) paint(layers, path, zoom, this.color);
  }
}

const mid = (a: P, b: P): P => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];

/**
 * Adds a stroke's undrawn curve segments to `path` (all of them when
 * `restart`). The curve is the usual smooth one: quadratics through the
 * midpoints of consecutive points, then a straight bit to the last point.
 */
function addSegments(path: Path2D, s: Stroke, restart: boolean): boolean {
  if (restart) {
    s.drawn = 0;
    s.tailDrawn = false;
  }
  const pts = s.pts;
  const n = pts.length;
  let added = false;
  // A tap: a dot.
  if (n === 1) {
    if (s.finished && !s.tailDrawn) {
      path.moveTo(pts[0][0], pts[0][1]);
      path.lineTo(pts[0][0] + 0.01, pts[0][1]);
      s.tailDrawn = true;
      return true;
    }
    return false;
  }
  // Segment i (1..n-2) runs from the previous midpoint to mid(i, i+1).
  for (let i = s.drawn + 1; i <= n - 2; i++) {
    const start = i === 1 ? pts[0] : mid(pts[i - 1], pts[i]);
    const end = mid(pts[i], pts[i + 1]);
    path.moveTo(start[0], start[1]);
    path.quadraticCurveTo(pts[i][0], pts[i][1], end[0], end[1]);
    s.drawn = i;
    added = true;
  }
  if (s.finished && !s.tailDrawn) {
    const from = n === 2 ? pts[0] : mid(pts[n - 2], pts[n - 1]);
    path.moveTo(from[0], from[1]);
    path.lineTo(pts[n - 1][0], pts[n - 1][1]);
    s.tailDrawn = true;
    added = true;
  }
  return added;
}

function paint(layers: LaserLayers, path: Path2D, zoom: number, color: string) {
  const stroke = (ctx: CanvasRenderingContext2D, style: string, px: number) => {
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.strokeStyle = style;
    ctx.lineWidth = px / zoom;
    ctx.stroke(path);
  };
  stroke(layers.glow, color, GLOW_PX);
  stroke(layers.tube, color, TUBE_PX);
  stroke(layers.core, "#ffffff", CORE_PX);
}
