// Notebook cover designs. Each design draws a whole cover page in the
// notebook's colour and says where its title and date go (they are ordinary,
// editable text boxes). Drawing uses only plain paths and fills, so covers
// also export as sharp vectors; "random" patterns use a fixed seed so a cover
// always looks the same.

import type { Template } from "../api";
import type { FontId } from "../fonts";

export type CoverDesign = "classic" | "minimal" | "geometric" | "waves" | "marble" | "leather" | "kraft" | "dots";

export const COVER_DESIGNS: { id: CoverDesign; label: string; template: Template }[] = [
  { id: "classic", label: "Classic", template: "cover" },
  { id: "minimal", label: "Minimal", template: "cover-minimal" },
  { id: "geometric", label: "Geometric", template: "cover-geometric" },
  { id: "waves", label: "Waves", template: "cover-waves" },
  { id: "marble", label: "Marble", template: "cover-marble" },
  { id: "leather", label: "Leather", template: "cover-leather" },
  { id: "kraft", label: "Kraft", template: "cover-kraft" },
  { id: "dots", label: "Dots", template: "cover-dots" },
];

export function isCoverTemplate(t: string): boolean {
  return t === "cover" || t.startsWith("cover-");
}

export function coverDesignOf(t: string): CoverDesign {
  return COVER_DESIGNS.find((d) => d.template === t)?.id ?? "classic";
}

export function coverTemplate(d: CoverDesign): Template {
  return COVER_DESIGNS.find((x) => x.id === d)?.template ?? "cover";
}

/** Where a design puts its title and date, and how they look. */
export interface CoverText {
  title: { x: number; y: number; w: number; h: number };
  date: { x: number; y: number; w: number };
  align: "left" | "center" | "right";
  titleColor: string;
  dateColor: string;
  font: FontId;
  maxSize: number;
  dateSize: number;
}

type Ctx = CanvasRenderingContext2D;
type Box = { x: number; y: number; w: number; h: number };

// --- helpers ---------------------------------------------------------------------

/** Mixes a #rrggbb colour with black (amount < 0) or white (amount > 0). */
export function shade(color: string, amount: number): string {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(color.trim());
  if (!m) return color;
  const target = amount < 0 ? 0 : 255;
  const k = Math.abs(amount);
  const ch = (hx: string) => Math.round(parseInt(hx, 16) * (1 - k) + target * k);
  return `rgb(${ch(m[1])}, ${ch(m[2])}, ${ch(m[3])})`;
}

