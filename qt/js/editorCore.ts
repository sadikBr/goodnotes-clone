// The editor's brain for the native app: the web editor's gestures and
// editing (src/editor/editor.ts), in page coordinates, over the same element
// model and helpers. The native canvas draws (ink meshes, element pictures,
// this module's overlay) and sends pointer events; QML shows the tool and
// selection bars and the text editor.
/* eslint-disable @typescript-eslint/no-explicit-any */
import type { P2, RecognizedShape, StoredElement, Template } from "../../src/api";
import { fromStored, newId, nextZ, toStored, type El, type PenStyle, type Pt, type ShapeEl, type StickyEl, type StrokeEl, type TextEl, type TextAlign } from "../../src/editor/model";
import {
  bounds,
  buildShape,
  convertShape,
  eraseElement,
  geomBox,
  groupHandleAt,
  groupTransform,
  handleAt,
  hitForSelect,
  hitTest,
  insideLasso,
  isClosedShape,
  isHeld,
  isResizable,
  rectContains,
  resizeElement,
  transformElement,
  transformShape,
  translate,
  unionRects,
  applyAffine,
  type Affine,
  type GroupHandle,
  type Handle,
  type Rect,
  type ShapeKind,
} from "../../src/editor/geometry";
import { beautify, recognizeShape } from "../../src/editor/beautify";
import { matchSymbol, symbolShape } from "../../src/editor/symbols";
import { drawElement, drawGroupHandles, drawGuides, drawHandles, drawLasso, drawSelection, drawStroke, drawTransformedBox, naturalTextWidth, paperLayout, renderPage, steadyHighlight, textAnchors, type ImageCache } from "../../src/editor/render";
import { thinPoints, smoothPoints } from "../../src/editor/ink";
import { anchorsAsTargets, paperTargets, snapMove, type Anchor, type Guide, type Target } from "../../src/editor/snap";
import { continueList, currentList, toggleList, type ListKind } from "../../src/editor/textFormat";
import { isCoverTemplate } from "../../src/editor/covers";
import { isRtl, type FontId } from "../../src/fonts";

const N = (globalThis as any).__native;
const HOLD_MS = 550;
const MAX_UNDO = 100;

function call<T>(cmd: string, args: object): T {
  const r = JSON.parse(N.call(cmd, JSON.stringify(args)));
  if (r.error) throw new Error(r.error);
  return r.ok as T;
}

export interface PageMeta {
  id: string;
  width: number;
  height: number;
  template: Template;
  paperColor: string;
  thumbnail?: string | null;
  backgroundAsset?: string | null;
  backgroundPage?: number | null;
}

interface PageState {
  meta: PageMeta;
  elements: El[] | null;
  dirty: boolean;
}

type Tool = "pen" | "highlighter" | "eraser" | "lasso" | "shape" | "text" | "sticky" | "laser" | "hand";

interface Held {
  mode: "transform" | "move";
  base: ShapeEl;
  anchor: P2;
  pivot: P2;
  still: P2;
  stillTimer: number;
}

type Gesture =
  | { type: "draw"; tool: "pen" | "highlighter" | "shape"; points: Pt[]; times: number[]; preview: ShapeEl | null; holdTimer: number; held: Held | null; absorb?: string[]; ruler?: { line: [P2, P2] } }
  | { type: "erase"; changed: boolean; last: P2 | null }
  | { type: "lasso"; points: P2[] }
  | { type: "move"; start: P2; original: El[]; dx: number; dy: number; snap: { anchors: Anchor[]; targets: Target[] } | null; guides: Guide[] }
  | { type: "shape"; start: P2; preview: ShapeEl | null }
  | { type: "handle"; handle: Handle; original: El; preview: El }
  | { type: "transform"; handle: GroupHandle; start: P2; box: Rect; originals: El[]; previews: El[]; m: Affine; sx: number; sy: number; angle: number };

/** Bounds of elements, remembered per element object (elements are never changed in place). */
const boundsCache = new WeakMap<El, Rect>();
function boxOfEl(el: El): Rect {
  let r = boundsCache.get(el);
  if (!r) {
    r = bounds(el);
    boundsCache.set(el, r);
  }
  return r;
}
const near = (r: Rect, x: number, y: number, pad: number) => x >= r.x - pad && x <= r.x + r.w + pad && y >= r.y - pad && y <= r.y + r.h + pad;
const overlaps = (a: Rect, b: Rect) => a.x <= b.x + b.w && b.x <= a.x + a.w && a.y <= b.y + b.h && b.y <= a.y + a.h;

const rectOfPts = (pts: P2[]): Rect => {
  const xs = pts.map((p) => p[0]);
  const ys = pts.map((p) => p[1]);
  const x = Math.min(...xs);
  const y = Math.min(...ys);
  return { x, y, w: Math.max(...xs) - x, h: Math.max(...ys) - y };
};

/** The editor for one notebook. */
export class EditorCore {
  pages: PageState[] = [];
  active = 0;
  zoom = 1;

  // Tool state (set by the app).
  tool: Tool = "pen";
  colors: Record<string, string> = { pen: "#111827", highlighter: "#facc15", shape: "#111827", text: "#111827", sticky: "#fef08a", laser: "#ff1f1f" };
  widths: Record<string, number> = { pen: 3, highlighter: 18, eraser: 14, shape: 3, text: 20, sticky: 18 };
  penStyle: PenStyle = "fountain";
  streamline = 0.38;
  shapeKind: ShapeKind = "auto";
  shapeFill = false;
  eraserMode: "precise" | "stroke" = "precise";
  textFont: FontId = "sans";
  textFormat: { bold: boolean; italic: boolean; align: TextAlign } = { bold: false, italic: false, align: "left" };

  selection = new Set<string>();
  hiddenIds = new Set<string>();
  gesture: Gesture | null = null;
  cursor: P2 | null = null;
  shift = false;
  // History: the page's element list before each change. Elements are never
  // changed in place (a change makes a new object), so the lists share them.
  undoStack: { page: number; els: El[] }[] = [];
  redoStack: { page: number; els: El[] }[] = [];
  clipboard: El[] = [];
  /** The text box or sticky note being typed into (hidden on the page meanwhile). */
  editing: TextEl | StickyEl | null = null;
  private editingNew = false;
  private editingBefore: El[] = [];
  private recentInk: { id: string; t: number; pts: P2[] }[] = [];
  /** Bumped whenever something the app shows changes. */
  rev = 0;
  overlayRev = 0;
  selectionRev = 0;
  images: ImageCache = new Map();
  /** Ruler edge for guided strokes, set by the app ([a, b] in page units of the active page). */
  rulerEdge: [P2, P2] | null = null;

