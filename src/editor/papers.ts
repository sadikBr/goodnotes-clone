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

function drawGraph(ctx: Ctx, w: number, h: number, ink: Inks) {
  const minor = 5 * MM;
  const major = minor * 5;
  const ox = (w % major) / 2;
  const oy = (h % major) / 2;
  const thin = new Lines();
  const thick = new Lines();
  let i = 0;
  for (let x = ox; x <= w; x += minor, i++) (i % 5 === 0 ? thick : thin).v(x, 0, h);
  i = 0;
  for (let y = oy; y <= h; y += minor, i++) (i % 5 === 0 ? thick : thin).h(y, 0, w);
  thin.stroke(ctx, ink.faint, 0.8);
  thick.stroke(ctx, ink.line, 1.1);
}

function drawCoordinate(ctx: Ctx, w: number, h: number, ink: Inks) {
  const s = 24;
  const cx = Math.round(w / 2 / s) * s;
  const cy = Math.round(h / 2 / s) * s;
  const grid = new Lines();
  for (let x = cx % s; x < w; x += s) grid.v(x, 0, h);
  for (let y = cy % s; y < h; y += s) grid.h(y, 0, w);
  grid.stroke(ctx, ink.faint, 0.9);
  const axes = new Lines();
  axes.h(cy, 12, w - 12).v(cx, 12, h - 12);
  // Ticks: small on every line, longer every five.
  for (let x = cx % s; x < w; x += s) {
    const k = Math.round((x - cx) / s);
    if (k === 0) continue;
    const t = k % 5 === 0 ? 6 : 3;
    axes.add(x + 0.5, cy + 0.5 - t, x + 0.5, cy + 0.5 + t);
  }
  for (let y = cy % s; y < h; y += s) {
    const k = Math.round((y - cy) / s);
    if (k === 0) continue;
    const t = k % 5 === 0 ? 6 : 3;
    axes.add(cx + 0.5 - t, y + 0.5, cx + 0.5 + t, y + 0.5);
  }
  // Arrow heads on the positive ends (right and up).
  axes.add(w - 12, cy + 0.5, w - 21, cy - 4.5).add(w - 12, cy + 0.5, w - 21, cy + 5.5);
  axes.add(cx + 0.5, 12, cx - 4.5, 21).add(cx + 0.5, 12, cx + 5.5, 21);
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
    case "blank":
      break;
  }
}

/** Templates whose labels use the bundled sans font (PDF export loads it). */
export function templateUsesText(template: Template): boolean {
  return template === "planner-day" || template === "planner-week";
}
