import type { P2, Template } from "../api";
import { boxOf, groupHandles, handlesFor, isClosedShape, pressureWidth, setLineCounter, textHeight, type Rect, type TextStyle } from "./geometry";
import { LIST_MARKER } from "./textFormat";
import { fontCss } from "../fonts";
import type { El, Pt, ShapeEl, StickyEl, StrokeEl, TextEl } from "./model";
import type { Anchor, Guide } from "./snap";

/** True for dark paper, where lines and dots need to be light. */
export function isDarkPaper(color: string): boolean {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(color.trim());
  if (!m) return false;
  const [r, g, b] = [m[1], m[2], m[3]].map((x) => parseInt(x, 16) / 255);
  return 0.2126 * r + 0.7152 * g + 0.0722 * b < 0.45;
}

/** Where the paper's writing lines and margin are (shared with alignment guides). */
export function paperLayout(template: Template, w: number, h: number): { rules: number[]; ruleSpan: [number, number]; margin: number | null } {
  const rules: number[] = [];
  switch (template) {
    case "ruled":
    case "narrow-ruled": {
      const gap = template === "ruled" ? 34 : 26;
      for (let y = 110; y < h - 30; y += gap) rules.push(y);
      return { rules, ruleSpan: [0, w], margin: 90 };
    }
    case "grid":
    case "dotted":
      for (let y = 24; y < h; y += 24) rules.push(y);
      return { rules, ruleSpan: [0, w], margin: null };
    case "cornell": {
      const cue = Math.round(w * 0.28);
      const summary = h - Math.round(h * 0.2);
      for (let y = 110; y < summary - 10; y += 34) rules.push(y);
      return { rules, ruleSpan: [cue, w], margin: cue };
    }
    case "blank":
      return { rules, ruleSpan: [0, w], margin: null };
  }
}

export function drawTemplate(ctx: CanvasRenderingContext2D, template: Template, w: number, h: number, paper: string) {
  ctx.fillStyle = paper;
  ctx.fillRect(0, 0, w, h);
  ctx.save();
  const dark = isDarkPaper(paper);
  const LINE_COLOR = dark ? "rgba(170, 190, 225, 0.28)" : "rgba(96, 140, 200, 0.35)";
  const MARGIN_COLOR = dark ? "rgba(255, 120, 120, 0.45)" : "rgba(220, 80, 80, 0.45)";
  ctx.lineWidth = 1;
  ctx.strokeStyle = LINE_COLOR;
  const hline = (y: number, x0 = 0, x1 = w) => {
    ctx.beginPath();
    ctx.moveTo(x0, y + 0.5);
    ctx.lineTo(x1, y + 0.5);
    ctx.stroke();
  };
  const vline = (x: number, y0 = 0, y1 = h) => {
    ctx.beginPath();
    ctx.moveTo(x + 0.5, y0);
    ctx.lineTo(x + 0.5, y1);
    ctx.stroke();
  };
  switch (template) {
    case "ruled":
    case "narrow-ruled": {
      for (const y of paperLayout(template, w, h).rules) hline(y);
      ctx.strokeStyle = MARGIN_COLOR;
      vline(90);
      break;
    }
    case "grid":
      for (let y = 24; y < h; y += 24) hline(y);
      for (let x = 24; x < w; x += 24) vline(x);
      break;
    case "dotted":
      ctx.fillStyle = dark ? "rgba(200, 210, 225, 0.4)" : "rgba(90, 100, 120, 0.45)";
      for (let y = 24; y < h; y += 24) for (let x = 24; x < w; x += 24) ctx.fillRect(x - 1, y - 1, 2.2, 2.2);
      break;
    case "cornell": {
      const cue = Math.round(w * 0.28);
      const summary = h - Math.round(h * 0.2);
      for (const y of paperLayout(template, w, h).rules) hline(y, cue, w);
      ctx.strokeStyle = MARGIN_COLOR;
      vline(cue, 70, summary);
      hline(70);
      hline(summary);
      break;
    }
    case "blank":
      break;
  }
  ctx.restore();
}

