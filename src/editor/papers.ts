// Paper templates: drawing the lines, dots and layouts of every paper style.
// Everything is computed from the page size, so papers work at any paper
// size and orientation, and line colours adapt to dark paper. The same code
// draws the screen, thumbnails and (through the PDF painter) vector exports.

import type { Template } from "../api";
import { fontCss } from "../fonts";

/** Inkleaf units per millimetre (units are 1/96 inch). */
const MM = 96 / 25.4;

/** True for dark paper, where lines and dots need to be light. */
export function isDarkPaper(color: string): boolean {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(color.trim());
  if (!m) return false;
  const [r, g, b] = [m[1], m[2], m[3]].map((x) => parseInt(x, 16) / 255);
  return 0.2126 * r + 0.7152 * g + 0.0722 * b < 0.45;
}

interface Inks {
  line: string;
  strong: string;
  faint: string;
  margin: string;
  dot: string;
  text: string;
}

function inks(paper: string): Inks {
  return isDarkPaper(paper)
    ? {
        line: "rgba(170, 190, 225, 0.28)",
        strong: "rgba(190, 205, 235, 0.5)",
        faint: "rgba(170, 190, 225, 0.15)",
        margin: "rgba(255, 120, 120, 0.45)",
        dot: "rgba(200, 210, 225, 0.4)",
        text: "rgba(205, 215, 230, 0.72)",
      }
    : {
        line: "rgba(96, 140, 200, 0.35)",
        strong: "rgba(70, 110, 170, 0.6)",
        faint: "rgba(96, 140, 200, 0.18)",
        margin: "rgba(220, 80, 80, 0.45)",
        dot: "rgba(90, 100, 120, 0.45)",
        text: "rgba(80, 92, 112, 0.8)",
      };
}

// --- shared geometry --------------------------------------------------------

const RULE_TOP = 110;

function ruledRows(gap: number, h: number, top = RULE_TOP, bottom = h - 30): number[] {
  const out: number[] = [];
  for (let y = top; y < bottom; y += gap) out.push(y);
  return out;
}

/** Handwriting practice rows: ascender, dashed middle, baseline, descender. */
function handwritingRows(h: number) {
  const body = 44; // top line to baseline
  const desc = 18;
  const gap = 26;
  const rows: { top: number; mid: number; base: number; desc: number }[] = [];
  for (let top = 80; top + body + desc < h - 30; top += body + desc + gap) rows.push({ top, mid: top + body / 2, base: top + body, desc: top + body + desc });
  return rows;
}

/** Calligraphy rows: ascender, x-height, baseline, descender (x-height 22). */
function calligraphyRows(h: number) {
  const x = 22;
  const asc = x * 2;
  const desc = x * 1.5;
  const gap = 18;
  const rows: { asc: number; xh: number; base: number; desc: number }[] = [];
  for (let base = 70 + asc; base + desc < h - 30; base += asc + desc + gap) rows.push({ asc: base - x - asc, xh: base - x, base, desc: base + desc });
  return rows;
}

function twoColumnRules(h: number): number[] {
  return ruledRows(34, h, 144);
}

