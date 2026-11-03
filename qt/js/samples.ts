// Small previews for the tool drawer's menus (src/editor/editor.ts nibSample,
// smoothingSample): drawn in a 56 × 22 box.
import { drawStroke } from "../../src/editor/render";
import type { PenStyle, Pt } from "../../src/editor/model";

function isLightColor(hex: string): boolean {
  const m = /^#?([0-9a-f]{6})$/i.exec(hex.trim());
  if (!m) return false;
  const n = parseInt(m[1], 16);
  const lin = (v: number) => ((v /= 255) <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4);
  return 0.2126 * lin(n >> 16) + 0.7152 * lin((n >> 8) & 255) + 0.0722 * lin(n & 255) > 0.45;
}

/** An ink colour that shows on the drawer (very dark ink on a dark drawer, white on light). */
export function sampleInk(color: string, dark: boolean): string {
  if (!dark && isLightColor(color) && /^#f{6}$/i.test(color)) return "#111827";
  if (dark && !isLightColor(color) && /^#[0-3][0-9a-f][0-3][0-9a-f][0-3][0-9a-f]$/i.test(color)) return "#e7e5e4";
  return color;
}

export function nibSample(ctx: CanvasRenderingContext2D, style: PenStyle, color: string, size: number, dark: boolean) {
  const w = 56;
  const hgt = 22;
  const points: Pt[] = [];
  for (let i = 0; i <= 24; i++) {
    const t = i / 24;
    points.push([4 + t * (w - 8), hgt / 2 + Math.sin(t * Math.PI * 2) * 6, 0.35 + 0.5 * Math.sin(t * Math.PI)]);
  }
  drawStroke(ctx, { id: "sample", z: 0, kind: "stroke", tool: "pen", style, color: sampleInk(color, dark), width: Math.min(6.5, Math.max(1.4, size * 0.9)), points });
}

export function smoothingSample(ctx: CanvasRenderingContext2D, streamline: number, color: string, dark: boolean) {
  const w = 56;
  const hgt = 22;
  const points: Pt[] = [];
  for (let i = 0; i <= 40; i++) {
    const t = i / 40;
    const jitter = Math.sin(i * 12.9898) * 43758.5453;
    points.push([4 + t * (w - 8), hgt / 2 + Math.sin(t * Math.PI * 2) * 5 + (jitter - Math.floor(jitter) - 0.5) * 3.2, 0.5]);
  }
  drawStroke(ctx, { id: "sample", z: 0, kind: "stroke", tool: "pen", style: "ballpoint", color: sampleInk(color, dark), width: 1.6, points, smooth: streamline });
}
