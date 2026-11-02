// Vector PDF export. `PdfPainter` stands in for a CanvasRenderingContext2D,
// so the page is drawn by the same code that draws it on screen, but every
// path, line and letter becomes a PDF drawing operator instead of pixels.
// Text is drawn from the fonts' glyph outlines (identical to the screen) and
// an invisible text layer on top keeps it selectable and searchable.
// `PdfDocument` assembles pages into a compressed PDF file.

import { faceFor, parseFontCss, type LoadedFace } from "./pdfFonts";

type M = [number, number, number, number, number, number];

const IDENTITY: M = [1, 0, 0, 1, 0, 0];
/** PDF points per Inkleaf unit (units are 1/96 inch). */
export const UNIT_PT = 72 / 96;

function mul(m: M, n: M): M {
  // Returns m·n (apply n first, then m), canvas-style.
  return [m[0] * n[0] + m[2] * n[1], m[1] * n[0] + m[3] * n[1], m[0] * n[2] + m[2] * n[3], m[1] * n[2] + m[3] * n[3], m[0] * n[4] + m[2] * n[5] + m[4], m[1] * n[4] + m[3] * n[5] + m[5]];
}

/** Compact number formatting for PDF operators. */
export function num(n: number): string {
  if (!Number.isFinite(n)) return "0";
  const s = (Math.round(n * 1000) / 1000).toFixed(3);
  return s.replace(/\.?0+$/, "") || "0";
}

export interface Rgba {
  r: number;
  g: number;
  b: number;
  a: number;
}

let colorCtx: CanvasRenderingContext2D | null | undefined;
/** Parses a CSS colour (hex, rgb()/rgba(), or anything the browser knows). */
export function parseColor(input: string): Rgba {
  const s = input.trim().toLowerCase();
  if (s === "transparent") return { r: 0, g: 0, b: 0, a: 0 };
  let m = /^#([0-9a-f]{3,8})$/.exec(s);
  if (m) {
    let h = m[1];
    if (h.length === 3 || h.length === 4) h = [...h].map((c) => c + c).join("");
    if (h.length === 6 || h.length === 8) {
      const v = (i: number) => parseInt(h.slice(i, i + 2), 16);
      return { r: v(0) / 255, g: v(2) / 255, b: v(4) / 255, a: h.length === 8 ? v(6) / 255 : 1 };
    }
  }
  m = /^rgba?\(([^)]+)\)$/.exec(s);
  if (m) {
    const parts = m[1].split(/[\s,/]+/).filter(Boolean).map((p) => (p.endsWith("%") ? (parseFloat(p) / 100) * 255 : parseFloat(p)));
    const a = m[1].includes("%") && parts.length > 3 ? parts[3] / 255 : (parts[3] ?? 1);
    return { r: (parts[0] ?? 0) / 255, g: (parts[1] ?? 0) / 255, b: (parts[2] ?? 0) / 255, a };
  }
  // Named colours etc.: let the browser normalise it, then parse that.
  if (colorCtx === undefined) colorCtx = typeof document !== "undefined" ? document.createElement("canvas").getContext("2d") : null;
  if (colorCtx) {
    colorCtx.fillStyle = "#000000";
    colorCtx.fillStyle = input;
    const norm = String(colorCtx.fillStyle);
    if (norm !== input) return parseColor(norm);
  }
  return { r: 0, g: 0, b: 0, a: 1 };
}

/** Measures text like a canvas would (the real canvas in the app). */
export interface Measurer {
  measure(font: string, text: string, baseline: CanvasTextBaseline): TextMetrics;
}

export function canvasMeasurer(): Measurer {
  const ctx = document.createElement("canvas").getContext("2d")!;
  return {
    measure(font, text, baseline) {
      ctx.font = font;
      ctx.textBaseline = baseline;
      return ctx.measureText(text);
    },
  };
}

interface State {
  m: M;
  fillStyle: string;
  strokeStyle: string;
  lineWidth: number;
  lineCap: CanvasLineCap;
  lineJoin: CanvasLineJoin;
  globalAlpha: number;
  composite: string;
  font: string;
  textBaseline: CanvasTextBaseline;
  dash: number[];
  /** Stroke alpha factor standing in for the pencil grain texture. */
  grain: number;
}