/** Where the paper's writing lines and margin are (shared with alignment guides). */
export function paperLayout(template: Template, w: number, h: number): { rules: number[]; ruleSpan: [number, number]; margin: number | null } {
  switch (template) {
    case "ruled":
      return { rules: ruledRows(34, h), ruleSpan: [0, w], margin: 90 };
    case "narrow-ruled":
      return { rules: ruledRows(26, h), ruleSpan: [0, w], margin: 90 };
    case "wide-ruled":
      return { rules: ruledRows(44, h), ruleSpan: [0, w], margin: 90 };
    case "legal":
      return { rules: ruledRows(34, h), ruleSpan: [0, w], margin: 118 };
    case "checklist":
      return { rules: ruledRows(40, h, 120), ruleSpan: [72, w], margin: 72 };
    case "two-column":
      return { rules: twoColumnRules(h), ruleSpan: [0, w], margin: null };
    case "grid":
    case "dotted":
      return { rules: ruledRows(24, h, 24, h), ruleSpan: [0, w], margin: null };
    case "cornell": {
      const cue = Math.round(w * 0.28);
      const summary = h - Math.round(h * 0.2);
      return { rules: ruledRows(34, h, RULE_TOP, summary - 10), ruleSpan: [cue, w], margin: cue };
    }
    case "handwriting":
      return { rules: handwritingRows(h).map((r) => r.base), ruleSpan: [0, w], margin: null };
    case "calligraphy":
      return { rules: calligraphyRows(h).map((r) => r.base), ruleSpan: [0, w], margin: null };
    default:
      return { rules: [], ruleSpan: [0, w], margin: null };
  }
}

// --- drawing ----------------------------------------------------------------

type Ctx = CanvasRenderingContext2D;

/** Collects line segments and strokes them as one path (crossings don't darken). */
class Lines {
  private segs: [number, number, number, number][] = [];
  add(x0: number, y0: number, x1: number, y1: number) {
    this.segs.push([x0, y0, x1, y1]);
    return this;
  }
  h(y: number, x0: number, x1: number) {
    return this.add(x0, y + 0.5, x1, y + 0.5);
  }
  v(x: number, y0: number, y1: number) {
    return this.add(x + 0.5, y0, x + 0.5, y1);
  }
  rect(x: number, y: number, w: number, h: number) {
    return this.h(y, x, x + w).h(y + h, x, x + w).v(x, y, y + h).v(x + w, y, y + h);
  }
  stroke(ctx: Ctx, color: string, width = 1, dash: number[] = []) {
    if (!this.segs.length) return;
    ctx.save();
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.lineCap = "butt";
    ctx.setLineDash(dash);
    ctx.beginPath();
    for (const [x0, y0, x1, y1] of this.segs) {
      ctx.moveTo(x0, y0);
      ctx.lineTo(x1, y1);
    }
    ctx.stroke();
    ctx.restore();
    this.segs = [];
  }
}

function label(ctx: Ctx, text: string, x: number, y: number, ink: Inks, size = 13, bold = false) {
  ctx.save();
  ctx.font = fontCss("sans", size, { bold });
  ctx.fillStyle = ink.text;
  ctx.textBaseline = "alphabetic";
  ctx.fillText(text, x, y);
  ctx.restore();
}

function checkbox(lines: Lines, x: number, baseline: number, size = 14) {
  lines.rect(x, baseline - size - 5, size, size);
}

/**
 * Millimetre paper: faint 1 mm lines, medium 5 mm lines and bolder 1 cm
 * lines. One centimetre line passes through (ox, oy).
 */
function drawMillimetreGrid(ctx: Ctx, w: number, h: number, ink: Inks, ox: number, oy: number) {
  const mm = new Lines();
  const half = new Lines();
  const cm = new Lines();
  const level = (k: number) => (k % 10 === 0 ? cm : k % 5 === 0 ? half : mm);
  const first = (o: number) => -Math.floor(o / MM);
  for (let k = first(ox); ox + k * MM <= w; k++) level(((k % 10) + 10) % 10).add(ox + k * MM, 0, ox + k * MM, h);
  for (let k = first(oy); oy + k * MM <= h; k++) level(((k % 10) + 10) % 10).add(0, oy + k * MM, w, oy + k * MM);
  mm.stroke(ctx, ink.faint, 0.45);
  half.stroke(ctx, ink.faint, 0.9);
  cm.stroke(ctx, ink.line, 1);
}

function drawGraph(ctx: Ctx, w: number, h: number, ink: Inks) {
  // Centre the centimetre grid on the page.
  const cmU = 10 * MM;
  drawMillimetreGrid(ctx, w, h, ink, (w % cmU) / 2, (h % cmU) / 2);
}