function strokePath(ctx: CanvasRenderingContext2D, pts: Pt[]) {
  ctx.beginPath();
  ctx.moveTo(pts[0][0], pts[0][1]);
  if (pts.length === 1) {
    ctx.lineTo(pts[0][0] + 0.01, pts[0][1]);
    return;
  }
  // Quadratic smoothing through segment midpoints.
  for (let i = 1; i < pts.length - 1; i++) {
    const mx = (pts[i][0] + pts[i + 1][0]) / 2;
    const my = (pts[i][1] + pts[i + 1][1]) / 2;
    ctx.quadraticCurveTo(pts[i][0], pts[i][1], mx, my);
  }
  const last = pts[pts.length - 1];
  ctx.lineTo(last[0], last[1]);
}

/** Draws smoothed segments, each with its own width (for pressure/taper). */
function strokeSegments(ctx: CanvasRenderingContext2D, pts: Pt[], widthAt: (i: number) => number) {
  if (pts.length === 1) {
    ctx.lineWidth = widthAt(0);
    strokePath(ctx, pts);
    ctx.stroke();
    return;
  }
  for (let i = 1; i < pts.length; i++) {
    const a = pts[i - 1];
    const b = pts[i];
    const prevMid: [number, number] = i > 1 ? [(pts[i - 2][0] + a[0]) / 2, (pts[i - 2][1] + a[1]) / 2] : [a[0], a[1]];
    const mid: [number, number] = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
    ctx.lineWidth = (widthAt(i - 1) + widthAt(i)) / 2;
    ctx.beginPath();
    ctx.moveTo(prevMid[0], prevMid[1]);
    ctx.quadraticCurveTo(a[0], a[1], mid[0], mid[1]);
    if (i === pts.length - 1) ctx.lineTo(b[0], b[1]);
    ctx.stroke();
  }
}

// Pencil grain: a small tile of per-pixel alpha noise, tinted per colour.
const grainCache = new Map<string, HTMLCanvasElement>();
function grainTile(color: string): HTMLCanvasElement {
  let tile = grainCache.get(color);
  if (tile) return tile;
  tile = document.createElement("canvas");
  tile.width = tile.height = 48;
  const t = tile.getContext("2d")!;
  t.fillStyle = color;
  t.fillRect(0, 0, 48, 48);
  const img = t.getImageData(0, 0, 48, 48);
  let seed = 7;
  for (let i = 3; i < img.data.length; i += 4) {
    seed = (seed * 16807) % 2147483647;
    img.data[i] = 70 + (seed % 186); // alpha 70..255
  }
  t.putImageData(img, 0, 0);
  grainCache.set(color, tile);
  return tile;
}

/** Calligraphy: a flat nib held at 45°, swept along the path. */
function drawCalligraphy(ctx: CanvasRenderingContext2D, el: StrokeEl) {
  const pts = el.points;
  const angle = -Math.PI / 4;
  ctx.fillStyle = el.color;
  ctx.strokeStyle = el.color;
  ctx.lineWidth = Math.max(0.6, el.width * 0.18);
  const nib = (p: Pt): [number, number] => {
    const half = (el.width * 1.4 * (0.55 + 0.6 * Math.min(1, Math.max(0.05, p[2])))) / 2;
    return [Math.cos(angle) * half, Math.sin(angle) * half];
  };
  if (pts.length === 1) {
    const [nx, ny] = nib(pts[0]);
    ctx.beginPath();
    ctx.moveTo(pts[0][0] - nx, pts[0][1] - ny);
    ctx.lineTo(pts[0][0] + nx, pts[0][1] + ny);
    ctx.stroke();
    return;
  }
  // One ribbon outline (one nib edge out, the other back). Non-zero filling
  // keeps twisted sections solid and avoids seams between segments.
  const offs = pts.map(nib);
  ctx.beginPath();
  ctx.moveTo(pts[0][0] + offs[0][0], pts[0][1] + offs[0][1]);
  for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i][0] + offs[i][0], pts[i][1] + offs[i][1]);
  for (let i = pts.length - 1; i >= 0; i--) ctx.lineTo(pts[i][0] - offs[i][0], pts[i][1] - offs[i][1]);
  ctx.closePath();
  ctx.fill("nonzero");
  // Thin centre line keeps strokes parallel to the nib visible.
  strokePath(ctx, pts);
  ctx.stroke();
}

