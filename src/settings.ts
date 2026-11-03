// App settings: one typed place for preferences shown on the Settings
// screen, saved like other preferences and announced when they change.

import { getPref, setPref } from "./prefs";

export interface Settings {
  /** Path smoothing for the pen. */
  smoothing: "off" | "light" | "normal" | "strong";
  /** The cursor under the pen for drawing tools. */
  penCursor: "cross" | "dot" | "none";
  /** How stylus pressure maps to line width. */
  pressure: "light" | "normal" | "firm";
  /** What the stylus side (barrel) button does while held. */
  sideButton: "eraser" | "lasso" | "none";
  /** The stylus's eraser end erases. */
  eraserEnd: boolean;
  /** Ignore touches from a resting hand while the pen is in use. */
  palmRejection: boolean;
}

const SPEC: { [K in keyof Settings]: { key: string; fallback: Settings[K]; allowed?: readonly string[] } } = {
  smoothing: { key: "inkleaf-smoothing", fallback: "normal", allowed: ["off", "light", "normal", "strong"] },
  penCursor: { key: "inkleaf-pen-cursor", fallback: "cross", allowed: ["cross", "dot", "none"] },
  pressure: { key: "inkleaf-pressure", fallback: "normal", allowed: ["light", "normal", "firm"] },
  sideButton: { key: "inkleaf-side-button", fallback: "eraser", allowed: ["eraser", "lasso", "none"] },
  eraserEnd: { key: "inkleaf-eraser-end", fallback: true },
  palmRejection: { key: "inkleaf-palm-rejection", fallback: true },
};

const listeners = new Set<() => void>();

export function getSetting<K extends keyof Settings>(k: K): Settings[K] {
  const s = SPEC[k];
  if (typeof s.fallback === "boolean") return (getPref(s.key, s.fallback ? "on" : "off", ["on", "off"]) === "on") as Settings[K];
  return getPref(s.key, s.fallback as string, s.allowed as string[]) as Settings[K];
}

export function setSetting<K extends keyof Settings>(k: K, v: Settings[K]) {
  setPref(SPEC[k].key, typeof v === "boolean" ? (v ? "on" : "off") : String(v));
  for (const fn of listeners) fn();
}

/** Tells listeners that something they show changed (e.g. pen-only mode, kept by the editor). */
export function notifySettings() {
  for (const fn of listeners) fn();
}

export const PEN_ONLY_KEY = "inkleaf-pen-only";

/** Calls `fn` whenever a setting changes; returns a function that stops it. */
export function onSettingsChange(fn: () => void): () => void {
  listeners.add(fn);
  return () => listeners.delete(fn);
}

/** Stylus pressure through the chosen curve (Light makes light touches bolder). */
export function pressureCurve(p: number, curve: Settings["pressure"] = getSetting("pressure")): number {
  const gamma = curve === "light" ? 0.6 : curve === "firm" ? 1.6 : 1;
  return Math.max(0, Math.min(1, p)) ** gamma;
}