function drawCoordinate(ctx: Ctx, w: number, h: number, ink: Inks) {
  const cmU = 10 * MM;
  // Origin on the centimetre line nearest the page centre.
  const ox = (w % cmU) / 2;
  const oy = (h % cmU) / 2;
  const cx = ox + Math.round((w / 2 - ox) / cmU) * cmU;
  const cy = oy + Math.round((h / 2 - oy) / cmU) * cmU;
  drawMillimetreGrid(ctx, w, h, ink, ox, oy);
  const axes = new Lines();
  axes.add(12, cy, w - 12, cy).add(cx, 12, cx, h - 12);
  // Ticks every 5 mm, longer every centimetre.
  const step = 5 * MM;
  for (let k = -Math.floor(cx / step); cx + k * step < w - 14; k++) {
    if (k === 0) continue;
    const t = k % 2 === 0 ? 6 : 3;
    const x = cx + k * step;
    axes.add(x, cy - t, x, cy + t);
  }
  for (let k = -Math.floor(cy / step); cy + k * step < h - 14; k++) {
    if (k === 0 || cy + k * step < 14) continue;
    const t = k % 2 === 0 ? 6 : 3;
    const y = cy + k * step;
    axes.add(cx - t, y, cx + t, y);
  }
  // Arrow heads on the positive ends (right and up).
  axes.add(w - 12, cy, w - 21, cy - 5).add(w - 12, cy, w - 21, cy + 5);
  axes.add(cx, 12, cx - 5, 21).add(cx, 12, cx + 5, 21);
  axes.stroke(ctx, ink.strong, 1.4);
}

function drawIsometric(ctx: Ctx, w: number, h: number, ink: Inks) {
  const s = 24;
  const rowH = s * Math.sin(Math.PI / 3);
  ctx.save();
  ctx.fillStyle = ink.dot;
  let row = 0;
  for (let y = 20; y < h - 4; y += rowH, row++) {
    for (let x = row % 2 ? s / 2 + 8 : 8; x < w - 4; x += s) ctx.fillRect(x - 1.1, y - 1.1, 2.2, 2.2);
  }
  ctx.restore();
}

function drawHexagons(ctx: Ctx, w: number, h: number, ink: Inks) {
  const r = 18; // corner radius, flat-topped hexagons
  const colW = r * 1.5;
  const rowH = r * Math.sqrt(3);
  const lines = new Lines();
  for (let col = 0, x = 0; x < w + r; col++, x += colW) {
    for (let y = col % 2 ? rowH / 2 : 0; y < h + rowH; y += rowH) {
      // Three edges per hexagon (upper-left, top, upper-right) tile the plane
      // without drawing shared edges twice.
      const p = (a: number): [number, number] => [x + r * Math.cos(a), y + r * Math.sin(a)];
      const [ax, ay] = p(Math.PI);
      const [bx, by] = p((4 * Math.PI) / 3);
      const [cx, cy] = p((5 * Math.PI) / 3);
      const [dx, dy] = p(0);
      lines.add(ax, ay, bx, by).add(bx, by, cx, cy).add(cx, cy, dx, dy);
    }
  }
  lines.stroke(ctx, ink.line, 1);
}

function drawHandwriting(ctx: Ctx, w: number, h: number, ink: Inks) {
  const solid = new Lines();
  const base = new Lines();
  const dashed = new Lines();
  const faint = new Lines();
  for (const r of handwritingRows(h)) {
    solid.h(r.top, 40, w - 40);
    dashed.h(r.mid, 40, w - 40);
    base.h(r.base, 40, w - 40);
    faint.h(r.desc, 40, w - 40);
  }
  faint.stroke(ctx, ink.faint, 1);
  solid.stroke(ctx, ink.line, 1);
  dashed.stroke(ctx, ink.line, 1, [7, 6]);
  base.stroke(ctx, ink.strong, 1.3);
}