  constructor(pages: PageMeta[]) {
    this.pages = pages.map((meta) => ({ meta, elements: null, dirty: false }));
  }

  // ------------------------------------------------------------ pages

  get page(): PageMeta {
    return this.pages[this.active].meta;
  }

  get elements(): El[] {
    return this.load(this.active);
  }

  set elements(els: El[]) {
    this.pages[this.active].elements = els;
  }

  load(i: number): El[] {
    const p = this.pages[i];
    if (!p) return [];
    if (!p.elements) p.elements = fromStored(call<StoredElement[]>("elements", { pageId: p.meta.id }));
    return p.elements;
  }

  /** The page the pen works on (its selection is dropped when it changes). */
  activate(i: number) {
    if (i === this.active || i < 0 || i >= this.pages.length) return;
    this.finishTextEdit();
    this.selection.clear();
    this.active = i;
    this.load(i);
    this.touchOverlay();
    this.selectionRev++;
  }

  /** Drops what's loaded for every page (after changes made outside the editor). */
  forgetAll() {
    this.finishTextEdit();
    this.selection.clear();
    for (const p of this.pages) if (!p.dirty) p.elements = null;
    this.undoStack = [];
    this.redoStack = [];
    this.rev++;
    this.selectionRev++;
  }

  setPages(pages: PageMeta[]) {
    const old = new Map(this.pages.map((p) => [p.meta.id, p]));
    const activeId = this.page?.id;
    this.pages = pages.map((meta) => {
      const was = old.get(meta.id);
      return was ? { ...was, meta } : { meta, elements: null, dirty: false };
    });
    const idx = this.pages.findIndex((p) => p.meta.id === activeId);
    this.active = idx >= 0 ? idx : Math.min(this.active, this.pages.length - 1);
    this.rev++;
  }

  /** Elements of a page in z order. */
  scene(i: number): El[] {
    return this.load(i);
  }

  private revs = new WeakMap<El, number>();
  private nextRev = 1;
  /**
   * What the canvas draws on page i, cheaply: a line "id rev[ h]" per
   * element in z order (rev changes when the element does; h: hidden).
   */
  sceneKeys(i: number): string {
    const els = this.load(i);
    const hidden = i === this.active ? this.hiddenIds : null;
    let out = "";
    for (let k = 0; k < els.length; k++) {
      const el = els[k];
      let r = this.revs.get(el);
      if (!r) {
        r = this.nextRev++;
        this.revs.set(el, r);
      }
      out += el.id + " " + r + (hidden && hidden.has(el.id) ? " h\n" : "\n");
    }
    return out;
  }

  /**
   * Elements by id for the canvas: JSON (strokes without their points, others
   * whole, with their bounds) and every stroke's points packed in order
   * (x, y, pressure as floats).
   */
  packElements(i: number, ids: string[]): { meta: string; pts: ArrayBuffer } {
    const byId = new Map<string, El>();
    for (const el of this.load(i)) byId.set(el.id, el);
    let n = 0;
    const els: El[] = [];
    for (const id of ids) {
      const el = byId.get(id);
      if (!el) continue;
      els.push(el);
      if (el.kind === "stroke") n += el.points.length * 3;
    }
    const buf = new Float32Array(n);
    let o = 0;
    const meta: object[] = [];
    for (const el of els) {
      if (el.kind === "stroke") {
        const pts = el.points;
        for (let k = 0; k < pts.length; k++) {
          const p = pts[k];
          buf[o++] = p[0];
          buf[o++] = p[1];
          buf[o++] = p[2] === undefined ? 0.5 : p[2];
        }
        meta.push({ id: el.id, kind: "stroke", rev: this.revs.get(el) ?? 0, tool: el.tool, style: el.style, color: el.color, width: el.width, smooth: el.smooth, n: pts.length, b: boxOfEl(el) });
      } else {
        meta.push({ id: el.id, kind: el.kind, rev: this.revs.get(el) ?? 0, el, b: boxOfEl(el) });
      }
    }
    return { meta: JSON.stringify(meta), pts: buf.buffer };
  }

  isHidden(i: number, id: string): boolean {
    return i === this.active && this.hiddenIds.has(id);
  }

  // ------------------------------------------------------------ history and saving

  private snapshot() {
    this.undoStack.push({ page: this.active, els: this.elements.slice() });
    if (this.undoStack.length > MAX_UNDO) this.undoStack.shift();
    this.redoStack = [];
  }

  private changed() {
    this.pages[this.active].dirty = true;
    this.rev++;
    this.touchOverlay();
    this.selectionRev++;
  }

  private touchOverlay() {
    this.overlayRev++;
  }

  undo() {
    this.finishTextEdit();
    const prev = this.undoStack.pop();
    if (!prev) return;
    this.activate(prev.page);
    this.redoStack.push({ page: this.active, els: this.elements.slice() });
    this.elements = prev.els.slice();
    this.selection.clear();
    this.changed();
  }

  redo() {
    this.finishTextEdit();
    const next = this.redoStack.pop();
    if (!next) return;
    this.activate(next.page);
    this.undoStack.push({ page: this.active, els: this.elements.slice() });
    this.elements = next.els.slice();
    this.selection.clear();
    this.changed();
  }

  canUndo() {
    return this.undoStack.length > 0;
  }

  canRedo() {
    return this.redoStack.length > 0;
  }

  addElement(el: El) {
    this.snapshot();
    this.elements.push(el);
    this.changed();
  }

  /**
   * Saves every changed page; returns the indexes saved (the app makes their
   * library pictures). `thumbnails` draws them here instead (slower).
   */
  save(thumbnails = false): number[] {
    const saved: number[] = [];
    for (let i = 0; i < this.pages.length; i++) {
      const p = this.pages[i];
      if (!p.dirty || !p.elements) continue;
      p.dirty = false;
      try {
        call("saveElements", { pageId: p.meta.id, elements: p.elements.map(toStored) });
        if (thumbnails) {
          const scale = (isCoverTemplate(p.meta.template) ? 420 : 200) / p.meta.width;
          const thumb = (renderPage(p.meta.template, p.meta.paperColor, p.meta.width, p.meta.height, p.elements, scale, this.images, null) as any).toDataURL("image/jpeg", 0.82);
          call("setPageThumbnail", { pageId: p.meta.id, thumbnail: thumb });
          p.meta.thumbnail = thumb;
        }
        saved.push(i);
      } catch (e) {
        p.dirty = true;
        N.log(`save failed: ${e}`);
      }
    }
    return saved;
  }

  dirty(): boolean {
    return this.pages.some((p) => p.dirty);
  }

  /** Pages with unsaved changes (their flags are cleared: the app saves them). */
  takeDirty(): number[] {
    const out: number[] = [];
    this.pages.forEach((p, i) => {
      if (p.dirty && p.elements) {
        p.dirty = false;
        out.push(i);
      }
    });
    return out;
  }

