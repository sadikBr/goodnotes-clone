// Laser pointer for presenting: a glowing dot that follows the pen or mouse,
// and (in trail mode) a glowing line that fades away by itself. Nothing is
// ever added to the page.

export type LaserMode = "trail" | "dot";

/** How long a bit of trail stays fully visible, then how long it takes to fade (ms). */
const HOLD_MS = 900;
const FADE_MS = 450;
/** Alpha steps used when drawing a fading trail (each step is one path, so no beading). */
const STEPS = 16;

interface TrailPoint {
  x: number;
  y: number;
  t: number;
}

export class Laser {
  mode: LaserMode = "trail";
  color = "#ef4444";
  private trails: TrailPoint[][] = [];
  private current: TrailPoint[] | null = null;
  private dot: { x: number; y: number } | null = null;

  /** Where the pointer is (or null when it leaves the page). */
  pointAt(p: [number, number] | null) {
    this.dot = p ? { x: p[0], y: p[1] } : null;
  }

  begin(p: [number, number], now: number) {
    this.dot = { x: p[0], y: p[1] };
    if (this.mode !== "trail") return;
    this.current = [{ x: p[0], y: p[1], t: now }];
    this.trails.push(this.current);
  }

  extend(p: [number, number], now: number) {
    this.dot = { x: p[0], y: p[1] };
    const cur = this.current;
    if (!cur) return;
    const last = cur[cur.length - 1];
    if (Math.hypot(p[0] - last.x, p[1] - last.y) < 0.5) {
      last.t = now; // still pen: keep the tip alive
      return;
    }
    cur.push({ x: p[0], y: p[1], t: now });
  }

  end() {
    this.current = null;
  }

  clear() {
    this.trails = [];
    this.current = null;
    this.dot = null;
  }

  /** True while something is fading, so the editor keeps animating. */
  animating(now: number): boolean {
    this.prune(now);
    return this.trails.length > 0;
  }

  private prune(now: number) {
    for (const tr of this.trails) {
      // The pen tip of a trail being drawn stays alive.
      const keepFrom = tr === this.current ? tr.length - 1 : tr.length;
      let cut = 0;
      while (cut < keepFrom - 1 && now - tr[cut + 1].t > HOLD_MS + FADE_MS) cut++;
      if (cut) tr.splice(0, cut);
    }
    this.trails = this.trails.filter((tr) => tr === this.current || (tr.length && now - tr[tr.length - 1].t < HOLD_MS + FADE_MS));
  }

  /**
   * Draws the laser on a context set up in page units. `zoom` converts the
   * sizes so they look the same at any zoom; `dpr` is canvas pixels per CSS
   * pixel (shadow blur is in canvas pixels, unaffected by the transform).
   */
  draw(ctx: CanvasRenderingContext2D, zoom: number, dpr: number, now: number) {
    this.prune(now);
    const alphaOf = (t: number) => {
      const age = now - t;
      return age <= HOLD_MS ? 1 : Math.max(0, 1 - (age - HOLD_MS) / FADE_MS);
    };
    const width = 6 / zoom;
    ctx.save();
    // Flat ends between fade steps, so neighbouring steps don't overlap.
    ctx.lineCap = "butt";
    ctx.lineJoin = "round";
    for (const tr of this.trails) {
      if (tr.length < 2) continue;
      // Group consecutive segments by fade step and draw each group as one path.
      let i = 1;
      while (i < tr.length) {
        const step = Math.ceil(alphaOf(tr[i].t) * STEPS);
        const start = i - 1;
        while (i < tr.length && Math.ceil(alphaOf(tr[i].t) * STEPS) === step) i++;
        if (step === 0) continue;
        const pts = tr.slice(start, i);
        this.glowPath(ctx, pts, width, step / STEPS, dpr);
      }
    }
    if (this.dot) this.glowDot(ctx, this.dot.x, this.dot.y, 7 / zoom, dpr);
    ctx.restore();
  }

  private glowPath(ctx: CanvasRenderingContext2D, pts: TrailPoint[], width: number, alpha: number, dpr: number) {
    const path = () => {
      ctx.beginPath();
      ctx.moveTo(pts[0].x, pts[0].y);
      for (let k = 1; k < pts.length; k++) ctx.lineTo(pts[k].x, pts[k].y);
    };
    ctx.globalAlpha = alpha;
    // Coloured glow, then a bright core.
    ctx.shadowColor = this.color;
    ctx.shadowBlur = 14 * dpr;
    ctx.strokeStyle = this.color;
    ctx.lineWidth = width;
    path();
    ctx.stroke();
    ctx.shadowBlur = 0;
    ctx.strokeStyle = this.core();
    ctx.lineWidth = width * 0.4;
    path();
    ctx.stroke();
  }

  /** The hot centre of the beam: the laser colour mixed with white. */
  private core(): string {
    const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(this.color);
    if (!m) return "rgba(255, 255, 255, 0.85)";
    const mix = (hex: string) => Math.round(parseInt(hex, 16) * 0.35 + 255 * 0.65);
    return `rgb(${mix(m[1])}, ${mix(m[2])}, ${mix(m[3])})`;
  }

  private glowDot(ctx: CanvasRenderingContext2D, x: number, y: number, r: number, dpr: number) {
    ctx.globalAlpha = 1;
    ctx.shadowColor = this.color;
    ctx.shadowBlur = 18 * dpr;
    ctx.fillStyle = this.color;
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
    ctx.shadowBlur = 0;
    ctx.fillStyle = this.core();
    ctx.beginPath();
    ctx.arc(x, y, r * 0.45, 0, Math.PI * 2);
    ctx.fill();
  }
}
