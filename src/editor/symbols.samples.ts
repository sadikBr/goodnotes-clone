// Synthetic hand-drawn samples for testing the symbol recogniser.
import type { P2 } from "../api";

export function rng(seed: number) {
  let s = seed >>> 0;
  return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 2 ** 32);
}

/** Densify, then distort like handwriting: proportions, tilt, wobble, order and direction. */
export function handDrawn(strokes: P2[][], r: () => number, amount = 1): P2[][] {
  const sx = 1 + (r() - 0.5) * 0.4 * amount;
  const sy = 1 + (r() - 0.5) * 0.4 * amount;
  const rot = (r() - 0.5) * 0.24 * amount;
  let size = 0;
  for (const st of strokes) for (const p of st) size = Math.max(size, Math.abs(p[0]), Math.abs(p[1]));
  const wobSym = 0.02 * size * amount;
  const out = strokes.map((st) => {
    const dense: P2[] = [];
    for (let i = 0; i < st.length - 1; i++) for (let k = 0; k < 8; k++) dense.push([st[i][0] + ((st[i + 1][0] - st[i][0]) * k) / 8, st[i][1] + ((st[i + 1][1] - st[i][1]) * k) / 8]);
    dense.push(st[st.length - 1]);
    // Slow, wavy tremor along the stroke (a few waves per stroke), plus a
    // little noise; never more than a few percent of the stroke itself.
    let len = 0;
    for (let i = 1; i < st.length; i++) len += Math.hypot(st[i][0] - st[i - 1][0], st[i][1] - st[i - 1][1]);
    const wob = Math.min(wobSym, len * 0.035);
    const ph = r() * 6;
    const waves = 1.5 + r() * 2;
    const pts = dense.map(([x, y], i): P2 => {
      const u = (i / Math.max(1, dense.length - 1)) * Math.PI * 2 * waves;
      const wx = x + Math.sin(u + ph) * wob + (r() - 0.5) * wob * 0.25;
      const wy = y + Math.cos(u * 1.3 + ph) * wob + (r() - 0.5) * wob * 0.25;
      const X = wx * sx;
      const Y = wy * sy;
      return [X * Math.cos(rot) - Y * Math.sin(rot) + 200, X * Math.sin(rot) + Y * Math.cos(rot) + 300];
    });
    return r() < 0.3 ? pts.reverse() : pts;
  });
  // Stroke order varies.
  for (let i = out.length - 1; i > 0; i--) {
    const j = Math.floor(r() * (i + 1));
    [out[i], out[j]] = [out[j], out[i]];
  }
  return out.map((st) => st.map(([x, y]): P2 => [x * 40, y * 40]));
}

const curve = (f: (t: number) => P2, n = 30): P2[] => Array.from({ length: n + 1 }, (_, i) => f(i / n));

/** Things that are not maths symbols (letters, digits, scribbles). */
export const NOT_SYMBOLS: Record<string, P2[][]> = {
  S: [curve((t) => [0.5 + 0.45 * Math.cos(Math.PI * 0.2 + t * Math.PI * 2.6) * (t < 0.5 ? 1 : -1), t])],
  Z: [[[0, 0], [1, 0], [0, 1], [1, 1]]],
  two: [[...curve((t) => [0.5 + 0.45 * Math.cos(Math.PI * (1 - t)), 0.35 - 0.35 * Math.sin(Math.PI * t)], 10), [0, 1], [1, 1]]],
  N: [[[0, 1], [0, 0], [1, 1], [1, 0]]],
  W: [[[0, 0], [0.25, 1], [0.5, 0.3], [0.75, 1], [1, 0]]],
  M: [[[0, 1], [0.2, 0], [0.5, 0.7], [0.8, 0], [1, 1]]],
  c: [curve((t) => [0.5 + 0.5 * Math.cos(Math.PI * 0.25 + t * Math.PI * 1.5), 0.5 + 0.5 * Math.sin(Math.PI * 0.25 + t * Math.PI * 1.5)])],
  u: [curve((t) => [t, 0.6 * Math.sin(Math.PI * t)])],
  seven: [[[0, 0], [1, 0], [0.4, 1]]],
  x: [[[0, 0], [1, 1]], [[1, 0], [0, 1]]],
  plus: [[[0.5, 0], [0.5, 1]], [[0, 0.5], [1, 0.5]]],
  A: [[[0, 1], [0.5, 0], [1, 1]], [[0.25, 0.55], [0.75, 0.55]]],
  E: [[[1, 0], [0, 0], [0, 1], [1, 1]], [[0, 0.5], [0.7, 0.5]]],
  H: [[[0, 0], [0, 1]], [[1, 0], [1, 1]], [[0, 0.5], [1, 0.5]]],
  eleven: [[[0, 0], [0, 1]], [[0.4, 0], [0.4, 1]]],
  T: [[[0, 0], [1, 0]], [[0.5, 0], [0.5, 1]]],
  K: [[[0, 0], [0, 1]], [[0.8, 0], [0, 0.55], [0.8, 1]]],
  scribble: [curve((t) => [t + 0.15 * Math.sin(t * 40), 0.5 + 0.4 * Math.sin(t * 17)], 60)],
  loop: [curve((t) => [0.5 + 0.5 * Math.cos(t * Math.PI * 2), 0.5 + 0.5 * Math.sin(t * Math.PI * 2)])],
};