  markDirty(i: number) {
    if (this.pages[i]) this.pages[i].dirty = true;
  }

  /**
   * Page i's elements for saving, quickly: JSON of every element (strokes
   * without their points, with "__n" = how many) and the points packed in
   * order. The app writes the stored form from these.
   */
  packForSave(i: number): { meta: string; pts: ArrayBuffer } {
    const els = this.load(i);
    let n = 0;
    for (const el of els) if (el.kind === "stroke") n += el.points.length * 3;
    const buf = new Float32Array(n);
    let o = 0;
    const meta: object[] = [];
    for (const el of els) {
      if (el.kind !== "stroke") {
        meta.push(el);
        continue;
      }
      const copy: any = {};
      for (const k in el) if (k !== "points") copy[k] = (el as any)[k];
      copy.__n = el.points.length;
      meta.push(copy);
      for (let k = 0; k < el.points.length; k++) {
        const p = el.points[k];
        buf[o++] = p[0];
        buf[o++] = p[1];
        buf[o++] = p[2] === undefined ? 0.5 : p[2];
      }
    }
    return { meta: JSON.stringify(meta), pts: buf.buffer };
  }

  // ------------------------------------------------------------ pointer

  /**
   * A press on page `pi` at (x, y) page units. `tool` is what the press does
   * (the pen's eraser end can stand in for the current tool). Returns what the
   * app should do: "draw" (native live ink), "edit" (open the text editor) or "".
   */
  down(pi: number, x: number, y: number, p: number, t: number, tool: Tool, shift: boolean): string {
    this.shift = shift;
    if (this.gesture) return "";
    if (pi !== this.active) this.activate(pi);
    this.finishTextEdit();
    this.cursor = [x, y];
    switch (tool) {
      case "pen":
      case "highlighter":
      case "shape": {
        if (this.rulerEdge) {
          const proj = projectOn(this.rulerEdge, [x, y]);
          if (proj && proj.dist < 26 / this.zoom) {
            const off = this.widths[tool] / 2 + 0.5 / this.zoom;
            const q = offsetFrom(this.rulerEdge, proj.p, [x, y], off);
            this.gesture = { type: "draw", tool, points: [[q[0], q[1], p]], times: [t], preview: null, holdTimer: 0, held: null, ruler: { line: this.rulerEdge } };
            return "ruler";
          }
        }
        if (tool === "shape" && this.shapeKind !== "auto") {
          this.gesture = { type: "shape", start: [x, y], preview: null };
          return "";
        }
        this.gesture = { type: "draw", tool, points: [[x, y, p]], times: [t], preview: null, holdTimer: 0, held: null };
        this.armHold();
        return "draw";
      }
      case "eraser":
        this.gesture = { type: "erase", changed: false, last: null };
        this.eraseAlong([x, y]);
        return "";
      case "lasso": {
        const single = this.singleSegment();
        const handle = single ? handleAt(single, x, y, 10 / this.zoom) : null;
        if (single && handle) {
          this.gesture = { type: "handle", handle, original: single, preview: single };
          this.hiddenIds.add(single.id);
          this.rev++;
          break;
        }
        const content = this.selectionBounds();
        const onContent = !!content && rectContains(content, x, y);
        const tbox = single || onContent ? null : this.transformBox();
        const gh = tbox ? groupHandleAt(tbox, x, y, this.zoom) : null;
        if (tbox && gh) {
          const originals = this.elements.filter((el) => this.selection.has(el.id));
          this.gesture = { type: "transform", handle: gh, start: [x, y], box: tbox, originals, previews: originals, m: { a: 1, b: 0, c: 0, d: 1, e: 0, f: 0 }, sx: 1, sy: 1, angle: 0 };
          break;
        }
        const sel = this.selectionBounds();
        if (sel && rectContains({ x: sel.x - 8, y: sel.y - 8, w: sel.w + 16, h: sel.h + 16 }, x, y)) {
          const original = this.elements.filter((el) => this.selection.has(el.id));
          this.gesture = { type: "move", start: [x, y], original, dx: 0, dy: 0, snap: this.buildSnap(original), guides: [] };
        } else {
          if (this.selection.size) this.selectionRev++;
          this.selection.clear();
          this.gesture = { type: "lasso", points: [[x, y]] };
        }
        break;
      }
      case "text":
      case "sticky": {
        const existing = [...this.elements].reverse().find((el) => el.kind === tool && rectContains(bounds(el), x, y));
        if (existing && (existing.kind === "text" || existing.kind === "sticky")) this.startTextEdit(existing, false);
        else if (tool === "text") {
          const el: TextEl = { id: newId(), z: nextZ(this.elements), kind: "text", x, y: y - 12, w: Math.min(420, this.page.width - x - 20), text: "", fontSize: this.widths.text, color: this.colors.text, font: this.textFont, ...this.textFormat };
          el.w = Math.max(120, el.w);
          this.startTextEdit(el, true);
        } else {
          const el: StickyEl = { id: newId(), z: nextZ(this.elements), kind: "sticky", x: x - 20, y: y - 20, w: 220, h: 200, text: "", color: this.colors.sticky, font: this.textFont, fontSize: this.widths.sticky, ...this.textFormat };
          this.startTextEdit(el, true);
        }
        return "edit";
      }
    }
    this.touchOverlay();
    return "";
  }

  /** A pen point accepted by the native lazy pen (draw gestures), or a move. */
  move(x: number, y: number, p: number, t: number, shift: boolean, alt: boolean) {
    this.shift = shift;
    const g = this.gesture;
    this.cursor = [x, y];
    if (!g) {
      if (this.tool === "lasso") this.touchOverlay();
      return;
    }
    switch (g.type) {
      case "draw": {
        if (g.preview) {
          if (g.held) this.updateHeld(g, [x, y]);
          this.touchOverlay();
          return;
        }
        if (g.ruler) {
          const proj = projectOn(g.ruler.line, [x, y]);
          if (proj) {
            const q = offsetFrom(g.ruler.line, proj.p, [g.points[0][0], g.points[0][1]], this.widths[g.tool] / 2 + 0.5 / this.zoom);
            g.points.push([q[0], q[1], p]);
            g.times.push(t);
          }
          this.touchOverlay();
          return;
        }
        g.points.push([x, y, p]);
        g.times.push(t);
        this.armHold();
        return;
      }
      case "erase":
        this.eraseAlong([x, y]);
        return;
      case "lasso":
        g.points.push([x, y]);
        break;
      case "move":
        g.dx = x - g.start[0];
        g.dy = y - g.start[1];
        g.guides = [];
        if (g.snap && !alt) {
          const s = snapMove(g.snap.anchors, g.snap.targets, g.dx, g.dy, 7 / this.zoom);
          g.dx = s.dx;
          g.dy = s.dy;
          g.guides = s.guides;
        }
        break;
      case "handle":
        g.preview = resizeElement(g.original, g.handle, [x, y], shift);
        break;
      case "transform": {
        const tr = groupTransform(g.box, g.handle, g.start, [x, y], shift);
        g.m = tr.m;
        g.sx = tr.sx;
        g.sy = tr.sy;
        g.angle = tr.angle;
        break;
      }
      case "shape": {
        const shape = buildShape(this.shapeKind, g.start, [x, y], shift);
        g.preview = shape ? this.makeShapeEl(shape) : null;
        break;
      }
    }
    this.touchOverlay();
  }