export function drawStroke(ctx: CanvasRenderingContext2D, el: StrokeEl) {
  const pts = el.points;
  if (pts.length === 0) return;
  ctx.save();
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.strokeStyle = el.color;
  if (el.tool === "highlighter") {
    ctx.globalAlpha = 0.35;
    ctx.globalCompositeOperation = "multiply";
    ctx.lineCap = "butt";
    ctx.lineWidth = el.width;
    strokePath(ctx, pts);
    ctx.stroke();
    ctx.restore();
    return;
  }
  const n = pts.length;
  switch (el.style ?? "fountain") {
    case "ballpoint":
      // Even line regardless of pressure.
      ctx.lineWidth = Math.max(0.8, el.width * 0.8);
      strokePath(ctx, pts);
      ctx.stroke();
      break;
    case "pencil": {
      const avg = pts.reduce((s, p) => s + p[2], 0) / n;
      ctx.strokeStyle = ctx.createPattern(grainTile(el.color), "repeat") ?? el.color;
      ctx.globalAlpha = 0.55 + 0.4 * Math.min(1, avg * 1.4);
      ctx.lineWidth = Math.max(0.8, el.width * (0.55 + 0.45 * avg));
      strokePath(ctx, pts);
      ctx.stroke();
      break;
    }
    case "calligraphy":
      drawCalligraphy(ctx, el);
      break;
    case "brush": {
      const ramp = Math.min(8, Math.max(2, Math.floor(n / 4)));
      strokeSegments(ctx, pts, (i) => {
        const taper = Math.min(1, (i + 1) / ramp, (n - i) / ramp);
        return el.width * 1.7 * (0.3 + 0.8 * Math.min(1, pts[i][2])) * (0.2 + 0.8 * taper);
      });
      break;
    }
    case "fountain": {
      const uniform = pts.every((p) => Math.abs(p[2] - pts[0][2]) < 0.02);
      if (uniform) {
        ctx.lineWidth = pressureWidth(el.width, pts[0][2]);
        strokePath(ctx, pts);
        ctx.stroke();
      } else {
        strokeSegments(ctx, pts, (i) => pressureWidth(el.width, pts[i][2]));
      }
      break;
    }
  }
  ctx.restore();
}

function drawArrowHead(ctx: CanvasRenderingContext2D, from: [number, number], to: [number, number], width: number) {
  const angle = Math.atan2(to[1] - from[1], to[0] - from[0]);
  const len = Math.max(12, width * 4.5);
  ctx.beginPath();
  for (const side of [-1, 1]) {
    const a = angle + Math.PI - side * 0.45;
    ctx.moveTo(to[0], to[1]);
    ctx.lineTo(to[0] + Math.cos(a) * len, to[1] + Math.sin(a) * len);
  }
  ctx.stroke();
}

function drawShape(ctx: CanvasRenderingContext2D, el: ShapeEl) {
  const s = el.shape;
  ctx.save();
  ctx.lineWidth = el.width;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.strokeStyle = el.color;
  ctx.beginPath();
  if (s.kind === "ellipse") {
    ctx.ellipse(s.cx, s.cy, Math.max(s.rx, 0.5), Math.max(s.ry, 0.5), 0, 0, Math.PI * 2);
  } else {
    ctx.moveTo(s.points[0][0], s.points[0][1]);
    for (const p of s.points.slice(1)) ctx.lineTo(p[0], p[1]);
    if (isClosedShape(s)) ctx.closePath();
  }
  if (el.fill && isClosedShape(s)) {
    ctx.fillStyle = el.fill;
    ctx.fill();
  }
  ctx.stroke();
  if (s.kind === "arrow" && s.points.length >= 2) drawArrowHead(ctx, s.points[s.points.length - 2], s.points[s.points.length - 1], el.width);
  ctx.restore();
}

