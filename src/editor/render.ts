import type { P2, Template } from "../api";
import { boxOf, groupHandles, handlesFor, isClosedShape, setLineCounter, textHeight, type Rect, type TextStyle } from "./geometry";
import { LIST_MARKER } from "./textFormat";
import { outlinePath, strokeOutline } from "./ink";
import { fontCss, isRtl } from "../fonts";
import type { El, Pt, ShapeEl, StickyEl, StrokeEl, TextEl } from "./model";
import type { Anchor, Guide } from "./snap";

export { drawTemplate, isDarkPaper, paperLayout } from "./papers";
import { drawTemplate } from "./papers";

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

/** Calligraphy nib angle (from the horizontal, rising to the right). */
const NIB_ANGLE = -Math.PI / 4;

/**
 * Evenly spaced, smoothed points along a calligraphy stroke: hand jitter
 * would otherwise show as ragged edges on the wide parts of the stroke.
 */
function calligraphyPath(pts: Pt[], step: number, smoothing: number): Pt[] {
  if (pts.length < 2) return pts;
  // Resample to an even spacing (pressure interpolated).
  const even: Pt[] = [pts[0]];
  let carry = 0;
  for (let i = 1; i < pts.length; i++) {
    const a = pts[i - 1];
    const b = pts[i];
    const len = Math.hypot(b[0] - a[0], b[1] - a[1]);
    let t = step - carry;
    while (t <= len) {
      const k = t / len;
      even.push([a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k]);
      t += step;
    }
    carry = len - (t - step);
  }
  const last = pts[pts.length - 1];
  if (Math.hypot(last[0] - even[even.length - 1][0], last[1] - even[even.length - 1][1]) > step * 0.3) even.push(last);
  return smoothAlong(smoothAlong(even, smoothing), smoothing);
}

/**
 * A moving average over `radius` points of an evenly spaced path (the window
 * shrinks near the ends, which stay put). Irons out hand tremor over a few
 * millimetres without rounding off real shapes.
 */
function smoothAlong(pts: Pt[], radius: number): Pt[] {
  const n = pts.length;
  if (n < 3 || radius < 1) return pts;
  const out: Pt[] = [];
  for (let i = 0; i < n; i++) {
    const r = Math.min(radius, i, n - 1 - i);
    let x = 0;
    let y = 0;
    for (let k = i - r; k <= i + r; k++) {
      x += pts[k][0];
      y += pts[k][1];
    }
    out.push([x / (2 * r + 1), y / (2 * r + 1), pts[i][2]]);
  }
  return out;
}

/**
 * Calligraphy: a flat nib held at a fixed angle, swept along the path, like
 * a broad-edged pen or qalam. The ink is the area the nib sweeps: one small
 * four-sided piece between each pair of nib positions, all traced the same
 * way round and filled together, so the stroke has no holes or slivers
 * however it turns or crosses itself, and its ends are cut flat at the nib
 * angle. A hairline the nib's thickness keeps strokes along the nib visible.
 */
function drawCalligraphy(ctx: CanvasRenderingContext2D, el: StrokeEl) {
  const ux = Math.cos(NIB_ANGLE);
  const uy = Math.sin(NIB_ANGLE);
  const half = (p: Pt) => (el.width * 1.4 * (0.55 + 0.6 * Math.min(1, Math.max(0.05, p[2])))) / 2;
  // Even spacing, smoothed over about a nib width (at least ~1 mm).
  const step = Math.max(0.6, el.width * 0.25);
  const pts = calligraphyPath(el.points, step, Math.round(Math.max(4, el.width * 0.9) / step));
  ctx.fillStyle = el.color;
  ctx.strokeStyle = el.color;
  ctx.lineCap = "butt";
  ctx.lineJoin = "round";
  ctx.lineWidth = Math.max(0.6, el.width * 0.16);
  ctx.beginPath();
  if (pts.length === 1) {
    const h = half(pts[0]);
    ctx.moveTo(pts[0][0] - ux * h, pts[0][1] - uy * h);
    ctx.lineTo(pts[0][0] + ux * h, pts[0][1] + uy * h);
    ctx.stroke();
    return;
  }
  for (let i = 1; i < pts.length; i++) {
    const a = pts[i - 1];
    const b = pts[i];
    const ha = half(a);
    const hb = half(b);
    const quad: [number, number][] = [
      [a[0] + ux * ha, a[1] + uy * ha],
      [b[0] + ux * hb, b[1] + uy * hb],
      [b[0] - ux * hb, b[1] - uy * hb],
      [a[0] - ux * ha, a[1] - uy * ha],
    ];
    // Same winding for every piece, so the non-zero fill unites them.
    let area = 0;
    for (let k = 0; k < 4; k++) area += quad[k][0] * quad[(k + 1) % 4][1] - quad[(k + 1) % 4][0] * quad[k][1];
    if (area < 0) quad.reverse();
    ctx.moveTo(quad[0][0], quad[0][1]);
    for (let k = 1; k < 4; k++) ctx.lineTo(quad[k][0], quad[k][1]);
    ctx.closePath();
  }
  ctx.fill("nonzero");
  // The nib's own thickness: strokes drawn along the nib stay a crisp hairline.
  ctx.beginPath();
  ctx.moveTo(pts[0][0], pts[0][1]);
  for (const p of pts.slice(1)) ctx.lineTo(p[0], p[1]);
  ctx.stroke();
}

