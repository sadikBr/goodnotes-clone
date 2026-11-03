import { describe, expect, it } from "vitest";
import { newer } from "./updates";

describe("version comparison", () => {
  it("compares numerically, ignoring a leading v", () => {
    expect(newer("0.10.0", "0.9.9")).toBe(true);
    expect(newer("v0.8.1", "0.8.0")).toBe(true);
    expect(newer("0.8.0", "0.8.0")).toBe(false);
    expect(newer("0.7.2", "0.8.0")).toBe(false);
  });
});