  hover(x: number, y: number) {
    this.cursor = [x, y];
    if (this.tool === "lasso" && this.selection.size) this.touchOverlay();
  }

  /** The cursor for the pointer at (x, y) with the lasso (handles, moving). */
  cursorAt(x: number, y: number): string {
    if (this.tool !== "lasso" || this.gesture) return "";
    const single = this.singleSegment();
    if (single && handleAt(single, x, y, 10 / this.zoom)) return "crosshair";
    const content = this.selectionBounds();
    const tbox = single || (content && rectContains(content, x, y)) ? null : this.transformBox();
    const gh = tbox ? groupHandleAt(tbox, x, y, this.zoom) : null;
    if (gh === "rot") return "rotate";
    if (gh === "n" || gh === "s") return "ns";
    if (gh === "e" || gh === "w") return "ew";
    if (gh === "nw" || gh === "se") return "nwse";
    if (gh === "ne" || gh === "sw") return "nesw";
    const sel = this.selectionBounds();
    if (sel && rectContains({ x: sel.x - 8, y: sel.y - 8, w: sel.w + 16, h: sel.h + 16 }, x, y)) return "move";
    return "";
  }

  up() {
    const g = this.gesture;
    if (!g) return;
    switch (g.type) {
      case "draw": {
        clearTimeout(g.holdTimer);
        if (g.held) clearTimeout(g.held.stillTimer);
        let shape = g.preview;
        if (g.ruler && g.tool === "shape") {
          const ext = extent(g.points, g.ruler.line);
          shape = { id: newId(), z: 0, kind: "shape", shape: { kind: "line", points: ext }, color: this.colors.shape, width: this.widths.shape, fill: null };
        } else if (!shape && g.tool === "shape") shape = this.refine(g);
        this.gesture = null;
        if (shape && g.tool === "highlighter" && shape.shape.kind === "line") {
          this.addElement({ ...this.straightHighlight(g, shape.shape.points), id: newId(), z: nextZ(this.elements) });
        } else if (shape) {
          shape.z = nextZ(this.elements);
          if (g.absorb?.length) {
            const gone = new Set(g.absorb);
            for (const id of gone) this.hiddenIds.delete(id);
            this.recentInk = this.recentInk.filter((r) => !gone.has(r.id));
            this.snapshot();
            this.elements = this.elements.filter((e) => !gone.has(e.id));
            this.elements.push(shape);
            this.changed();
          } else this.addElement(shape);
        } else if (g.points.length) {
          const tool = g.tool === "highlighter" ? "highlighter" : "pen";
          const points = thinPoints(tool === "highlighter" ? steadyHighlight(g.points, this.widths.highlighter) : g.points, Math.min(0.3, 0.6 / this.zoom));
          const id = newId();
          this.addElement({
            id,
            z: nextZ(this.elements),
            kind: "stroke",
            tool,
            style: tool === "pen" ? this.penStyle : undefined,
            color: this.colors[g.tool],
            width: this.widths[g.tool],
            points,
            ...(tool === "pen" ? { smooth: this.streamline } : {}),
          } as StrokeEl);
          if (tool === "pen" && g.tool === "pen") {
            this.recentInk.push({ id, t: g.times[g.times.length - 1], pts: g.points.map((p) => [p[0], p[1]] as P2) });
            if (this.recentInk.length > 6) this.recentInk.shift();
          }
        }
        break;
      }
      case "erase":
        this.gesture = null;
        if (g.changed) this.changed();
        break;
      case "lasso": {
        this.gesture = null;
        const xs = g.points.map((p) => p[0]);
        const ys = g.points.map((p) => p[1]);
        const tap = Math.max(...xs) - Math.min(...xs) < 6 / this.zoom && Math.max(...ys) - Math.min(...ys) < 6 / this.zoom;
        if (tap) {
          const [tx, ty] = g.points[0];
          const hit = [...this.elements].reverse().find((el) => near(boxOfEl(el), tx, ty, 8 / this.zoom) && hitForSelect(el, tx, ty, 6 / this.zoom));
          if (hit) this.selection.add(hit.id);
        } else {
          // Only what overlaps the lasso can be inside it.
          const box = rectOfPts(g.points);
          for (const el of this.elements) if (overlaps(boxOfEl(el), box) && insideLasso(el, g.points)) this.selection.add(el.id);
        }
        this.selectionRev++;
        break;
      }
      case "transform": {
        this.gesture = null;
        if (g.m.a !== 1 || g.m.b !== 0 || g.m.c !== 0 || g.m.d !== 1 || g.m.e !== 0 || g.m.f !== 0) {
          const next = new Map(g.originals.map((el) => [el.id, transformElement(el, g.m, g.sx, g.sy, g.angle)]));
          this.snapshot();
          this.elements = this.elements.map((el) => next.get(el.id) ?? el);
          this.changed();
        }
        break;
      }
      case "handle": {
        this.gesture = null;
        this.hiddenIds.delete(g.original.id);
        if (g.preview !== g.original) {
          this.snapshot();
          this.elements = this.elements.map((el) => (el.id === g.original.id ? g.preview : el));
          this.changed();
        } else this.rev++;
        break;
      }
      case "move": {
        this.gesture = null;
        if (Math.abs(g.dx) > 0.5 || Math.abs(g.dy) > 0.5) {
          this.snapshot();
          this.elements = this.elements.map((el) => (this.selection.has(el.id) ? translate(el, g.dx, g.dy) : el));
          this.changed();
        }
        break;
      }
      case "shape":
        this.gesture = null;
        if (g.preview) {
          g.preview.z = nextZ(this.elements);
          this.addElement(g.preview);
        }
        break;
    }
    this.touchOverlay();
    this.selectionRev++;
  }

