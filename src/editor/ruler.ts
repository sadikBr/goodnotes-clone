// Drawing instruments: ruler, protractor and set squares. One instrument at
// a time floats over the page: drag it to move it, drag a round grip (or
// scroll over it, or twist with two fingers) to rotate it. Strokes started
// next to an edge lock to that edge, like drawing along the real thing; the
// protractor's curved edge guides arcs, and its two arms measure angles.
// Instruments are DOM overlays positioned in page units, so moving them
// costs nothing, and they are never part of the page or exports.

import type { P2 } from "../api";

/** Inkleaf units per millimetre (units are 1/96 inch). */
export const MM = 96 / 25.4;

export type InstrumentKind = "ruler" | "protractor" | "square45" | "square60";

export const INSTRUMENTS: { id: InstrumentKind; label: string }[] = [
  { id: "ruler", label: "Ruler" },
  { id: "protractor", label: "Protractor" },
  { id: "square45", label: "Set square 45°" },
  { id: "square60", label: "Set square 30°/60°" },
];

/** Angles instruments snap to while rotating (degrees), and how close it must be. */
const SNAP_ANGLES = [0, 15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180];
const SNAP_DEG = 2.5;

/** A guide a stroke can lock to, in page units. */
export type Guide =
  /** A straight edge from `a` along `dir`; `normal` points away from the instrument. */
  | { kind: "line"; a: P2; dir: P2; normal: P2 }
  /** A circular edge around `c` with radius `r` (ink goes outside it). */
  | { kind: "arc"; c: P2; r: number };

/** A straight edge. */
export type RulerEdge = Extract<Guide, { kind: "line" }>;

export interface RulerHost {
  /** Page coordinates of a client (screen) point. */
  toPage(clientX: number, clientY: number): P2;
  zoom(): number;
  /** Width of the visible page area, in screen pixels. */
  viewWidth(): number;
  /** Where the page's top-left corner is, in the instrument layer's pixels. */
  pageOrigin(): P2;
}

// --- shapes -------------------------------------------------------------------
// Each instrument is described in local units (page units, y down) around
// its pivot (0, 0), which is the point it rotates about.

interface Shape {
  /** SVG path of the body (local units). */
  outline: string;
  /** Straight edges a stroke can follow. */
  lines: { a: P2; b: P2 }[];
  /** The protractor's curved edge. */
  arc?: { r: number };
  /** Local bounding box. */
  box: { x: number; y: number; w: number; h: number };
  /** Markings (SVG, local units). */
  marks: string;
  /** Rotation grips (local units); the ruler's are placed by layout. */
  grips: P2[];
  /** Where the angle label goes (local units). */
  labelAt: P2;
}

const RULER_L = 300 * MM;
const RULER_H = 15 * MM;
const PROT_R = 75 * MM;
const PROT_BASE = 9 * MM;

const f = (n: number) => n.toFixed(2);

/** Millimetre ticks along a→b (pointing `inward`), starting `from` units in, numbered every cm. */
function tickMarks(a: P2, b: P2, inward: P2, from: number, numbers: boolean): string {
  const len = Math.hypot(b[0] - a[0], b[1] - a[1]);
  const d: P2 = [(b[0] - a[0]) / len, (b[1] - a[1]) / len];
  const parts: string[] = [];
  const labels: string[] = [];
  for (let k = 0; from + k * MM <= len - 2 * MM; k++) {
    const t = from + k * MM;
    const l = k % 10 === 0 ? 3.2 * MM : k % 5 === 0 ? 2.2 * MM : 1.3 * MM;
    const p: P2 = [a[0] + d[0] * t, a[1] + d[1] * t];
    parts.push(`M${f(p[0])} ${f(p[1])}l${f(inward[0] * l)} ${f(inward[1] * l)}`);
    if (numbers && k % 10 === 0) {
      const q: P2 = [p[0] + inward[0] * 5.4 * MM, p[1] + inward[1] * 5.4 * MM];
      let rot = (Math.atan2(d[1], d[0]) * 180) / Math.PI;
      if (rot > 90 || rot < -90) rot += 180; // keep numbers upright
      labels.push(`<text transform="translate(${f(q[0])} ${f(q[1])}) rotate(${rot.toFixed(1)})" dy="0.35em">${k / 10}</text>`);
    }
  }
  return `<path class="ticks" d="${parts.join("")}"/>${labels.join("")}`;
}