function wrapLines(ctx: CanvasRenderingContext2D, text: string, maxWidth: number): string[] {
  const out: string[] = [];
  for (const para of text.split("\n")) {
    let line = "";
    for (const word of para.split(/(\s+)/)) {
      const candidate = line + word;
      if (line && ctx.measureText(candidate).width > maxWidth) {
        out.push(line.trimEnd());
        line = word.trimStart();
      } else {
        line = candidate;
      }
    }
    out.push(line);
  }
  return out;
}

// Give geometry (selection, hit-testing) the same line wrapping as drawing.
let measureCtx: CanvasRenderingContext2D | null = null;
setLineCounter((text, fontSize, width, style) => {
  if (typeof document === "undefined") return text.split("\n").length;
  measureCtx ??= document.createElement("canvas").getContext("2d");
  if (!measureCtx) return text.split("\n").length;
  return layoutText(measureCtx, text, 0, 0, width, fontSize, fontSize, style).length;
});

export interface LaidLine {
  text: string;
  x: number;
  y: number;
  width: number;
}

/**
 * Lays out text: word wrap, list items with a hanging indent (wrapped lines
 * line up after the bullet/number) and left/centre/right alignment. Shared
 * by drawing, hit-testing, alignment guides and PDF export.
 */
export function layoutText(ctx: CanvasRenderingContext2D, text: string, x: number, y: number, w: number, size: number, lineH: number, style: TextStyle): LaidLine[] {
  ctx.font = fontCss(style.font, size, style);
  const out: LaidLine[] = [];
  let row = 0;
  for (const para of text.split("\n")) {
    const m = LIST_MARKER.exec(para);
    const indent = m ? ctx.measureText(m[0]).width : 0;
    const body = m ? para.slice(m[0].length) : para;
    wrapLines(ctx, body, Math.max(10, w - indent)).forEach((line, i) => {
      const t = i === 0 && m ? m[0] + line : line;
      const lw = ctx.measureText(t).width;
      const baseX = i === 0 ? x : x + indent;
      const avail = i === 0 ? w : w - indent;
      const lx = style.align === "center" ? baseX + (avail - lw) / 2 : style.align === "right" ? baseX + avail - lw : baseX;
      out.push({ text: t, x: lx, y: y + row * lineH, width: lw });
      row++;
    });
  }
  return out;
}

/** Width of the longest line of `text` in the given font and size. */
export function measureText(text: string, font: string | undefined, size: number): number {
  measureCtx ??= document.createElement("canvas").getContext("2d");
  if (!measureCtx) return text.length * size * 0.5;
  measureCtx.font = fontCss(font, size);
  return Math.max(...text.split("\n").map((line) => measureCtx!.measureText(line).width));
}

/** Width a text box needs to show every paragraph on one line. */
export function naturalTextWidth(el: TextEl): number {
  measureCtx ??= document.createElement("canvas").getContext("2d");
  if (!measureCtx) return el.w;
  return Math.max(0, ...layoutText(measureCtx, el.text, 0, 0, 1e6, el.fontSize, el.fontSize, el).map((l) => l.width));
}

/**
 * Picks a font size so `text` covers roughly the same area as the
 * handwriting it replaces: width-driven, limited by the ink's height.
 */
export function fitTextToBox(text: string, font: string | undefined, box: Rect): { fontSize: number; width: number } {
  const lines = Math.max(1, text.split("\n").length);
  const perUnit = measureText(text, font, 100) / 100;
  const byWidth = perUnit > 0 ? box.w / perUnit : box.h;
  const byHeight = (box.h / lines) * 1.15;
  const fontSize = Math.round(Math.min(160, Math.max(10, Math.min(byWidth, byHeight * 1.6))));
  return { fontSize, width: Math.ceil(measureText(text, font, fontSize) + fontSize * 0.4) };
}