  cancel() {
    const g = this.gesture;
    if (!g) return;
    if (g.type === "draw") {
      clearTimeout(g.holdTimer);
      if (g.held) clearTimeout(g.held.stillTimer);
      for (const id of g.absorb ?? []) this.hiddenIds.delete(id);
    }
    if (g.type === "handle") this.hiddenIds.delete(g.original.id);
    if (g.type === "erase" && g.changed) this.changed();
    this.gesture = null;
    this.rev++;
    this.touchOverlay();
  }

  /** What the native canvas should do with the live stroke: hide it while a refined shape shows. */
  liveHidden(): boolean {
    const g = this.gesture;
    return g?.type === "draw" && (!!g.preview || !!g.ruler);
  }

  /** Selected elements being dragged or transformed: an affine map the canvas applies to their pictures. */
  liveTransform(): { ids: string[]; m: Affine } | null {
    const g = this.gesture;
    if (g?.type === "move") return { ids: g.original.map((e) => e.id), m: { a: 1, b: 0, c: 0, d: 1, e: g.dx, f: g.dy } };
    if (g?.type === "transform") return { ids: g.originals.map((e) => e.id), m: g.m };
    return null;
  }

  // ------------------------------------------------------------ holding a stroke still

  private armHold() {
    const g = this.gesture;
    if (g?.type !== "draw") return;
    clearTimeout(g.holdTimer);
    g.holdTimer = setTimeout(() => {
      if (this.gesture !== g || g.preview || g.points.length < 4) return;
      const now = performance.now();
      const times = [...g.times.slice(0, -1), Math.max(now, g.times[g.times.length - 1])];
      if (!isHeld(times, g.points, HOLD_MS - 50, 4 / this.zoom)) return;
      const multi = this.multiStrokeSymbol(g);
      const shape = multi?.shape ?? this.refine(g);
      if (!shape) return;
      g.preview = shape;
      g.absorb = multi?.absorb;
      for (const id of g.absorb ?? []) this.hiddenIds.add(id);
      if (g.absorb?.length) this.rev++;
      const last = g.points[g.points.length - 1];
      const anchor: P2 = [last[0], last[1]];
      g.held = { mode: "transform", base: shape, anchor, pivot: this.pivotFor(shape), still: anchor, stillTimer: 0 };
      this.armModeToggle(g);
      this.touchOverlay();
    }, HOLD_MS) as unknown as number;
  }

  private refine(g: Extract<Gesture, { type: "draw" }>): ShapeEl | null {
    const pts = g.points.map((p) => [p[0], p[1]] as P2);
    let shape: RecognizedShape | null;
    if (g.tool === "highlighter") {
      const s = recognizeShape(pts);
      const len = s?.kind === "line" ? Math.hypot(s.points[1][0] - s.points[0][0], s.points[1][1] - s.points[0][1]) : 0;
      shape = s?.kind === "line" && len > Math.max(24 / this.zoom, this.widths.highlighter * 1.5) ? s : null;
    } else {
      const sym = pts.length > 3 ? matchSymbol([pts]) : null;
      shape = sym ? symbolShape(sym.name, rectOfPts(pts)) : beautify(pts);
    }
    if (!shape) return null;
    return { id: newId(), z: nextZ(this.elements), kind: "shape", shape, color: this.colors[g.tool], width: this.widths[g.tool], fill: null };
  }

  private multiStrokeSymbol(g: Extract<Gesture, { type: "draw" }>): { shape: ShapeEl; absorb: string[] } | null {
    if (g.tool !== "pen") return null;
    const pts = g.points.map((p) => [p[0], p[1]] as P2);
    const cur = rectOfPts(pts);
    const size = Math.max(cur.w, cur.h, 12 / this.zoom);
    const group: { id: string; pts: P2[] }[] = [];
    let t = g.times[0];
    let box = cur;
    for (let i = this.recentInk.length - 1; i >= 0 && group.length < 3; i--) {
      const r = this.recentInk[i];
      if (t - r.t > 2500 || !this.elements.some((e) => e.id === r.id)) break;
      const b = rectOfPts(r.pts);
      const reach = size * 0.9 + 8 / this.zoom;
      const near = b.x < box.x + box.w + reach && b.x + b.w > box.x - reach && b.y < box.y + box.h + reach && b.y + b.h > box.y - reach;
      if (!near) break;
      group.unshift({ id: r.id, pts: r.pts });
      box = unionRects([box, b]) ?? box;
      t = r.t;
    }
    for (let n = group.length; n >= 1; n--) {
      const used = group.slice(group.length - n);
      const strokes = [...used.map((u) => u.pts), pts];
      const m = matchSymbol(strokes);
      if (!m) continue;
      const shape = symbolShape(m.name, rectOfPts(strokes.flat()));
      return { shape: { id: newId(), z: nextZ(this.elements), kind: "shape", shape, color: this.colors.pen, width: this.widths.pen, fill: null }, absorb: used.map((u) => u.id) };
    }
    return null;
  }

  private straightHighlight(g: Extract<Gesture, { type: "draw" }>, line: P2[]): StrokeEl {
    const a = line[0];
    const b = line[line.length - 1];
    const p = g.points[0][2];
    return { id: "live", z: 0, kind: "stroke", tool: "highlighter", color: this.colors.highlighter, width: this.widths.highlighter, points: [[a[0], a[1], p], [b[0], b[1], p]] };
  }

  private pivotFor(el: ShapeEl): P2 {
    const s = el.shape;
    if ((s.kind === "line" || s.kind === "arrow") && s.points.length) return s.points[0];
    const r = geomBox(el);
    return [r.x + r.w / 2, r.y + r.h / 2];
  }

  private armModeToggle(g: Extract<Gesture, { type: "draw" }>) {
    const held = g.held;
    if (!held) return;
    clearTimeout(held.stillTimer);
    if (g.tool === "highlighter") return;
    held.stillTimer = setTimeout(() => {
      if (this.gesture !== g || !g.held || !g.preview) return;
      held.mode = held.mode === "transform" ? "move" : "transform";
      held.base = g.preview;
      held.anchor = held.still;
      held.pivot = this.pivotFor(g.preview);
      this.touchOverlay();
    }, HOLD_MS + 150) as unknown as number;
  }

  private updateHeld(g: Extract<Gesture, { type: "draw" }>, p: P2) {
    const h = g.held!;
    if (Math.hypot(p[0] - h.still[0], p[1] - h.still[1]) > 4 / this.zoom) {
      h.still = p;
      this.armModeToggle(g);
    }
    let shape: RecognizedShape;
    if (h.mode === "move") shape = transformShape(h.base.shape, h.pivot, 0, 1, p[0] - h.anchor[0], p[1] - h.anchor[1]);
    else {
      const v0: P2 = [h.anchor[0] - h.pivot[0], h.anchor[1] - h.pivot[1]];
      const v1: P2 = [p[0] - h.pivot[0], p[1] - h.pivot[1]];
      const l0 = Math.hypot(v0[0], v0[1]);
      if (l0 < 3 / this.zoom) shape = h.base.shape;
      else {
        const scale = Math.max(0.05, Math.min(20, Math.hypot(v1[0], v1[1]) / l0));
        const angle = Math.atan2(v1[1], v1[0]) - Math.atan2(v0[1], v0[0]);
        shape = transformShape(h.base.shape, h.pivot, angle, scale);
      }
    }
    g.preview = { ...h.base, shape };
  }