/** Outward unit normal of edge a→b for a shape around the origin. */
function outward(a: P2, b: P2): { d: P2; n: P2; len: number } {
  const len = Math.hypot(b[0] - a[0], b[1] - a[1]);
  const d: P2 = [(b[0] - a[0]) / len, (b[1] - a[1]) / len];
  const mid: P2 = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
  let n: P2 = [-d[1], d[0]];
  if (n[0] * mid[0] + n[1] * mid[1] < 0) n = [-n[0], -n[1]];
  return { d, n, len };
}

function polygonShape(verts: P2[], markLegs: [number, number][]): Shape {
  // The pivot is the centroid.
  const g: P2 = [verts.reduce((s, v) => s + v[0], 0) / verts.length, verts.reduce((s, v) => s + v[1], 0) / verts.length];
  const v = verts.map((p) => [p[0] - g[0], p[1] - g[1]] as P2);
  const xs = v.map((p) => p[0]);
  const ys = v.map((p) => p[1]);
  const box = { x: Math.min(...xs), y: Math.min(...ys), w: Math.max(...xs) - Math.min(...xs), h: Math.max(...ys) - Math.min(...ys) };
  const lines = v.map((p, i) => ({ a: p, b: v[(i + 1) % v.length] }));
  const path = (pts: P2[]) => `M${pts.map((p) => `${f(p[0])} ${f(p[1])}`).join("L")}Z`;
  // Decorative inner cut-out, like a real set square.
  let marks = `<path class="cutout" d="${path(v.map((p) => [p[0] * 0.4, p[1] * 0.4] as P2))}"/>`;
  for (const [i, j] of markLegs) {
    const { n } = outward(v[i], v[j]);
    marks += tickMarks(v[i], v[j], [-n[0], -n[1]], 4 * MM, true);
  }
  // Grips near the two acute corners, pulled towards the middle.
  const grips: P2[] = [v[1], v[2]].map((p) => [p[0] * 0.6, p[1] * 0.6] as P2);
  return { outline: path(v), lines, box, marks, grips, labelAt: [0, 0] };
}

function makeShape(kind: InstrumentKind): Shape {
  switch (kind) {
    case "ruler": {
      const L = RULER_L;
      const H = RULER_H;
      const tl: P2 = [-L / 2, -H / 2];
      const tr: P2 = [L / 2, -H / 2];
      const bl: P2 = [-L / 2, H / 2];
      const br: P2 = [L / 2, H / 2];
      return {
        outline: `M${f(-L / 2)} ${f(-H / 2)}H${f(L / 2)}V${f(H / 2)}H${f(-L / 2)}Z`,
        lines: [
          { a: tl, b: tr },
          { a: bl, b: br },
        ],
        box: { x: -L / 2, y: -H / 2, w: L, h: H },
        marks: tickMarks(tl, tr, [0, 1], 6 * MM, true) + tickMarks(bl, br, [0, -1], 6 * MM, false),
        grips: [
          [-L / 4, 0],
          [L / 4, 0],
        ],
        labelAt: [0, 0],
      };
    }
    case "protractor": {
      const R = PROT_R;
      const B = PROT_BASE;
      const ticks: string[] = [];
      const labels: string[] = [];
      for (let d = 0; d <= 180; d++) {
        const th = (d * Math.PI) / 180;
        const u: P2 = [Math.cos(th), -Math.sin(th)];
        const l = d % 10 === 0 ? 6 * MM : d % 5 === 0 ? 4 * MM : 2.2 * MM;
        ticks.push(`M${f(u[0] * R)} ${f(u[1] * R)}L${f(u[0] * (R - l))} ${f(u[1] * (R - l))}`);
        if (d % 10 === 0) {
          const rot = 90 - d;
          const o: P2 = [u[0] * (R - 9.5 * MM), u[1] * (R - 9.5 * MM)];
          const i: P2 = [u[0] * (R - 15 * MM), u[1] * (R - 15 * MM)];
          labels.push(`<text transform="translate(${f(o[0])} ${f(o[1])}) rotate(${rot})" dy="0.35em">${d}</text>`);
          labels.push(`<text class="inner" transform="translate(${f(i[0])} ${f(i[1])}) rotate(${rot})" dy="0.35em">${180 - d}</text>`);
        }
      }
      const c = 2.5 * MM;
      const marks = `<path class="ticks" d="${ticks.join("")}"/>${labels.join("")}<path class="ticks" d="M${f(-R + 4 * MM)} 0H${f(R - 4 * MM)}M${f(-c)} 0H${f(c)}M0 ${f(-c)}V${f(c)}"/>`;
      return {
        outline: `M${f(-R)} ${f(B)}V0A${f(R)} ${f(R)} 0 0 1 ${f(R)} 0V${f(B)}Z`,
        lines: [{ a: [R, B], b: [-R, B] }],
        arc: { r: R },
        box: { x: -R, y: -R, w: 2 * R, h: R + B },
        marks,
        grips: [
          [-R + 6 * MM, B / 2],
          [R - 6 * MM, B / 2],
        ],
        labelAt: [0, -R * 0.34],
      };
    }
    case "square45": {
      const S = 120 * MM;
      return polygonShape(
        [
          [0, 0],
          [S, 0],
          [0, -S],
        ],
        [
          [0, 1],
          [0, 2],
        ],
      );
    }
    case "square60": {
      const S = 80 * MM;
      return polygonShape(
        [
          [0, 0],
          [S * Math.sqrt(3), 0],
          [0, -S],
        ],
        [
          [0, 1],
          [0, 2],
        ],
      );
    }
  }
}