/**
 * Alignment anchors from the rendered glyphs: per line the baseline and the
 * x-height centre, and for the block the ink's left/right edges and centre.
 */
export function textAnchors(el: TextEl): Anchor[] {
  measureCtx ??= document.createElement("canvas").getContext("2d");
  const ctx = measureCtx;
  if (!ctx) return [];
  const lines = layoutText(ctx, el.text, el.x, el.y, el.w, el.fontSize, el.fontSize * 1.35, el);
  ctx.textBaseline = "top";
  const baseOff = ctx.measureText("H").actualBoundingBoxDescent;
  const xm = ctx.measureText("x");
  const midOff = (xm.actualBoundingBoxDescent - xm.actualBoundingBoxAscent) / 2;
  const out: Anchor[] = [];
  let left = Infinity, right = -Infinity, top = Infinity, bottom = -Infinity;
  for (const line of lines) {
    if (!line.text.trim()) continue;
    const m = ctx.measureText(line.text);
    const l = line.x - m.actualBoundingBoxLeft;
    const r = line.x + m.actualBoundingBoxRight;
    out.push({ axis: "y", kind: "baseline", value: line.y + baseOff, span: [l, r] }, { axis: "y", kind: "mid", value: line.y + midOff, span: [l, r] });
    left = Math.min(left, l);
    right = Math.max(right, r);
    top = Math.min(top, line.y - m.actualBoundingBoxAscent);
    bottom = Math.max(bottom, line.y + m.actualBoundingBoxDescent);
  }
  ctx.textBaseline = "alphabetic";
  if (out.length) {
    const span: [number, number] = [top, bottom];
    out.push({ axis: "x", kind: "left", value: left, span }, { axis: "x", kind: "right", value: right, span }, { axis: "x", kind: "center", value: (left + right) / 2, span });
  }
  return out;
}

/** Alignment guides: thin magenta lines with a small label. */
export function drawGuides(ctx: CanvasRenderingContext2D, guides: Guide[], zoom: number) {
  if (!guides.length) return;
  ctx.save();
  ctx.strokeStyle = "#db2777";
  ctx.fillStyle = "#db2777";
  ctx.lineWidth = 1.25 / zoom;
  ctx.font = `${11 / zoom}px system-ui, sans-serif`;
  ctx.textBaseline = "bottom";
  for (const g of guides) {
    ctx.beginPath();
    if (g.axis === "y") {
      ctx.moveTo(g.from - 12 / zoom, g.value);
      ctx.lineTo(g.to + 12 / zoom, g.value);
    } else {
      ctx.moveTo(g.value, g.from - 12 / zoom);
      ctx.lineTo(g.value, g.to + 12 / zoom);
    }
    ctx.stroke();
    const lx = g.labelAt[0];
    const ly = g.axis === "y" ? g.labelAt[1] - 3 / zoom : g.labelAt[1];
    const tw = ctx.measureText(g.label).width;
    ctx.save();
    ctx.fillStyle = "rgba(255,255,255,0.9)";
    ctx.fillRect(lx - 2 / zoom, ly - 13 / zoom, tw + 4 / zoom, 14 / zoom);
    ctx.restore();
    ctx.fillText(g.label, lx, ly);
  }
  ctx.restore();
}

function drawText(ctx: CanvasRenderingContext2D, el: TextEl) {
  ctx.save();
  ctx.fillStyle = el.color;
  ctx.textBaseline = "top";
  for (const line of layoutText(ctx, el.text, el.x, el.y, el.w, el.fontSize, el.fontSize * 1.35, el)) ctx.fillText(line.text, line.x, line.y);
  ctx.restore();
}

