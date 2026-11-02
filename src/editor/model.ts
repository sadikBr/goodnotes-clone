import type { RecognizedShape, StoredElement } from "../api";
import type { FontId } from "../fonts";

/** x, y and normalised pen pressure (0..1). */
export type Pt = [number, number, number];

interface Base {
  id: string;
  z: number;
}

export type PenStyle = "fountain" | "ballpoint" | "pencil" | "calligraphy" | "brush";

export const PEN_STYLES: { id: PenStyle; label: string }[] = [
  { id: "fountain", label: "Fountain pen" },
  { id: "ballpoint", label: "Ballpoint" },
  { id: "pencil", label: "Pencil" },
  { id: "calligraphy", label: "Calligraphy" },
  { id: "brush", label: "Brush pen" },
];

export interface StrokeEl extends Base {
  kind: "stroke";
  tool: "pen" | "highlighter";
  /** Pen nib; strokes saved before pen styles existed default to fountain. */
  style?: PenStyle;
  color: string;
  width: number;
  points: Pt[];
}

export interface ShapeEl extends Base {
  kind: "shape";
  shape: RecognizedShape;
  color: string;
  width: number;
  fill: string | null;
}

export interface TextEl extends Base {
  kind: "text";
  x: number;
  y: number;
  w: number;
  text: string;
  fontSize: number;
  color: string;
  /** Font preset; missing means the clean sans-serif default. */
  font?: FontId;
  bold?: boolean;
  italic?: boolean;
  align?: TextAlign;
  /** Rotation in radians around the box centre. */
  rotation?: number;
}

export type TextAlign = "left" | "center" | "right";

export interface StickyEl extends Base {
  kind: "sticky";
  x: number;
  y: number;
  w: number;
  h: number;
  text: string;
  color: string;
  font?: FontId;
  /** Text size; missing means 18. */
  fontSize?: number;
  bold?: boolean;
  italic?: boolean;
  align?: TextAlign;
  rotation?: number;
}

export interface ImageEl extends Base {
  kind: "image";
  x: number;
  y: number;
  w: number;
  h: number;
  src: string;
  rotation?: number;
}

export type El = StrokeEl | ShapeEl | TextEl | StickyEl | ImageEl;

export const KINDS = ["stroke", "shape", "text", "sticky", "image"] as const;

export function toStored(el: El): StoredElement {
  return { id: el.id, kind: el.kind, z: el.z, data: el };
}

/** Rebuilds editor elements from storage, skipping unknown or corrupt rows. */
export function fromStored(rows: StoredElement[]): El[] {
  const out: El[] = [];
  for (const row of rows) {
    const data = row.data as Partial<El> | null;
    if (!data || typeof data !== "object") continue;
    if (!(KINDS as readonly string[]).includes(row.kind)) continue;
    out.push({ ...data, id: row.id, z: row.z, kind: row.kind } as El);
  }
  return out.sort((a, b) => a.z - b.z);
}

export function newId(): string {
  return crypto.randomUUID();
}

export function nextZ(elements: El[]): number {
  return elements.reduce((m, e) => Math.max(m, e.z), -1) + 1;
}