function drawCalligraphy(ctx: Ctx, w: number, h: number, ink: Inks) {
  const faint = new Lines();
  const line = new Lines();
  const base = new Lines();
  const slant = new Lines();
  const angle = (55 * Math.PI) / 180; // slant lines, from the horizontal
  const run = 1 / Math.tan(angle);
  for (const r of calligraphyRows(h)) {
    faint.h(r.asc, 40, w - 40).h(r.desc, 40, w - 40);
    line.h(r.xh, 40, w - 40);
    base.h(r.base, 40, w - 40);
    const span = r.desc - r.asc;
    for (let x = 40; x + span * run < w - 40; x += 30) slant.add(x, r.desc, x + span * run, r.asc);
  }
  slant.stroke(ctx, ink.faint, 0.9);
  faint.stroke(ctx, ink.faint, 1);
  line.stroke(ctx, ink.line, 1);
  base.stroke(ctx, ink.strong, 1.3);
}

function drawStoryboard(ctx: Ctx, w: number, h: number, ink: Inks) {
  const m = 44;
  const g = 26;
  const cols = w > h ? 3 : 2;
  const fw = (w - 2 * m - (cols - 1) * g) / cols;
  const fh = (fw * 9) / 16;
  const capLines = 2;
  const capGap = 24;
  const cellH = fh + 14 + capLines * capGap;
  const rows = Math.max(1, Math.floor((h - 2 * m + g) / (cellH + g)));
  const top = (h - (rows * cellH + (rows - 1) * g)) / 2;
  const frames = new Lines();
  const captions = new Lines();
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const x = m + c * (fw + g);
      const y = top + r * (cellH + g);
      frames.rect(x, y, fw, fh);
      for (let k = 1; k <= capLines; k++) captions.h(y + fh + 10 + k * capGap, x, x + fw);
    }
  }
  captions.stroke(ctx, ink.line, 1);
  frames.stroke(ctx, ink.strong, 1.4);
}

function drawDailyPlanner(ctx: Ctx, w: number, h: number, ink: Inks) {
  const m = 48;
  const headerY = m + 40;
  label(ctx, "Date", m, m + 22, ink, 13, true);
  const strong = new Lines();
  const line = new Lines();
  const dashed = new Lines();
  strong.h(headerY, m, w - m);
  // Schedule on the left, priorities / to-do / notes on the right.
  const gap = 28;
  const leftW = Math.round((w - 2 * m - gap) * 0.56);
  const rx = m + leftW + gap;
  const top = headerY + 34;
  const bottom = h - m;
  label(ctx, "Schedule", m, top - 12, ink, 12, true);
  const hours = 16; // 6:00 to 21:00
  const slot = (bottom - top) / hours;
  for (let i = 0; i <= hours; i++) {
    const y = top + i * slot;
    line.h(y, m, m + leftW);
    if (i < hours) {
      dashed.h(y + slot / 2, m + 46, m + leftW);
      label(ctx, `${6 + i}:00`, m + 2, y + 16, ink, 11);
    }
  }
  strong.v(m + 40, top, bottom);
  // Right column.
  label(ctx, "Top priorities", rx, top - 12, ink, 12, true);
  const rowGap = 30;
  let y = top + rowGap;
  for (let i = 0; i < 3; i++, y += rowGap) {
    checkbox(line, rx, y);
    line.h(y, rx + 22, w - m);
  }
  y += 18;
  label(ctx, "To do", rx, y - 12, ink, 12, true);
  y += rowGap - 4;
  const notesTop = top + (bottom - top) * 0.62;
  for (; y < notesTop - 10; y += rowGap) {
    checkbox(line, rx, y);
    line.h(y, rx + 22, w - m);
  }
  label(ctx, "Notes", rx, notesTop + 14, ink, 12, true);
  for (let ny = notesTop + 26 + rowGap; ny < bottom; ny += rowGap) line.h(ny, rx, w - m);
  dashed.stroke(ctx, ink.faint, 1, [4, 4]);
  line.stroke(ctx, ink.line, 1);
  strong.stroke(ctx, ink.strong, 1.3);
}

