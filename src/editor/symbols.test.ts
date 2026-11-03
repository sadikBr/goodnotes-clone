import { describe, expect, it } from "vitest";
import type { P2 } from "../api";
import { MODELS_FOR_TESTS, matchSymbol, symbolShape, type SymbolName } from "./symbols";
import { handDrawn, NOT_SYMBOLS, rng } from "./symbols.samples";

describe("maths symbol recognition", () => {
  const names = Object.keys(MODELS_FOR_TESTS) as SymbolName[];

  it("recognises the clean models", () => {
    for (const name of names) {
      const scaled = MODELS_FOR_TESTS[name].map((st) => st.map(([x, y]): P2 => [x * 40 + 100, y * 40 + 100]));
      expect(matchSymbol(scaled)?.name, name).toBe(name);
    }
  });

  it("recognises hand-drawn versions (any stroke order and direction)", () => {
    const r = rng(11);
    for (const name of names) {
      let right = 0;
      const n = 40;
      for (let k = 0; k < n; k++) if (matchSymbol(handDrawn(MODELS_FOR_TESTS[name], r))?.name === name) right++;
      expect(right / n, name).toBeGreaterThanOrEqual(0.85);
    }
  }, 60_000);

  it("hardly ever mistakes them for another symbol", () => {
    // The generator occasionally squashes a part out of shape (e.g. a ⇒ whose
    // head comes out as a straight slash, which really reads as ≠): allow one
    // such sample per symbol in 25.
    const r = rng(5);
    for (const name of names) {
      let wrong = 0;
      for (let k = 0; k < 25; k++) {
        const m = matchSymbol(handDrawn(MODELS_FOR_TESTS[name], r));
        if (m && m.name !== name) wrong++;
      }
      expect(wrong, name).toBeLessThanOrEqual(1);
    }
  }, 60_000);

  it("leaves letters, digits and scribbles alone", () => {
    const r = rng(9);
    for (const [name, model] of Object.entries(NOT_SYMBOLS)) {
      for (let k = 0; k < 25; k++) expect(matchSymbol(handDrawn(model, r))?.name ?? null, name).toBeNull();
    }
  }, 60_000);

  it("draws the clean symbol where it was written, one stroke per part", () => {
    const s = symbolShape("implies", { x: 100, y: 200, w: 64, h: 40 });
    if (s.kind !== "path") throw new Error("expected a path");
    expect(s.breaks).toEqual([2, 4]);
    const xs = s.points.map((p) => p[0]);
    const ys = s.points.map((p) => p[1]);
    expect((Math.min(...xs) + Math.max(...xs)) / 2).toBeCloseTo(132, 0);
    expect((Math.min(...ys) + Math.max(...ys)) / 2).toBeCloseTo(220, 0);
  });
});