function drawSticky(ctx: CanvasRenderingContext2D, el: StickyEl) {
  ctx.save();
  ctx.shadowColor = "rgba(0,0,0,0.18)";
  ctx.shadowBlur = 10;
  ctx.shadowOffsetY = 3;
  ctx.fillStyle = el.color;
  ctx.fillRect(el.x, el.y, el.w, el.h);
  ctx.shadowColor = "transparent";
  ctx.fillStyle = "#1f2937";
  const size = el.fontSize ?? 18;
  const lineH = size * 1.33;
  ctx.textBaseline = "top";
  const lines = layoutText(ctx, el.text, el.x + 12, el.y + 12, el.w - 24, size, lineH, el);
  for (const line of lines.slice(0, Math.max(1, Math.floor((el.h - 24) / lineH)))) ctx.fillText(line.text, line.x, line.y);
  ctx.restore();
}

export type ImageCache = Map<string, HTMLImageElement>;

export function drawElement(ctx: CanvasRenderingContext2D, el: El, images: ImageCache, onImageLoad: () => void) {
  // Text boxes, sticky notes and images can be rotated around their centre.
  if ((el.kind === "text" || el.kind === "sticky" || el.kind === "image") && el.rotation) {
    const r = boxOf(el);
    const cx = r.x + r.w / 2;
    const cy = r.y + r.h / 2;
    ctx.save();
    ctx.translate(cx, cy);
    ctx.rotate(el.rotation);
    ctx.translate(-cx, -cy);
    drawElement(ctx, { ...el, rotation: 0 }, images, onImageLoad);
    ctx.restore();
    return;
  }
  switch (el.kind) {
    case "stroke":
      return drawStroke(ctx, el);
    case "shape":
      return drawShape(ctx, el);
    case "text":
      return drawText(ctx, el);
    case "sticky":
      return drawSticky(ctx, el);
    case "image": {
      let img = images.get(el.src);
      if (!img) {
        img = new Image();
        img.onload = onImageLoad;
        img.src = el.src;
        images.set(el.src, img);
      }
      if (img.complete && img.naturalWidth > 0) ctx.drawImage(img, el.x, el.y, el.w, el.h);
      else {
        ctx.save();
        ctx.strokeStyle = "#9ca3af";
        ctx.setLineDash([6, 4]);
        ctx.strokeRect(el.x, el.y, el.w, el.h);
        ctx.restore();
      }
    }
  }
}

export function drawSelection(ctx: CanvasRenderingContext2D, r: Rect, zoom: number) {
  ctx.save();
  ctx.strokeStyle = "#2563eb";
  ctx.lineWidth = 1.5 / zoom;
  ctx.setLineDash([6 / zoom, 4 / zoom]);
  ctx.strokeRect(r.x - 6, r.y - 6, r.w + 12, r.h + 12);
  ctx.restore();
}

/** Selection transform handles: 8 resize squares and a rotation knob below. */
export function drawGroupHandles(ctx: CanvasRenderingContext2D, box: Rect, zoom: number) {
  const size = 9 / zoom;
  const hs = groupHandles(box, zoom);
  const rot = hs.find((h) => h.id === "rot")!;
  ctx.save();
  ctx.lineWidth = 1.5 / zoom;
  ctx.strokeStyle = "#2563eb";
  ctx.fillStyle = "#ffffff";
  ctx.beginPath();
  ctx.moveTo(rot.x, box.y + box.h);
  ctx.lineTo(rot.x, rot.y - size * 0.7);
  ctx.stroke();
  for (const h of hs) {
    ctx.beginPath();
    if (h.id === "rot") ctx.arc(h.x, h.y, size * 0.7, 0, Math.PI * 2);
    else ctx.rect(h.x - size / 2, h.y - size / 2, size, size);
    ctx.fill();
    ctx.stroke();
  }
  // A small turning arrow inside the rotation knob.
  ctx.beginPath();
  ctx.arc(rot.x, rot.y, size * 0.35, -Math.PI * 0.9, Math.PI * 0.5);
  ctx.stroke();
  ctx.restore();
}

