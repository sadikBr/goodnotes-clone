import { describe, expect, it } from "vitest";
import { num, parseColor, pdfTextString, PdfDocument, PdfPainter, winAnsiString, type Measurer } from "./pdfVector";

const measurer: Measurer = {
  measure: (_font, text) => ({ width: text.length * 10, actualBoundingBoxAscent: 8, actualBoundingBoxDescent: 2, actualBoundingBoxLeft: 0, actualBoundingBoxRight: text.length * 10 }) as TextMetrics,
};

function ctx(p: PdfPainter): CanvasRenderingContext2D {
  return p as unknown as CanvasRenderingContext2D;
}

describe("pdf painter", () => {
  it("formats numbers compactly", () => {
    expect(num(1)).toBe("1");
    expect(num(0.5)).toBe("0.5");
    expect(num(1 / 3)).toBe("0.333");
    expect(num(-0.0001)).toBe("0");
    expect(num(NaN)).toBe("0");
  });

  it("parses css colours", () => {
    expect(parseColor("#ff0000")).toEqual({ r: 1, g: 0, b: 0, a: 1 });
    expect(parseColor("#0f08").a).toBeCloseTo(0.533, 2);
    expect(parseColor("rgba(96, 140, 200, 0.35)")).toEqual({ r: 96 / 255, g: 140 / 255, b: 200 / 255, a: 0.35 });
    expect(parseColor("transparent").a).toBe(0);
  });

  it("records strokes in page units with width, cap and transparency", () => {
    const p = new PdfPainter(800, 600, measurer);
    const c = ctx(p);
    c.save();
    c.translate(10, 20);
    c.scale(2, 2);
    c.strokeStyle = "rgba(0, 0, 255, 0.5)";
    c.lineWidth = 3;
    c.lineCap = "round";
    c.beginPath();
    c.moveTo(0, 0);
    c.lineTo(5, 5);
    c.stroke();
    c.restore();
    const op = p.rec.ops.join("");
    expect(op).toContain("10 20 m");
    expect(op).toContain("20 30 l");
    expect(op).toContain("6 w 1 J");
    expect(op).toContain("0 0 1 RG");
    expect(op).toMatch(/\/G0 gs/);
    expect(p.rec.gstates.get("G0")).toEqual([0.5, "Normal"]);
  });

  it("uses multiply blending for highlighter ink and skips invisible paint", () => {
    const p = new PdfPainter(100, 100, measurer);
    const c = ctx(p);
    c.globalCompositeOperation = "multiply";
    c.globalAlpha = 0.35;
    c.beginPath();
    c.moveTo(0, 0);
    c.lineTo(10, 0);
    c.stroke();
    expect([...p.rec.gstates.values()]).toEqual([[0.35, "Multiply"]]);
    c.fillStyle = "transparent";
    c.fillRect(0, 0, 5, 5);
    expect(p.rec.ops).toHaveLength(1);
  });

  it("draws full circles as four cubic curves and converts quadratics", () => {
    const p = new PdfPainter(100, 100, measurer);
    const c = ctx(p);
    c.beginPath();
    c.arc(50, 50, 10, 0, Math.PI * 2);
    c.fill();
    const op = p.rec.ops[0];
    expect(op.match(/ c\b/g)).toHaveLength(4);
    expect(op).toContain("60 50 m");
    c.beginPath();
    c.moveTo(0, 0);
    c.quadraticCurveTo(30, 0, 30, 30);
    c.stroke();
    expect(p.rec.ops[1]).toContain("20 0 30 10 30 30 c");
  });

  it("encodes text strings safely", () => {
    expect(winAnsiString("a (b) \\ é •")).toBe("(a \\(b\\) \\\\ \\351 \\225)");
    expect(winAnsiString("漢")).toBe("(?)");
    expect(pdfTextString("Notes")).toBe("(Notes)");
    expect(pdfTextString("été")).toBe("<FEFF00E9007400E9>");
  });
});

describe("pdf document", () => {
  it("writes a valid file with a correct cross-reference table", async () => {
    const doc = new PdfDocument();
    for (let i = 0; i < 2; i++) {
      const p = new PdfPainter(794, 1123, measurer);
      const c = ctx(p);
      c.fillStyle = "#ffffff";
      c.fillRect(0, 0, 794, 1123);
      c.globalAlpha = 0.5;
      c.fillStyle = "#000";
      c.fillRect(10, 10, 20, 20);
      await doc.addPage(p.rec);
    }
    const bytes = doc.finish("Biology");
    const text = new TextDecoder("latin1").decode(bytes);
    expect(text.startsWith("%PDF-1.7\n")).toBe(true);
    expect(text.trimEnd().endsWith("%%EOF")).toBe(true);
    expect(text).toContain("/Count 2");
    expect(text).toContain("/MediaBox [0 0 595.5 842.25]");
    expect(text).toContain("/Title (Biology)");
    // Every xref offset points at its object.
    const xrefAt = Number(/startxref\n(\d+)/.exec(text)![1]);
    expect(text.slice(xrefAt, xrefAt + 4)).toBe("xref");
    const entries = text.slice(xrefAt).split("\n").slice(3).filter((l) => / n $/.test(l));
    entries.forEach((line, i) => {
      const off = Number(line.slice(0, 10));
      expect(text.slice(off).startsWith(`${i + 1} 0 obj`)).toBe(true);
    });
    // Content streams are zlib-compressed and start with the units→points flip.
    const m = /<< \/Length (\d+) \/Filter \/FlateDecode >>\nstream\n/.exec(text)!;
    const start = m.index + m[0].length;
    const packed = bytes.slice(start, start + Number(m[1]));
    const stream = new Blob([packed]).stream().pipeThrough(new DecompressionStream("deflate"));
    const content = new TextDecoder("latin1").decode(await new Response(stream).arrayBuffer());
    expect(content.startsWith("0.75 0 0 -0.75 0 842.25 cm")).toBe(true);
    expect(content).toContain("10 10 m");
  });
});