  private buildSnap(moving: El[]): { anchors: Anchor[]; targets: Target[] } | null {
    const texts = moving.filter((el): el is TextEl => el.kind === "text" && el.text.trim().length > 0 && !el.rotation);
    if (!texts.length) return null;
    const ids = new Set(moving.map((el) => el.id));
    const targets: Target[] = [];
    for (const el of this.elements) if (!ids.has(el.id) && el.kind === "text" && el.text.trim() && !el.rotation) targets.push(...anchorsAsTargets(textAnchors(el)));
    const paper = paperLayout(this.page.template, this.page.width, this.page.height);
    targets.push(...paperTargets(paper.rules, paper.ruleSpan, paper.margin, this.page.width, this.page.height));
    return { anchors: texts.flatMap(textAnchors), targets };
  }

  private makeShapeEl(shape: RecognizedShape): ShapeEl {
    const color = this.colors.shape;
    const closed = isClosedShape(shape);
    return { id: newId(), z: 0, kind: "shape", shape, color, width: this.widths.shape, fill: this.shapeFill && closed ? `${color}33` : null };
  }

  // ------------------------------------------------------------ erasing

  private eraseAlong(p: P2) {
    const g = this.gesture;
    if (g?.type !== "erase") return;
    const from = g.last ?? p;
    const step = Math.max(1, this.widths.eraser / 4);
    const n = Math.max(1, Math.ceil(Math.hypot(p[0] - from[0], p[1] - from[1]) / step));
    for (let i = g.last ? 1 : 0; i <= n; i++) this.eraseAt(from[0] + ((p[0] - from[0]) * i) / n, from[1] + ((p[1] - from[1]) * i) / n);
    g.last = p;
  }

  private eraseAt(x: number, y: number) {
    const g = this.gesture;
    if (g?.type !== "erase") return;
    const radius = this.widths.eraser / 2;
    let changed = false;
    const next: El[] = [];
    for (const el of this.elements) {
      if (el.kind === "image" || !near(boxOfEl(el), x, y, radius + 2 + ((el as any).width ?? 0))) {
        next.push(el);
        continue;
      }
      if (this.eraserMode === "stroke" || el.kind === "text" || el.kind === "sticky") {
        if (hitTest(el, x, y, radius)) changed = true;
        else next.push(el);
        continue;
      }
      const pieces = eraseElement(el, x, y, radius);
      if (pieces === null) next.push(el);
      else {
        changed = true;
        next.push(...pieces);
      }
    }
    if (!changed) return;
    if (!g.changed) this.snapshot();
    g.changed = true;
    this.elements = next;
    this.pages[this.active].dirty = true;
    this.rev++;
  }

  // ------------------------------------------------------------ selection

  selectionBounds(): Rect | null {
    if (this.selection.size === 0) return null;
    return unionRects(this.elements.filter((el) => this.selection.has(el.id)).map(boxOfEl));
  }

  clearSelection() {
    if (!this.selection.size) return;
    this.selection.clear();
    this.touchOverlay();
    this.selectionRev++;
  }

  selectAll() {
    for (const el of this.elements) this.selection.add(el.id);
    this.touchOverlay();
    this.selectionRev++;
  }

  private singleShape(): El | null {
    if (this.selection.size !== 1) return null;
    const el = this.elements.find((e) => this.selection.has(e.id));
    return el && el.kind === "shape" ? el : null;
  }

  /** One selected line or arrow: it gets end handles instead of a box. */
  private singleSegment(): El | null {
    const el = this.singleShape();
    return el && el.kind === "shape" && (el.shape.kind === "line" || el.shape.kind === "arrow") && isResizable(el) ? el : null;
  }

  private transformBox(): Rect | null {
    if (!this.selection.size) return null;
    return this.selectionBounds();
  }

  /** What the selection bar shows. */
  selectionInfo(): any {
    const els = this.elements.filter((el) => this.selection.has(el.id));
    if (!els.length || this.gesture) return { count: els.length };
    const texts = els.filter((el): el is TextEl | StickyEl => el.kind === "text" || el.kind === "sticky");
    const single = this.singleShape() as ShapeEl | null;
    const r = this.selectionBounds()!;
    const colors = new Set(els.filter((el) => el.kind === "stroke" || el.kind === "shape" || el.kind === "text").map((el) => (el as any).color.toLowerCase()));
    const fonts = new Set(texts.map((t) => t.font ?? "sans"));
    return {
      count: els.length,
      page: this.active,
      box: r,
      color: colors.size === 1 ? [...colors][0] : null,
      hasInk: els.some((el) => el.kind === "stroke" && el.tool === "pen"),
      texts: texts.length,
      font: fonts.size === 1 ? [...fonts][0] : null,
      bold: texts.length > 0 && texts.every((t) => !!t.bold),
      italic: texts.length > 0 && texts.every((t) => !!t.italic),
      align: texts.every((t) => (t.align ?? "left") === "center") ? "center" : texts.every((t) => t.align === "right") ? "right" : "left",
      list: texts.length && texts.every((t) => currentList(t.text) === "bullet") ? "bullet" : texts.length && texts.every((t) => currentList(t.text) === "number") ? "number" : null,
      shape: single ? { kind: single.shape.kind, closed: isClosedShape(single.shape), width: single.width, fill: !!single.fill } : null,
    };
  }

  styleSelectedText(change: { font?: FontId; scale?: number; bold?: boolean; italic?: boolean; align?: TextAlign; list?: ListKind }) {
    if (!this.elements.some((el) => this.selection.has(el.id) && (el.kind === "text" || el.kind === "sticky"))) return;
    this.snapshot();
    this.elements = this.elements.map((el) => {
      if (!this.selection.has(el.id) || (el.kind !== "text" && el.kind !== "sticky")) return el;
      const next = { ...el };
      if (change.font) next.font = change.font;
      if (change.bold !== undefined) next.bold = change.bold;
      if (change.italic !== undefined) next.italic = change.italic;
      if (change.align) next.align = change.align;
      if (change.list) next.text = toggleList(el.text, change.list);
      if (change.scale) {
        const size = (el.kind === "text" ? el.fontSize : el.fontSize ?? 18) * change.scale;
        next.fontSize = Math.round(Math.min(96, Math.max(8, size)));
      }
      return next;
    });
    this.changed();
  }