const GRAIN = {} as CanvasPattern;
/** Average opacity of the pencil grain tile (alpha 70..255). */
const GRAIN_ALPHA = (70 + 255) / 2 / 255;

/** Image drawn on a page; encoded when the document is finished. */
export type PdfImageSource = HTMLImageElement | HTMLCanvasElement | ImageBitmap;

export interface PageRecording {
  width: number;
  height: number;
  ops: string[];
  /** ExtGState name → [alpha, blend mode]. */
  gstates: Map<string, [number, string]>;
  /** Image name → source. */
  images: Map<string, PdfImageSource>;
  usesFont: boolean;
}

const LATIN1_EXTRA: Record<string, number> = {
  "€": 0x80, "‚": 0x82, "ƒ": 0x83, "„": 0x84, "…": 0x85, "†": 0x86, "‡": 0x87, "ˆ": 0x88, "‰": 0x89, "Š": 0x8a, "‹": 0x8b, "Œ": 0x8c, "Ž": 0x8e,
  "‘": 0x91, "’": 0x92, "“": 0x93, "”": 0x94, "•": 0x95, "–": 0x96, "—": 0x97, "˜": 0x98, "™": 0x99, "š": 0x9a, "›": 0x9b, "œ": 0x9c, "ž": 0x9e, "Ÿ": 0x9f,
};

/** Encodes text for a WinAnsi (Helvetica) PDF string; unknown characters become "?". */
export function winAnsiString(text: string): string {
  let out = "(";
  for (const ch of text) {
    const c = ch.codePointAt(0)!;
    const code = LATIN1_EXTRA[ch] ?? ((c >= 0x20 && c < 0x7f) || (c >= 0xa0 && c <= 0xff) ? c : 0x3f);
    if (code === 0x28 || code === 0x29 || code === 0x5c) out += "\\" + String.fromCharCode(code);
    else if (code < 0x20 || code > 0x7e) out += "\\" + code.toString(8).padStart(3, "0");
    else out += String.fromCharCode(code);
  }
  return out + ")";
}

/**
 * A drawing context that records PDF operators. Implements the parts of
 * CanvasRenderingContext2D the page renderer uses; coordinates are in
 * Inkleaf units with y pointing down, like the canvas.
 */
export class PdfPainter {
  private st: State = {
    m: IDENTITY,
    fillStyle: "#000000",
    strokeStyle: "#000000",
    lineWidth: 1,
    lineCap: "butt",
    lineJoin: "miter",
    globalAlpha: 1,
    composite: "source-over",
    font: "10px sans-serif",
    textBaseline: "alphabetic",
    dash: [],
    grain: 1,
  };
  private stack: State[] = [];
  private path: string[] = [];
  private cur: [number, number] | null = null;
  private start: [number, number] | null = null;
  readonly rec: PageRecording;
  // Canvas properties the renderer sets but PDF export ignores.
  shadowColor = "transparent";
  shadowBlur = 0;
  shadowOffsetX = 0;
  shadowOffsetY = 0;

  constructor(
    width: number,
    height: number,
    private measurer: Measurer,
  ) {
    this.rec = { width, height, ops: [], gstates: new Map(), images: new Map(), usesFont: false };
  }

  // --- state -------------------------------------------------------------
  get fillStyle(): string { return this.st.fillStyle; }
  set fillStyle(v: string | CanvasGradient | CanvasPattern) { if (typeof v === "string") this.st.fillStyle = v; }
  get strokeStyle(): string { return this.st.strokeStyle; }
  set strokeStyle(v: string | CanvasGradient | CanvasPattern) {
    // A pattern is the pencil grain over the colour set just before: keep the
    // colour at the grain's average strength.
    if (typeof v === "string") [this.st.strokeStyle, this.st.grain] = [v, 1];
    else if (v === GRAIN) this.st.grain = GRAIN_ALPHA;
  }
  get lineWidth(): number { return this.st.lineWidth; }
  set lineWidth(v: number) { if (v > 0 && Number.isFinite(v)) this.st.lineWidth = v; }
  get lineCap(): CanvasLineCap { return this.st.lineCap; }
  set lineCap(v: CanvasLineCap) { this.st.lineCap = v; }
  get lineJoin(): CanvasLineJoin { return this.st.lineJoin; }
  set lineJoin(v: CanvasLineJoin) { this.st.lineJoin = v; }
  get globalAlpha(): number { return this.st.globalAlpha; }
  set globalAlpha(v: number) { if (v >= 0 && v <= 1) this.st.globalAlpha = v; }
  get globalCompositeOperation(): string { return this.st.composite; }
  set globalCompositeOperation(v: string) { this.st.composite = v; }
  get font(): string { return this.st.font; }
  set font(v: string) { this.st.font = v; }
  get textBaseline(): CanvasTextBaseline { return this.st.textBaseline; }
  set textBaseline(v: CanvasTextBaseline) { this.st.textBaseline = v; }
  setLineDash(d: number[]) { this.st.dash = d.slice(); }
  getLineDash(): number[] { return this.st.dash.slice(); }
  /** Patterns (pencil grain) become a marker; see the strokeStyle setter. */
  createPattern(): CanvasPattern { return GRAIN; }