function drawWeeklyPlanner(ctx: Ctx, w: number, h: number, ink: Inks) {
  const m = 40;
  label(ctx, "Week of", m, m + 20, ink, 13, true);
  const strong = new Lines();
  const line = new Lines();
  const headerY = m + 36;
  strong.h(headerY, m, w - m);
  const cols = w > h ? 4 : 2;
  const rows = 8 / cols;
  const g = 16;
  const top = headerY + 20;
  const cw = (w - 2 * m - (cols - 1) * g) / cols;
  const ch = (h - m - top - (rows - 1) * g) / rows;
  const names = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday", "Notes"];
  names.forEach((name, i) => {
    const x = m + (i % cols) * (cw + g);
    const y = top + Math.floor(i / cols) * (ch + g);
    strong.rect(x, y, cw, ch);
    label(ctx, name, x + 10, y + 22, ink, 12, true);
    line.h(y + 32, x, x + cw);
    for (let ly = y + 32 + 28; ly < y + ch - 8; ly += 28) line.h(ly, x + 10, x + cw - 10);
  });
  line.stroke(ctx, ink.faint, 1);
  strong.stroke(ctx, ink.line, 1.2);
}

// --- cover --------------------------------------------------------------------

/** Mixes a #rrggbb colour with black (amount < 0) or white (amount > 0). */
function shade(color: string, amount: number): string {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(color.trim());
  if (!m) return color;
  const target = amount < 0 ? 0 : 255;
  const k = Math.abs(amount);
  const ch = (h: string) => Math.round(parseInt(h, 16) * (1 - k) + target * k);
  return `rgb(${ch(m[1])}, ${ch(m[2])}, ${ch(m[3])})`;
}

export interface CoverLayout {
  spine: number;
  /** The closure band's left edge and width. */
  band: { x: number; w: number };
  /** The label plate. */
  plate: { x: number; y: number; w: number; h: number };
  /** Where the title and the date go (text boxes). */
  title: { x: number; y: number; w: number; h: number };
  date: { x: number; y: number; w: number };
  /** The divider between title and date. */
  divider: number;
}

/** Layout of a cover page, shared by the drawing and the editable title and date. */
export function coverLayout(w: number, h: number): CoverLayout {
  const spine = Math.round(Math.max(30, w * 0.075));
  const bandW = Math.max(12, w * 0.018);
  const bandX = w - Math.max(34, w * 0.07) - bandW;
  // The plate sits in the open area between the spine and the band, in the upper third.
  const left = spine + 26;
  const right = bandX - 26;
  const pw = Math.min(520, (right - left) * 0.84);
  const ph = Math.max(150, Math.min(220, h * 0.17));
  const px = left + (right - left - pw) / 2;
  const py = Math.round(h * (w > h ? 0.26 : 0.22));
  const pad = 26;
  const divider = py + ph * 0.66;
  return {
    spine,
    band: { x: bandX, w: bandW },
    plate: { x: px, y: py, w: pw, h: ph },
    title: { x: px + pad, y: py + 26, w: pw - 2 * pad, h: divider - py - 34 },
    date: { x: px + pad, y: divider + 12, w: pw - 2 * pad },
    divider,
  };
}

function roundRect(ctx: Ctx, x: number, y: number, w: number, h: number, r: number) {
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + w - r, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + r);
  ctx.lineTo(x + w, y + h - r);
  ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  ctx.lineTo(x + r, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - r);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.closePath();
}

/**
 * A notebook cover in the cover colour: a linen texture, a darker binding
 * spine with stitching, a stitched border, an elastic closure band and a
 * cream label plate for the title (the title and date are editable text).
 */