/**
 * Drops highlighter points that barely move from the previous one, so the
 * jitter of a pen held still doesn't make the stroke wobble while drawing.
 */
export function steadyHighlight(pts: Pt[], width: number): Pt[] {
  if (pts.length < 3) return pts;
  const min = Math.max(1.5, width * 0.35);
  const out: Pt[] = [pts[0]];
  for (let i = 1; i < pts.length; i++) {
    const last = out[out.length - 1];
    if (Math.hypot(pts[i][0] - last[0], pts[i][1] - last[1]) >= min) out.push(pts[i]);
  }
  // Keep a lone dab visible.
  if (out.length === 1) out.push(pts[pts.length - 1]);
  return out;
}

/**
 * The curve strokePath draws (quadratics through segment midpoints), as a
 * polyline of short pieces.
 */
export function flattenSmooth(pts: Pt[]): [number, number][] {
  if (pts.length < 3) return pts.map((p) => [p[0], p[1]]);
  const out: [number, number][] = [[pts[0][0], pts[0][1]]];
  let cur: [number, number] = out[0];
  for (let i = 1; i < pts.length - 1; i++) {
    const c = pts[i];
    const end: [number, number] = [(pts[i][0] + pts[i + 1][0]) / 2, (pts[i][1] + pts[i + 1][1]) / 2];
    const len = Math.hypot(c[0] - cur[0], c[1] - cur[1]) + Math.hypot(end[0] - c[0], end[1] - c[1]);
    const n = Math.max(1, Math.min(16, Math.ceil(len / 2)));
    for (let k = 1; k <= n; k++) {
      const t = k / n;
      const u = 1 - t;
      out.push([u * u * cur[0] + 2 * u * t * c[0] + t * t * end[0], u * u * cur[1] + 2 * u * t * c[1] + t * t * end[1]]);
    }
    cur = end;
  }
  const last = pts[pts.length - 1];
  out.push([last[0], last[1]]);
  return out;
}

/**
 * Flat (chisel) highlighter ends: adds a short straight piece of marker just
 * behind each end, along the end direction. On straight strokes it lies
 * inside the stroke; when the stroke curls back on a tight turn, it fills
 * the notch a flat end would otherwise leave. Added as subpaths of the same
 * path, so it is painted in the same single stroke.
 */
function flatEndFillers(ctx: CanvasRenderingContext2D, pts: [number, number][], width: number) {
  if (pts.length < 2) return;
  let total = 0;
  for (let i = 1; i < pts.length; i++) total += Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]);
  const len = Math.min(width / 2, total);
  const piece = (end: [number, number], towards: [number, number]) => {
    const dx = towards[0] - end[0];
    const dy = towards[1] - end[1];
    const d = Math.hypot(dx, dy);
    if (d < 1e-6) return;
    ctx.moveTo(end[0], end[1]);
    ctx.lineTo(end[0] + (dx / d) * len, end[1] + (dy / d) * len);
  };
  // The path's end tangents: towards the neighbouring point at each end.
  piece(pts[0], pts[1]);
  piece(pts[pts.length - 1], pts[pts.length - 2]);
}