  updateSelectedShape(change: "width" | "fill" | ShapeKind, value?: number) {
    const single = this.singleShape();
    if (!single) return;
    this.snapshot();
    this.elements = this.elements.map((el) => {
      if (el.id !== single.id || el.kind !== "shape") return el;
      if (change === "width") return { ...el, width: value ?? el.width };
      if (change === "fill") return { ...el, fill: el.fill ? null : `${el.color}33` };
      return convertShape(el, change);
    });
    this.changed();
  }

  deleteSelection() {
    if (!this.selection.size) return;
    this.snapshot();
    this.elements = this.elements.filter((el) => !this.selection.has(el.id));
    this.selection.clear();
    this.changed();
  }

  recolorSelection(color: string) {
    this.snapshot();
    this.elements = this.elements.map((el) => {
      if (!this.selection.has(el.id)) return el;
      if (el.kind === "shape") return { ...el, color, fill: el.fill ? `${color}33` : null };
      if (el.kind === "stroke" || el.kind === "text") return { ...el, color };
      return el;
    });
    this.changed();
  }

  copySelection(): number {
    this.clipboard = this.elements.filter((el) => this.selection.has(el.id)).map((el) => structuredClone(el));
    return this.clipboard.length;
  }

  cutSelection(): number {
    const n = this.copySelection();
    this.deleteSelection();
    return n;
  }

  pasteClipboard(): number {
    if (!this.clipboard.length) return 0;
    this.placeCopies(this.clipboard);
    return this.clipboard.length;
  }

  duplicateSelection() {
    const items = this.elements.filter((el) => this.selection.has(el.id));
    if (items.length) this.placeCopies(items);
  }

  private placeCopies(items: El[]) {
    this.snapshot();
    let z = nextZ(this.elements);
    const copies = items.map((el) => ({ ...translate(structuredClone(el), 24, 24), id: newId(), z: z++ }) as El);
    this.elements.push(...copies);
    this.selection = new Set(copies.map((c) => c.id));
    this.changed();
  }

  /** A picture placed in the middle of the view (cx, cy on the active page). */
  insertImage(src: string, naturalW: number, naturalH: number, cx: number, cy: number) {
    const maxW = this.page.width * 0.6;
    const scale = Math.min(1, maxW / naturalW);
    const w = naturalW * scale;
    const hh = naturalH * scale;
    const el: El = { id: newId(), z: nextZ(this.elements), kind: "image", x: Math.max(0, cx - w / 2), y: Math.max(0, cy - hh / 2), w, h: hh, src };
    this.addElement(el);
    this.selection = new Set([el.id]);
    this.selectionRev++;
  }

  /** Text elements to add (e.g. handwriting turned into text), replacing `remove`. */
  replaceElements(remove: string[], add: El[]) {
    this.snapshot();
    const gone = new Set(remove);
    let z = nextZ(this.elements);
    this.elements = this.elements.filter((e) => !gone.has(e.id)).concat(add.map((e) => ({ ...e, z: z++ })));
    this.selection = new Set(add.map((e) => e.id));
    this.changed();
  }

  /** Selected pen strokes, for "Apply font" (handwriting recognition). */
  selectedInk(): StrokeEl[] {
    return this.elements.filter((el): el is StrokeEl => this.selection.has(el.id) && el.kind === "stroke" && el.tool === "pen");
  }

  // ------------------------------------------------------------ text editing

  private startTextEdit(el: TextEl | StickyEl, isNew: boolean) {
    this.finishTextEdit();
    this.editingBefore = this.elements.slice();
    this.snapshot();
    if (isNew) this.elements.push(el);
    else {
      // Typing changes a copy, so the history keeps the text as it was.
      const live = { ...el };
      this.elements = this.elements.map((x) => (x === el ? live : x));
      el = live;
    }
    this.editing = el;
    this.editingNew = isNew;
    this.hiddenIds.add(el.id);
    this.rev++;
    this.touchOverlay();
  }

  /** The element being edited, with its text as typed so far (the app's editor sends it). */
  updateEdit(text: string) {
    const el = this.editing;
    if (!el) return;
    // A new text box opens to the right of the tap; Arabic flips it to the left.
    if (this.editingNew && el.kind === "text" && !el.text && /\p{L}/u.test(text) && isRtl(text)) el.x = Math.max(0, el.x - el.w);
    el.text = text;
  }

  /** Enter in the editor: continues a list; returns the new text and caret, or null. */
  continueList(text: string, caret: number): { value: string; caret: number } | null {
    return continueList(text, caret);
  }

  /** Formatting for new text, or the text being edited. */
  applyFormat(change: { bold?: boolean; italic?: boolean; align?: TextAlign; list?: ListKind; font?: FontId; fontSize?: number; color?: string }) {
    if (change.bold !== undefined) this.textFormat.bold = change.bold;
    if (change.italic !== undefined) this.textFormat.italic = change.italic;
    if (change.align) this.textFormat.align = change.align;
    if (change.font) this.textFont = change.font;
    const el = this.editing;
    if (!el) return;
    if (change.bold !== undefined) el.bold = change.bold;
    if (change.italic !== undefined) el.italic = change.italic;
    if (change.align) el.align = change.align;
    if (change.list) el.text = toggleList(el.text, change.list);
    if (change.font) el.font = change.font;
    if (change.fontSize) el.fontSize = change.fontSize;
    if (change.color) el.color = change.color;
  }

  finishTextEdit() {
    const el = this.editing;
    if (!el) return;
    this.editing = null;
    el.text = el.text.replace(/\s+$/, "");
    this.hiddenIds.delete(el.id);
    if (el.kind === "text" && !el.text.trim()) {
      this.elements = this.elements.filter((x) => x.id !== el.id);
    } else if (el.kind === "text") {
      const natural = Math.ceil(naturalTextWidth(el) + 4);
      if (natural < el.w) {
        const shrunk = { ...el, w: Math.max(24, natural) };
        const rightAligned = isRtl(el.text) ? el.align !== "right" && el.align !== "center" : el.align === "right";
        if (el.align === "center") shrunk.x = el.x + (el.w - shrunk.w) / 2;
        else if (rightAligned) shrunk.x = el.x + el.w - shrunk.w;
        this.elements = this.elements.map((x) => (x.id === el.id ? shrunk : x));
      }
    }
    // A copy, so the canvas sees a changed element.
    this.elements = this.elements.map((x) => (x.id === el.id ? { ...x } : x));
    // Nothing changed: no history step.
    const before = this.editingBefore;
    const same = before.length === this.elements.length && before.every((b, k) => {
      const a = this.elements[k];
      return a === b || (a.id === b.id && JSON.stringify(a) === JSON.stringify(b));
    });
    if (same) this.undoStack.pop();
    this.changed();
  }