// --- the instrument -------------------------------------------------------------

export class Ruler {
  visible = false;
  kind: InstrumentKind = "ruler";
  cx = 400;
  cy = 400;
  /** Radians, clockwise on screen. */
  angle = 0;
  /** Protractor arms (degrees, 0 = right, counter-clockwise). */
  arms: [number, number] = [30, 120];
  readonly el: HTMLDivElement;
  private shape: Shape = makeShape("ruler");
  private armsG: SVGGElement | null = null;
  private label: HTMLDivElement;
  private readout: HTMLDivElement;
  private grips: HTMLDivElement[] = [];
  private armKnobs: HTMLDivElement[] = [];
  private pointers = new Map<number, P2>();
  private drag:
    | { kind: "move"; start: P2; c0: P2 }
    | { kind: "rotate"; a0: number; angle0: number }
    | { kind: "twist"; a0: number; angle0: number; m0: P2; c0: P2 }
    | { kind: "arm"; index: 0 | 1 }
    | null = null;
  private wheelUntil = 0;

  constructor(private host: RulerHost) {
    this.el = document.createElement("div");
    this.el.className = "ruler";
    this.el.hidden = true;
    this.el.setAttribute("role", "slider");
    this.label = document.createElement("div");
    this.label.className = "ruler-angle";
    this.readout = document.createElement("div");
    this.readout.className = "ruler-readout";
    this.readout.hidden = true;
    this.build();
    this.bind();
  }

  /** Switches instrument (keeping position and rotation). */
  setKind(kind: InstrumentKind) {
    if (kind === this.kind) return;
    this.kind = kind;
    this.build();
    this.layout();
  }

  private build() {
    const sh = (this.shape = makeShape(this.kind));
    const { x, y, w, h } = sh.box;
    this.el.dataset.kind = this.kind;
    this.el.setAttribute("aria-label", INSTRUMENTS.find((i) => i.id === this.kind)!.label);
    this.el.innerHTML = `<svg class="ruler-marks" viewBox="${f(x)} ${f(y)} ${f(w)} ${f(h)}" aria-hidden="true">
      <path class="ruler-body" d="${sh.outline}"/>
      ${sh.marks}
      ${this.kind === "protractor" ? '<g class="ruler-arms"></g>' : ""}
    </svg>`;
    this.armsG = this.el.querySelector(".ruler-arms");
    this.grips = sh.grips.map(() => {
      const g = document.createElement("div");
      g.className = "ruler-grip";
      g.title = "Drag to rotate";
      return g;
    });
    this.armKnobs =
      this.kind === "protractor"
        ? [0, 1].map((i) => {
            const k = document.createElement("div");
            k.className = "ruler-arm-knob";
            k.dataset.arm = String(i);
            k.title = "Drag to measure an angle";
            return k;
          })
        : [];
    this.el.append(this.label, this.readout, ...this.grips, ...this.armKnobs);
  }