/** A small seeded random generator (same cover, same pattern). */
function rng(seed: number) {
  let s = seed >>> 0 || 1;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 4294967296;
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

function fillRound(ctx: Ctx, b: Box, r: number, style: string) {
  ctx.fillStyle = style;
  ctx.beginPath();
  roundRect(ctx, b.x, b.y, b.w, b.h, r);
  ctx.fill();
}

/** A soft drop shadow under a rounded box (a few faint layers). */
function softShadow(ctx: Ctx, b: Box, r: number, dy = 4) {
  for (let k = 3; k >= 1; k--) fillRound(ctx, { x: b.x - k + 1, y: b.y - k + dy, w: b.w + 2 * k - 2, h: b.h + 2 * k }, r + k, "rgba(0, 0, 0, 0.07)");
}

/** Many parallel lines as one stroke. */
function lines(ctx: Ctx, segs: number[][], style: string, width: number, dash: number[] = []) {
  if (!segs.length) return;
  ctx.save();
  ctx.strokeStyle = style;
  ctx.lineWidth = width;
  ctx.setLineDash(dash);
  ctx.beginPath();
  for (const [x0, y0, x1, y1] of segs) {
    ctx.moveTo(x0, y0);
    ctx.lineTo(x1, y1);
  }
  ctx.stroke();
  ctx.restore();
}

/** Fine cross threads (cloth texture). */
function linen(ctx: Ctx, w: number, h: number, alpha: number) {
  const light: number[][] = [];
  const dark: number[][] = [];
  for (let y = 1.5, i = 0; y < h; y += 2.6, i++) (i % 2 ? light : dark).push([0, y, w, y]);
  for (let x = 1.5, i = 0; x < w; x += 2.6, i++) (i % 2 ? light : dark).push([x, 0, x, h]);
  lines(ctx, light, `rgba(255, 255, 255, ${alpha})`, 0.8);
  lines(ctx, dark, `rgba(0, 0, 0, ${alpha})`, 0.8);
}

/** A soft sheen from the top-left (many faint layers; vector export has no gradients). */
function sheen(ctx: Ctx, w: number, h: number, strength = 0.0065) {
  ctx.save();
  ctx.fillStyle = `rgba(255, 255, 255, ${strength})`;
  const n = 28;
  for (let k = 0; k < n; k++) {
    const t = 1 - k / n;
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(w * 1.05 * t, 0);
    ctx.lineTo(0, h * 0.8 * t);
    ctx.closePath();
    ctx.fill();
  }
  ctx.restore();
}

/** A title plate: box for the text, and the divider line under the title. */
function plateText(p: Box, pad = 26): { title: Box; date: { x: number; y: number; w: number }; divider: number } {
  const divider = p.y + p.h * 0.66;
  return {
    title: { x: p.x + pad, y: p.y + 26, w: p.w - 2 * pad, h: divider - p.y - 34 },
    date: { x: p.x + pad, y: divider + 12, w: p.w - 2 * pad },
    divider,
  };
}

function divider(ctx: Ctx, x0: number, x1: number, y: number, color: string, diamond: string) {
  const cx = (x0 + x1) / 2;
  lines(
    ctx,
    [
      [x0, y, cx - 9, y],
      [cx + 9, y, x1, y],
    ],
    color,
    1,
  );
  ctx.fillStyle = diamond;
  ctx.beginPath();
  ctx.moveTo(cx, y - 4);
  ctx.lineTo(cx + 4, y);
  ctx.lineTo(cx, y + 4);
  ctx.lineTo(cx - 4, y);
  ctx.closePath();
  ctx.fill();
}

// --- layouts ---------------------------------------------------------------------

function classicLayout(w: number, h: number) {
  const spine = Math.round(Math.max(30, w * 0.075));
  const bandW = Math.max(12, w * 0.018);
  const bandX = w - Math.max(34, w * 0.07) - bandW;
  const left = spine + 26;
  const right = bandX - 26;
  const pw = Math.min(520, (right - left) * 0.84);
  const ph = Math.max(150, Math.min(220, h * 0.17));
  const plate = { x: left + (right - left - pw) / 2, y: Math.round(h * (w > h ? 0.26 : 0.22)), w: pw, h: ph };
  return { spine, band: { x: bandX, w: bandW }, plate, ...plateText(plate) };
}

/** Where each design's title and date go. */
export function coverText(design: CoverDesign, w: number, h: number): CoverText {
  const dark = { titleColor: "#1f2937", dateColor: "#6b7280" };
  const light = { titleColor: "#ffffff", dateColor: "rgba(255, 255, 255, 0.78)" };
  const m = Math.max(48, w * 0.08);
  switch (design) {
    case "classic": {
      const L = classicLayout(w, h);
      return { title: L.title, date: L.date, align: "center", ...dark, font: "sans", maxSize: 34, dateSize: 15 };
    }
    case "minimal": {
      const y = h * (w > h ? 0.5 : 0.6);
      return { title: { x: m, y, w: w - 2 * m, h: Math.min(150, h * 0.16) }, date: { x: m, y: y + Math.min(150, h * 0.16) + 14, w: w - 2 * m }, align: "left", ...light, font: "sans", maxSize: 52, dateSize: 16 };
    }
    case "geometric": {
      const y = h * 0.66;
      return { title: { x: m, y, w: w - 2 * m, h: Math.min(130, h * 0.14) }, date: { x: m, y: y + Math.min(130, h * 0.14) + 10, w: w - 2 * m }, align: "left", ...light, font: "sans", maxSize: 44, dateSize: 16 };
    }
    case "waves": {
      const y = h * 0.16;
      return { title: { x: m, y, w: w - 2 * m, h: Math.min(140, h * 0.15) }, date: { x: m, y: y + Math.min(140, h * 0.15) + 8, w: w - 2 * m }, align: "center", ...light, font: "sans", maxSize: 46, dateSize: 16 };
    }
    case "marble": {
      const p = marblePlate(w, h);
      return { title: { x: p.x + 24, y: p.y + 40, w: p.w - 48, h: p.h * 0.55 - 40 }, date: { x: p.x + 24, y: p.y + p.h * 0.72 - 18, w: p.w - 48 }, align: "center", titleColor: "#111827", dateColor: "#4b5563", font: "caveat", maxSize: 40, dateSize: 20 };
    }
    case "leather": {
      const y = h * 0.36;
      const gold = "#e8cf8a";
      return { title: { x: m + 20, y, w: w - 2 * m - 40, h: Math.min(130, h * 0.13) }, date: { x: m + 20, y: y + Math.min(130, h * 0.13) + 34, w: w - 2 * m - 40 }, align: "center", titleColor: gold, dateColor: "rgba(232, 207, 138, 0.75)", font: "sans", maxSize: 40, dateSize: 15 };
    }
    case "kraft": {
      const p = kraftLabel(w, h);
      return { title: { x: p.x + 22, y: p.y + 22, w: p.w - 44, h: p.h * 0.62 - 22 }, date: { x: p.x + 22, y: p.y + p.h * 0.66, w: p.w - 44 }, align: "center", titleColor: "#3f2d1c", dateColor: "#7a6047", font: "kalam", maxSize: 38, dateSize: 17 };
    }
    case "dots": {
      const c = dotsLabel(w, h);
      const tw = c.r * 1.5;
      return { title: { x: c.x - tw / 2, y: c.y - c.r * 0.42, w: tw, h: c.r * 0.62 }, date: { x: c.x - tw / 2, y: c.y + c.r * 0.3, w: tw }, align: "center", ...dark, font: "sans", maxSize: 36, dateSize: 15 };
    }
  }
}

function marblePlate(w: number, h: number): Box {
  const pw = Math.min(460, w * 0.6);
  const ph = Math.max(170, Math.min(230, h * 0.19));
  return { x: (w + Math.max(40, w * 0.09) - pw) / 2, y: h * 0.17, w: pw, h: ph };
}

function kraftLabel(w: number, h: number): Box {
  const band = Math.max(36, w * 0.09);
  const pw = Math.min(440, (w - band) * 0.66);
  const ph = Math.max(150, Math.min(200, h * 0.16));
  return { x: band + (w - band - pw) / 2, y: h * 0.2, w: pw, h: ph };
}

function dotsLabel(w: number, h: number) {
  const r = Math.min(w, h) * 0.28;
  return { x: w / 2, y: h * (w > h ? 0.45 : 0.36), r };
}

// --- designs -----------------------------------------------------------------------

function drawClassic(ctx: Ctx, w: number, h: number, color: string) {
  const L = classicLayout(w, h);
  linen(ctx, w, h, 0.025);
  sheen(ctx, w, h);
  ctx.fillStyle = shade(color, -0.28);
  ctx.fillRect(0, 0, L.spine, h);
  ctx.fillStyle = "rgba(0, 0, 0, 0.18)";
  ctx.fillRect(L.spine, 0, 4, h);
  ctx.fillStyle = "rgba(255, 255, 255, 0.10)";
  ctx.fillRect(L.spine - 3, 0, 2, h);
  const inset = 16;
  const sx = L.spine + inset;
  const sw = w - L.spine - 2 * inset;
  lines(
    ctx,
    [
      [L.spine - 11, 0, L.spine - 11, h],
      [sx, inset, sx + sw, inset],
      [sx + sw, inset, sx + sw, h - inset],
      [sx + sw, h - inset, sx, h - inset],
      [sx, h - inset, sx, inset],
    ],
    "rgba(255, 255, 255, 0.42)",
    1.2,
    [5, 4],
  );
  ctx.fillStyle = "rgba(0, 0, 0, 0.18)";
  ctx.fillRect(L.band.x + 3, 0, L.band.w, h);
  ctx.fillStyle = shade(color, -0.42);
  ctx.fillRect(L.band.x, 0, L.band.w, h);
  ctx.fillStyle = "rgba(255, 255, 255, 0.14)";
  ctx.fillRect(L.band.x + 2, 0, 1.5, h);
  const p = L.plate;
  softShadow(ctx, p, 12);
  fillRound(ctx, p, 12, "#fbf8f1");
  ctx.strokeStyle = shade(color, 0.35);
  ctx.lineWidth = 1.4;
  ctx.beginPath();
  roundRect(ctx, p.x + 8, p.y + 8, p.w - 16, p.h - 16, 7);
  ctx.stroke();
  divider(ctx, p.x + 40, p.x + p.w - 40, L.divider, shade(color, 0.2), shade(color, 0.1));
}

function drawMinimal(ctx: Ctx, w: number, h: number, color: string) {
  sheen(ctx, w, h, 0.004);
  const m = Math.max(28, w * 0.035);
  ctx.save();
  ctx.strokeStyle = "rgba(255, 255, 255, 0.45)";
  ctx.lineWidth = 1;
  ctx.strokeRect(m, m, w - 2 * m, h - 2 * m);
  ctx.restore();
  const t = coverText("minimal", w, h);
  // A short bold bar above the title, and a small mark in the corner.
  ctx.fillStyle = "#ffffff";
  ctx.fillRect(t.title.x, t.title.y - 26, 54, 6);
  ctx.fillStyle = "rgba(255, 255, 255, 0.5)";
  ctx.beginPath();
  ctx.arc(w - m - 30, m + 30, 7, 0, Math.PI * 2);
  ctx.fill();
  void color;
}

function drawGeometric(ctx: Ctx, w: number, h: number, color: string) {
  // Low-poly facets over the top 58%, fading out towards the title.
  const rand = rng(7);
  const cols = 7;
  const size = w / cols;
  const rows = Math.ceil((h * 0.58) / size);
  const tones = [-0.3, -0.18, -0.08, 0.06, 0.16, 0.26];
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const x = c * size;
      const y = r * size;
      const flip = (r + c) % 2 === 0;
      const tris = flip
        ? [
            [x, y, x + size, y, x, y + size],
            [x + size, y, x + size, y + size, x, y + size],
          ]
        : [
            [x, y, x + size, y, x + size, y + size],
            [x, y, x + size, y + size, x, y + size],
          ];
      for (const tri of tris) {
        // Fade into the plain colour near the bottom of the pattern.
        const fade = Math.min(1, (rows - r) / 2.2);
        const tone = tones[Math.floor(rand() * tones.length)] * fade;
        ctx.fillStyle = shade(color, tone);
        ctx.beginPath();
        ctx.moveTo(tri[0], tri[1]);
        ctx.lineTo(tri[2], tri[3]);
        ctx.lineTo(tri[4], tri[5]);
        ctx.closePath();
        ctx.fill();
        // Hairline seams hide anti-aliasing gaps between facets.
        ctx.strokeStyle = ctx.fillStyle;
        ctx.lineWidth = 0.6;
        ctx.stroke();
      }
    }
  }
  const t = coverText("geometric", w, h);
  ctx.fillStyle = "rgba(255, 255, 255, 0.9)";
  ctx.fillRect(t.title.x, t.title.y - 24, 46, 5);
}

