import { describe, expect, it } from "vitest";
import { PAPER_SIZES, TEMPLATE_CATEGORIES, TEMPLATES } from "../api";
import { drawTemplate, paperLayout } from "./papers";
import { PdfPainter, type Measurer } from "./pdfVector";

const measurer: Measurer = { measure: (_f, t) => ({ width: t.length * 6, actualBoundingBoxAscent: 8, actualBoundingBoxDescent: 2 }) as TextMetrics };

describe("paper templates", () => {
  it("every template has a known category", () => {
    const cats = new Set(TEMPLATE_CATEGORIES.map((c) => c.id));
    for (const t of TEMPLATES) expect(cats.has(t.category)).toBe(true);
    expect(new Set(TEMPLATES.map((t) => t.id)).size).toBe(TEMPLATES.length);
  });

  it("draws every template on the page at every size and orientation", () => {
    for (const size of PAPER_SIZES) {
      for (const [w, h] of [[size.w, size.h], [size.h, size.w]]) {
        for (const t of TEMPLATES) {
          for (const paper of ["#ffffff", "#1f2329"]) {
            const p = new PdfPainter(w, h, measurer);
            drawTemplate(p as unknown as CanvasRenderingContext2D, t.id, w, h, paper);
            const ops = p.rec.ops.join("");
            // Paper fill plus (except blank) some lines or dots.
            expect(p.rec.ops.length).toBeGreaterThan(t.id === "blank" ? 0 : 1);
            if (paper !== "#ffffff") continue;
            // All coordinates stay near the page.
            for (const m of ops.matchAll(/(-?\d+(?:\.\d+)?) (-?\d+(?:\.\d+)?) [ml]\b/g)) {
              const x = Number(m[1]);
              const y = Number(m[2]);
              expect(x).toBeGreaterThan(-40);
              expect(y).toBeGreaterThan(-40);
              expect(x).toBeLessThan(w + 40);
              expect(y).toBeLessThan(h + 40);
            }
          }
        }
      }
    }
  }, 60_000);

  it("writing lines are inside the page, in order", () => {
    for (const t of TEMPLATES) {
      const { rules } = paperLayout(t.id, 794, 1123);
      for (let i = 0; i < rules.length; i++) {
        expect(rules[i]).toBeGreaterThan(0);
        expect(rules[i]).toBeLessThan(1123);
        if (i) expect(rules[i]).toBeGreaterThan(rules[i - 1]);
      }
    }
    expect(paperLayout("wide-ruled", 794, 1123).rules.length).toBeLessThan(paperLayout("ruled", 794, 1123).rules.length);
  });

  it("graph and coordinate papers are true millimetre grids", () => {
    const mm = 96 / 25.4;
    for (const t of ["graph", "coordinate"] as const) {
      const p = new PdfPainter(794, 1123, measurer);
      drawTemplate(p as unknown as CanvasRenderingContext2D, t, 794, 1123, "#ffffff");
      // Vertical grid lines: "x 0 m x 1123 l".
      const xs = [...p.rec.ops.join("").matchAll(/(-?[\d.]+) 0 m\s+\1 1123 l/g)].map((m) => Number(m[1])).sort((a, b) => a - b);
      expect(xs.length).toBeGreaterThan(200); // 210 mm across A4
      for (let i = 1; i < xs.length; i++) expect(xs[i] - xs[i - 1]).toBeCloseTo(mm, 2);
    }
  });
});