  /** Shows the instrument with its whole shape centred on `center`. */
  show(center: P2) {
    this.visible = true;
    const { x, y, w, h } = this.shape.box;
    // Protractor arms reach a little beyond its body.
    const top = this.kind === "protractor" ? y - 12 * MM : y;
    this.cx = center[0] - (x + w / 2);
    this.cy = center[1] - (top + (y + h - top) / 2);
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
    const { x, y, w, h } = this.shape.box;
    const s = this.el.style;
    s.width = `${w * z}px`;
    s.height = `${h * z}px`;
    // The pivot (local 0,0) is the transform origin.
    s.transformOrigin = `${-x * z}px ${-y * z}px`;
    const [ox, oy] = this.host.pageOrigin();
    s.transform = `translate(${ox + (this.cx + x) * z}px, ${oy + (this.cy + y) * z}px) rotate(${this.angle}rad)`;
    const at = (el: HTMLElement, p: P2) => {
      el.style.left = `${(p[0] - x) * z}px`;
      el.style.top = `${(p[1] - y) * z}px`;
    };
    // Ruler grips stay within reach even when the ruler is wider than the view.
    let grips = this.shape.grips;
    if (this.kind === "ruler") {
      const g = Math.max(40, Math.min((RULER_L * z) / 2 - 24, this.host.viewWidth() * 0.36)) / z;
      grips = [
        [-g, 0],
        [g, 0],
      ];
    }
    this.grips.forEach((el, i) => at(el, grips[i]));
    at(this.label, this.shape.labelAt);
    at(this.readout, [this.shape.labelAt[0], this.shape.labelAt[1] + 7 * MM]);
    if (this.kind === "protractor") {
      this.label.textContent = `${Math.round(Math.abs(this.arms[0] - this.arms[1]))}°`;
      this.label.title = "Angle between the arms";
      this.renderArms();
      this.armKnobs.forEach((k, i) => at(k, this.armEnd(i as 0 | 1)));
    } else {
      // The rotation, folded to 0–90°.
      let deg = ((Math.round((this.angle * 180) / Math.PI) % 180) + 180) % 180;
      if (deg > 90) deg = 180 - deg;
      this.label.textContent = `${deg}°`;
      this.label.title = "Rotation";
    }
  }

  private armEnd(i: 0 | 1): P2 {
    const th = (this.arms[i] * Math.PI) / 180;
    const r = PROT_R + 10 * MM;
    return [Math.cos(th) * r, -Math.sin(th) * r];
  }

  private renderArms() {
    if (!this.armsG) return;
    const [a, b] = [this.armEnd(0), this.armEnd(1)];
    // The measured angle as a small arc between the arms.
    const r = 14 * MM;
    const t0 = (Math.min(...this.arms) * Math.PI) / 180;
    const t1 = (Math.max(...this.arms) * Math.PI) / 180;
    const p0: P2 = [Math.cos(t0) * r, -Math.sin(t0) * r];
    const p1: P2 = [Math.cos(t1) * r, -Math.sin(t1) * r];
    this.armsG.innerHTML = `<path class="arm" d="M0 0L${f(a[0])} ${f(a[1])}M0 0L${f(b[0])} ${f(b[1])}"/><path class="arm-arc" d="M${f(p0[0])} ${f(p0[1])}A${f(r)} ${f(r)} 0 0 0 ${f(p1[0])} ${f(p1[1])}"/>`;
  }

  // --- geometry ---------------------------------------------------------------

  private toPage(p: P2): P2 {
    const c = Math.cos(this.angle);
    const s = Math.sin(this.angle);
    return [this.cx + p[0] * c - p[1] * s, this.cy + p[0] * s + p[1] * c];
  }

