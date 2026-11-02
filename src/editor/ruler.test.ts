import { describe, expect, it } from "vitest";
import { Ruler, snapAngle } from "./ruler";

const deg = (r: number) => Math.round((r * 180) / Math.PI * 100) / 100;

describe("ruler", () => {
  it("snaps to common angles when close, else whole degrees", () => {
    expect(deg(snapAngle((44 * Math.PI) / 180, false))).toBe(45);
    expect(deg(snapAngle((-1.5 * Math.PI) / 180, false))).toBe(0);
    expect(deg(snapAngle((37.4 * Math.PI) / 180, false))).toBe(37);
    expect(deg(snapAngle((52 * Math.PI) / 180, true))).toBe(45);
  });

  it("projects onto an edge, offset so the ink sits against it", () => {
    const e = { a: [0, 100] as [number, number], dir: [1, 0] as [number, number], normal: [0, -1] as [number, number] };
    expect(Ruler.project(e, [50, 87], 2)).toEqual([50, 98]);
    const d = Math.SQRT1_2;
    const diag = { a: [0, 0] as [number, number], dir: [d, d] as [number, number], normal: [d, -d] as [number, number] };
    const [x, y] = Ruler.project(diag, [10, 0], 0);
    expect(x).toBeCloseTo(5);
    expect(y).toBeCloseTo(5);
  });
});
