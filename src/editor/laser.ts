// Laser pointer for presenting and explaining. In trail mode everything
// drawn stays visible while you keep going; about a second after the pen
// lifts with nothing new drawn, it all fades away together (the editor fades
// the laser layer with a CSS transition, so fading costs nothing). Dot mode
// just shows a glowing pointer. Nothing is ever added to the page.

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
  /** Pointer position in page units, or null when it isn't over the page. */
  dot: P | null = null;

  pointAt(p: P | null) {
    this.dot = p ? [p[0], p[1]] : null;
  }

  begin(p: P) {
    this.dot = [p[0], p[1]];
    if (this.mode !== "trail") return;
    this.current = [[p[0], p[1]]];
    this.strokes.push(this.current);
  }

  /** Adds a point; returns true if the drawing changed. */
  extend(p: P, minStep: number): boolean {
    this.dot = [p[0], p[1]];
    const cur = this.current;
    if (!cur) return false;
    const last = cur[cur.length - 1];
    if (Math.hypot(p[0] - last[0], p[1] - last[1]) < minStep) return false;
    cur.push([p[0], p[1]]);
    return true;
  }

  end() {
    this.current = null;
  }

  get drawing(): boolean {
    return this.current !== null;
  }

  /** Removes the drawing (after it has faded out). */
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

  /**
   * Draws the strokes on a context set up in page units; sizes are screen
   * pixels divided by `zoom`. The glow is a wide faint line under the solid
   * one (canvas shadow blur is far too slow for live drawing).
   */
  draw(ctx: CanvasRenderingContext2D, zoom: number) {
    if (!this.strokes.length) return;
    ctx.save();
    ctx.beginPath();
    for (const s of this.strokes) smoothPath(ctx, s);
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.strokeStyle = this.color;
    // Two faint bands give a soft falloff.
    ctx.globalAlpha = 0.08;
    ctx.lineWidth = 16 / zoom;
    ctx.stroke();
    ctx.globalAlpha = 0.14;
    ctx.lineWidth = 10 / zoom;
    ctx.stroke();
    ctx.globalAlpha = 1;
    ctx.lineWidth = 4.5 / zoom;
    ctx.stroke();
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
