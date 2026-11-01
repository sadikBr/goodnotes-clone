// Smart alignment guides for moving text.
//
// Anchors come from the actual glyphs (baseline, x-height centre, ink
// edges), not the text box, so text lines up the way it looks.

export type AnchorKind = "baseline" | "mid" | "left" | "right" | "center";
export type TargetKind = AnchorKind | "rule" | "rulemid" | "margin" | "pagecenter";

/** A line on the moving text: horizontal (axis "y") or vertical (axis "x"). */
export interface Anchor {
  axis: "x" | "y";
  kind: AnchorKind;
  value: number;
  /** Extent along the other axis, for drawing the guide. */
  span: [number, number];
}

export interface Target {
  axis: "x" | "y";
  kind: TargetKind;
  value: number;
  span: [number, number];
}

export interface Guide {
  axis: "x" | "y";
  value: number;
  from: number;
  to: number;
  label: string;
  /** Where to put the label: next to the text being moved. */
  labelAt: [number, number];
}

/** Which page features each text anchor can snap to. */
const PAIRS: Record<AnchorKind, TargetKind[]> = {
  baseline: ["baseline", "rule"],
  mid: ["mid", "rulemid"],
  left: ["left", "margin"],
  right: ["right"],
  center: ["center", "pagecenter"],
};

const LABELS: Record<TargetKind, string> = {
  baseline: "Baseline",
  mid: "Text centre",
  left: "Left edge",
  right: "Right edge",
  center: "Centre",
  rule: "On the line",
  rulemid: "Centred in line",
  margin: "Margin",
  pagecenter: "Page centre",
};

function bestOffset(anchors: Anchor[], targets: Target[], delta: number, threshold: number): number | null {
  let best: number | null = null;
  for (const a of anchors) {
    for (const t of targets) {
      if (t.axis !== a.axis || !PAIRS[a.kind].includes(t.kind)) continue;
      const off = t.value - (a.value + delta);
      if (Math.abs(off) <= threshold && (best === null || Math.abs(off) < Math.abs(best))) best = off;
    }
  }
  return best;
}

/**
 * Adjusts a proposed move (dx, dy) so the nearest text anchor within
 * `threshold` lands exactly on a target, and returns the guides to draw.
 */
export function snapMove(anchors: Anchor[], targets: Target[], dx: number, dy: number, threshold: number): { dx: number; dy: number; guides: Guide[] } {
  const xs = anchors.filter((a) => a.axis === "x");
  const ys = anchors.filter((a) => a.axis === "y");
  const ox = bestOffset(xs, targets, dx, threshold);
  const oy = bestOffset(ys, targets, dy, threshold);
  const ndx = dx + (ox ?? 0);
  const ndy = dy + (oy ?? 0);
  const guides: Guide[] = [];
  const seen = new Set<string>();
  for (const a of anchors) {
    const delta = a.axis === "x" ? ndx : ndy;
    const shift = a.axis === "x" ? ndy : ndx; // the span runs along the other axis
    if ((a.axis === "x" ? ox : oy) === null) continue;
    for (const t of targets) {
      if (t.axis !== a.axis || !PAIRS[a.kind].includes(t.kind)) continue;
      if (Math.abs(t.value - (a.value + delta)) > 0.5) continue;
      const key = `${t.axis}:${t.value.toFixed(1)}:${t.kind}`;
      if (seen.has(key)) continue;
      seen.add(key);
      guides.push({
        axis: t.axis,
        value: t.value,
        from: Math.min(a.span[0] + shift, t.span[0]),
        to: Math.max(a.span[1] + shift, t.span[1]),
        label: LABELS[t.kind],
        labelAt: a.axis === "y" ? [a.span[1] + shift + 8, t.value] : [t.value + 4, a.span[0] + shift - 6],
      });
    }
  }
  return { dx: ndx, dy: ndy, guides };
}

/** Converts another text's anchors into targets to align with. */
export function anchorsAsTargets(anchors: Anchor[]): Target[] {
  return anchors.map((a) => ({ axis: a.axis, kind: a.kind, value: a.value, span: a.span }));
}

/** Targets from the paper: ruled lines, the space between them, the margin and page centre. */
export function paperTargets(rules: number[], ruleSpan: [number, number], margin: number | null, pageW: number, pageH: number): Target[] {
  const out: Target[] = rules.map((y) => ({ axis: "y" as const, kind: "rule" as const, value: y, span: ruleSpan }));
  for (let i = 1; i < rules.length; i++) out.push({ axis: "y", kind: "rulemid", value: (rules[i - 1] + rules[i]) / 2, span: ruleSpan });
  if (margin !== null) out.push({ axis: "x", kind: "margin", value: margin, span: [0, pageH] });
  out.push({ axis: "x", kind: "pagecenter", value: pageW / 2, span: [0, pageH] });
  return out;
}