function drawWaves(ctx: Ctx, w: number, h: number, color: string) {
  sheen(ctx, w, h, 0.005);
  // Layered waves rising from the bottom, each a little darker.
  const layers = [
    { top: 0.5, amp: 0.035, phase: 0.2, tone: 0.18 },
    { top: 0.6, amp: 0.04, phase: 1.4, tone: 0.05 },
    { top: 0.7, amp: 0.035, phase: 2.6, tone: -0.12 },
    { top: 0.8, amp: 0.03, phase: 3.9, tone: -0.26 },
    { top: 0.89, amp: 0.025, phase: 5.1, tone: -0.4 },
  ];
  for (const l of layers) {
    const y0 = h * l.top;
    const a = h * l.amp;
    ctx.fillStyle = shade(color, l.tone);
    ctx.beginPath();
    ctx.moveTo(0, h);
    ctx.lineTo(0, y0 + Math.sin(l.phase) * a);
    const n = 6;
    for (let k = 1; k <= n; k++) {
      const x1 = (w * (k - 0.5)) / n;
      const x2 = (w * k) / n;
      ctx.quadraticCurveTo(x1, y0 + Math.sin(l.phase + k * 1.3 - 0.65) * a * 1.8, x2, y0 + Math.sin(l.phase + k * 1.3) * a);
    }
    ctx.lineTo(w, h);
    ctx.closePath();
    ctx.fill();
  }
}