  save() { this.stack.push({ ...this.st, dash: this.st.dash.slice() }); }
  restore() { const s = this.stack.pop(); if (s) this.st = s; }
  translate(x: number, y: number) { this.st.m = mul(this.st.m, [1, 0, 0, 1, x, y]); }
  scale(x: number, y: number) { this.st.m = mul(this.st.m, [x, 0, 0, y, 0, 0]); }
  rotate(a: number) { const c = Math.cos(a), s = Math.sin(a); this.st.m = mul(this.st.m, [c, s, -s, c, 0, 0]); }
  setTransform(a: number, b: number, c: number, d: number, e: number, f: number) { this.st.m = [a, b, c, d, e, f]; }
  resetTransform() { this.st.m = IDENTITY; }

  private tp(x: number, y: number): [number, number] {
    const m = this.st.m;
    return [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]];
  }
  private scaleFactor(): number {
    const m = this.st.m;
    return Math.sqrt(Math.abs(m[0] * m[3] - m[1] * m[2]));
  }

  // --- paths -------------------------------------------------------------
  beginPath() { this.path = []; this.cur = null; this.start = null; }
  moveTo(x: number, y: number) {
    const p = this.tp(x, y);
    this.path.push(`${num(p[0])} ${num(p[1])} m`);
    this.cur = p;
    this.start = p;
  }
  lineTo(x: number, y: number) {
    if (!this.cur) return this.moveTo(x, y);
    const p = this.tp(x, y);
    this.path.push(`${num(p[0])} ${num(p[1])} l`);
    this.cur = p;
  }
  private cubicAbs(c1: [number, number], c2: [number, number], p: [number, number]) {
    this.path.push(`${num(c1[0])} ${num(c1[1])} ${num(c2[0])} ${num(c2[1])} ${num(p[0])} ${num(p[1])} c`);
    this.cur = p;
  }
  bezierCurveTo(x1: number, y1: number, x2: number, y2: number, x: number, y: number) {
    if (!this.cur) this.moveTo(x1, y1);
    this.cubicAbs(this.tp(x1, y1), this.tp(x2, y2), this.tp(x, y));
  }
  quadraticCurveTo(qx: number, qy: number, x: number, y: number) {
    if (!this.cur) this.moveTo(qx, qy);
    const p0 = this.cur!;
    const q = this.tp(qx, qy);
    const p = this.tp(x, y);
    this.cubicAbs([p0[0] + (2 / 3) * (q[0] - p0[0]), p0[1] + (2 / 3) * (q[1] - p0[1])], [p[0] + (2 / 3) * (q[0] - p[0]), p[1] + (2 / 3) * (q[1] - p[1])], p);
  }
  closePath() {
    if (!this.cur) return;
    this.path.push("h");
    this.cur = this.start;
  }
  rect(x: number, y: number, w: number, h: number) {
    this.moveTo(x, y);
    this.lineTo(x + w, y);
    this.lineTo(x + w, y + h);
    this.lineTo(x, y + h);
    this.closePath();
  }
  arc(cx: number, cy: number, r: number, a0: number, a1: number, ccw = false) {
    this.ellipse(cx, cy, r, r, 0, a0, a1, ccw);
  }
  ellipse(cx: number, cy: number, rx: number, ry: number, rot: number, a0: number, a1: number, ccw = false) {
    let sweep = a1 - a0;
    const full = Math.PI * 2;
    if (!ccw && sweep < 0) sweep = (sweep % full) + full;
    if (ccw && sweep > 0) sweep = (sweep % full) - full;
    if (Math.abs(a1 - a0) >= full) sweep = ccw ? -full : full;
    const cr = Math.cos(rot), sr = Math.sin(rot);
    const at = (a: number): [number, number] => {
      const x = rx * Math.cos(a), y = ry * Math.sin(a);
      return [cx + x * cr - y * sr, cy + x * sr + y * cr];
    };
    const d = (a: number): [number, number] => {
      const x = -rx * Math.sin(a), y = ry * Math.cos(a);
      return [x * cr - y * sr, x * sr + y * cr];
    };
    const p0 = at(a0);
    if (this.cur) this.lineTo(p0[0], p0[1]);
    else this.moveTo(p0[0], p0[1]);
    const n = Math.max(1, Math.ceil(Math.abs(sweep) / (Math.PI / 2)));
    const step = sweep / n;
    const k = (4 / 3) * Math.tan(step / 4);
    for (let i = 0; i < n; i++) {
      const s = a0 + i * step, e = s + step;
      const ps = at(s), pe = at(e), ds = d(s), de = d(e);
      this.cubicAbs(this.tp(ps[0] + k * ds[0], ps[1] + k * ds[1]), this.tp(pe[0] - k * de[0], pe[1] - k * de[1]), this.tp(pe[0], pe[1]));
    }
  }

  // --- painting ----------------------------------------------------------
  private gstate(alpha: number, blend: string): string {
    const key = `${num(alpha)}/${blend}`;
    for (const [name, [a, b]] of this.rec.gstates) if (`${num(a)}/${b}` === key) return name;
    const name = `G${this.rec.gstates.size}`;
    this.rec.gstates.set(name, [alpha, blend]);
    return name;
  }
  private blend(): string {
    return this.st.composite === "multiply" ? "Multiply" : "Normal";
  }
  /** Colour and transparency operators; null if nothing would show. */
  private paintState(color: string, stroke: boolean): string | null {
    const c = parseColor(color);
    const alpha = c.a * this.st.globalAlpha * (stroke ? this.st.grain : 1);
    if (alpha <= 0.001) return null;
    let s = `${num(c.r)} ${num(c.g)} ${num(c.b)} ${stroke ? "RG" : "rg"}`;
    const blend = this.blend();
    if (alpha < 0.999 || blend !== "Normal") s = `/${this.gstate(alpha, blend)} gs ${s}`;
    return s;
  }
  private strokeState(): string {
    const k = this.scaleFactor();
    const cap = { butt: 0, round: 1, square: 2 }[this.st.lineCap] ?? 0;
    const join = { miter: 0, round: 1, bevel: 2 }[this.st.lineJoin] ?? 0;
    const dash = this.st.dash.length ? `[${this.st.dash.map((d) => num(d * k)).join(" ")}] 0 d` : "";
    return `${num(this.st.lineWidth * k)} w ${cap} J ${join} j ${dash}`.trim();
  }
  private emit(paint: string, body: string[]) {
    if (!body.length) return;
    this.rec.ops.push(`q ${paint}\n${body.join("\n")}\n`);
  }
  fill(rule?: CanvasFillRule) {
    const s = this.paintState(this.st.fillStyle, false);
    if (!s || !this.path.length) return;
    this.emit(s, [...this.path, `${rule === "evenodd" ? "f*" : "f"} Q`]);
  }
  stroke() {
    const s = this.paintState(this.st.strokeStyle, true);
    if (!s || !this.path.length) return;
    this.emit(`${s} ${this.strokeState()}`, [...this.path, "S Q"]);
  }
  private withRect(x: number, y: number, w: number, h: number, fn: () => void) {
    const saved = [this.path, this.cur, this.start] as const;
    this.beginPath();
    this.rect(x, y, w, h);
    fn();
    [this.path, this.cur, this.start] = [saved[0], saved[1], saved[2]];
  }
  fillRect(x: number, y: number, w: number, h: number) {
    this.shadow(x, y, w, h);
    this.withRect(x, y, w, h, () => this.fill());
  }
  /** Soft drop shadow (sticky notes), as a few stacked translucent rects. */
  private shadow(x: number, y: number, w: number, h: number) {
    const c = parseColor(this.shadowColor);
    if (c.a <= 0 || this.shadowBlur <= 0) return;
    const saved = { fill: this.st.fillStyle, alpha: this.st.globalAlpha };
    const steps = 4;
    this.st.fillStyle = `rgb(${c.r * 255}, ${c.g * 255}, ${c.b * 255})`;
    this.st.globalAlpha = saved.alpha * (c.a / steps) * 0.8;
    for (let i = steps; i >= 1; i--) {
      const spread = (this.shadowBlur / 2) * (i / steps);
      this.withRect(x - spread + this.shadowOffsetX, y - spread + this.shadowOffsetY, w + spread * 2, h + spread * 2, () => this.fill());
    }
    [this.st.fillStyle, this.st.globalAlpha] = [saved.fill, saved.alpha];
  }
  strokeRect(x: number, y: number, w: number, h: number) { this.withRect(x, y, w, h, () => this.stroke()); }

  drawImage(img: PdfImageSource, x: number, y: number, w?: number, h?: number) {
    const iw = w ?? ("naturalWidth" in img ? img.naturalWidth : img.width);
    const ih = h ?? ("naturalHeight" in img ? img.naturalHeight : img.height);
    if (!(iw > 0 && ih > 0)) return;
    let name = [...this.rec.images].find(([, src]) => src === img)?.[0];
    if (!name) {
      name = `Im${this.rec.images.size}`;
      this.rec.images.set(name, img);
    }
    // Image space is the unit square with y up; map it onto the box.
    const m = mul(this.st.m, [iw, 0, 0, -ih, x, y + ih]);
    const alpha = this.st.globalAlpha;
    const gs = alpha < 0.999 || this.blend() !== "Normal" ? `/${this.gstate(alpha, this.blend())} gs ` : "";
    this.rec.ops.push(`q ${gs}${m.map(num).join(" ")} cm /${name} Do Q\n`);
  }

  // --- text --------------------------------------------------------------
  measureText(text: string): TextMetrics {
    return this.measurer.measure(this.st.font, text, this.st.textBaseline);
  }

  /** Distance from the y passed to fillText down to the alphabetic baseline. */
  private baselineShift(text: string): number {
    if (this.st.textBaseline === "alphabetic") return 0;
    const sample = text.trim() ? text : "Hg";
    const a = this.measurer.measure(this.st.font, sample, "alphabetic");
    const b = this.measurer.measure(this.st.font, sample, this.st.textBaseline);
    return a.actualBoundingBoxAscent - b.actualBoundingBoxAscent;
  }

  fillText(text: string, x: number, y: number) {
    if (!text) return;
    const spec = parseFontCss(this.st.font);
    const face = spec && faceFor(spec);
    const paint = this.paintState(this.st.fillStyle, false);
    if (!paint) return;
    const baseY = y + this.baselineShift(text);
    if (!spec || !face || !this.glyphText(text, x, baseY, spec.size, face, paint)) {
      this.textAsImage(text, x, baseY);
      return;
    }
    this.invisibleText(text, x, baseY, spec.size);
  }

  /** Draws text from glyph outlines; false if a character has no glyph. */
  private glyphText(text: string, x: number, baseY: number, size: number, face: LoadedFace, paint: string): boolean {
    // Split into runs by which font file (Latin / Latin Extended) has the glyph.
    const runs: { font: LoadedFace["fonts"][number]; text: string }[] = [];
    for (const ch of text) {
      const font = /\s/.test(ch) ? (runs[runs.length - 1]?.font ?? face.fonts[0]) : face.fonts.find((f) => f.charToGlyphIndex(ch) > 0);
      if (!font) return false;
      if (runs[runs.length - 1]?.font === font) runs[runs.length - 1].text += ch;
      else runs.push({ font, text: ch });
    }
    const out: string[] = [];
    const skew = face.fakeItalic ? 0.25 : 0;
    let pen = x;
    for (const run of runs) {
      const path = run.font.getPath(run.text, pen, baseY, size, { kerning: true });
      const pt = (px: number, py: number) => {
        const p = this.tp(px - skew * (py - baseY), py);
        return `${num(p[0])} ${num(p[1])}`;
      };
      // Glyph outlines in local coordinates; TrueType quadratics become cubics.
      let cx = 0, cy = 0, sx = 0, sy = 0;
      for (const c of path.commands) {
        if (c.type === "M") {
          out.push(`${pt(c.x, c.y)} m`);
          [cx, cy, sx, sy] = [c.x, c.y, c.x, c.y];
        } else if (c.type === "L") {
          out.push(`${pt(c.x, c.y)} l`);
          [cx, cy] = [c.x, c.y];
        } else if (c.type === "C") {
          out.push(`${pt(c.x1, c.y1)} ${pt(c.x2, c.y2)} ${pt(c.x, c.y)} c`);
          [cx, cy] = [c.x, c.y];
        } else if (c.type === "Q") {
          out.push(`${pt(cx + (2 / 3) * (c.x1 - cx), cy + (2 / 3) * (c.y1 - cy))} ${pt(c.x + (2 / 3) * (c.x1 - c.x), c.y + (2 / 3) * (c.y1 - c.y))} ${pt(c.x, c.y)} c`);
          [cx, cy] = [c.x, c.y];
        } else if (c.type === "Z") {
          out.push("h");
          [cx, cy] = [sx, sy];
        }
      }
      pen += run.font.getAdvanceWidth(run.text, size, { kerning: true });
    }
    if (!out.length) return true;
    if (face.fakeBold) {
      // Synthetic bold like the browser: fill plus a thin outline.
      const ratio = size <= 9 ? 1 / 24 : size >= 36 ? 1 / 32 : 1 / 24 + ((size - 9) / 27) * (1 / 32 - 1 / 24);
      const stroke = paint.replace(/rg$/, "RG");
      this.emit(`${paint} ${stroke} ${num(size * ratio * this.scaleFactor())} w 1 j`, [...out, "B Q"]);
    } else {
      this.emit(paint, [...out, "f Q"]);
    }
    return true;
  }

  /** Characters the bundled fonts don't have: draw that line as a sharp image. */
  private textAsImage(text: string, x: number, baseY: number) {
    if (typeof document === "undefined") return;
    const m = this.measurer.measure(this.st.font, text, "alphabetic");
    const asc = Math.max(m.actualBoundingBoxAscent, m.fontBoundingBoxAscent || 0) + 2;
    const desc = Math.max(m.actualBoundingBoxDescent, m.fontBoundingBoxDescent || 0) + 2;
    const left = Math.max(0, m.actualBoundingBoxLeft) + 2;
    const w = Math.max(1, m.width + left + Math.max(0, m.actualBoundingBoxRight - m.width) + 2);
    const h = asc + desc;
    const k = 4 * Math.max(1, this.scaleFactor());
    const c = document.createElement("canvas");
    c.width = Math.ceil(w * k);
    c.height = Math.ceil(h * k);
    const ctx = c.getContext("2d")!;
    ctx.scale(k, k);
    ctx.font = this.st.font;
    ctx.fillStyle = this.st.fillStyle;
    ctx.fillText(text, left, asc);
    this.drawImage(c, x - left, baseY - asc, w, h);
  }

  /** Invisible Helvetica text over the glyphs, so the text can be selected and searched. */
  private invisibleText(text: string, x: number, baseY: number, size: number) {
    const target = this.measurer.measure(this.st.font, text, "alphabetic").width;
    const helv = this.measurer.measure(`${size}px Helvetica, Arial, "Liberation Sans", sans-serif`, text, "alphabetic").width;
    if (!(target > 0 && helv > 0)) return;
    const m = this.st.m;
    const [px, py] = this.tp(x, baseY);
    // Text space is y-up; the page content is drawn y-down.
    const tm = [m[0], m[1], -m[2], -m[3], px, py].map(num).join(" ");
    this.rec.usesFont = true;
    this.rec.ops.push(`BT 3 Tr /F1 ${num(size)} Tf ${num((target / helv) * 100)} Tz ${tm} Tm ${winAnsiString(text)} Tj ET\n`);
  }
}

