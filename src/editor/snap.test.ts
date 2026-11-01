import { describe, expect, it } from "vitest";
import { paperTargets, snapMove, type Anchor, type Target } from "./snap";

const textAt = (y: number, left: number): Anchor[] => [
  { axis: "y", kind: "baseline", value: y + 15, span: [left, left + 100] },
  { axis: "y", kind: "mid", value: y + 10, span: [left, left + 100] },
  { axis: "x", kind: "left", value: left, span: [y, y + 20] },
  { axis: "x", kind: "right", value: left + 100, span: [y, y + 20] },
  { axis: "x", kind: "center", value: left + 50, span: [y, y + 20] },
];

describe("snapMove", () => {
  const other: Target[] = textAt(200, 50).map((a) => ({ ...a }));

  it("snaps a nearby baseline onto another text's baseline", () => {
    const r = snapMove(textAt(100, 300), other, 0, 103, 6);
    expect(r.dy).toBe(100);
    expect(r.guides.some((g) => g.label === "Baseline" && g.value === 215)).toBe(true);
  });

  it("aligns left edges and leaves far-away moves alone", () => {
    const r = snapMove(textAt(400, 53), other, 0, 0, 6);
    expect(r.dx).toBe(-3);
    expect(r.guides.find((g) => g.axis === "x")?.label).toBe("Left edge");
    const far = snapMove(textAt(400, 200), other, 1, 1, 6);
    expect(far).toMatchObject({ dx: 1, dy: 1, guides: [] });
  });

  it("puts text on ruled lines, centres it between them, and on the page centre", () => {
    const paper = paperTargets([110, 144], [0, 794], 90, 794, 1123);
    const onLine = snapMove(textAt(92, 300), paper, 0, 0, 6);
    expect(onLine.dy).toBe(3); // baseline 107 -> rule 110
    const centred = snapMove(textAt(114, 300), paper, 0, 0, 6);
    expect(centred.dy).toBe(3); // x-height centre 124 -> 127 between the rules
    expect(centred.guides[0].label).toBe("Centred in line");
    const page = snapMove(textAt(600, 345), paper, 0, 0, 6);
    expect(page.dx).toBe(2); // centre 395 -> 397
  });
});
