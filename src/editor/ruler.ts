// On-page ruler. A see-through ruler that floats over the page: drag it to
// move it, drag a round grip (or scroll over it, or twist with two fingers)
// to rotate it. Strokes started next to either edge lock to that edge, like
// drawing along a real ruler. It is a DOM overlay positioned in page units,
// so moving it costs nothing and it is never part of the page or exports.

import type { P2 } from "../api";

/** Inkleaf units per millimetre (units are 1/96 inch). */
export const MM = 96 / 25.4;
const LENGTH = 300 * MM; // 30 cm
const HEIGHT = 15 * MM; // 1.5 cm
const PAD = 6 * MM; // space before the 0 mark
/** Angles the ruler snaps to while rotating (degrees), and how close it must be. */
const SNAP_ANGLES = [0, 15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180];
const SNAP_DEG = 2.5;

/** A ruler edge: the line from `a` towards `dir`, with `normal` pointing away from the ruler. */
export interface RulerEdge {
  a: P2;
  dir: P2;
  normal: P2;
}

export interface RulerHost {
  /** Page coordinates of a client (screen) point. */
  toPage(clientX: number, clientY: number): P2;
  zoom(): number;
  /** Width of the visible page area, in screen pixels. */
  viewWidth(): number;
  /** Where the page's top-left corner is, in the ruler layer's pixels. */
  pageOrigin(): P2;
}

export class Ruler {
  visible = false;
  cx = 400;
  cy = 400;
  /** Radians, clockwise on screen. */
  angle = 0;
  readonly el: HTMLDivElement;
  private label: HTMLDivElement;
  private readout: HTMLDivElement;
  private pointers = new Map<number, P2>();
  private drag:
    | { kind: "move"; start: P2; c0: P2 }
    | { kind: "rotate"; a0: number; angle0: number }
    | { kind: "twist"; a0: number; angle0: number; m0: P2; c0: P2 }
    | null = null;

  constructor(private host: RulerHost) {
    this.el = document.createElement("div");
    this.el.className = "ruler";
    this.el.hidden = true;
    this.el.setAttribute("role", "slider");
    this.el.setAttribute("aria-label", "Ruler");
    this.el.innerHTML = rulerSvg();
    this.label = document.createElement("div");
    this.label.className = "ruler-angle";
    this.readout = document.createElement("div");
    this.readout.className = "ruler-readout";
    this.readout.hidden = true;
    const grips = [0, 1].map((i) => {
      const g = document.createElement("div");
      g.className = `ruler-grip ${i ? "end" : "start"}`;
      g.title = "Drag to rotate";
      return g;
    });
    this.el.append(this.label, this.readout, ...grips);
    this.bind();
  }

  show(center: P2) {
    this.visible = true;
    [this.cx, this.cy] = center;
    this.angle = 0;
    this.el.hidden = false;
    this.layout();
  }

  hide() {
    this.visible = false;
    this.el.hidden = true;
    this.readout.hidden = true;
  }

  /** Positions and sizes the element for the current zoom. */
  layout() {
    if (!this.visible) return;
    const z = this.host.zoom();
    const s = this.el.style;
    s.width = `${LENGTH * z}px`;
    s.height = `${HEIGHT * z}px`;
    // Rotate about the centre (transform-origin: centre).
    const [ox, oy] = this.host.pageOrigin();
    s.transform = `translate(${ox + (this.cx - LENGTH / 2) * z}px, ${oy + (this.cy - HEIGHT / 2) * z}px) rotate(${this.angle}rad)`;
    // Keep the labels upright-ish and readable.
    let deg = ((Math.round((this.angle * 180) / Math.PI) % 180) + 180) % 180;
    if (deg > 90) deg = 180 - deg;
    this.label.textContent = `${deg}°`;
    // Rotation grips stay within reach even when the ruler is wider than the view.
    const grip = Math.max(40, Math.min((LENGTH * z) / 2 - 24, this.host.viewWidth() * 0.36));
    this.el.style.setProperty("--grip", `${grip}px`);
  }

  /** The ruler's two long edges, in page units. */
  edges(): RulerEdge[] {
    const dir: P2 = [Math.cos(this.angle), Math.sin(this.angle)];
    const up: P2 = [Math.sin(this.angle), -Math.cos(this.angle)]; // towards the top edge
    const half = HEIGHT / 2;
    const along = LENGTH / 2;
    const top: P2 = [this.cx + up[0] * half - dir[0] * along, this.cy + up[1] * half - dir[1] * along];
    const bottom: P2 = [this.cx - up[0] * half - dir[0] * along, this.cy - up[1] * half - dir[1] * along];
    return [
      { a: top, dir, normal: up },
      { a: bottom, dir, normal: [-up[0], -up[1]] },
    ];
  }

  /**
   * The edge a stroke starting at `p` should lock to: within `band` outside
   * an edge (and roughly along the ruler), else null.
   */
  edgeNear(p: P2, band: number): RulerEdge | null {
    if (!this.visible) return null;
    for (const e of this.edges()) {
      const rel: P2 = [p[0] - e.a[0], p[1] - e.a[1]];
      const out = rel[0] * e.normal[0] + rel[1] * e.normal[1];
      const t = rel[0] * e.dir[0] + rel[1] * e.dir[1];
      if (out >= -2 && out <= band && t >= -band && t <= LENGTH + band) return e;
    }
    return null;
  }

  /** Projects `p` onto the line `offset` away from the edge (ink sits against the ruler). */
  static project(e: RulerEdge, p: P2, offset: number): P2 {
    const t = (p[0] - e.a[0]) * e.dir[0] + (p[1] - e.a[1]) * e.dir[1];
    return [e.a[0] + e.dir[0] * t + e.normal[0] * offset, e.a[1] + e.dir[1] * t + e.normal[1] * offset];
  }

