import type { Template } from "../api";
import { pressureWidth, setLineCounter, textHeight, type Rect } from "./geometry";
import { fontCss } from "../fonts";
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
    if (s.kind !== "line" && s.kind !== "arrow") ctx.closePath();
  }
  if (el.fill && s.kind !== "line" && s.kind !== "arrow") {
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
setLineCounter((text, fontSize, width, font) => {
  if (typeof document === "undefined") return text.split("\n").length;
  measureCtx ??= document.createElement("canvas").getContext("2d");
  if (!measureCtx) return text.split("\n").length;
  measureCtx.font = fontCss(font, fontSize);
  return wrapLines(measureCtx, text, width).length;
});

function drawText(ctx: CanvasRenderingContext2D, el: TextEl) {
  ctx.save();
  ctx.fillStyle = el.color;
  ctx.font = fontCss(el.font, el.fontSize);
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
  const size = el.fontSize ?? 18;
  const lineH = size * 1.33;
  ctx.font = fontCss(el.font, size);
  ctx.textBaseline = "top";
  const lines = wrapLines(ctx, el.text, el.w - 24);
  lines.slice(0, Math.max(1, Math.floor((el.h - 24) / lineH))).forEach((line, i) => ctx.fillText(line, el.x + 12, el.y + 12 + i * lineH));
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