function drawCover(ctx: Ctx, w: number, h: number, color: string) {
  const L = coverLayout(w, h);
  // Linen: very fine cross threads, light and dark.
  const light = new Lines();
  const dark = new Lines();
  for (let y = 1.5, i = 0; y < h; y += 2.6, i++) (i % 2 ? light : dark).h(y, 0, w);
  for (let x = 1.5, i = 0; x < w; x += 2.6, i++) (i % 2 ? light : dark).v(x, 0, h);
  light.stroke(ctx, "rgba(255, 255, 255, 0.025)", 0.8);
  dark.stroke(ctx, "rgba(0, 0, 0, 0.025)", 0.8);
  // A soft sheen from the top-left: many very faint layers, so it reads as a
  // smooth gradient (also in vector PDF export, which has no gradients).
  ctx.save();
  const layers = 28;
  ctx.fillStyle = "rgba(255, 255, 255, 0.0065)";
  for (let k = 0; k < layers; k++) {
    const t = 1 - k / layers;
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(w * 1.05 * t, 0);
    ctx.lineTo(0, h * 0.8 * t);
    ctx.closePath();
    ctx.fill();
  }
  ctx.restore();
  // Binding spine with a highlight and a crease.
  ctx.save();
  ctx.fillStyle = shade(color, -0.28);
  ctx.fillRect(0, 0, L.spine, h);
  ctx.fillStyle = "rgba(0, 0, 0, 0.18)";
  ctx.fillRect(L.spine, 0, 4, h);
  ctx.fillStyle = "rgba(255, 255, 255, 0.10)";
  ctx.fillRect(L.spine - 3, 0, 2, h);
  ctx.restore();
  // Stitching along the spine and around the cover.
  const stitch = new Lines();
  stitch.v(L.spine - 11, 0, h);
  const inset = 16;
  stitch.rect(L.spine + inset, inset, w - L.spine - 2 * inset, h - 2 * inset);
  stitch.stroke(ctx, "rgba(255, 255, 255, 0.42)", 1.2, [5, 4]);
  // Elastic closure band.
  ctx.save();
  ctx.fillStyle = "rgba(0, 0, 0, 0.18)";
  ctx.fillRect(L.band.x + 3, 0, L.band.w, h);
  ctx.fillStyle = shade(color, -0.42);
  ctx.fillRect(L.band.x, 0, L.band.w, h);
  ctx.fillStyle = "rgba(255, 255, 255, 0.14)";
  ctx.fillRect(L.band.x + 2, 0, 1.5, h);
  ctx.restore();
  // Label plate: a soft shadow, the cream card and an inner border in the cover colour.
  const p = L.plate;
  ctx.save();
  for (let k = 3; k >= 1; k--) {
    ctx.fillStyle = `rgba(0, 0, 0, ${0.07})`;
    ctx.beginPath();
    roundRect(ctx, p.x - k + 2, p.y - k + 4, p.w + 2 * k, p.h + 2 * k, 12 + k);
    ctx.fill();
  }
  ctx.fillStyle = "#fbf8f1";
  ctx.beginPath();
  roundRect(ctx, p.x, p.y, p.w, p.h, 12);
  ctx.fill();
  ctx.strokeStyle = shade(color, 0.35);
  ctx.lineWidth = 1.4;
  ctx.beginPath();
  roundRect(ctx, p.x + 8, p.y + 8, p.w - 16, p.h - 16, 7);
  ctx.stroke();
  // Divider between the title and the date, with a small diamond.
  ctx.strokeStyle = shade(color, 0.2);
  ctx.lineWidth = 1;
  const cx = p.x + p.w / 2;
  ctx.beginPath();
  ctx.moveTo(p.x + 40, L.divider);
  ctx.lineTo(cx - 9, L.divider);
  ctx.moveTo(cx + 9, L.divider);
  ctx.lineTo(p.x + p.w - 40, L.divider);
  ctx.stroke();
  ctx.fillStyle = shade(color, 0.1);
  ctx.beginPath();
  ctx.moveTo(cx, L.divider - 4);
  ctx.lineTo(cx + 4, L.divider);
  ctx.lineTo(cx, L.divider + 4);
  ctx.lineTo(cx - 4, L.divider);
  ctx.closePath();
  ctx.fill();
  ctx.restore();
}