/** The selection outline while it is being transformed (possibly rotated). */
export function drawTransformedBox(ctx: CanvasRenderingContext2D, corners: P2[], zoom: number) {
  ctx.save();
  ctx.strokeStyle = "#2563eb";
  ctx.lineWidth = 1.5 / zoom;
  ctx.setLineDash([6 / zoom, 4 / zoom]);
  ctx.beginPath();
  corners.forEach((p, i) => (i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1])));
  ctx.closePath();
  ctx.stroke();
  ctx.restore();
}

/** Resize handles: squares on the box, circles on line endpoints. */
export function drawHandles(ctx: CanvasRenderingContext2D, el: El, zoom: number) {
  const size = 9 / zoom;
  ctx.save();
  ctx.lineWidth = 1.5 / zoom;
  ctx.strokeStyle = "#2563eb";
  ctx.fillStyle = "#ffffff";
  for (const hdl of handlesFor(el)) {
    ctx.beginPath();
    if (hdl.id === "p0" || hdl.id === "p1") ctx.arc(hdl.x, hdl.y, size * 0.6, 0, Math.PI * 2);
    else ctx.rect(hdl.x - size / 2, hdl.y - size / 2, size, size);
    ctx.fill();
    ctx.stroke();
  }
  ctx.restore();
}

export function drawLasso(ctx: CanvasRenderingContext2D, pts: [number, number][], zoom: number) {
  if (pts.length < 2) return;
  ctx.save();
  ctx.strokeStyle = "#2563eb";
  ctx.fillStyle = "rgba(37, 99, 235, 0.06)";
  ctx.lineWidth = 1.5 / zoom;
  ctx.setLineDash([5 / zoom, 5 / zoom]);
  ctx.beginPath();
  ctx.moveTo(pts[0][0], pts[0][1]);
  for (const p of pts.slice(1)) ctx.lineTo(p[0], p[1]);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
  ctx.restore();
}

/** Renders a whole page (paper + elements) into a fresh canvas. */
export function renderPage(
  template: Template,
  paper: string,
  w: number,
  h: number,
  elements: El[],
  scale: number,
  images: ImageCache,
): HTMLCanvasElement {
  const c = document.createElement("canvas");
  c.width = Math.max(1, Math.round(w * scale));
  c.height = Math.max(1, Math.round(h * scale));
  const ctx = c.getContext("2d")!;
  ctx.scale(scale, scale);
  drawTemplate(ctx, template, w, h, paper);
  for (const el of elements) drawElement(ctx, el, images, () => {});
  return c;
}

export { textHeight };

/** Ensures every image element is decoded before an offscreen render. */
export async function preloadImages(elements: El[], images: ImageCache): Promise<void> {
  await Promise.all(
    elements
      .filter((el) => el.kind === "image")
      .map((el) => {
        const src = (el as Extract<El, { kind: "image" }>).src;
        let img = images.get(src);
        if (img && img.complete) return Promise.resolve();
        if (!img) {
          img = new Image();
          images.set(src, img);
          img.src = src;
        }
        const pending = img;
        return new Promise<void>((resolve) => {
          pending.addEventListener("load", () => resolve(), { once: true });
          pending.addEventListener("error", () => resolve(), { once: true });
        });
      }),
  );
}

/**
 * A small preview of a paper template. Shows the page's top-left corner
 * (the full page would make lines too faint at thumbnail size).
 */
export function templatePreview(template: Template, paper: string, cssW: number, cssH: number): HTMLCanvasElement {
  const dpr = Math.max(2, window.devicePixelRatio || 1);
  const region = 300; // page units shown across
  const scale = cssW / region;
  const c = document.createElement("canvas");
  c.width = Math.round(cssW * dpr);
  c.height = Math.round(cssH * dpr);
  c.style.width = `${cssW}px`;
  c.style.height = `${cssH}px`;
  const ctx = c.getContext("2d")!;
  ctx.scale(scale * dpr, scale * dpr);
  // Start a little above the first ruled line so lines are visible.
  ctx.translate(-40, -70);
  drawTemplate(ctx, template, 794, 1123, paper);
  return c;
}