function drawMarble(ctx: Ctx, w: number, h: number, color: string) {
  // Composition-book mottle: many small irregular blobs, light and dark.
  const rand = rng(11);
  const base = shade(color, -0.2);
  ctx.fillStyle = base;
  ctx.fillRect(0, 0, w, h);
  const count = Math.round((w * h) / 520);
  const tones = ["rgba(255, 255, 255, 0.55)", "rgba(255, 255, 255, 0.3)", "rgba(0, 0, 0, 0.35)", shade(color, 0.25), shade(color, -0.45)];
  for (let i = 0; i < count; i++) {
    const x = rand() * w;
    const y = rand() * h;
    const r = 1.5 + rand() * rand() * 9;
    ctx.fillStyle = tones[Math.floor(rand() * tones.length)];
    ctx.beginPath();
    const pts = 5;
    for (let k = 0; k <= pts; k++) {
      const a = (k / pts) * Math.PI * 2;
      const rr = r * (0.55 + rand() * 0.6);
      const px = x + Math.cos(a) * rr;
      const py = y + Math.sin(a) * rr * 0.8;
      if (k === 0) ctx.moveTo(px, py);
      else ctx.lineTo(px, py);
    }
    ctx.closePath();
    ctx.fill();
  }
  // Cloth tape spine.
  const spine = Math.max(40, w * 0.09);
  ctx.fillStyle = "#1f2328";
  ctx.fillRect(0, 0, spine, h);
  linen(ctx, spine, h, 0.06);
  ctx.fillStyle = "rgba(255, 255, 255, 0.08)";
  ctx.fillRect(spine - 3, 0, 2, h);
  // White label with rules.
  const p = marblePlate(w, h);
  softShadow(ctx, p, 8, 3);
  fillRound(ctx, p, 8, "#fbfaf7");
  ctx.strokeStyle = "#1f2328";
  ctx.lineWidth = 2;
  ctx.beginPath();
  roundRect(ctx, p.x + 7, p.y + 7, p.w - 14, p.h - 14, 5);
  ctx.stroke();
  lines(
    ctx,
    [
      [p.x + 24, p.y + p.h * 0.6, p.x + p.w - 24, p.y + p.h * 0.6],
      [p.x + 24, p.y + p.h * 0.82, p.x + p.w - 24, p.y + p.h * 0.82],
    ],
    "rgba(31, 35, 40, 0.35)",
    1,
  );
}