export function drawStroke(ctx: CanvasRenderingContext2D, el: StrokeEl, live = false, part?: { first: boolean; last: boolean }) {
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
    const steady = steadyHighlight(pts, el.width);
    // The smoothed curve as many short straight pieces with round joins:
    // stroking curves that bend tighter than the marker is wide makes the
    // outline fold over and leave holes.
    const flat = flattenSmooth(steady);
    ctx.beginPath();
    ctx.moveTo(flat[0][0], flat[0][1]);
    if (flat.length === 1) ctx.lineTo(flat[0][0] + 0.01, flat[0][1]);
    for (const p of flat.slice(1)) ctx.lineTo(p[0], p[1]);
    flatEndFillers(ctx, flat, el.width);
    ctx.stroke(); // one stroke: overlapping parts don't darken
    ctx.restore();
    return;
  }
  const style = el.style ?? "fountain";
  if (style === "calligraphy") {
    drawCalligraphy(ctx, el);
    ctx.restore();
    return;
  }
  // Every other pen is one smooth filled outline (no beads or steps).
  ctx.beginPath();
  for (const outline of strokeOutline(pts, style, el.width, !live, part)) outlinePath(ctx, outline);
  if (style === "pencil") {
    const avg = pts.reduce((s, p) => s + p[2], 0) / pts.length;
    ctx.fillStyle = el.color; // what vector export uses
    ctx.fillStyle = ctx.createPattern(grainTile(el.color), "repeat") ?? el.color;
    ctx.globalAlpha = 0.55 + 0.4 * Math.min(1, avg * 1.4);
  } else {
    ctx.fillStyle = el.color;
  }
  ctx.fill("nonzero");
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
  /** The line belongs to a right-to-left (Arabic) paragraph. */
  rtl: boolean;
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
    // Alignment is logical: "left" is the paragraph's start, which is the
    // right edge in Arabic. List indents go on the start side too.
    const rtl = isRtl(para);
    const align = rtl ? (style.align === "right" ? "left" : style.align === "center" ? "center" : "right") : (style.align ?? "left");
    wrapLines(ctx, body, Math.max(10, w - indent)).forEach((line, i) => {
      const t = i === 0 && m ? m[0] + line : line;
      const lw = ctx.measureText(t).width;
      const baseX = i === 0 || rtl ? x : x + indent;
      const avail = i === 0 ? w : w - indent;
      const lx = align === "center" ? baseX + (avail - lw) / 2 : align === "right" ? baseX + avail - lw : baseX;
      out.push({ text: t, x: lx, y: y + row * lineH, width: lw, rtl });
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

/** Draws a laid-out line, right to left for Arabic (x is always its left edge). */
function fillLine(ctx: CanvasRenderingContext2D, line: LaidLine) {
  ctx.direction = line.rtl ? "rtl" : "ltr";
  ctx.textAlign = "left";
  ctx.fillText(line.text, line.x, line.y);
}

function drawText(ctx: CanvasRenderingContext2D, el: TextEl) {
  ctx.save();
  ctx.fillStyle = el.color;
  ctx.textBaseline = "top";
  for (const line of layoutText(ctx, el.text, el.x, el.y, el.w, el.fontSize, el.fontSize * 1.35, el)) fillLine(ctx, line);
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
  for (const line of lines.slice(0, Math.max(1, Math.floor((el.h - 24) / lineH)))) fillLine(ctx, line);
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
  background: CanvasImageSource | null = null,
): HTMLCanvasElement {
  const c = document.createElement("canvas");
  c.width = Math.max(1, Math.round(w * scale));
  c.height = Math.max(1, Math.round(h * scale));
  const ctx = c.getContext("2d")!;
  ctx.scale(scale, scale);
  drawTemplate(ctx, template, w, h, paper);
  if (background) ctx.drawImage(background, 0, 0, w, h);
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
 * A small preview of a paper template: a close-up of the page's top-left
 * corner (lines would be too faint at thumbnail size), or the whole page
 * for structured layouts (`whole`).
 */
export function templatePreview(template: Template, paper: string, cssW: number, cssH: number, whole = false): HTMLCanvasElement {
  const dpr = Math.max(2, window.devicePixelRatio || 1);
  const region = 300; // page units shown across
  const scale = cssW / region;
  const c = document.createElement("canvas");
  c.width = Math.round(cssW * dpr);
  c.height = Math.round(cssH * dpr);
  c.style.width = `${cssW}px`;
  c.style.height = `${cssH}px`;
  const ctx = c.getContext("2d")!;
  if (whole) {
    ctx.scale((cssW / 794) * dpr, (cssH / 1123) * dpr);
    drawTemplate(ctx, template, 794, 1123, paper);
    return c;
  }
  ctx.scale(scale * dpr, scale * dpr);
  // Start a little above the first ruled line so lines are visible.
  ctx.translate(-40, -70);
  drawTemplate(ctx, template, 794, 1123, paper);
  return c;
}