  /** Shows a measurement while drawing along the ruler (null hides it). */
  setReadout(text: string | null) {
    this.readout.hidden = text === null;
    if (text !== null) this.readout.textContent = text;
  }

  // --- moving and rotating --------------------------------------------------

  private wheelUntil = 0;

  /**
   * True right after the wheel rotated the ruler: the next notches keep
   * rotating it even if it has turned out from under the pointer.
   */
  inWheelSession(): boolean {
    return this.visible && performance.now() < this.wheelUntil;
  }

  wheelRotate(e: WheelEvent) {
    const step = ((e.shiftKey ? 15 : 1) * Math.PI) / 180;
    const deg = Math.round(((this.angle + Math.sign(e.deltaY || e.deltaX) * step) * 180) / Math.PI);
    this.angle = (deg * Math.PI) / 180;
    this.wheelUntil = performance.now() + 400;
    this.layout();
  }

  private bind() {
    const el = this.el;
    el.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      e.stopPropagation();
      try {
        el.setPointerCapture(e.pointerId);
      } catch {
        /* not an active pointer (synthetic events) */
      }
      const p = this.host.toPage(e.clientX, e.clientY);
      this.pointers.set(e.pointerId, p);
      if (this.pointers.size === 2) {
        const [a, b] = [...this.pointers.values()];
        this.drag = { kind: "twist", a0: Math.atan2(b[1] - a[1], b[0] - a[0]), angle0: this.angle, m0: [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2], c0: [this.cx, this.cy] };
      } else if ((e.target as HTMLElement).classList.contains("ruler-grip")) {
        this.drag = { kind: "rotate", a0: Math.atan2(p[1] - this.cy, p[0] - this.cx), angle0: this.angle };
      } else {
        this.drag = { kind: "move", start: p, c0: [this.cx, this.cy] };
      }
      el.classList.add("active");
    });
    el.addEventListener("pointermove", (e) => {
      if (!this.pointers.has(e.pointerId) || !this.drag) return;
      const p = this.host.toPage(e.clientX, e.clientY);
      this.pointers.set(e.pointerId, p);
      const d = this.drag;
      if (d.kind === "move") {
        this.cx = d.c0[0] + p[0] - d.start[0];
        this.cy = d.c0[1] + p[1] - d.start[1];
      } else if (d.kind === "rotate") {
        this.angle = snapAngle(d.angle0 + Math.atan2(p[1] - this.cy, p[0] - this.cx) - d.a0, e.shiftKey);
      } else if (this.pointers.size >= 2) {
        const [a, b] = [...this.pointers.values()];
        const m: P2 = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
        this.angle = snapAngle(d.angle0 + Math.atan2(b[1] - a[1], b[0] - a[0]) - d.a0, false);
        this.cx = d.c0[0] + m[0] - d.m0[0];
        this.cy = d.c0[1] + m[1] - d.m0[1];
      }
      this.layout();
    });
    const up = (e: PointerEvent) => {
      if (!this.pointers.delete(e.pointerId)) return;
      if (this.pointers.size === 1 && this.drag?.kind === "twist") {
        // One finger left: carry on moving with it.
        const p = [...this.pointers.values()][0];
        this.drag = { kind: "move", start: p, c0: [this.cx, this.cy] };
      } else if (!this.pointers.size) {
        this.drag = null;
        el.classList.remove("active");
      }
    };
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
    // Wheel over the ruler rotates it (1° per notch, 15° with Shift).
    el.addEventListener(
      "wheel",
      (e) => {
        if (e.ctrlKey) return; // let pinch-zoom through
        e.preventDefault();
        e.stopPropagation();
        this.wheelRotate(e);
      },
      { passive: false },
    );
  }
}

/** Snaps to common angles (or to 15° steps with `coarse`), in radians. */
export function snapAngle(rad: number, coarse: boolean): number {
  let deg = (rad * 180) / Math.PI;
  deg = ((deg % 360) + 360) % 360;
  if (coarse) return (Math.round(deg / 15) * 15 * Math.PI) / 180;
  for (const base of [0, 180]) {
    for (const s of SNAP_ANGLES) {
      const target = base + s;
      if (Math.abs(deg - target) < SNAP_DEG) return ((target % 360) * Math.PI) / 180;
    }
  }
  if (Math.abs(deg - 360) < SNAP_DEG) return 0;
  return (Math.round(deg) * Math.PI) / 180;
}

/** Ruler markings in page units (millimetres, numbered centimetres), scaled by the browser. */
function rulerSvg(): string {
  const ticks: string[] = [];
  const labels: string[] = [];
  const cm = Math.floor((LENGTH - 2 * PAD) / (10 * MM));
  for (let k = 0; k <= cm * 10; k++) {
    const x = (PAD + k * MM).toFixed(2);
    const len = k % 10 === 0 ? 3.2 * MM : k % 5 === 0 ? 2.2 * MM : 1.3 * MM;
    ticks.push(`M${x} 0V${len.toFixed(2)}M${x} ${HEIGHT}V${(HEIGHT - len).toFixed(2)}`);
    if (k % 10 === 0) labels.push(`<text x="${x}" y="${(5.4 * MM).toFixed(2)}">${k / 10}</text>`);
  }
  return `<svg class="ruler-marks" viewBox="0 0 ${LENGTH.toFixed(2)} ${HEIGHT.toFixed(2)}" preserveAspectRatio="none" aria-hidden="true">
    <path d="${ticks.join("")}" />
    <g class="ruler-numbers">${labels.join("")}</g>
  </svg>`;
}