function drawLeather(ctx: Ctx, w: number, h: number, color: string) {
  ctx.fillStyle = shade(color, -0.42);
  ctx.fillRect(0, 0, w, h);
  // Grain: tiny pores, light and dark.
  const rand = rng(23);
  const pores = Math.round((w * h) / 110);
  const dark = new Path2DLike();
  const light = new Path2DLike();
  for (let i = 0; i < pores; i++) {
    const x = rand() * w;
    const y = rand() * h;
    const r = 0.5 + rand() * 1.1;
    (rand() < 0.6 ? dark : light).circle(x, y, r);
  }
  dark.fill(ctx, "rgba(0, 0, 0, 0.22)");
  light.fill(ctx, "rgba(255, 255, 255, 0.05)");
  sheen(ctx, w, h, 0.004);
  // Embossed double border.
  const m = Math.max(30, w * 0.045);
  ctx.save();
  ctx.lineWidth = 1.6;
  for (const [off, style] of [
    [1.2, "rgba(255, 255, 255, 0.08)"],
    [0, "rgba(0, 0, 0, 0.45)"],
  ] as const) {
    ctx.strokeStyle = style;
    ctx.strokeRect(m + off, m + off, w - 2 * m, h - 2 * m);
    ctx.strokeRect(m + 8 + off, m + 8 + off, w - 2 * m - 16, h - 2 * m - 16);
  }
  ctx.restore();
  // Metal corner protectors.
  const c = Math.max(38, w * 0.07);
  const metal = "#b9975b";
  for (const [x, y, sx, sy] of [
    [0, 0, 1, 1],
    [w, 0, -1, 1],
    [0, h, 1, -1],
    [w, h, -1, -1],
  ]) {
    ctx.fillStyle = metal;
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.lineTo(x + sx * c, y);
    ctx.lineTo(x, y + sy * c);
    ctx.closePath();
    ctx.fill();
    ctx.strokeStyle = "rgba(255, 255, 255, 0.35)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(x + sx * (c - 6), y + sy * 2);
    ctx.lineTo(x + sx * 2, y + sy * (c - 6));
    ctx.stroke();
  }
  // Gold rules around the title.
  const t = coverText("leather", w, h);
  const cx = w / 2;
  const gold = "rgba(232, 207, 138, 0.7)";
  lines(
    ctx,
    [
      [cx - 90, t.title.y - 22, cx + 90, t.title.y - 22],
      [cx - 90, t.date.y - 14, cx + 90, t.date.y - 14],
    ],
    gold,
    1.2,
  );
}

