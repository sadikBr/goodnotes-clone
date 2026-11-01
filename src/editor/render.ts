import type { Template } from "../api";
import { pressureWidth, textHeight, type Rect } from "./geometry";
import type { El, Pt, ShapeEl, StickyEl, StrokeEl, TextEl } from "./model";

const LINE_COLOR = "rgba(96, 140, 200, 0.35)";
const MARGIN_COLOR = "rgba(220, 80, 80, 0.45)";

export function drawTemplate(ctx: CanvasRenderingContext2D, template: Template, w: number, h: number, paper: string) {
  ctx.fillStyle = paper;
  ctx.fillRect(0, 0, w, h);
  ctx.save();
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
      const gap = template === "ruled" ? 34 : 26;
      for (let y = 110; y < h - 30; y += gap) hline(y);
      ctx.strokeStyle = MARGIN_COLOR;
      vline(90);
      break;
    }
    case "grid":
      for (let y = 24; y < h; y += 24) hline(y);
      for (let x = 24; x < w; x += 24) vline(x);
      break;
    case "dotted":
      ctx.fillStyle = "rgba(90, 100, 120, 0.45)";
      for (let y = 24; y < h; y += 24) for (let x = 24; x < w; x += 24) ctx.fillRect(x - 1, y - 1, 2.2, 2.2);
      break;
    case "cornell": {
      const cue = Math.round(w * 0.28);
      const summary = h - Math.round(h * 0.2);
      for (let y = 110; y < summary - 10; y += 34) hline(y, cue, w);
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
  const uniform = pts.every((p) => Math.abs(p[2] - pts[0][2]) < 0.02);
  if (uniform) {
    ctx.lineWidth = pressureWidth(el.width, pts[0][2]);
    strokePath(ctx, pts);
    ctx.stroke();
  } else {
    // Variable width: draw smoothed segments, each with its own width.
    for (let i = 1; i < pts.length; i++) {
      const a = pts[i - 1];
      const b = pts[i];
      const prevMid: [number, number] = i > 1 ? [(pts[i - 2][0] + a[0]) / 2, (pts[i - 2][1] + a[1]) / 2] : [a[0], a[1]];
      const mid: [number, number] = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
      ctx.lineWidth = pressureWidth(el.width, (a[2] + b[2]) / 2);
      ctx.beginPath();
      ctx.moveTo(prevMid[0], prevMid[1]);
      ctx.quadraticCurveTo(a[0], a[1], mid[0], mid[1]);
      if (i === pts.length - 1) ctx.lineTo(b[0], b[1]);
      ctx.stroke();
    }
  }
  ctx.restore();
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
    if (s.kind !== "line") ctx.closePath();
  }
  if (el.fill) {
    ctx.fillStyle = el.fill;
    ctx.fill();
  }
  ctx.stroke();
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

export const FONT_STACK = '"Inter", "Segoe UI", system-ui, -apple-system, sans-serif';

function drawText(ctx: CanvasRenderingContext2D, el: TextEl) {
  ctx.save();
  ctx.fillStyle = el.color;
  ctx.font = `${el.fontSize}px ${FONT_STACK}`;
  ctx.textBaseline = "top";
  const lines = wrapLines(ctx, el.text, el.w);
  lines.forEach((line, i) => ctx.fillText(line, el.x, el.y + i * el.fontSize * 1.35));
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
  ctx.font = `18px ${FONT_STACK}`;
  ctx.textBaseline = "top";
  const lines = wrapLines(ctx, el.text, el.w - 24);
  lines.slice(0, Math.floor((el.h - 24) / 24)).forEach((line, i) => ctx.fillText(line, el.x + 12, el.y + 12 + i * 24));
  ctx.restore();
}

export type ImageCache = Map<string, HTMLImageElement>;

export function drawElement(ctx: CanvasRenderingContext2D, el: El, images: ImageCache, onImageLoad: () => void) {
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