  // ------------------------------------------------------------ drawing

  /** Everything drawn over the page: the gesture in progress, the selection and its handles. */
  drawOverlay(ctx: CanvasRenderingContext2D, pi: number) {
    if (pi !== this.active) return;
    const g = this.gesture;
    if (g?.type === "draw") {
      if (g.preview && g.tool === "highlighter" && g.preview.shape.kind === "line") drawStroke(ctx, this.straightHighlight(g, g.preview.shape.points));
      else if (g.preview) {
        drawElement(ctx, g.preview, this.images, () => {});
        if (g.held) this.drawHeldHint(ctx, g.preview, g.held.mode);
      } else if (g.ruler) {
        drawStroke(ctx, { id: "live", z: 0, kind: "stroke", tool: g.tool === "highlighter" ? "highlighter" : "pen", style: g.tool === "pen" ? this.penStyle : undefined, color: this.colors[g.tool], width: this.widths[g.tool], points: g.points, smooth: 0 }, true);
      }
    } else if (g?.type === "shape") {
      if (g.preview) drawElement(ctx, g.preview, this.images, () => {});
    } else if (g?.type === "lasso") {
      drawLasso(ctx, g.points, this.zoom);
    } else if (g?.type === "move") {
      drawGuides(ctx, g.guides, this.zoom);
    } else if (g?.type === "handle") {
      drawElement(ctx, g.preview, this.images, () => {});
    } else if (g?.type === "transform") {
      const b = g.box;
      drawTransformedBox(ctx, ([[b.x, b.y], [b.x + b.w, b.y], [b.x + b.w, b.y + b.h], [b.x, b.y + b.h]] as P2[]).map((p) => applyAffine(g.m, p)), this.zoom);
    }
    const sel = g?.type === "handle" ? bounds(g.preview) : g?.type === "transform" ? null : this.selectionBounds();
    if (sel) {
      const r = g?.type === "move" ? { ...sel, x: sel.x + g.dx, y: sel.y + g.dy } : sel;
      drawSelection(ctx, r, this.zoom);
    }
    const segment = g?.type === "handle" ? g.preview : g ? null : this.singleSegment();
    if (segment) drawHandles(ctx, segment, this.zoom);
    else if (!g) {
      const box = this.transformBox();
      if (box) drawGroupHandles(ctx, box, this.zoom);
    }
  }

  /** Where the overlay draws on the active page (page units), or null when there's nothing. */
  overlayBounds(): Rect | null {
    const g = this.gesture;
    const parts: Rect[] = [];
    const sel = this.selectionBounds();
    if (sel) parts.push(sel);
    if (g?.type === "draw" && (g.preview || g.ruler)) {
      if (g.preview) parts.push(bounds(g.preview));
      if (g.points.length) parts.push(rectOfPts(g.points.map((p) => [p[0], p[1]] as P2)));
    }
    if (g?.type === "shape" && g.preview) parts.push(bounds(g.preview));
    if (g?.type === "lasso") parts.push(rectOfPts(g.points));
    if (g?.type === "handle") parts.push(bounds(g.preview));
    if (g?.type === "move" && sel) parts.push({ ...sel, x: sel.x + g.dx, y: sel.y + g.dy });
    if (g?.type === "move" && g.guides.length) parts.push({ x: 0, y: 0, w: this.page.width, h: this.page.height });
    if (g?.type === "transform") {
      const b = g.box;
      parts.push(rectOfPts(([[b.x, b.y], [b.x + b.w, b.y], [b.x + b.w, b.y + b.h], [b.x, b.y + b.h]] as P2[]).map((p) => applyAffine(g.m, p))));
    }
    const u = unionRects(parts);
    if (!u) return null;
    // Room for handles, the rotation knob, guides' labels and the held hint.
    const pad = 48 / this.zoom;
    return { x: u.x - pad, y: u.y - pad, w: u.w + 2 * pad, h: u.h + 2 * pad };
  }

  private drawHeldHint(ctx: CanvasRenderingContext2D, el: ShapeEl, mode: Held["mode"]) {
    const text = mode === "transform" ? "Rotate & resize · hold still to move" : "Move · hold still to rotate & resize";
    const r = bounds(el);
    const z = this.zoom;
    ctx.save();
    ctx.font = `${12 / z}px system-ui, sans-serif`;
    const w = ctx.measureText(text).width + 16 / z;
    const hgt = 22 / z;
    const x = Math.max(4 / z, Math.min(this.page.width - w - 4 / z, r.x + r.w / 2 - w / 2));
    const y = Math.max(4 / z, r.y - hgt - 10 / z);
    ctx.fillStyle = "rgba(17, 24, 39, 0.85)";
    ctx.beginPath();
    ctx.roundRect(x, y, w, hgt, hgt / 2);
    ctx.fill();
    ctx.fillStyle = "#fff";
    ctx.textBaseline = "middle";
    ctx.fillText(text, x + 8 / z, y + hgt / 2);
    ctx.restore();
  }
}

// --- ruler edges ---------------------------------------------------------------

function projectOn(line: [P2, P2], p: P2): { p: P2; dist: number; t: number } | null {
  const [a, b] = line;
  const dx = b[0] - a[0];
  const dy = b[1] - a[1];
  const len2 = dx * dx + dy * dy;
  if (len2 < 1e-9) return null;
  const t = ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / len2;
  const q: P2 = [a[0] + dx * t, a[1] + dy * t];
  return { p: q, dist: Math.hypot(p[0] - q[0], p[1] - q[1]), t };
}

/** A point on the edge, moved `off` to the side of `side` (ink sits against the edge). */
function offsetFrom(line: [P2, P2], q: P2, side: P2, off: number): P2 {
  const [a, b] = line;
  const len = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
  let nx = -(b[1] - a[1]) / len;
  let ny = (b[0] - a[0]) / len;
  if ((side[0] - q[0]) * nx + (side[1] - q[1]) * ny < 0) {
    nx = -nx;
    ny = -ny;
  }
  return [q[0] + nx * off, q[1] + ny * off];
}

function extent(points: Pt[], line: [P2, P2]): [P2, P2] {
  const dir = [line[1][0] - line[0][0], line[1][1] - line[0][1]];
  let lo = points[0];
  let hi = points[0];
  const t = (p: Pt) => p[0] * dir[0] + p[1] * dir[1];
  for (const p of points) {
    if (t(p) < t(lo)) lo = p;
    if (t(p) > t(hi)) hi = p;
  }
  return [[lo[0], lo[1]], [hi[0], hi[1]]];
}

export function createEditor(pages: PageMeta[]): EditorCore {
  return new EditorCore(pages);
}

export { smoothPoints };