function drawKraft(ctx: Ctx, w: number, h: number, color: string) {
  ctx.fillStyle = "#c8a77c";
  ctx.fillRect(0, 0, w, h);
  // Paper fibres.
  const rand = rng(31);
  const dark: number[][] = [];
  const light: number[][] = [];
  const fibres = Math.round((w * h) / 260);
  for (let i = 0; i < fibres; i++) {
    const x = rand() * w;
    const y = rand() * h;
    const a = rand() * Math.PI;
    const l = 2 + rand() * 7;
    (rand() < 0.55 ? dark : light).push([x, y, x + Math.cos(a) * l, y + Math.sin(a) * l]);
  }
  lines(ctx, dark, "rgba(92, 62, 30, 0.22)", 0.7);
  lines(ctx, light, "rgba(255, 244, 222, 0.28)", 0.7);
  // Cloth tape in the notebook colour.
  const band = Math.max(36, w * 0.09);
  ctx.fillStyle = color;
  ctx.fillRect(0, 0, band, h);
  linen(ctx, band, h, 0.07);
  ctx.fillStyle = "rgba(0, 0, 0, 0.15)";
  ctx.fillRect(band, 0, 3, h);
  // Sticker label with a coloured edge.
  const p = kraftLabel(w, h);
  softShadow(ctx, p, 14, 3);
  fillRound(ctx, p, 14, "#fffdf8");
  ctx.strokeStyle = color;
  ctx.lineWidth = 3;
  ctx.beginPath();
  roundRect(ctx, p.x + 7, p.y + 7, p.w - 14, p.h - 14, 9);
  ctx.stroke();
  lines(ctx, [[p.x + 34, p.y + p.h * 0.64, p.x + p.w - 34, p.y + p.h * 0.64]], "rgba(63, 45, 28, 0.3)", 1, [3, 3]);
}

function drawDots(ctx: Ctx, w: number, h: number, color: string) {
  const dots = new Path2DLike();
  const gap = Math.max(30, w * 0.042);
  for (let row = 0, y = gap / 2; y < h + gap; y += gap * 0.866, row++) {
    for (let x = row % 2 ? gap / 2 : 0; x < w + gap; x += gap) dots.circle(x, y, gap * 0.17);
  }
  dots.fill(ctx, shade(color, 0.22));
  const c = dotsLabel(w, h);
  // Shadow, white disc, ring in the cover colour.
  for (let k = 3; k >= 1; k--) {
    ctx.fillStyle = "rgba(0, 0, 0, 0.07)";
    ctx.beginPath();
    ctx.arc(c.x, c.y + 4, c.r + k, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.fillStyle = "#fffdf8";
  ctx.beginPath();
  ctx.arc(c.x, c.y, c.r, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = shade(color, 0.1);
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.arc(c.x, c.y, c.r - 10, 0, Math.PI * 2);
  ctx.stroke();
  const t = coverText("dots", w, h);
  divider(ctx, c.x - c.r * 0.45, c.x + c.r * 0.45, t.date.y - 10, shade(color, 0.2), shade(color, 0.05));
}

/** Collects many small circles to fill in one go (one path, one fill). */
class Path2DLike {
  private circles: number[][] = [];
  circle(x: number, y: number, r: number) {
    this.circles.push([x, y, r]);
  }
  fill(ctx: Ctx, style: string) {
    ctx.fillStyle = style;
    ctx.beginPath();
    for (const [x, y, r] of this.circles) {
      ctx.moveTo(x + r, y);
      ctx.arc(x, y, r, 0, Math.PI * 2);
    }
    ctx.fill();
  }
}

/** Draws a cover page (the page's paper colour is the notebook's cover colour). */
export function drawCoverDesign(ctx: Ctx, template: string, w: number, h: number, color: string) {
  ctx.fillStyle = color;
  ctx.fillRect(0, 0, w, h);
  ctx.save();
  switch (coverDesignOf(template)) {
    case "classic":
      drawClassic(ctx, w, h, color);
      break;
    case "minimal":
      drawMinimal(ctx, w, h, color);
      break;
    case "geometric":
      drawGeometric(ctx, w, h, color);
      break;
    case "waves":
      drawWaves(ctx, w, h, color);
      break;
    case "marble":
      drawMarble(ctx, w, h, color);
      break;
    case "leather":
      drawLeather(ctx, w, h, color);
      break;
    case "kraft":
      drawKraft(ctx, w, h, color);
      break;
    case "dots":
      drawDots(ctx, w, h, color);
      break;
  }
  ctx.restore();
}