/** Draws the paper colour and the template's lines. */
export function drawTemplate(ctx: Ctx, template: Template, w: number, h: number, paper: string) {
  ctx.fillStyle = paper;
  ctx.fillRect(0, 0, w, h);
  const ink = inks(paper);
  const L = new Lines();
  const M = new Lines();
  switch (template) {
    case "ruled":
    case "narrow-ruled":
    case "wide-ruled":
      for (const y of paperLayout(template, w, h).rules) L.h(y, 0, w);
      L.stroke(ctx, ink.line);
      M.v(90, 0, h).stroke(ctx, ink.margin);
      break;
    case "legal":
      for (const y of paperLayout(template, w, h).rules) L.h(y, 0, w);
      L.stroke(ctx, ink.line);
      M.v(112, 0, h).v(118, 0, h).stroke(ctx, ink.margin);
      break;
    case "checklist": {
      for (const y of paperLayout(template, w, h).rules) {
        L.h(y, 72, w - 40);
        checkbox(L, 44, y, 15);
      }
      L.stroke(ctx, ink.line);
      break;
    }
    case "two-column": {
      const mid = Math.round(w / 2);
      L.h(110, 40, w - 40);
      for (const y of twoColumnRules(h)) L.h(y, 40, mid - 14).h(y, mid + 14, w - 40);
      L.stroke(ctx, ink.line);
      M.v(mid, 70, h - 30).stroke(ctx, ink.strong, 1.2);
      break;
    }
    case "grid":
      for (let y = 24; y < h; y += 24) L.h(y, 0, w);
      for (let x = 24; x < w; x += 24) L.v(x, 0, h);
      L.stroke(ctx, ink.line);
      break;
    case "dotted":
      ctx.save();
      ctx.fillStyle = ink.dot;
      for (let y = 24; y < h; y += 24) for (let x = 24; x < w; x += 24) ctx.fillRect(x - 1, y - 1, 2.2, 2.2);
      ctx.restore();
      break;
    case "cornell": {
      const cue = Math.round(w * 0.28);
      const summary = h - Math.round(h * 0.2);
      for (const y of paperLayout(template, w, h).rules) L.h(y, cue, w);
      L.stroke(ctx, ink.line);
      M.v(cue, 70, summary).h(70, 0, w).h(summary, 0, w).stroke(ctx, ink.margin);
      break;
    }
    case "graph":
      drawGraph(ctx, w, h, ink);
      break;
    case "coordinate":
      drawCoordinate(ctx, w, h, ink);
      break;
    case "isometric":
      drawIsometric(ctx, w, h, ink);
      break;
    case "hexagon":
      drawHexagons(ctx, w, h, ink);
      break;
    case "handwriting":
      drawHandwriting(ctx, w, h, ink);
      break;
    case "calligraphy":
      drawCalligraphy(ctx, w, h, ink);
      break;
    case "storyboard":
      drawStoryboard(ctx, w, h, ink);
      break;
    case "planner-day":
      drawDailyPlanner(ctx, w, h, ink);
      break;
    case "planner-week":
      drawWeeklyPlanner(ctx, w, h, ink);
      break;
    case "cover":
      drawCover(ctx, w, h, paper);
      break;
    case "blank":
      break;
  }
}

/** Templates whose labels use the bundled sans font (PDF export loads it). */
export function templateUsesText(template: Template): boolean {
  return template === "planner-day" || template === "planner-week";
}
