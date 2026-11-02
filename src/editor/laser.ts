// Laser pointer for presenting and explaining. In trail mode everything
// drawn stays visible while you keep going; about a second after the pen
// lifts with nothing new drawn, it all fades away together. Dot mode just
// shows a glowing pointer. Nothing is ever added to the page.

export type LaserMode = "trail" | "dot";

/** How long the drawing stays after the last stroke ends, then the fade (ms). */
export const LASER_HOLD_MS = 1000;
export const LASER_FADE_MS = 500;

type P = [number, number];

export class Laser {
  mode: LaserMode = "trail";
  color = "#ef4444";
  private strokes: P[][] = [];
  private current: P[] | null = null;
  private dot: P | null = null;
  private lastActivity = 0;

  /** Where the pointer is (or null when it leaves the page). */
  pointAt(p: P | null) {
    this.dot = p ? [p[0], p[1]] : null;
  }

  begin(p: P, now: number) {
    this.dot = [p[0], p[1]];
    if (this.mode !== "trail") return;
    // Drawing again before the fade ends brings everything back.
    if (this.opacity(now) <= 0) this.strokes = [];
    this.current = [[p[0], p[1]]];
    this.strokes.push(this.current);
    this.lastActivity = now;
  }

  extend(p: P, now: number, minStep: number) {
    this.dot = [p[0], p[1]];
    const cur = this.current;
    if (!cur) return;
    const last = cur[cur.length - 1];
    if (Math.hypot(p[0] - last[0], p[1] - last[1]) >= minStep) cur.push([p[0], p[1]]);
    this.lastActivity = now;
  }

  end(now: number) {
    if (this.current) this.lastActivity = now;
    this.current = null;
  }

  clear() {
    this.strokes = [];
    this.current = null;
    this.dot = null;
  }

  /** Opacity of the drawing: 1 while writing and for a moment after, then fading to 0. */
  opacity(now: number): number {
    if (this.current) return 1;
    const idle = now - this.lastActivity;
    if (idle <= LASER_HOLD_MS) return 1;
    return Math.max(0, 1 - (idle - LASER_HOLD_MS) / LASER_FADE_MS);
  }

  /** True while the drawing is waiting to fade or fading, so the editor keeps animating. */
  animating(now: number): boolean {
    if (!this.strokes.length || this.current) return false;
    if (this.opacity(now) <= 0) {
      this.strokes = [];
      return false;
    }
    return true;
  }

  /** Number of strokes currently shown (for tests). */
  get strokeCount(): number {
    return this.strokes.length;
  }

  /**
   * Draws the laser on a context set up in page units. Sizes are in screen
   * pixels (divided by `zoom`); `dpr` converts the glow to canvas pixels.
   */
  draw(ctx: CanvasRenderingContext2D, zoom: number, dpr: number, now: number) {
    const alpha = this.opacity(now);
    ctx.save();
    if (this.strokes.length && alpha > 0) {
      // All strokes in one path: one stroke() paints overlaps only once.
      ctx.beginPath();
      for (const s of this.strokes) smoothPath(ctx, s);
      ctx.globalAlpha = alpha;
      ctx.lineCap = "round";
      ctx.lineJoin = "round";
      ctx.strokeStyle = this.color;
      ctx.lineWidth = 4.5 / zoom;
      ctx.shadowColor = this.color;
      ctx.shadowBlur = 10 * dpr;
      ctx.stroke();
    }
    if (this.dot) {
      ctx.globalAlpha = 1;
      ctx.fillStyle = this.color;
      ctx.shadowColor = this.color;
      ctx.shadowBlur = 12 * dpr;
      ctx.beginPath();
      ctx.arc(this.dot[0], this.dot[1], 5.5 / zoom, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }
}

/** Adds a smooth curve through the points (quadratics through midpoints). */
function smoothPath(ctx: CanvasRenderingContext2D, pts: P[]) {
  ctx.moveTo(pts[0][0], pts[0][1]);
  if (pts.length === 1) {
    ctx.lineTo(pts[0][0] + 0.01, pts[0][1]);
    return;
  }
  for (let i = 1; i < pts.length - 1; i++) {
    ctx.quadraticCurveTo(pts[i][0], pts[i][1], (pts[i][0] + pts[i + 1][0]) / 2, (pts[i][1] + pts[i + 1][1]) / 2);
  }
  const last = pts[pts.length - 1];
  ctx.lineTo(last[0], last[1]);
}