  private toLocal(p: P2): P2 {
    const c = Math.cos(this.angle);
    const s = Math.sin(this.angle);
    const dx = p[0] - this.cx;
    const dy = p[1] - this.cy;
    return [dx * c + dy * s, -dx * s + dy * c];
  }

  private rot(v: P2): P2 {
    const c = Math.cos(this.angle);
    const s = Math.sin(this.angle);
    return [v[0] * c - v[1] * s, v[0] * s + v[1] * c];
  }

  /** The straight edges, in page units, with normals pointing away from the instrument. */
  edges(): RulerEdge[] {
    return this.shape.lines.map(({ a, b }) => {
      const { d, n } = outward(a, b);
      return { kind: "line", a: this.toPage(a), dir: this.rot(d), normal: this.rot(n) };
    });
  }

  /**
   * The guide a stroke starting at `p` should lock to: within `band` outside
   * an edge (and roughly along it), else null.
   */
  edgeNear(p: P2, band: number): Guide | null {
    if (!this.visible) return null;
    const q = this.toLocal(p);
    for (const { a, b } of this.shape.lines) {
      const { d, n, len } = outward(a, b);
      const rel: P2 = [q[0] - a[0], q[1] - a[1]];
      const out = rel[0] * n[0] + rel[1] * n[1];
      const t = rel[0] * d[0] + rel[1] * d[1];
      if (out >= -2 && out <= band && t >= -band && t <= len + band) return { kind: "line", a: this.toPage(a), dir: this.rot(d), normal: this.rot(n) };
    }
    if (this.shape.arc) {
      const r = this.shape.arc.r;
      const dist = Math.hypot(q[0], q[1]);
      if (dist >= r - 2 && dist <= r + band && q[1] <= band) return { kind: "arc", c: [this.cx, this.cy], r };
    }
    return null;
  }

  /** Projects `p` onto a guide, `offset` outside it (so ink sits against the edge). */
  static project(g: Guide, p: P2, offset: number): P2 {
    if (g.kind === "arc") {
      const dx = p[0] - g.c[0];
      const dy = p[1] - g.c[1];
      const d = Math.hypot(dx, dy) || 1;
      return [g.c[0] + (dx / d) * (g.r + offset), g.c[1] + (dy / d) * (g.r + offset)];
    }
    const t = (p[0] - g.a[0]) * g.dir[0] + (p[1] - g.a[1]) * g.dir[1];
    return [g.a[0] + g.dir[0] * t + g.normal[0] * offset, g.a[1] + g.dir[1] * t + g.normal[1] * offset];
  }

  /** Shows a measurement while drawing along an edge (null hides it). */
  setReadout(text: string | null) {
    this.readout.hidden = text === null;
    if (text !== null) this.readout.textContent = text;
  }

  // --- moving and rotating ----------------------------------------------------

  /**
   * True right after the wheel rotated the instrument: the next notches keep
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
      const target = e.target as Element;
      e.preventDefault();
      e.stopPropagation();
      try {
        // Capture on the pressed part (the container itself takes no input).
        target.setPointerCapture(e.pointerId);
      } catch {
        /* not an active pointer (synthetic events) */
      }
      const p = this.host.toPage(e.clientX, e.clientY);
      this.pointers.set(e.pointerId, p);
      const knob = target.closest(".ruler-arm-knob") as HTMLElement | null;
      if (this.pointers.size === 2) {
        const [a, b] = [...this.pointers.values()];
        this.drag = { kind: "twist", a0: Math.atan2(b[1] - a[1], b[0] - a[0]), angle0: this.angle, m0: [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2], c0: [this.cx, this.cy] };
      } else if (knob) {
        this.drag = { kind: "arm", index: Number(knob.dataset.arm) as 0 | 1 };
      } else if (target.closest(".ruler-grip")) {
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
      } else if (d.kind === "arm") {
        const q = this.toLocal(p);
        let deg = (Math.atan2(-q[1], q[0]) * 180) / Math.PI;
        if (deg < -90) deg = 180; // dragged below the base on the left
        deg = Math.min(180, Math.max(0, deg));
        this.arms[d.index] = e.shiftKey ? Math.round(deg / 5) * 5 : Math.round(deg);
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
    // Wheel over the instrument rotates it (1° per notch, 15° with Shift).
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