// --- document ------------------------------------------------------------

const enc = new TextEncoder();

/** zlib (FlateDecode) compression via the platform's CompressionStream. */
export async function deflate(data: Uint8Array): Promise<Uint8Array> {
  const stream = new Blob([data as BlobPart]).stream().pipeThrough(new CompressionStream("deflate"));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

/** A PDF text string: plain when ASCII, otherwise UTF-16BE hex. */
export function pdfTextString(s: string): string {
  if (/^[\x20-\x7e]*$/.test(s)) return `(${s.replace(/[\\()]/g, (c) => "\\" + c)})`;
  let hex = "FEFF";
  for (let i = 0; i < s.length; i++) hex += s.charCodeAt(i).toString(16).padStart(4, "0");
  return `<${hex.toUpperCase()}>`;
}

interface EncodedImage {
  dict: string;
  data: Uint8Array;
  smask?: { dict: string; data: Uint8Array };
}

function dataUrlBytes(url: string): Uint8Array | null {
  const m = /^data:image\/jpeg;base64,(.*)$/i.exec(url);
  if (!m) return null;
  const bin = atob(m[1]);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

async function canvasJpeg(c: HTMLCanvasElement, quality: number): Promise<Uint8Array> {
  const blob = await new Promise<Blob | null>((res) => c.toBlob(res, "image/jpeg", quality));
  if (!blob) throw new Error("could not encode an image");
  return new Uint8Array(await blob.arrayBuffer());
}

/** Canvases known to be fully opaque (PDF page backgrounds): stored as JPEG. */
const opaqueCanvases = new WeakSet<HTMLCanvasElement>();
export function markOpaque(c: HTMLCanvasElement): HTMLCanvasElement {
  opaqueCanvases.add(c);
  return c;
}

/** Largest side of embedded user images, in pixels. */
const MAX_IMAGE_SIDE = 3000;

/**
 * Encodes an image for embedding. PDF page backgrounds become JPEG; JPEG
 * photos are embedded as they are; anything else (PNG images, text drawn
 * as an image) is stored losslessly with its transparency.
 */
async function encodeImage(src: PdfImageSource): Promise<EncodedImage> {
  const w0 = "naturalWidth" in src ? src.naturalWidth : src.width;
  const h0 = "naturalHeight" in src ? src.naturalHeight : src.height;
  if (src instanceof HTMLCanvasElement && opaqueCanvases.has(src)) {
    return { dict: `/Type /XObject /Subtype /Image /Width ${src.width} /Height ${src.height} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode`, data: await canvasJpeg(src, 0.9) };
  }
  if (src instanceof HTMLImageElement) {
    const jpeg = dataUrlBytes(src.src);
    if (jpeg) return { dict: `/Type /XObject /Subtype /Image /Width ${w0} /Height ${h0} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode`, data: jpeg };
  }
  const k = Math.min(1, MAX_IMAGE_SIDE / Math.max(w0, h0));
  const w = Math.max(1, Math.round(w0 * k));
  const h = Math.max(1, Math.round(h0 * k));
  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  const ctx = c.getContext("2d")!;
  ctx.drawImage(src, 0, 0, w, h);
  const px = ctx.getImageData(0, 0, w, h).data;
  const rgb = new Uint8Array(w * h * 3);
  const alpha = new Uint8Array(w * h);
  let transparent = false;
  for (let i = 0, j = 0; i < w * h; i++, j += 4) {
    rgb[i * 3] = px[j];
    rgb[i * 3 + 1] = px[j + 1];
    rgb[i * 3 + 2] = px[j + 2];
    alpha[i] = px[j + 3];
    if (px[j + 3] < 255) transparent = true;
  }
  const base = `/Type /XObject /Subtype /Image /Width ${w} /Height ${h} /BitsPerComponent 8 /Filter /FlateDecode`;
  return {
    dict: `${base} /ColorSpace /DeviceRGB`,
    data: await deflate(rgb),
    smask: transparent ? { dict: `${base} /ColorSpace /DeviceGray`, data: await deflate(alpha) } : undefined,
  };
}

/** Builds a PDF from recorded pages. */
export class PdfDocument {
  private chunks: Uint8Array[] = [];
  private offsets: number[] = [];
  private length = 0;
  private pageIds: number[] = [];
  private imageIds = new Map<PdfImageSource, number>();
  private nextId = 4; // 1 catalog, 2 pages, 3 font
  private pending: { id: number; body: string | Uint8Array; stream?: Uint8Array }[] = [];

  private alloc(): number {
    return this.nextId++;
  }
  private put(id: number, body: string, stream?: Uint8Array) {
    this.pending.push({ id, body, stream });
  }

  async addPage(rec: PageRecording): Promise<void> {
    const pageId = this.alloc();
    const contentId = this.alloc();
    const xobjects: string[] = [];
    for (const [name, src] of rec.images) {
      let id = this.imageIds.get(src);
      if (id === undefined) {
        id = this.alloc();
        this.imageIds.set(src, id);
        const img = await encodeImage(src);
        let dict = img.dict;
        if (img.smask) {
          const maskId = this.alloc();
          this.put(maskId, `<< ${img.smask.dict} /Length ${img.smask.data.length} >>`, img.smask.data);
          dict += ` /SMask ${maskId} 0 R`;
        }
        this.put(id, `<< ${dict} /Length ${img.data.length} >>`, img.data);
      }
      xobjects.push(`/${name} ${id} 0 R`);
    }
    const gs = [...rec.gstates].map(([name, [a, blend]]) => `/${name} << /Type /ExtGState /CA ${num(a)} /ca ${num(a)} /BM /${blend} >>`);
    const res = [
      gs.length ? `/ExtGState << ${gs.join(" ")} >>` : "",
      xobjects.length ? `/XObject << ${xobjects.join(" ")} >>` : "",
      rec.usesFont ? "/Font << /F1 3 0 R >>" : "",
    ]
      .filter(Boolean)
      .join(" ");
    const wPt = rec.width * UNIT_PT;
    const hPt = rec.height * UNIT_PT;
    // Units → points, with y pointing down like the canvas.
    const content = enc.encode(`${num(UNIT_PT)} 0 0 ${num(-UNIT_PT)} 0 ${num(hPt)} cm\n${rec.ops.join("")}`);
    const packed = await deflate(content);
    this.put(contentId, `<< /Length ${packed.length} /Filter /FlateDecode >>`, packed);
    this.put(pageId, `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ${num(wPt)} ${num(hPt)}] /Resources << ${res} >> /Contents ${contentId} 0 R >>`);
    this.pageIds.push(pageId);
  }

  private write(bytes: Uint8Array | string) {
    const b = typeof bytes === "string" ? enc.encode(bytes) : bytes;
    this.chunks.push(b);
    this.length += b.length;
  }
  private writeObject(id: number, body: string, stream?: Uint8Array) {
    this.offsets[id] = this.length;
    this.write(`${id} 0 obj\n${body}\n`);
    if (stream) {
      this.write("stream\n");
      this.write(stream);
      this.write("\nendstream\n");
    }
    this.write("endobj\n");
  }

  /** Finishes the file and returns its bytes. */
  finish(title: string): Uint8Array {
    const infoId = this.alloc();
    this.write("%PDF-1.7\n");
    this.write(new Uint8Array([0x25, 0xe2, 0xe3, 0xcf, 0xd3, 0x0a])); // marks the file as binary
    this.writeObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    this.writeObject(2, `<< /Type /Pages /Kids [${this.pageIds.map((id) => `${id} 0 R`).join(" ")}] /Count ${this.pageIds.length} >>`);
    this.writeObject(3, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    for (const o of this.pending.sort((a, b) => a.id - b.id)) this.writeObject(o.id, o.body as string, o.stream);
    this.writeObject(infoId, `<< /Title ${pdfTextString(title)} /Producer (Inkleaf) >>`);
    const xref = this.length;
    let table = `xref\n0 ${this.nextId}\n0000000000 65535 f \n`;
    for (let id = 1; id < this.nextId; id++) table += `${String(this.offsets[id] ?? 0).padStart(10, "0")} 00000 n \n`;
    this.write(`${table}trailer\n<< /Size ${this.nextId} /Root 1 0 R /Info ${infoId} 0 R >>\nstartxref\n${xref}\n%%EOF\n`);
    const out = new Uint8Array(this.length);
    let at = 0;
    for (const c of this.chunks) {
      out.set(c, at);
      at += c.length;
    }
    return out;
  }
}
