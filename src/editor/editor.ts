// Notebook editor: page canvas, tools, page sidebar, undo/redo and autosave.

import { api, PAPER_COLORS, TEMPLATE_CATEGORIES, TEMPLATES, type ExportFormat, type ExportPage, type Notebook, type P2, type Page, type RecognizedShape, type Template } from "../api";
import { ensureFonts, FONT_PRESETS, fontPreset, isRtl, type FontId } from "../fonts";
import { getPref, hasPref, setPref } from "../prefs";
import { customColorButton } from "../colorPicker";
import { isDark, toggleTheme } from "../theme";
import { shortcutsSheet } from "./shortcuts";
import { choiceChip, colorFavourites, Favourites, paletteFor, popover, sizeFavourites, type FavTool } from "./toolkit";
import { confirmDialog, COVER_COLORS, formDialog, h, icon, popupMenu, reportError, sizeControl, toast } from "../ui";
import {
  bounds,
  buildShape,
  convertShape,
  HANDLE_CURSORS,
  handleAt,
  hitForSelect,
  hitTest,
  insideLasso,
  isClosedShape,
  isHeld,
  isResizable,
  rectContains,
  resizeElement,
  eraseElement,
  geomBox,
  transformShape,
  applyAffine,
  groupHandleAt,
  groupTransform,
  transformElement,
  type Affine,
  type GroupHandle,
  translate,
  unionRects,
  type Handle,
  type Rect,
  type ShapeKind,
} from "./geometry";
import { inkToTextDialog } from "./inkToText";
import { addLessonPage, DAYS, isoDate, lessonEntry, nextFreeRow, parseDaftar, type DaftarMeta } from "../daftar";
import { isLessonTemplate, lessonLayout } from "./daftarPapers";
import { cachedBackground, renderBackground } from "./pdfRender";
import { INSTRUMENTS, MM, Ruler, type Guide as EdgeGuide, type InstrumentKind } from "./ruler";
import { smoothPoints, SpeedInk, speedSensitive } from "./ink";
import { COVER_DESIGNS, coverDesignOf, isCoverTemplate, type CoverDesign } from "./covers";
import { applyCover, coverPreview } from "../cover";
import { Laser, LASER_CORE_FADE_MS, LASER_HOLD_MS, type LaserLayers, type LaserMode } from "./laser";
import { buildVectorPdf, type PdfPageInput } from "./pdfExport";
import { continueList, currentList, toggleList, type ListKind } from "./textFormat";
import { beautify, detectBrace, recognizeShape } from "./beautify";
import { anchorsAsTargets, paperTargets, snapMove, type Anchor, type Guide, type Target } from "./snap";
import { fromStored, newId, nextZ, PEN_STYLES, toStored, type El, type PenStyle, type Pt, type ShapeEl, type StickyEl, type StrokeEl, type TextAlign, type TextEl } from "./model";
import { drawElement, drawGroupHandles, drawGuides, drawHandles, drawLasso, drawTransformedBox, drawSelection, drawStroke, drawTemplate, steadyHighlight, fitTextToBox, naturalTextWidth, paperLayout, preloadImages, renderPage, templatePreview, textAnchors, type ImageCache } from "./render";

type Tool = "pen" | "highlighter" | "eraser" | "lasso" | "shape" | "text" | "sticky" | "laser" | "hand";

/** The tools, in three trays: writing, arranging and presenting. */
const TOOLS: { id: Tool; label: string; key: string; tray: number }[] = [
  { id: "pen", label: "Pen", key: "p", tray: 0 },
  { id: "highlighter", label: "Highlighter", key: "h", tray: 0 },
  { id: "eraser", label: "Eraser", key: "e", tray: 0 },
  { id: "lasso", label: "Lasso select", key: "l", tray: 1 },
  { id: "shape", label: "Shape", key: "s", tray: 1 },
  { id: "text", label: "Text box", key: "t", tray: 1 },
  { id: "sticky", label: "Sticky note", key: "n", tray: 1 },
  { id: "laser", label: "Laser pointer", key: "k", tray: 2 },
  { id: "hand", label: "Pan", key: " ", tray: 2 },
];

const INK_COLORS = ["#111827", "#2563eb", "#dc2626", "#16a34a", "#9333ea", "#ea580c", "#0891b2", "#facc15", "#ffffff"];
const LASER_COLORS = ["#ff1f1f", "#12c95a", "#1f7bff", "#b44dff", "#ff8a00"];
const STICKY_COLORS = ["#fef08a", "#bbf7d0", "#bfdbfe", "#fbcfe8", "#fed7aa"];
const HOLD_MS = 550;
/** Touches this soon after pen activity are treated as a resting palm. */
const PALM_GUARD_MS = 400;
const PEN_ONLY_KEY = "inkleaf-pen-only";
const PEN_STYLE_KEY = "inkleaf-pen-style";
const SHAPE_KIND_KEY = "inkleaf-shape-kind";

const SHAPE_KINDS: { id: ShapeKind; label: string; icon: Parameters<typeof icon>[0] }[] = [
  { id: "auto", label: "Freehand (auto-detect shape)", icon: "sAuto" },
  { id: "line", label: "Line", icon: "sLine" },
  { id: "arrow", label: "Arrow", icon: "sArrow" },
  { id: "rectangle", label: "Rectangle", icon: "sRectangle" },
  { id: "ellipse", label: "Ellipse / circle", icon: "sEllipse" },
  { id: "triangle", label: "Triangle", icon: "sTriangle" },
  { id: "diamond", label: "Diamond", icon: "sDiamond" },
  { id: "star", label: "Star", icon: "sStar" },
];

const WIDTHS_KEY = "inkleaf-sizes";
const TEXT_FONT_KEY = "inkleaf-text-font";
const ERASER_MODE_KEY = "inkleaf-eraser-mode";
const LASER_MODE_KEY = "inkleaf-laser-mode";
const INSTRUMENT_KEY = "inkleaf-instrument";
const SIDEBAR_KEY = "inkleaf-page-sidebar";
const DRAWER_KEY = "inkleaf-tool-drawer";
const DRAWER_TIP_KEY = "inkleaf-tool-drawer-tip";
const FAV_TOOLS: FavTool[] = ["pen", "highlighter", "shape", "text", "sticky", "laser", "eraser"];
const isFavTool = (t: string): t is FavTool => (FAV_TOOLS as string[]).includes(t);

/** True for colours light enough to need a dark icon on top. */
function isLightColor(hex: string): boolean {
  const m = /^#?([0-9a-f]{6})$/i.exec(hex.trim());
  if (!m) return false;
  const n = parseInt(m[1], 16);
  const lin = (v: number) => ((v /= 255) <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4);
  const l = 0.2126 * lin(n >> 16) + 0.7152 * lin((n >> 8) & 255) + 0.0722 * lin(n & 255);
  return l > 0.45;
}

/** An ink colour that shows on the drawer (very dark ink on a dark drawer, white on light). */
function sampleInk(color: string): string {
  const dark = isDark();
  if (!dark && isLightColor(color) && /^#f{6}$/i.test(color)) return "#111827";
  if (dark && !isLightColor(color) && /^#[0-3][0-9a-f][0-3][0-9a-f][0-3][0-9a-f]$/i.test(color)) return "#e7e5e4";
  return color;
}

/** A short stroke drawn with a pen nib, for the nib menu. */
function nibSample(style: PenStyle, color: string, size = 3): HTMLElement {
  const w = 56;
  const hgt = 22;
  const dpr = Math.max(2, window.devicePixelRatio || 1);
  const c = document.createElement("canvas");
  c.width = w * dpr;
  c.height = hgt * dpr;
  c.className = "nib-sample";
  c.style.width = `${w}px`;
  c.style.height = `${hgt}px`;
  const ctx = c.getContext("2d")!;
  ctx.scale(dpr, dpr);
  const points: Pt[] = [];
  for (let i = 0; i <= 24; i++) {
    const t = i / 24;
    points.push([4 + t * (w - 8), hgt / 2 + Math.sin(t * Math.PI * 2) * 6, 0.35 + 0.5 * Math.sin(t * Math.PI)]);
  }
  drawStroke(ctx, { id: "sample", z: 0, kind: "stroke", tool: "pen", style, color: sampleInk(color), width: Math.min(6.5, Math.max(1.4, size * 0.9)), points });
  return c;
}

/** Size control ranges and one-tap presets per tool. */
const SIZE_SPECS: Record<string, { label: string; min: number; max: number; step: number; presets: number[]; preview: "dot" | "letter" }> = {
  pen: { label: "Pen size", min: 0.5, max: 20, step: 0.5, presets: [1, 2, 3, 5, 8], preview: "dot" },
  highlighter: { label: "Highlighter size", min: 4, max: 48, step: 1, presets: [8, 12, 18, 26, 36], preview: "dot" },
  eraser: { label: "Eraser size", min: 4, max: 80, step: 1, presets: [6, 10, 16, 28, 48], preview: "dot" },
  shape: { label: "Line width", min: 0.5, max: 20, step: 0.5, presets: [1, 2, 3, 5, 8], preview: "dot" },
  text: { label: "Text size", min: 8, max: 96, step: 1, presets: [14, 18, 24, 32, 48], preview: "letter" },
  sticky: { label: "Text size", min: 10, max: 48, step: 1, presets: [14, 18, 22, 28, 36], preview: "letter" },
};
const DEFAULT_WIDTHS: Record<string, number> = { pen: 3, highlighter: 18, eraser: 14, shape: 3, text: 20, sticky: 18 };

/** Thumbnail scale: covers are drawn sharper, since the library shows them full-size. */
function thumbScale(page: Page): number {
  return (isCoverTemplate(page.template) ? 420 : 200) / page.width;
}

function loadWidths(): Record<string, number> {
  const out = { ...DEFAULT_WIDTHS };
  try {
    const saved = JSON.parse(getPref<string>(WIDTHS_KEY, "{}")) as Record<string, unknown>;
    for (const [k, v] of Object.entries(saved)) {
      const spec = SIZE_SPECS[k];
      if (spec && typeof v === "number" && v >= spec.min && v <= spec.max) out[k] = v;
    }
  } catch {
    /* ignore corrupt preferences */
  }
  return out;
}

const EXPORT_SCALE: Record<ExportFormat, number> = { pdf: 3, png: 3, pptx: 2 };
const MAX_UNDO = 100;

type Gesture =
  | { type: "draw"; tool: "pen" | "highlighter" | "shape"; points: Pt[]; times: number[]; preview: ShapeEl | null; holdTimer: number; held: Held | null; ruler?: { edge: EdgeGuide; offset: number }; ink?: SpeedInk | null; frozen?: number; smooth?: Pt[] }
  | { type: "erase"; changed: boolean; last: P2 | null }
  | { type: "lasso"; points: P2[] }
  | { type: "laser" }
  | { type: "move"; start: P2; original: El[]; dx: number; dy: number; snap: { anchors: Anchor[]; targets: Target[] } | null; guides: Guide[] }
  | { type: "pan"; sx: number; sy: number; left: number; top: number }
  | { type: "shape"; start: P2; preview: ShapeEl | null }
  | { type: "handle"; handle: Handle; original: El; preview: El }
  | { type: "transform"; handle: GroupHandle; start: P2; box: Rect; originals: El[]; previews: El[]; m: Affine };

/**
 * After a held stroke snaps to a refined shape, the pen keeps control of it
 * until it lifts: "transform" rotates and resizes around the pivot so the
 * grabbed point follows the pen; "move" drags it. Holding still toggles.
 */
interface Held {
  mode: "transform" | "move";
  base: ShapeEl;
  anchor: P2;
  pivot: P2;
  still: P2;
  stillTimer: number;
}

/** Finger / mouse navigation while in pen-only mode. */
type Nav =
  | { kind: "pan"; id: number; sx: number; sy: number; left: number; top: number }
  | { kind: "pinch"; startDist: number; startZoom: number; anchor: P2 };

export interface EditorCallbacks {
  close(): void;
}

export class EditorScreen {
  readonly root = h("div", { class: "editor" });
  private notebook!: Notebook;
  /** Set when the notebook is a daftar annossous (sections per class). */
  private daftar: DaftarMeta | null = null;
  private daftarBar = h("div", { class: "daftar-bar", hidden: true });
  private pages: Page[] = [];
  private index = 0;
  private elements: El[] = [];
  private undoStack: string[] = [];
  private redoStack: string[] = [];
  private tool: Tool = "pen";
  private colors: Record<string, string> = { pen: INK_COLORS[0], highlighter: "#facc15", shape: INK_COLORS[0], text: INK_COLORS[0], sticky: STICKY_COLORS[0], laser: LASER_COLORS[0] };
  /** Each tool's favourite colours and sizes. */
  private favs = new Favourites();
  /** Presentation laser: never part of the page. */
  private laser = Object.assign(new Laser(), { mode: getPref<LaserMode>(LASER_MODE_KEY, "trail", ["trail", "dot"]) });
  /** The laser has its own layer over the visible area, so the page is never redrawn for it. */
  private laserGlow = h("canvas", { class: "laser-glow" });
  private laserTube = h("canvas", { class: "laser-tube" });
  private laserCore = h("canvas", { class: "laser-core" });
  private laserLayer = h("div", { class: "laser-layer", "aria-hidden": "true" }, this.laserGlow, this.laserTube, this.laserCore);
  /** Where the page was when the laser layers were last fully painted. */
  private laserView = "";
  private laserDot = h("div", { class: "laser-dot", "aria-hidden": "true", hidden: true });
  private laserFrame = 0;
  private laserFadeTimer = 0;
  private widths: Record<string, number> = loadWidths();
  private textFont: FontId = getPref(TEXT_FONT_KEY, "sans", FONT_PRESETS.map((f) => f.id));
  private selectionBarKey = "";
  /** Formatting applied to new text boxes and sticky notes. */
  private textFormat: { bold: boolean; italic: boolean; align: TextAlign } = { bold: false, italic: false, align: "left" };
  private eraserMode: "precise" | "stroke" = getPref(ERASER_MODE_KEY, "precise", ["precise", "stroke"] as const);
  private zoom = 1;
  private selection = new Set<string>();
  private clipboard: El[] = [];
  private gesture: Gesture | null = null;
  private penOnly = getPref<string>(PEN_ONLY_KEY, "false") === "true";
  private lastPenAt = -Infinity;
  private navPointers = new Map<number, { x: number; y: number }>();
  private nav: Nav | null = null;
  private pinchFrame = 0;
  private penStyle: PenStyle = getPref(PEN_STYLE_KEY, "fountain", PEN_STYLES.map((p) => p.id));
  private shapeKind: ShapeKind = getPref(SHAPE_KIND_KEY, "auto", SHAPE_KINDS.map((k) => k.id));
  private shapeFill = false;
  /** On-page ruler (an overlay, never part of the page). */
  private ruler = new Ruler({
    toPage: (x, y) => this.point({ clientX: x, clientY: y }),
    zoom: () => this.zoom,
    viewWidth: () => this.scroller.clientWidth,
    pageOrigin: () => {
      const layer = this.rulerLayer.getBoundingClientRect();
      const page = this.canvas.getBoundingClientRect();
      return [page.left - layer.left, page.top - layer.top];
    },
  });
  /** The ruler floats in its own layer over the visible area, so it never changes scrolling. */
  private rulerLayer = h("div", { class: "ruler-layer" });
  private rulerBtn = h("button", { class: "icon-btn ruler-btn", "aria-label": "Ruler and drawing instruments", title: "Ruler, protractor, set squares (R)", "aria-haspopup": "menu", "aria-pressed": "false", onclick: () => this.instrumentMenu() }, icon("ruler"));
  private penOnlyBtn = h("button", { class: "icon-btn pen-only-btn", "aria-label": "Pen-only mode", onclick: () => this.setPenOnly(!this.penOnly, true) }, icon("stylus"));
  private hiddenIds = new Set<string>();
  private images: ImageCache = new Map();
  private saveTimer: number | undefined;
  private dirty = false;
  private saving: Promise<void> = Promise.resolve();
  private cursor: P2 | null = null;
  private editing: { el: TextEl | StickyEl; input: HTMLTextAreaElement } | null = null;

  private canvas = h("canvas", { class: "page-canvas", "aria-label": "Page canvas" });
  private ctx = this.canvas.getContext("2d")!;
  private cache = document.createElement("canvas");
  private pageWrap = h("div", { class: "page-wrap" }, this.canvas);
  /** One slot per page, stacked for continuous scrolling. */
  private column = h("div", { class: "page-column" });
  private scroller = h("div", { class: "page-scroller" }, this.column);
  private slots: HTMLElement[] = [];
  /** Elements of every loaded page; the active page's live list is `elements`. */
  private elementCache = new Map<string, El[]>();
  private history = new Map<string, { undo: string[]; redo: string[] }>();
  /** Id of the page whose elements are live in `elements`. */
  private activeId: string | null = null;
  private visibleSlots = new Set<number>();
  private observer: IntersectionObserver | null = null;
  private staticZoom = 0;
  private staticTimer = 0;
  private scrollTimer = 0;
  /** Scrolls we cause ourselves shouldn't change the current page. */
  private ignoreScrollUntil = 0;
  private pageList = h("ol", { class: "page-list", "aria-label": "Pages" });
  private toolbar = h("div", { class: "toolbar", role: "toolbar", "aria-label": "Tools" });
  private statusBar = h("div", { class: "status-bar" });
  private optionsBar = h("div", { class: "options-bar", role: "toolbar", "aria-label": "Tool options" });
  private selectionBar = h("div", { class: "selection-bar", hidden: true });
  private titleBtn = h("button", { class: "title-btn", title: "Rename notebook" });
  private pageChip = h("span", { class: "page-chip" });
  private pageCount = h("span", { class: "page-count" });
  private focusExit = h("button", { class: "focus-exit", title: "Leave focus mode (F)", onclick: () => this.toggleFocus() }, icon("focus"), "Exit focus");
  private fileInput = h("input", { type: "file", accept: "image/png,image/jpeg,image/gif,image/webp", hidden: true });

  constructor(private cb: EditorCallbacks) {
    this.root.append(
      h("header", { class: "editor-header" }, this.headerLeft(), this.toolbar, this.headerRight()),
      h(
        "div",
        { class: "editor-body" },
        h(
          "aside",
          { class: "page-sidebar", "aria-label": "Pages" },
          h("div", { class: "page-sidebar-head" }, h("span", {}, "Pages"), this.pageCount),
          this.pageList,
          h("button", { class: "add-page", title: "Add a page at the end", onclick: () => (this.daftar ? this.addDaftarPage(this.page.section) : this.addPage(this.pages.length)) }, icon("plus"), "Add page"),
        ),
        // The tool options and the page/zoom controls float over the canvas.
        h("div", { class: "canvas-area" }, this.scroller, this.optionsBar, this.statusBar, this.rulerLayer, this.laserLayer, this.laserDot, this.focusExit),
      ),
      this.fileInput,
    );
    this.root.classList.toggle("no-sidebar", getPref(SIDEBAR_KEY, "shown", ["shown", "hidden"]) === "hidden");
    // Start every tool on its selected favourites.
    for (const t of FAV_TOOLS) {
      const c = this.favs.color(t);
      const w = this.favs.size(t);
      if (c) this.colors[t] = c;
      if (w !== undefined) this.widths[t] = w;
    }
    this.pageWrap.append(this.selectionBar);
    this.rulerLayer.append(this.ruler.el);
    this.ruler.setKind(getPref<InstrumentKind>(INSTRUMENT_KEY, "ruler", INSTRUMENTS.map((i) => i.id)));
    this.bindCanvas();
    this.fileInput.addEventListener("change", () => this.insertImageFile(this.fileInput.files?.[0]));
    this.scroller.addEventListener("wheel", (e) => this.onWheel(e), { passive: false });
    this.scroller.addEventListener("scroll", () => this.onScroll(), { passive: true });
    window.addEventListener("resize", () => {
      if (!this.root.isConnected) return;
      this.layout();
      this.placeDrawer();
      if (!this.selectionBar.hidden) this.positionSelectionBar();
    });
  }

  // ------------------------------------------------------------- lifecycle

  async open(notebookId: string, pageId?: string | null) {
    try {
      this.notebook = await api.notebook(notebookId);
      this.pages = await api.pages(notebookId);
      const i = pageId ? this.pages.findIndex((p) => p.id === pageId) : 0;
      this.titleBtn.textContent = this.notebook.title;
      this.daftar = parseDaftar(this.notebook);
      this.renderToolbar();
      await this.loadPage(Math.max(0, i), true);
      if (!this.drawerPinned && getPref<string>(DRAWER_TIP_KEY, "") !== "seen") {
        setPref(DRAWER_TIP_KEY, "seen");
        toast("Tip: tap the selected tool again for its colours and sizes");
      }
    } catch (e) {
      reportError(e);
      this.cb.close();
    }
  }

  /** Persists pending edits; call before leaving the editor. */
  async flush() {
    this.finishTextEdit();
    if (this.dirty) await this.saveNow();
    await this.saving;
  }

  handleKey(e: KeyboardEvent): boolean {
    const target = e.target as HTMLElement;
    if (target.closest("input, textarea, select, .modal, .color-popover, .template-popover")) return false;
    const mod = e.ctrlKey || e.metaKey;
    const key = e.key.toLowerCase();
    if (mod && key === "z") return this.consume(e, () => (e.shiftKey ? this.redo() : this.undo()));
    if (mod && key === "y") return this.consume(e, () => this.redo());
    if (mod && key === "c") return this.consume(e, () => this.copySelection());
    if (mod && key === "v") return false; // handled by the paste event
    if (mod && key === "d") return this.consume(e, () => this.duplicateSelection());
    if (mod && (key === "=" || key === "+")) return this.consume(e, () => this.setZoom(this.zoom * 1.2));
    if (mod && key === "-") return this.consume(e, () => this.setZoom(this.zoom / 1.2));
    if (mod && key === "0") return this.consume(e, () => this.fitWidth());
    if (mod && key === "a") return this.consume(e, () => this.selectAll());
    if (mod) return false;
    if ((key === "delete" || key === "backspace") && this.selection.size) return this.consume(e, () => this.deleteSelection());
    if (key === "escape") return this.consume(e, () => this.clearSelection());
    if (key === "pagedown" || key === "arrowright") return this.consume(e, () => this.goTo(this.index + 1));
    if (key === "pageup" || key === "arrowleft") return this.consume(e, () => this.goTo(this.index - 1));
    if (key === "r") return this.consume(e, () => this.toggleRuler());
    if (key === "f") return this.consume(e, () => this.toggleFocus());
    if (key === "?" || (key === "/" && e.shiftKey)) return this.consume(e, () => shortcutsSheet());
    if (key === "[") return this.consume(e, () => this.toggleSidebar());
    const tool = TOOLS.find((t) => t.key === key && t.key !== " ");
    if (tool) return this.consume(e, () => this.setTool(tool.id));
    return false;
  }

  handlePaste(e: ClipboardEvent) {
    if ((e.target as HTMLElement).closest("input, textarea")) return;
    const file = Array.from(e.clipboardData?.files ?? []).find((f) => f.type.startsWith("image/"));
    e.preventDefault();
    if (file) this.insertImageFile(file);
    else this.pasteClipboard();
  }

  private consume(e: Event, fn: () => void): boolean {
    e.preventDefault();
    fn();
    return true;
  }

  private get page(): Page {
    return this.pages[this.index];
  }

  /**
   * Loads page `i` (if needed), makes it the live page and, by default,
   * scrolls it into view.
   */
  private async loadPage(i: number, fit = false, scroll = true) {
    if (i < 0 || i >= this.pages.length) return;
    await this.flush();
    const id = this.pages[i].id;
    if (!this.elementCache.has(id)) {
      try {
        this.elementCache.set(id, fromStored(await api.elements(id)));
      } catch (e) {
        reportError(e);
        this.elementCache.set(id, []);
      }
    }
    if (this.slots.length !== this.pages.length) this.buildColumn();
    this.activate(i);
    void this.loadFontsFor(this.elements);
    void this.ensureThumbnail(this.page);
    void this.prefetchThumbnails();
    if (fit) this.fitWidth();
    if (scroll) this.scrollToPage(i);
  }

  private goTo(i: number) {
    if (i < 0 || i >= this.pages.length) return;
    if (i === this.index && this.activeId) this.scrollToPage(i);
    else void this.loadPage(i);
  }

  /** Creates one slot per page and watches which ones are on screen. */
  private buildColumn() {
    this.observer?.disconnect();
    this.visibleSlots.clear();
    this.slots = this.pages.map((_, i) => {
      const slot = h("div", { class: "page-slot", "data-index": String(i) });
      slot.addEventListener("pointerdown", (e) => this.onSlotDown(e, i));
      return slot;
    });
    this.column.replaceChildren(...this.slots);
    this.sizeSlots();
    this.observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          const i = Number((entry.target as HTMLElement).dataset.index);
          if (entry.isIntersecting) {
            this.visibleSlots.add(i);
            void this.ensureStatic(i);
          } else {
            this.visibleSlots.delete(i);
            // Free the render of pages far off screen.
            if (i !== this.index) this.slots[i]?.replaceChildren();
          }
        }
      },
      { root: this.scroller, rootMargin: "60% 0px" },
    );
    for (const slot of this.slots) this.observer.observe(slot);
  }

  private sizeSlots() {
    this.pages.forEach((p, i) => {
      const slot = this.slots[i];
      if (!slot) return;
      slot.style.width = `${p.width * this.zoom}px`;
      slot.style.height = `${p.height * this.zoom}px`;
      slot.style.background = p.paperColor;
    });
  }

  /**
   * Makes page `i` the live, editable page: the previous page keeps its
   * elements and undo history and is shown as a cached render again.
   */
  private activate(i: number) {
    if (i === this.index && this.activeId === this.pages[i]?.id && this.slots[i]?.contains(this.pageWrap)) return;
    this.finishTextEdit();
    if (this.dirty) void this.saveNow();
    // Laser drawings belong to the page they were drawn on.
    if (!this.laser.drawing) {
      this.laser.clear();
      this.resetLaserLayers();
    }
    const prevIndex = this.index;
    if (this.activeId) {
      this.elementCache.set(this.activeId, this.elements);
      this.history.set(this.activeId, { undo: this.undoStack, redo: this.redoStack });
    }
    this.index = i;
    const page = this.page;
    this.activeId = page.id;
    this.elements = this.elementCache.get(page.id) ?? [];
    const hist = this.history.get(page.id);
    this.undoStack = hist?.undo ?? [];
    this.redoStack = hist?.redo ?? [];
    this.selection.clear();
    this.selectionBarKey = "";
    this.slots[i].replaceChildren(this.pageWrap);
    if (prevIndex !== i && this.slots[prevIndex] && !this.slots[prevIndex].contains(this.pageWrap)) {
      if (this.visibleSlots.has(prevIndex)) this.renderStatic(prevIndex);
      else this.slots[prevIndex].replaceChildren();
    }
    this.layout();
    this.renderPageList();
    this.updateStatus();
  }

  /**
   * The imported-PDF background for a page: the best render available now
   * (or null), asking for a sharper one if needed and calling `onSharper`
   * when it arrives.
   */
  private background(page: Page, scale: number, onSharper: () => void): HTMLCanvasElement | null {
    if (!page.backgroundAsset || page.backgroundPage === null) return null;
    const have = cachedBackground(page.backgroundAsset, page.backgroundPage);
    if (!have || have.requested < scale * 0.95) {
      renderBackground(page.backgroundAsset, page.backgroundPage, scale).then(onSharper, (e) => reportError(e));
    }
    return have?.canvas ?? null;
  }

  /** Gives a PDF page a stored thumbnail the first time it is shown. */
  private async ensureThumbnail(page: Page) {
    if (page.thumbnail || !page.backgroundAsset) return;
    const els = this.elementCache.get(page.id) ?? [];
    const bg = await this.backgroundNow(page, thumbScale(page)).catch(() => null);
    if (page.thumbnail) return;
    const thumb = renderPage(page.template, page.paperColor, page.width, page.height, els, thumbScale(page), this.images, bg).toDataURL("image/jpeg", 0.82);
    page.thumbnail = thumb;
    await api.setPageThumbnail(page.id, thumb).catch(() => {});
    const i = this.pages.indexOf(page);
    this.pageList.children[i]?.querySelector(".page-thumb")?.replaceChildren(h("img", { src: thumb, alt: "", style: { aspectRatio: `${page.width} / ${page.height}` } }));
  }

  private prefetching = false;
  /** Fills in missing PDF page thumbnails in the background, one at a time. */
  private async prefetchThumbnails() {
    if (this.prefetching) return;
    this.prefetching = true;
    try {
      for (const page of [...this.pages]) {
        if (page.thumbnail || !page.backgroundAsset || !this.pages.includes(page)) continue;
        if (!this.elementCache.has(page.id)) {
          const els = fromStored(await api.elements(page.id).catch(() => []));
          if (!this.elementCache.has(page.id)) this.elementCache.set(page.id, els);
        }
        await this.ensureThumbnail(page);
      }
    } finally {
      this.prefetching = false;
    }
  }

  /** Waits for a page's PDF background at the given sharpness (for exports). */
  private async backgroundNow(page: Page, scale: number): Promise<HTMLCanvasElement | null> {
    if (!page.backgroundAsset || page.backgroundPage === null) return null;
    return renderBackground(page.backgroundAsset, page.backgroundPage, scale);
  }

  /** Loads a visible page's elements (once) and shows a render of it. */
  private async ensureStatic(i: number) {
    const page = this.pages[i];
    if (!page || i === this.index) return;
    if (!this.elementCache.has(page.id)) {
      try {
        const els = fromStored(await api.elements(page.id));
        if (!this.elementCache.has(page.id)) this.elementCache.set(page.id, els);
      } catch {
        return;
      }
    }
    const els = this.elementCache.get(page.id)!;
    await ensureFonts(els.map((el) => (el.kind === "text" || el.kind === "sticky" ? el.font : undefined)));
    await preloadImages(els, this.images);
    const dpr = window.devicePixelRatio || 1;
    await this.backgroundNow(page, Math.min(this.zoom * dpr, 4096 / Math.max(page.width, page.height))).catch(() => null);
    if (this.visibleSlots.has(i) && i !== this.index && this.pages[i] === page) this.renderStatic(i);
    void this.ensureThumbnail(page);
  }

  private renderStatic(i: number) {
    const page = this.pages[i];
    const els = this.elementCache.get(page.id);
    const slot = this.slots[i];
    if (!els || !slot || i === this.index) return;
    const dpr = window.devicePixelRatio || 1;
    const scale = Math.min(this.zoom * dpr, 4096 / Math.max(page.width, page.height));
    const bg = this.background(page, scale, () => {
      if (this.visibleSlots.has(i) && this.pages[i] === page) this.renderStatic(i);
    });
    const canvas = renderPage(page.template, page.paperColor, page.width, page.height, els, scale, this.images, bg);
    canvas.className = "page-static";
    slot.replaceChildren(canvas);
  }

  /** Re-renders on-screen pages after a zoom change (debounced). */
  private scheduleStatics() {
    window.clearTimeout(this.staticTimer);
    this.staticTimer = window.setTimeout(() => {
      this.staticZoom = this.zoom;
      for (const i of this.visibleSlots) if (i !== this.index) this.renderStatic(i);
    }, 160);
  }

  private scrollToPage(i: number) {
    const slot = this.slots[i];
    if (!slot) return;
    this.ignoreScrollUntil = performance.now() + 500;
    this.scroller.scrollTop = slot.offsetTop - 24;
  }

  /** The page under the middle of the view. */
  private pageAtViewCentre(): number {
    const mid = this.scroller.scrollTop + this.scroller.clientHeight / 2;
    let best = this.index;
    let bestDist = Infinity;
    this.slots.forEach((slot, i) => {
      const top = slot.offsetTop;
      const bottom = top + slot.offsetHeight;
      const d = mid < top ? top - mid : mid > bottom ? mid - bottom : 0;
      if (d < bestDist) {
        bestDist = d;
        best = i;
      }
    });
    return best;
  }

  /** After scrolling settles, the page in view becomes the current page. */
  private onScroll() {
    if (!this.selectionBar.hidden) this.positionSelectionBar();
    if (this.tool === "laser") this.renderLaser();
    if (this.ruler.visible) this.ruler.layout();
    window.clearTimeout(this.scrollTimer);
    this.scrollTimer = window.setTimeout(() => {
      if (this.gesture || this.editing || this.nav || performance.now() < this.ignoreScrollUntil) return;
      const i = this.pageAtViewCentre();
      if (i === this.index) return;
      if (this.elementCache.has(this.pages[i].id)) this.activate(i);
      else void this.loadPage(i, false, false);
    }, 220);
  }

  /** Pressing on another page makes it current and continues the gesture there. */
  private onSlotDown(e: PointerEvent, i: number) {
    if (this.pageWrap.contains(e.target as Node)) return; // the live page handles itself
    if ((this.penOnly && e.pointerType !== "pen") || this.tool === "hand") {
      this.onDown(e);
      return;
    }
    if (!this.elementCache.has(this.pages[i].id)) return;
    this.activate(i);
    this.onDown(e);
  }

  // ---------------------------------------------------------------- header

  private headerLeft(): HTMLElement {
    this.titleBtn.addEventListener("click", () => this.renameNotebook());
    return h(
      "div",
      { class: "header-left" },
      h("button", { class: "icon-btn", "aria-label": "Back to library", title: "Library", onclick: () => this.cb.close() }, icon("back")),
      h("button", { class: "icon-btn sidebar-btn", "aria-label": "Show or hide pages", title: "Pages sidebar", onclick: () => this.toggleSidebar() }, icon("sidebar")),
      this.titleBtn,
      this.pageChip,
      this.daftarBar,
    );
  }

  private headerRight(): HTMLElement {
    this.templateBtn.addEventListener("click", () => this.toggleTemplatePicker());
    return h(
      "div",
      { class: "header-right" },
      h(
        "div",
        { class: "header-pill", role: "group", "aria-label": "History" },
        h("button", { class: "icon-btn", "aria-label": "Undo", title: "Undo (Ctrl+Z)", onclick: () => this.undo() }, icon("undo")),
        h("button", { class: "icon-btn", "aria-label": "Redo", title: "Redo (Ctrl+Shift+Z)", onclick: () => this.redo() }, icon("redo")),
      ),
      h(
        "div",
        { class: "header-pill", role: "group", "aria-label": "Page" },
        this.templateBtn,
        h("button", { class: "icon-btn", "aria-label": "Insert image", title: "Insert image", onclick: () => this.fileInput.click() }, icon("image")),
        this.rulerBtn,
        this.penOnlyBtn,
      ),
      this.exportButton(),
      this.moreButton(),
    );
  }

  /** The ⋯ menu: focus mode, keyboard shortcuts and the theme. */
  private moreButton(): HTMLButtonElement {
    const btn = h("button", { class: "icon-btn", "aria-label": "More", title: "More", "aria-haspopup": "menu" }, icon("more"));
    btn.addEventListener("click", () =>
      popupMenu(btn, [
        { label: "Focus mode", shortcut: "F", action: () => this.toggleFocus() },
        { label: this.root.classList.contains("no-sidebar") ? "Show pages" : "Hide pages", shortcut: "[", action: () => this.toggleSidebar() },
        { label: "Keyboard shortcuts", shortcut: "?", action: () => shortcutsSheet() },
        { label: isDark() ? "Light mode" : "Dark mode", action: () => toggleTheme() },
      ]),
    );
    return btn;
  }

  /** Shows or hides the page thumbnails (remembered). */
  private toggleSidebar() {
    const hidden = this.root.classList.toggle("no-sidebar");
    setPref(SIDEBAR_KEY, hidden ? "hidden" : "shown");
    this.afterChromeChange();
  }

  /** Focus mode: only the page; the top bar slides in when the pointer reaches the top. */
  private toggleFocus() {
    const on = this.root.classList.toggle("focus");
    if (on) toast("Focus mode — move to the top edge for the toolbar, press F to leave");
    this.afterChromeChange();
  }

  /** Re-lays out the page once the sidebar or header has finished moving. */
  private afterChromeChange() {
    this.layout();
    this.placeDrawer();
    window.setTimeout(() => {
      if (!this.root.isConnected) return;
      this.layout();
      this.placeDrawer();
    }, 260);
  }

  private templateBtn = h("button", { class: "template-picker", "aria-haspopup": "dialog", "aria-expanded": "false", title: "Page paper" });

  /** Shows the current paper as a small preview plus its name. */
  private renderTemplateButton() {
    if (!this.page) return;
    const label = TEMPLATES.find((t) => t.id === this.page.template)?.label ?? (isCoverTemplate(this.page.template) ? "Cover" : this.page.template);
    this.templateBtn.setAttribute("aria-label", isCoverTemplate(this.page.template) ? "Cover design and colour" : `Page paper: ${label}`);
    this.templateBtn.title = isCoverTemplate(this.page.template) ? "Cover design and colour" : "Page paper";
    this.templateBtn.replaceChildren(
      h("span", { class: "template-thumb" }, templatePreview(this.page.template, this.page.paperColor, 22, 28)),
      h("span", { class: "template-name" }, label),
      icon("down"),
    );
  }

  /** On a cover page the paper button opens this: cover designs and colours. */
  private openCoverPicker() {
    const btn = this.templateBtn;
    const design = coverDesignOf(this.page.template);
    const color = this.page.paperColor;
    const close = () => {
      panel.remove();
      btn.setAttribute("aria-expanded", "false");
      document.removeEventListener("pointerdown", onOutside, true);
      document.removeEventListener("keydown", onKey, true);
    };
    const pick = (d: CoverDesign, c: string) => {
      close();
      if (d !== design || c.toLowerCase() !== color.toLowerCase()) void this.changeCover(d, c);
    };
    const grid = h(
      "div",
      { class: "template-grid cover-grid", role: "radiogroup", "aria-label": "Cover design" },
      ...COVER_DESIGNS.map((d) =>
        h(
          "button",
          { class: "template-card", role: "radio", title: d.label, "aria-checked": String(d.id === design), onclick: () => pick(d.id, color) },
          coverPreview(d.id, color, this.notebook.title, 88, this.page.width > this.page.height),
          h("span", { class: "template-card-label" }, d.label),
        ),
      ),
    );
    const colours = h(
      "div",
      { class: "swatches paper-colours", role: "radiogroup", "aria-label": "Cover colour" },
      ...COVER_COLORS.map((c) =>
        h("button", {
          class: "swatch",
          role: "radio",
          title: c,
          "aria-label": `Cover colour ${c}`,
          "aria-checked": String(c.toLowerCase() === color.toLowerCase()),
          style: { background: c },
          onclick: () => pick(design, c),
        }),
      ),
      customColorButton({ value: color, presets: COVER_COLORS, onPick: (c) => pick(design, c) }),
    );
    const panel = h(
      "div",
      { class: "template-popover", role: "dialog", "aria-label": "Cover" },
      h("div", { class: "template-popover-title" }, "Cover design"),
      grid,
      h("div", { class: "template-popover-title spaced" }, "Cover colour"),
      colours,
      h("p", { class: "popover-note" }, "The title and date move into place. Pick paper for your notes from any other page."),
    );
    const onOutside = (e: Event) => {
      const t = e.target as Node;
      if (!panel.contains(t) && !btn.contains(t) && !(t as Element).closest?.(".color-popover")) close();
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        e.stopPropagation();
        close();
        btn.focus();
      }
    };
    document.body.append(panel);
    const r = btn.getBoundingClientRect();
    panel.style.top = `${r.bottom + 8}px`;
    panel.style.left = `${Math.max(8, Math.min(window.innerWidth - panel.offsetWidth - 8, r.right - panel.offsetWidth))}px`;
    btn.setAttribute("aria-expanded", "true");
    setTimeout(() => {
      document.addEventListener("pointerdown", onOutside, true);
      document.addEventListener("keydown", onKey, true);
    });
  }

  /** Switches the cover's design or colour (moving its title and date into place). */
  private async changeCover(design: CoverDesign, color: string) {
    try {
      this.finishTextEdit();
      await this.flush();
      await applyCover(this.notebook, design, color);
      this.notebook.coverColor = color;
      const cover = (await api.pages(this.notebook.id))[0];
      const page = this.pages.find((p) => p.id === cover.id);
      if (page) Object.assign(page, cover);
      const els = fromStored(await api.elements(cover.id));
      this.elementCache.set(cover.id, els);
      this.history.delete(cover.id);
      if (this.activeId === cover.id) {
        this.elements = els;
        this.undoStack = [];
        this.redoStack = [];
        this.selection.clear();
      }
      this.renderTemplateButton();
      this.rebuildCache();
      this.draw();
      this.sizeSlots();
      this.renderPageList();
    } catch (e) {
      reportError(e);
    }
  }

  /** Opens a panel of paper styles with live previews. */
  private toggleTemplatePicker() {
    const existing = document.querySelector(".template-popover");
    if (existing) {
      existing.remove();
      this.templateBtn.setAttribute("aria-expanded", "false");
      return;
    }
    const current = this.page.template;
    if (isCoverTemplate(current)) return this.openCoverPicker();
    const close = () => {
      panel.remove();
      this.templateBtn.setAttribute("aria-expanded", "false");
      document.removeEventListener("pointerdown", onOutside, true);
      document.removeEventListener("keydown", onKey, true);
    };
    // Papers are grouped in tabs so the panel stays small; previews are
    // drawn when a tab is first shown.
    let category = TEMPLATES.find((t) => t.id === current)?.category ?? "basic";
    const grid = h("div", { class: "template-grid", role: "radiogroup", "aria-label": "Paper style" });
    const previews = new Map<Template, HTMLElement>();
    const card = (t: (typeof TEMPLATES)[number]) => {
      let el = previews.get(t.id);
      if (!el) {
        el = h(
          "button",
          {
            class: "template-card",
            role: "radio",
            title: t.label,
            "aria-checked": String(t.id === current),
            onclick: () => {
              close();
              if (t.id !== this.page.template) void this.setTemplate(t.id);
            },
          },
          templatePreview(t.id, this.page.paperColor, 88, 110, t.preview === "page"),
          h("span", { class: "template-card-label" }, t.label),
        );
        previews.set(t.id, el);
      }
      return el;
    };
    const tabs = TEMPLATE_CATEGORIES.map((c) =>
      h(
        "button",
        {
          class: "template-tab",
          role: "tab",
          title: c.label,
          onclick: () => {
            category = c.id;
            showCategory();
          },
        },
        c.short,
      ),
    );
    const showCategory = () => {
      TEMPLATE_CATEGORIES.forEach((c, i) => {
        tabs[i].setAttribute("aria-selected", String(c.id === category));
        tabs[i].classList.toggle("has-current", TEMPLATES.some((t) => t.id === current && t.category === c.id));
      });
      grid.replaceChildren(...TEMPLATES.filter((t) => t.category === category && !t.hidden).map(card));
    };
    showCategory();
    const colours = h(
      "div",
      { class: "swatches paper-colours", role: "radiogroup", "aria-label": "Paper colour" },
      ...PAPER_COLORS.map((c) =>
        h("button", {
          class: "swatch",
          role: "radio",
          title: c.label,
          "aria-label": `${c.label} paper`,
          "aria-checked": String(c.color.toLowerCase() === this.page.paperColor.toLowerCase()),
          style: { background: c.color },
          onclick: () => {
            close();
            void this.setPaper({ color: c.color }, false);
          },
        }),
      ),
    );
    const panel = h(
      "div",
      { class: "template-popover", role: "dialog", "aria-label": "Page paper" },
      h("div", { class: "template-popover-title" }, "Paper for this page"),
      h("div", { class: "template-tabs", role: "tablist", "aria-label": "Paper categories" }, ...tabs),
      grid,
      h("div", { class: "template-popover-title spaced" }, "Paper colour"),
      colours,
      h(
        "button",
        {
          class: "btn apply-all",
          onclick: () => {
            close();
            void this.setPaper({ template: this.page.template, color: this.page.paperColor }, true);
          },
        },
        "Apply this paper to all pages",
      ),
    );
    const onOutside = (e: Event) => {
      const t = e.target as Node;
      if (!panel.contains(t) && !this.templateBtn.contains(t)) close();
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        e.stopPropagation();
        close();
        this.templateBtn.focus();
      }
    };
    document.body.append(panel);
    const r = this.templateBtn.getBoundingClientRect();
    panel.style.top = `${r.bottom + 8}px`;
    panel.style.left = `${Math.max(8, Math.min(r.right - panel.offsetWidth, window.innerWidth - panel.offsetWidth - 8))}px`;
    this.templateBtn.setAttribute("aria-expanded", "true");
    setTimeout(() => {
      document.addEventListener("pointerdown", onOutside, true);
      document.addEventListener("keydown", onKey, true);
    });
    (panel.querySelector('[aria-checked="true"]') as HTMLElement | null)?.focus();
  }

  private exportButton(): HTMLButtonElement {
    const btn = h("button", { class: "export-btn", "aria-label": "Export", title: "Export as PDF, PowerPoint or image", "aria-haspopup": "menu" }, icon("download"), h("span", { class: "export-label" }, "Export"));
    btn.addEventListener("click", () =>
      popupMenu(btn, [
        { label: "PDF — whole notebook", action: () => this.exportAs("pdf", "all") },
        { label: "PDF — this page", action: () => this.exportAs("pdf", "current") },
        { label: "PowerPoint (.pptx) — whole notebook", action: () => this.exportAs("pptx", "all") },
        { label: "PowerPoint (.pptx) — this page", action: () => this.exportAs("pptx", "current") },
        { label: "PNG image — this page", action: () => this.exportAs("png", "current") },
      ]),
    );
    return btn;
  }

  /** Keeps the ruler layer over the visible page area. */
  private placeRulerLayer() {
    const area = this.rulerLayer.parentElement!.getBoundingClientRect();
    const view = this.scroller.getBoundingClientRect();
    const s = this.rulerLayer.style;
    s.left = `${view.left - area.left}px`;
    s.top = `${view.top - area.top}px`;
    s.width = `${this.scroller.clientWidth}px`;
    s.height = `${this.scroller.clientHeight}px`;
  }

  /** Picks an instrument (or hides the one showing). */
  private instrumentMenu() {
    popupMenu(this.rulerBtn, [
      ...INSTRUMENTS.map((i) => ({
        label: i.label,
        checked: this.ruler.visible && this.ruler.kind === i.id,
        action: () => this.showInstrument(i.id),
      })),
      ...(this.ruler.visible ? [{ label: "Hide", action: () => this.toggleRuler() }] : []),
    ]);
  }

  private showInstrument(kind: InstrumentKind) {
    this.ruler.setKind(kind);
    setPref(INSTRUMENT_KEY, kind);
    // (Re)show it centred in the view.
    if (this.ruler.visible) this.ruler.hide();
    this.toggleRuler();
  }

  /** Shows the last-used instrument in the middle of the view, or hides it (R). */
  private toggleRuler() {
    if (this.ruler.visible) this.ruler.hide();
    else {
      this.placeRulerLayer();
      const v = this.scroller.getBoundingClientRect();
      const [x, y] = this.point({ clientX: v.left + v.width / 2, clientY: v.top + v.height / 2 });
      this.ruler.show([Math.min(Math.max(x, 0), this.page.width), Math.min(Math.max(y, 0), this.page.height)]);
    }
    this.rulerBtn.classList.toggle("active", this.ruler.visible);
    this.rulerBtn.setAttribute("aria-pressed", String(this.ruler.visible));
  }

  private renderPenOnlyButton() {
    const b = this.penOnlyBtn;
    b.classList.toggle("active", this.penOnly);
    b.setAttribute("aria-pressed", String(this.penOnly));
    b.title = this.penOnly
      ? "Pen-only mode is on: only the pen writes; touch and mouse scroll and zoom. Click to turn off."
      : "Pen-only mode is off: any input writes. Click to turn on.";
  }

  private setPenOnly(on: boolean, explicit: boolean) {
    this.penOnly = on;
    if (explicit || !hasPref(PEN_ONLY_KEY)) setPref(PEN_ONLY_KEY, String(on));
    this.renderPenOnlyButton();
    toast(on ? "Pen-only mode on: the pen writes, touch and mouse scroll and pinch-zoom" : "Pen-only mode off: touch and mouse can write again");
  }

  /** Records pen activity; the first time a pen is seen, pen-only mode turns on. */
  private notePen() {
    this.lastPenAt = performance.now();
    if (!this.penOnly && !hasPref(PEN_ONLY_KEY)) this.setPenOnly(true, false);
  }

  private renderToolbar() {
    const tools = TOOLS.map((t) => {
      const active = this.tool === t.id;
      return h(
        "button",
        {
          class: `tool-btn${active ? " active" : ""}`,
          "aria-pressed": String(active),
          "aria-label": t.label,
          "data-tool": t.id,
          "aria-expanded": active ? String(this.drawerOpen) : undefined,
          title: active ? `${t.label} — tap again to ${this.drawerOpen ? "hide" : "show"} its options` : `${t.label}${t.key.trim() ? ` (${t.key.toUpperCase()})` : ""}`,
          onclick: () => (active ? this.toggleDrawer() : this.setTool(t.id)),
        },
        icon(t.id === "highlighter" ? "highlighter" : t.id === "sticky" ? "sticky" : t.id),
      );
    });
    // Favourite colours and sizes for the tool (the full choice is one tap away).
    const tool = this.tool;
    const fav = isFavTool(tool) ? this.favs.get(tool) : null;
    const swatches =
      fav && fav.colors.length
        ? colorFavourites({
            colors: fav.colors,
            index: fav.color,
            palette: paletteFor(tool as FavTool),
            onSelect: (i) => {
              this.favs.selectColor(tool as FavTool, i);
              this.useFavourites(tool);
            },
            onEdit: (i, c) => {
              this.favs.setColor(tool as FavTool, i, c);
              this.useFavourites(tool);
            },
          })
        : null;
    let width: HTMLElement | null = null;
    const spec = SIZE_SPECS[tool];
    if (spec && fav && fav.sizes.length) {
      width = sizeFavourites({
        sizes: fav.sizes,
        index: fav.size,
        spec,
        unit: "px",
        onSelect: (i) => {
          this.favs.selectSize(tool as FavTool, i);
          this.useFavourites(tool);
        },
        onEdit: (i, v) => {
          this.favs.setSize(tool as FavTool, i, v);
          this.useFavourites(tool, false);
        },
      });
    }
    // A colour bar under tools that draw in a colour shows their current ink.
    TOOLS.forEach((t, i) => {
      const c = t.id === "sticky" ? null : this.colors[t.id];
      if (c) {
        tools[i].dataset.ink = "";
        tools[i].style.setProperty("--ink", c);
        // The active tool is dipped in its ink: pick an icon colour that reads on it.
        if (this.tool === t.id) tools[i].dataset.inkTone = isLightColor(c) ? "light" : "dark";
      }
    });
    const trays = [0, 1, 2].map((n) => h("div", { class: "tool-tray" }, ...tools.filter((_, i) => TOOLS[i].tray === n)));
    this.toolbar.replaceChildren(h("div", { class: "tool-group tool-trays" }, ...trays));
    const options = [this.toolOptions(), swatches, width].filter((x): x is HTMLElement => x !== null);
    const hints: Partial<Record<Tool, string>> = {
      lasso: "Draw around items to select them, then drag to move",
      hand: "Drag to scroll · Ctrl + scroll or pinch to zoom",
    };
    if (options.length === 0) options.push(h("span", { class: "options-hint" }, hints[this.tool] ?? ""));
    const pin = h(
      "button",
      {
        class: `drawer-pin${this.drawerPinned ? " active" : ""}`,
        "aria-pressed": String(this.drawerPinned),
        "aria-label": "Keep options open",
        title: this.drawerPinned ? "Pinned open — unpin to tuck options away while you write" : "Keep options open",
        onclick: () => this.togglePin(),
      },
      icon("pin"),
    );
    this.optionsBar.replaceChildren(...options, pin);
    this.optionsBar.hidden = !this.drawerOpen;
    this.optionsBar.dataset.tool = this.tool;
    this.placeDrawer();
    this.renderPenOnlyButton();
    this.canvas.dataset.tool = this.tool;
    // The laser's three window-sized layers only exist while it's in use, so
    // the compositor doesn't blend empty layers on every frame otherwise.
    this.laserLayer.style.display = this.tool === "laser" ? "" : "none";
  }

  /** A pinned drawer stays open; otherwise it opens on demand and tucks away when you write. */
  private drawerPinned = getPref(DRAWER_KEY, "auto", ["auto", "pinned"]) === "pinned";
  private drawerOpen = this.drawerPinned;

  /** True while a menu, picker or the (unpinned) options drawer is open. */
  private menuOpen(): boolean {
    if (this.drawerOpen && !this.drawerPinned) return true;
    return !!document.querySelector(".popup-menu, .tool-popover, .template-popover, .color-popover");
  }

  /** Opens or closes the tool drawer; tapping the active tool does this. */
  private toggleDrawer(open = !this.drawerOpen) {
    if (open === this.drawerOpen) return;
    this.drawerOpen = open;
    if (!open) document.querySelectorAll(".tool-popover").forEach((p) => p.remove());
    this.renderToolbar();
    if (!this.selectionBar.hidden) this.positionSelectionBar();
  }

  /** Keeps the drawer open (or lets it tuck away again). */
  private togglePin() {
    this.drawerPinned = !this.drawerPinned;
    setPref(DRAWER_KEY, this.drawerPinned ? "pinned" : "auto");
    if (this.drawerPinned) this.drawerOpen = true;
    this.renderToolbar();
  }

  /**
   * Hangs the tool drawer under the active tool: centred on it where there's
   * room, with its pointer aimed at the tool. It glides when the tool changes.
   */
  private placeDrawer() {
    const bar = this.optionsBar;
    const area = bar.parentElement;
    const btn = this.toolbar.querySelector<HTMLElement>(".tool-btn.active");
    if (bar.hidden || !area || !btn || !this.root.isConnected) return;
    const a = area.getBoundingClientRect();
    const b = btn.getBoundingClientRect();
    if (!a.width) return;
    const margin = 12;
    const bw = Math.min(bar.offsetWidth, a.width - margin * 2);
    const centre = b.left + b.width / 2 - a.left;
    const left = Math.max(margin, Math.min(centre - bw / 2, a.width - bw - margin));
    bar.style.left = `${left}px`;
    bar.style.setProperty("--caret-x", `${Math.max(22, Math.min(bw - 22, centre - left))}px`);
    // Only glide once it has a place (not from the corner on first show).
    requestAnimationFrame(() => bar.classList.add("placed"));
  }

  /**
   * Applies a tool's selected favourites (colour and size). `rerender` is off
   * while a slider is being dragged, so the popover stays open.
   */
  private useFavourites(tool: Tool, rerender = true) {
    if (!isFavTool(tool)) return;
    const c = this.favs.color(tool);
    const w = this.favs.size(tool);
    if (c) this.colors[tool] = c;
    if (w !== undefined) {
      this.widths[tool] = w;
      setPref(WIDTHS_KEY, JSON.stringify(this.widths));
      if (tool === "text" || tool === "sticky") this.applyToEditing({ fontSize: w });
      if (tool === "eraser") this.draw();
    }
    if (rerender) this.renderToolbar();
    else {
      // Keep the size chip's number in step with the slider.
      const chip = this.optionsBar.querySelector(".value-chip");
      if (chip?.firstChild && w !== undefined) chip.firstChild.textContent = w.toFixed(SIZE_SPECS[tool]!.step < 1 ? 1 : 0);
      this.optionsBar.querySelectorAll<HTMLElement>(".fav-size").forEach((b, i) => b.setAttribute("aria-checked", String(i === this.favs.get(tool).size)));
    }
  }

  /** Secondary choices for the active tool: pen type, or shape kind and fill. */
  private toolOptions(): HTMLElement | null {
    const chip = (label: string, ic: Parameters<typeof icon>[0], active: boolean, onclick: () => void) =>
      h("button", { class: `tool-btn small${active ? " active" : ""}`, "aria-pressed": String(active), "aria-label": label, title: label, onclick }, icon(ic));
    if (this.tool === "pen") {
      return choiceChip({
        label: "Pen",
        title: "Pen type",
        current: this.penStyle,
        items: PEN_STYLES.map((p) => ({ id: p.id, label: p.label, art: () => nibSample(p.id, this.colors.pen, this.widths.pen) })),
        onPick: (id) => {
          this.penStyle = id as PenStyle;
          setPref(PEN_STYLE_KEY, id);
          this.renderToolbar();
        },
      });
    }
    if (this.tool === "text" || this.tool === "sticky") return h("div", { class: "text-options" }, this.fontPicker(), this.formatControls());
    if (this.tool === "laser") {
      const mode = (id: LaserMode, label: string, title: string) =>
        h(
          "button",
          {
            class: `tool-btn small text-chip${this.laser.mode === id ? " active" : ""}`,
            "aria-pressed": String(this.laser.mode === id),
            title,
            onclick: () => {
              this.laser.mode = id;
              setPref(LASER_MODE_KEY, id);
              this.renderToolbar();
            },
          },
          label,
        );
      return h(
        "div",
        { class: "tool-group options", role: "group", "aria-label": "Laser mode" },
        mode("trail", "Trail", "Draw a glowing line that fades away"),
        mode("dot", "Dot", "Just point with a glowing dot"),
      );
    }
    if (this.tool === "eraser") {
      const mode = (id: "precise" | "stroke", label: string, title: string) =>
        h(
          "button",
          {
            class: `tool-btn small text-chip${this.eraserMode === id ? " active" : ""}`,
            "aria-pressed": String(this.eraserMode === id),
            title,
            onclick: () => {
              this.eraserMode = id;
              setPref(ERASER_MODE_KEY, id);
              this.renderToolbar();
            },
          },
          label,
        );
      return h(
        "div",
        { class: "tool-group options", role: "group", "aria-label": "Eraser mode" },
        mode("precise", "Precise", "Erase only the parts the eraser touches"),
        mode("stroke", "Whole stroke", "Erase every stroke the eraser touches"),
      );
    }
    if (this.tool === "shape") {
      return h(
        "div",
        { class: "options-row" },
        choiceChip({
          label: "Shape",
          title: "Shape to draw",
          current: this.shapeKind,
          items: SHAPE_KINDS.map((k) => ({ id: k.id, label: k.id === "auto" ? "Auto shape" : k.label, art: () => icon(k.icon) })),
          onPick: (id) => {
            this.shapeKind = id as ShapeKind;
            setPref(SHAPE_KIND_KEY, id);
            this.renderToolbar();
          },
        }),
        chip(this.shapeFill ? "Fill on" : "Fill off", "fill", this.shapeFill, () => {
          this.shapeFill = !this.shapeFill;
          this.renderToolbar();
        }),
      );
    }
    return null;
  }

  /** Button showing the current handwriting font; opens the preset list. */
  private fontPicker(): HTMLElement {
    const current = fontPreset(this.editing?.el.font ?? this.textFont);
    const btn = h(
      "button",
      { class: "font-picker", "aria-haspopup": "menu", "aria-label": `Font: ${current.label}`, title: "Handwriting font", style: { fontFamily: current.family } },
      h("span", { class: "font-name" }, current.label),
      icon("down"),
    );
    btn.addEventListener("click", () =>
      this.openFontMenu(btn, current.id, (font) => {
        this.textFont = font;
        setPref(TEXT_FONT_KEY, font);
        this.applyToEditing({ font });
        this.renderToolbar();
      }),
    );
    return btn;
  }

  private openFontMenu(anchor: HTMLElement, current: FontId | null, pick: (font: FontId) => void) {
    void ensureFonts(FONT_PRESETS.map((f) => f.id)); // so the menu previews render in their own faces
    popupMenu(
      anchor,
      FONT_PRESETS.map((f) => ({ label: f.label, fontFamily: f.family, checked: f.id === current, action: () => pick(f.id) })),
    );
  }

  /**
   * Bold / italic / list / alignment buttons. They show and change the text
   * box being edited, or set the defaults for the next new one.
   */
  private formatControls(): HTMLElement {
    const target = this.editing?.el;
    const f = target ? { bold: !!target.bold, italic: !!target.italic, align: target.align ?? "left" } : this.textFormat;
    const list = target ? currentList(this.editing!.input.value) : null;
    const btn = (label: string, content: HTMLElement | string, active: boolean, onclick: () => void, cls = "") =>
      h("button", { class: `tool-btn small fmt-btn ${cls}${active ? " active" : ""}`, "aria-pressed": String(active), "aria-label": label, title: label, onclick }, content);
    return h(
      "div",
      { class: "tool-group options", role: "group", "aria-label": "Text format" },
      btn("Bold (Ctrl+B)", h("b", {}, "B"), f.bold, () => this.applyFormat({ bold: !f.bold })),
      btn("Italic (Ctrl+I)", h("i", {}, "I"), f.italic, () => this.applyFormat({ italic: !f.italic }), "serif"),
      this.paragraphChip(f.align, list),
    );
  }

  /** Lists and alignment, tucked behind one chip that shows the current alignment. */
  private paragraphChip(align: TextAlign, list: ListKind | null): HTMLElement {
    const alignIcon = { left: "alignLeft", center: "alignCenter", right: "alignRight" } as const;
    const chipBtn = h(
      "button",
      { class: "choice-chip", "aria-haspopup": "dialog", "aria-label": "Paragraph: lists and alignment", title: "Lists & alignment" },
      icon(alignIcon[align]),
      ...(list ? [icon(list === "bullet" ? "listBullet" : "listNumber")] : []),
      icon("down"),
    );
    // Keep the caret in the text box while picking.
    chipBtn.addEventListener("pointerdown", (e) => e.preventDefault());
    chipBtn.addEventListener("click", () => {
      let close = () => {};
      const opt = (label: string, art: HTMLElement, active: boolean, change: Parameters<EditorScreen["applyFormat"]>[0]) => {
        const b = h(
          "button",
          { class: `tool-btn small fmt-btn${active ? " active" : ""}`, "aria-pressed": String(active), "aria-label": label, title: label, onclick: () => (close(), this.applyFormat(change)) },
          art,
        );
        b.addEventListener("pointerdown", (e) => e.preventDefault());
        return b;
      };
      close = popover(
        chipBtn,
        h(
          "div",
          { class: "para-pop" },
          h("div", { class: "popover-title" }, "Lists"),
          h("div", { class: "para-row" }, opt("Bulleted list", icon("listBullet"), list === "bullet", { list: "bullet" }), opt("Numbered list", icon("listNumber"), list === "number", { list: "number" })),
          h("div", { class: "popover-title" }, "Alignment"),
          h(
            "div",
            { class: "para-row" },
            opt("Align left", icon("alignLeft"), align === "left", { align: "left" }),
            opt("Align centre", icon("alignCenter"), align === "center", { align: "center" }),
            opt("Align right", icon("alignRight"), align === "right", { align: "right" }),
          ),
        ),
      );
    });
    return chipBtn;
  }

  /** Applies formatting to the box being edited (and remembers it for new boxes). */
  private applyFormat(change: { bold?: boolean; italic?: boolean; align?: TextAlign; list?: ListKind }) {
    if (change.bold !== undefined) this.textFormat.bold = change.bold;
    if (change.italic !== undefined) this.textFormat.italic = change.italic;
    if (change.align) this.textFormat.align = change.align;
    const ed = this.editing;
    if (ed) {
      if (change.bold !== undefined) ed.el.bold = change.bold;
      if (change.italic !== undefined) ed.el.italic = change.italic;
      if (change.align) ed.el.align = change.align;
      if (change.list) {
        ed.input.value = toggleList(ed.input.value, change.list);
        ed.el.text = ed.input.value;
        ed.input.setSelectionRange(ed.input.value.length, ed.input.value.length);
      }
      this.positionEditor();
      ed.input.focus({ preventScroll: true });
    }
    this.renderToolbar();
  }

  /** Applies a font or size change to the text box being edited, if any. */
  private applyToEditing(patch: { font?: FontId; fontSize?: number }) {
    const ed = this.editing;
    if (!ed) return;
    if (patch.font) ed.el.font = patch.font;
    if (patch.fontSize !== undefined && (ed.el.kind === "text" || ed.el.kind === "sticky")) ed.el.fontSize = patch.fontSize;
    this.positionEditor();
    void ensureFonts([ed.el.font]);
    ed.input.focus({ preventScroll: true });
  }

  /** Waits for any handwriting fonts the elements use, then redraws. */
  private async loadFontsFor(elements: El[]) {
    const ids = elements.filter((el) => el.kind === "text" || el.kind === "sticky").map((el) => (el as TextEl | StickyEl).font);
    if (await ensureFonts(ids)) {
      this.rebuildCache();
      this.draw();
    }
  }

  private setTool(tool: Tool) {
    this.finishTextEdit();
    if (tool !== "laser") {
      this.laser.clear();
      this.resetLaserLayers();
    }
    if (tool !== "lasso") this.clearSelection();
    this.tool = tool;
    this.renderToolbar();
    this.draw();
  }

  private updateStatus() {
    const zoom = Math.round(this.zoom * 100);
    this.statusBar.replaceChildren(
      h("button", { class: "icon-btn small", "aria-label": "Previous page", onclick: () => this.goTo(this.index - 1), disabled: this.index === 0 }, icon("up")),
      h("span", { class: "status-page" }, `${this.index + 1} / ${this.pages.length}`),
      h("button", { class: "icon-btn small", "aria-label": "Next page", onclick: () => this.goTo(this.index + 1), disabled: this.index >= this.pages.length - 1 }, icon("down")),
      h("span", { class: "divider" }),
      h("button", { class: "icon-btn small", "aria-label": "Zoom out", onclick: () => this.setZoom(this.zoom / 1.2) }, icon("zoomOut")),
      h("span", { class: "zoom-label" }, `${zoom}%`),
      h("button", { class: "icon-btn small", "aria-label": "Zoom in", onclick: () => this.setZoom(this.zoom * 1.2) }, icon("zoomIn")),
      h("button", { class: "icon-btn small", "aria-label": "Fit width", title: "Fit width (Ctrl+0)", onclick: () => this.fitWidth() }, icon("fit")),
    );
    this.renderTemplateButton();
    this.pageChip.textContent = `Page ${this.index + 1} of ${this.pages.length}`;
    this.renderDaftarBar();
    this.pageCount.textContent = String(this.pages.length);
  }

  // ----------------------------------------------------------- page sidebar

  private renderPageList() {
    this.pageList.replaceChildren(
      ...this.pages.flatMap((p, i) => {
        const aspect = `${p.width} / ${p.height}`;
        const thumb = p.thumbnail ? h("img", { src: p.thumbnail, alt: "", style: { aspectRatio: aspect } }) : h("div", { class: "thumb-blank", style: { background: p.paperColor, aspectRatio: aspect } });
        const more = h("button", { class: "icon-btn small page-more", "aria-label": `Page ${i + 1} options` }, icon("more"));
        more.addEventListener("click", (e) => {
          e.stopPropagation();
          popupMenu(more, [
            { label: "Add page after", action: () => (this.daftar && p.section ? this.addDaftarPage(p.section) : this.addPage(i + 1)) },
            { label: "Duplicate page", action: () => this.duplicatePage(p) },
            { label: "Move up", action: () => this.movePage(p, i - 1) },
            { label: "Move down", action: () => this.movePage(p, i + 1) },
            { label: p.favourite ? "Remove bookmark" : "Bookmark page", action: () => this.toggleBookmark(p) },
            { label: "Delete page", danger: true, action: () => this.deletePage(p) },
          ]);
        });
        // A daftar's sidebar has a heading for each class's section.
        const heading = this.daftar && p.section && p.section !== this.pages[i - 1]?.section ? [h("li", { class: "page-section", role: "presentation" }, p.section)] : [];
        return [...heading, h(
          "li",
          { class: `page-item${i === this.index ? " active" : ""}` },
          h("button", { class: "page-thumb", "aria-label": `Go to page ${i + 1}`, "aria-current": i === this.index ? "page" : undefined, onclick: () => this.goTo(i) }, thumb),
          h("div", { class: "page-num" }, p.favourite ? "★ " : "", String(i + 1)),
          more,
        )];
      }),
    );
    this.pageList.querySelector(".active")?.scrollIntoView({ block: "nearest" });
  }

  private async reloadPages(selectIndex: number) {
    await this.flush();
    if (this.activeId) {
      this.elementCache.set(this.activeId, this.elements);
      this.history.set(this.activeId, { undo: this.undoStack, redo: this.redoStack });
    }
    this.activeId = null;
    this.pages = await api.pages(this.notebook.id);
    const ids = new Set(this.pages.map((p) => p.id));
    for (const id of [...this.elementCache.keys()]) if (!ids.has(id)) this.elementCache.delete(id);
    this.buildColumn();
    await this.loadPage(Math.max(0, Math.min(selectIndex, this.pages.length - 1)));
  }

  // ------------------------------------------------------------- daftar

  /** The class whose section the current page is in (or the first class). */
  private currentClass(): string | null {
    const d = this.daftar;
    if (!d) return null;
    const s = this.page?.section;
    return s && d.classes.includes(s) ? s : (d.classes[0] ?? null);
  }

  /** Header controls for a daftar: the class (switches section) and "New lesson". */
  private renderDaftarBar() {
    const d = this.daftar;
    this.daftarBar.hidden = !d;
    this.root.classList.toggle("is-daftar", !!d);
    if (!d) return;
    const cls = this.currentClass();
    const chip = h("button", { class: "daftar-class", "aria-haspopup": "menu", title: "Switch class · القسم" }, icon("book"), h("span", {}, cls ?? "—"), icon("down"));
    chip.addEventListener("click", () =>
      popupMenu(
        chip,
        d.classes.map((c) => ({ label: c, checked: c === cls, action: () => void this.goToClass(c) })),
      ),
    );
    const lesson = h("button", { class: "daftar-today", title: "Write today's date, the class and the lesson time on the next free line" }, icon("plus"), "حصة جديدة · New lesson");
    lesson.addEventListener("click", () => void this.newLesson());
    this.daftarBar.replaceChildren(chip, lesson);
  }

  /** Elements of a page (the live ones for the page being edited). */
  private async pageEls(p: Page): Promise<El[]> {
    if (p.id === this.activeId) return this.elements;
    return this.elementCache.get(p.id) ?? fromStored(await api.elements(p.id));
  }

  /** A class's lesson pages, and the one in use: the last with writing on it (else the first). */
  private async classPages(cls: string): Promise<{ pages: { p: Page; i: number }[]; current: number | null }> {
    const pages = this.pages.map((p, i) => ({ p, i })).filter(({ p }) => p.section === cls && isLessonTemplate(p.template));
    let current: number | null = pages[0]?.i ?? null;
    for (const { p, i } of pages) if ((await this.pageEls(p)).some((e) => !(e.kind === "text" && e.role === "daftar-field"))) current = i;
    return { pages, current };
  }

  /** Opens a class's current lesson page. */
  private async goToClass(cls: string) {
    const { current } = await this.classPages(cls);
    if (current !== null) this.goTo(current);
  }

  /**
   * A new lesson: today's date, the class and (from the timetable) the
   * lesson's time, written on the next free line of the class's current
   * lesson page — on a new page when that one is full. Undo removes it.
   */
  private async newLesson() {
    const d = this.daftar;
    const cls = this.currentClass();
    if (!d || !cls) return;
    try {
      await this.flush();
      const here = this.page;
      let { current } = await this.classPages(cls);
      if (here && here.section === cls && isLessonTemplate(here.template)) current = this.index;
      let target = current !== null ? this.pages[current] : null;
      let row: number | null = null;
      if (target) {
        const els = await this.pageEls(target);
        const ink = els.filter((e) => !(e.kind === "text" && e.role === "daftar-field")).map((e) => {
          const b = bounds(e);
          return { x0: b.x, y0: b.y, x1: b.x + b.w, y1: b.y + b.h };
        });
        row = nextFreeRow(ink, target.width, target.height);
      }
      if (!target || row === null) {
        const page = await addLessonPage(this.notebook, d, cls);
        await this.reloadPages(this.index);
        target = this.pages.find((p) => p.id === page.id) ?? null;
        row = 0;
      }
      if (!target) return;
      const i = this.pages.findIndex((p) => p.id === target!.id);
      if (i !== this.index || this.activeId !== target.id) await this.loadPage(i, false, false);
      // Today's lessons already logged on this page, so a second entry takes the next one.
      const today = isoDate(new Date());
      const logged = new Set(
        this.elements.flatMap((e) => (e.kind === "text" && e.role === "daftar-session" && e.session?.startsWith(today) ? [e.session.slice(11)] : [])),
      );
      const entry = lessonEntry(d, cls, row, target.width, target.height, new Date(), logged);
      this.snapshot();
      let z = nextZ(this.elements);
      for (const el of entry) this.elements.push({ ...el, z: z++ });
      this.changed();
      // Bring the new line into view.
      const L = lessonLayout(target.width, target.height);
      const slot = this.slots[i];
      if (slot) {
        this.ignoreScrollUntil = performance.now() + 500;
        this.scroller.scrollTop = slot.offsetTop + (L.bodyTop + row * L.step) * this.zoom - this.scroller.clientHeight * 0.35;
      }
      const now = new Date();
      const day = DAYS[now.getDay()];
      const time = entry[2]?.text; // date, class, then time when the timetable has one
      toast(`${cls} · ${day.ar} ${entry[0].text}${time ? ` · ${time}` : ""}`);
    } catch (e) {
      reportError(e);
    }
  }

  /** Adds a lesson page at the end of a class's section. */
  private async addDaftarPage(section: string | null) {
    const d = this.daftar;
    const cls = section && d?.classes.includes(section) ? section : null;
    if (!d || !cls) return this.addPage(this.pages.length);
    try {
      await this.flush();
      const page = await addLessonPage(this.notebook, d, cls);
      const pages = await api.pages(this.notebook.id);
      await this.reloadPages(Math.max(0, pages.findIndex((p) => p.id === page.id)));
    } catch (e) {
      reportError(e);
    }
  }

  private async addPage(at: number) {
    try {
      await this.flush();
      await api.addPage(this.notebook.id, at, null);
      await this.reloadPages(at);
    } catch (e) {
      reportError(e);
    }
  }

  private async duplicatePage(p: Page) {
    try {
      await this.flush();
      const copy = await api.duplicatePage(p.id);
      await this.reloadPages(copy.position);
    } catch (e) {
      reportError(e);
    }
  }

  private async movePage(p: Page, to: number) {
    if (to < 0 || to >= this.pages.length) return;
    try {
      await this.flush();
      await api.movePage(p.id, to);
      const currentId = this.page.id;
      const order = await api.pages(this.notebook.id);
      await this.reloadPages(order.findIndex((x) => x.id === currentId));
    } catch (e) {
      reportError(e);
    }
  }

  private async deletePage(p: Page) {
    if (this.pages.length <= 1) return toast("A notebook needs at least one page", "error");
    if (!(await confirmDialog("Delete this page?", "The page and everything on it will be deleted."))) return;
    try {
      const at = this.pages.indexOf(p);
      if (p.id === this.page.id) {
        window.clearTimeout(this.saveTimer);
        this.dirty = false;
      } else await this.flush();
      await api.deletePage(p.id);
      await this.reloadPages(at > this.index ? this.index : Math.max(0, this.index - 1));
    } catch (e) {
      reportError(e);
    }
  }

  private async toggleBookmark(p: Page) {
    try {
      await api.setPageFavourite(p.id, !p.favourite);
      p.favourite = !p.favourite;
      this.renderPageList();
    } catch (e) {
      reportError(e);
    }
  }

  /** Changes paper style and/or colour for this page, or every page. */
  private async setPaper(change: { template?: Template; color?: string }, allPages: boolean) {
    try {
      await this.flush();
      // "All pages" leaves the cover alone, and the cover design isn't a paper for other pages.
      if (allPages && change.template && isCoverTemplate(change.template)) {
        toast("The cover design stays on the cover — pick a paper on another page to apply it to all pages");
        return;
      }
      const targets = allPages ? this.pages.filter((p) => !isCoverTemplate(p.template)) : [this.page];
      for (const p of targets) {
        const template = change.template ?? p.template;
        await api.setPageTemplate(p.id, template, change.color ?? null);
        p.template = template;
        if (change.color) p.paperColor = change.color;
      }
      this.renderTemplateButton();
      this.rebuildCache();
      this.draw();
      this.scheduleSave(); // refreshes this page's thumbnail
      this.sizeSlots();
      if (allPages) {
        for (const i of this.visibleSlots) if (i !== this.index) this.renderStatic(i);
        await this.refreshThumbnails(targets.filter((p) => p.id !== this.page.id));
        toast(`Paper applied to all ${targets.length} pages`);
      }
    } catch (e) {
      reportError(e);
    }
  }

  /** Re-renders stored thumbnails for pages not currently open. */
  private async refreshThumbnails(pages: Page[]) {
    for (const p of pages) {
      const els = fromStored(await api.elements(p.id));
      await preloadImages(els, this.images);
      const bg = await this.backgroundNow(p, thumbScale(p));
      const thumb = renderPage(p.template, p.paperColor, p.width, p.height, els, thumbScale(p), this.images, bg).toDataURL("image/jpeg", 0.82);
      await api.setPageThumbnail(p.id, thumb);
      p.thumbnail = thumb;
    }
    this.renderPageList();
  }

  private async setTemplate(template: Template) {
    try {
      await api.setPageTemplate(this.page.id, template, null);
      this.page.template = template;
      this.renderTemplateButton();
      this.rebuildCache();
      this.draw();
      this.scheduleSave();
    } catch (e) {
      reportError(e);
    }
  }

  private async renameNotebook() {
    const r = await formDialog("Rename notebook", [{ name: "title", label: "Title", value: this.notebook.title }]);
    if (!r) return;
    try {
      await api.renameNotebook(this.notebook.id, r.title);
      this.notebook.title = r.title.trim();
      this.titleBtn.textContent = this.notebook.title;
    } catch (e) {
      reportError(e);
    }
  }

  // -------------------------------------------------------- layout & render

  private setZoom(z: number) {
    const prev = this.zoom;
    this.zoom = Math.max(0.25, Math.min(4, z));
    const cx = (this.scroller.scrollLeft + this.scroller.clientWidth / 2) / prev;
    const cy = (this.scroller.scrollTop + this.scroller.clientHeight / 2) / prev;
    this.layout();
    this.scroller.scrollLeft = cx * this.zoom - this.scroller.clientWidth / 2;
    this.scroller.scrollTop = cy * this.zoom - this.scroller.clientHeight / 2;
  }

  private fitWidth() {
    if (!this.page) return;
    const before = this.page;
    const available = Math.max(200, this.scroller.clientWidth - 64);
    this.zoom = Math.max(0.25, Math.min(2, available / Math.max(...this.pages.map((p) => p.width))));
    this.layout();
    this.scrollToPage(this.pages.indexOf(before));
  }

  private onWheel(e: WheelEvent) {
    if (!e.ctrlKey && this.ruler.inWheelSession()) {
      e.preventDefault();
      this.ruler.wheelRotate(e);
      return;
    }
    if (!(e.ctrlKey || e.metaKey)) return;
    e.preventDefault();
    this.zoomAt(this.zoom * Math.exp(-e.deltaY * 0.0025), { x: e.clientX, y: e.clientY });
  }

  /** Zooms so that the page point under `client` (or `anchor`) stays under `client`. */
  private zoomAt(z: number, client: { x: number; y: number }, anchor?: P2) {
    const pagePt = anchor ?? this.point({ clientX: client.x, clientY: client.y });
    this.zoom = Math.max(0.25, Math.min(4, z));
    this.layout();
    const r = this.canvas.getBoundingClientRect();
    this.scroller.scrollLeft += r.left + pagePt[0] * this.zoom - client.x;
    this.scroller.scrollTop += r.top + pagePt[1] * this.zoom - client.y;
  }

  private layout() {
    if (!this.page) return;
    this.sizeSlots();
    if (this.staticZoom !== this.zoom) this.scheduleStatics();
    const dpr = window.devicePixelRatio || 1;
    const w = this.page.width * this.zoom;
    const hgt = this.page.height * this.zoom;
    this.canvas.style.width = `${w}px`;
    this.canvas.style.height = `${hgt}px`;
    this.pageWrap.style.width = `${w}px`;
    this.pageWrap.style.height = `${hgt}px`;
    // Cap the backing store so very high zoom levels stay within canvas limits.
    const scale = Math.min(this.zoom * dpr, 8192 / Math.max(this.page.width, this.page.height));
    this.overlayRect = null;
    this.canvas.width = Math.round(this.page.width * scale);
    this.canvas.height = Math.round(this.page.height * scale);
    this.cache.width = this.canvas.width;
    this.cache.height = this.canvas.height;
    this.rebuildCache();
    this.draw();
    this.updateStatus();
    if (this.editing) this.positionEditor();
    if (this.tool === "laser") this.renderLaser();
    if (this.ruler.visible) {
      this.placeRulerLayer();
      this.ruler.layout();
    }
  }

  private get pixelScale() {
    return this.canvas.width / this.page.width;
  }

  /** Re-renders the committed content (paper + elements) into the cache. */
  private rebuildCache() {
    this.overlayRect = null; // the page underneath changed: repaint it all
    const c = this.cache.getContext("2d")!;
    c.setTransform(1, 0, 0, 1, 0, 0);
    c.clearRect(0, 0, this.cache.width, this.cache.height);
    c.setTransform(this.pixelScale, 0, 0, this.pixelScale, 0, 0);
    drawTemplate(c, this.page.template, this.page.width, this.page.height, this.page.paperColor);
    const page = this.page;
    const bg = this.background(page, this.pixelScale, () => {
      if (this.page === page) {
        this.rebuildCache();
        this.draw();
      }
    });
    if (bg) c.drawImage(bg, 0, 0, page.width, page.height);
    for (const el of this.elements) {
      if (this.hiddenIds.has(el.id)) continue;
      drawElement(c, el, this.images, () => {
        this.rebuildCache();
        this.draw();
      });
    }
  }

  /** Composites the cache with transient overlays (live ink, lasso, selection). */
  /**
   * The part of the canvas (device pixels) that differs from the cache, or
   * null when that is unknown (then the next draw repaints everything).
   */
  private overlayRect: { x0: number; y0: number; x1: number; y1: number } | null = null;
  private drawFrame = 0;

  /** Draws on the next frame (at most once per frame, however many pen events arrive). */
  private requestDraw() {
    if (this.drawFrame) return;
    this.drawFrame = requestAnimationFrame(() => {
      this.drawFrame = 0;
      if (this.root.isConnected) this.draw();
    });
  }

  /**
   * When the only thing over the page is the stroke being written (or the
   * eraser ring), its bounds in device pixels: then a redraw repaints just
   * that area instead of the whole page, which is what keeps writing fast
   * on big HiDPI canvases. Null when anything else is drawn on top.
   */
  private liveOverlayRect(): { x0: number; y0: number; x1: number; y1: number } | null {
    const g = this.gesture;
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    const add = (x: number, y: number, pad: number) => {
      x0 = Math.min(x0, x - pad);
      y0 = Math.min(y0, y - pad);
      x1 = Math.max(x1, x + pad);
      y1 = Math.max(y1, y + pad);
    };
    if (g) {
      if (g.type !== "draw" || g.preview || this.selection.size) return null;
      const pad = this.widths[g.tool] * 2 + 4 / this.zoom;
      // Only the unfrozen tail changes (see freezeLiveStroke).
      const pts = g.points;
      for (let i = Math.max(0, (g.frozen ?? 0) - 2); i < pts.length; i++) add(pts[i][0], pts[i][1], pad);
    } else if (this.selection.size) return null;
    if (this.tool === "eraser" && this.cursor) add(this.cursor[0], this.cursor[1], this.widths.eraser / 2 + 3 / this.zoom);
    const k = this.pixelScale;
    if (x1 < x0) return { x0: 0, y0: 0, x1: 0, y1: 0 }; // nothing over the page
    return {
      x0: Math.max(0, Math.floor(x0 * k)),
      y0: Math.max(0, Math.floor(y0 * k)),
      x1: Math.min(this.canvas.width, Math.ceil(x1 * k)),
      y1: Math.min(this.canvas.height, Math.ceil(y1 * k)),
    };
  }

  /**
   * Long strokes are drawn incrementally: once the stroke being written has
   * grown, its older part is drawn into the page cache for good and only the
   * last few points are redrawn each frame, so writing a long line costs the
   * same per frame as a short one. Opaque pens only (where pieces join
   * invisibly); the whole stroke is redrawn properly when it's finished.
   */
  private freezeLiveStroke() {
    const g = this.gesture;
    if (g?.type !== "draw" || g.preview || g.tool !== "pen") return;
    if (!["fountain", "ballpoint", "brush"].includes(this.penStyle)) return;
    const CHUNK = 32;
    const KEEP = 12;
    if (g.points.length - (g.frozen ?? 0) <= CHUNK + KEEP) return;
    // Pieces are drawn from the smoothed points so they line up exactly.
    const smooth = (g.smooth = smoothPoints(g.points, g.smooth));
    const c = this.cache.getContext("2d")!;
    while (g.points.length - (g.frozen ?? 0) > CHUNK + KEEP) {
      const from = g.frozen ?? 0;
      const to = g.points.length - KEEP;
      c.setTransform(this.pixelScale, 0, 0, this.pixelScale, 0, 0);
      drawStroke(
        c,
        { id: "live", z: 0, kind: "stroke", tool: "pen", style: this.penStyle, color: this.colors.pen, width: this.widths.pen, points: smooth.slice(from, to + 1) },
        false,
        { first: from === 0, last: false },
      );
      g.frozen = to;
    }
  }

  private draw() {
    if (!this.page) return;
    if (this.drawFrame) {
      cancelAnimationFrame(this.drawFrame);
      this.drawFrame = 0;
    }
    const ctx = this.ctx;
    this.freezeLiveStroke();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    const live = this.liveOverlayRect();
    const prev = this.overlayRect;
    this.overlayRect = live;
    if (live && prev) {
      // Repaint only what changed: where the overlay was and where it is now.
      const x0 = Math.min(live.x0, prev.x0), y0 = Math.min(live.y0, prev.y0);
      const x1 = Math.max(live.x1, prev.x1), y1 = Math.max(live.y1, prev.y1);
      if (x1 <= x0 || y1 <= y0) return;
      ctx.save();
      ctx.beginPath();
      ctx.rect(x0, y0, x1 - x0, y1 - y0);
      ctx.clip();
      ctx.clearRect(x0, y0, x1 - x0, y1 - y0);
      ctx.drawImage(this.cache, x0, y0, x1 - x0, y1 - y0, x0, y0, x1 - x0, y1 - y0);
      ctx.setTransform(this.pixelScale, 0, 0, this.pixelScale, 0, 0);
      this.drawOverlay(ctx);
      ctx.restore();
      return;
    }
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.drawImage(this.cache, 0, 0);
    ctx.setTransform(this.pixelScale, 0, 0, this.pixelScale, 0, 0);
    this.drawOverlay(ctx);
  }

  /** Everything drawn over the cached page: the gesture in progress, selection, handles, eraser ring. */
  private drawOverlay(ctx: CanvasRenderingContext2D) {
    const g = this.gesture;
    if (g?.type === "draw") {
      if (g.preview && g.tool === "highlighter" && g.preview.shape.kind === "line") {
        // A straightened highlight looks exactly like the highlight it becomes.
        drawStroke(ctx, this.straightHighlight(g, g.preview.shape.points));
      } else if (g.preview) {
        drawElement(ctx, g.preview, this.images, () => {});
        if (g.held) this.drawHeldHint(ctx, g.preview, g.held.mode);
      }
      else
        drawStroke(ctx, {
          id: "live",
          z: 0,
          kind: "stroke",
          tool: g.tool === "highlighter" ? "highlighter" : "pen",
          style: g.tool === "pen" ? this.penStyle : undefined,
          color: this.colors[g.tool],
          width: this.widths[g.tool],
          points: g.frozen ? smoothPoints(g.points, g.smooth).slice(g.frozen) : g.points,
        }, true, g.frozen ? { first: false, last: true } : undefined);
    } else if (g?.type === "shape") {
      if (g.preview) drawElement(ctx, g.preview, this.images, () => {});
    } else if (g?.type === "lasso") {
      drawLasso(ctx, g.points, this.zoom);
    } else if (g?.type === "move") {
      for (const el of g.original) drawElement(ctx, translate(el, g.dx, g.dy), this.images, () => {});
      drawGuides(ctx, g.guides, this.zoom);
    } else if (g?.type === "handle") {
      drawElement(ctx, g.preview, this.images, () => {});
    } else if (g?.type === "transform") {
      for (const el of g.previews) drawElement(ctx, el, this.images, () => {});
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
    if (this.tool === "eraser" && this.cursor) {
      ctx.save();
      ctx.strokeStyle = "rgba(100,100,100,0.8)";
      ctx.lineWidth = 1 / this.zoom;
      ctx.beginPath();
      ctx.arc(this.cursor[0], this.cursor[1], this.widths.eraser / 2, 0, Math.PI * 2);
      ctx.stroke();
      ctx.restore();
    }
    this.positionSelectionBar();
  }

  /** Redraws the laser layer on the next frame (at most once per frame). */
  private renderLaser() {
    if (this.laserFrame) return;
    this.laserFrame = requestAnimationFrame(() => {
      this.laserFrame = 0;
      if (this.root.isConnected) this.paintLaser();
    });
  }

  private paintLaser() {
    const layer = this.laserLayer;
    const area = layer.parentElement!.getBoundingClientRect();
    const view = this.scroller.getBoundingClientRect();
    const page = this.canvas.getBoundingClientRect();
    // Cover only the visible part of the scroller (scrollbars excluded).
    const w = this.scroller.clientWidth;
    const hgt = this.scroller.clientHeight;
    const dpr = window.devicePixelRatio || 1;
    const z = page.width / this.page.width;
    const ox = page.left - view.left;
    const oy = page.top - view.top;
    const key = [w, hgt, dpr, z, ox, oy].map((v) => v.toFixed(2)).join();
    const canvases = [this.laserGlow, this.laserTube, this.laserCore];
    const ctxs = canvases.map((c) => c.getContext("2d")!);
    const layers: LaserLayers = { glow: ctxs[0], tube: ctxs[1], core: ctxs[2] };
    if (key !== this.laserView) {
      // The page moved (scroll, zoom, resize): repaint everything once.
      this.laserView = key;
      layer.style.left = `${view.left - area.left}px`;
      layer.style.top = `${view.top - area.top}px`;
      layer.style.width = `${w}px`;
      layer.style.height = `${hgt}px`;
      for (const c of canvases) {
        const cw = Math.round(w * dpr);
        const ch = Math.round(hgt * dpr);
        if (c.width !== cw || c.height !== ch) {
          c.width = cw;
          c.height = ch;
        }
      }
      for (const ctx of ctxs) {
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
        ctx.setTransform(dpr * z, 0, 0, dpr * z, dpr * ox, dpr * oy);
      }
      this.laser.drawAll(layers, z);
    } else {
      // Only the new bits of curve.
      this.laser.drawNew(layers, z);
    }
    this.positionLaserDot();
  }

  /** Clears the laser layers (after a fade or when leaving the tool). */
  private resetLaserLayers() {
    this.laserView = "";
    this.renderLaser();
  }

  /** The pointer dot is a small element moved by the browser: no drawing at all. */
  private positionLaserDot() {
    const d = this.laser.dot;
    const dot = this.laserDot;
    if (!d || this.tool !== "laser") {
      dot.hidden = true;
      return;
    }
    const area = dot.parentElement!.getBoundingClientRect();
    const page = this.canvas.getBoundingClientRect();
    const z = page.width / this.page.width;
    dot.hidden = false;
    dot.style.setProperty("--laser", this.colors.laser);
    dot.style.transform = `translate(${page.left - area.left + d[0] * z}px, ${page.top - area.top + d[1] * z}px)`;
  }

  /** Shows the laser drawing at full strength (new stroke, or drawing again mid-fade). */
  private laserWake() {
    window.clearTimeout(this.laserFadeTimer);
    this.laserLayer.classList.remove("fading");
  }

  /** Shortly after the pen lifts, fade the drawing out (colour first, then the core), then clear it. */
  private laserScheduleFade() {
    window.clearTimeout(this.laserFadeTimer);
    this.laserFadeTimer = window.setTimeout(() => {
      this.laserLayer.classList.add("fading");
      this.laserFadeTimer = window.setTimeout(() => {
        if (this.laser.drawing) return;
        this.laser.clearStrokes();
        this.resetLaserLayers();
        this.laserLayer.classList.remove("fading");
      }, LASER_CORE_FADE_MS + 40);
    }, LASER_HOLD_MS);
  }

  // ------------------------------------------------------- state mutations

  private snapshot() {
    this.undoStack.push(JSON.stringify(this.elements));
    if (this.undoStack.length > MAX_UNDO) this.undoStack.shift();
    this.redoStack = [];
  }

  private changed() {
    this.rebuildCache();
    this.draw();
    this.scheduleSave();
  }

  private undo() {
    this.finishTextEdit();
    const prev = this.undoStack.pop();
    if (prev === undefined) return;
    this.redoStack.push(JSON.stringify(this.elements));
    this.elements = JSON.parse(prev);
    this.selection.clear();
    this.changed();
  }

  private redo() {
    this.finishTextEdit();
    const next = this.redoStack.pop();
    if (next === undefined) return;
    this.undoStack.push(JSON.stringify(this.elements));
    this.elements = JSON.parse(next);
    this.selection.clear();
    this.changed();
  }

  private addElement(el: El) {
    this.snapshot();
    this.elements.push(el);
    this.changed();
  }

  private scheduleSave() {
    this.dirty = true;
    window.clearTimeout(this.saveTimer);
    this.saveTimer = window.setTimeout(() => this.saveNow(), 500);
  }

  private saveNow(): Promise<void> {
    window.clearTimeout(this.saveTimer);
    if (!this.dirty) return this.saving;
    this.dirty = false;
    const page = this.page;
    const elements = this.elements.map(toStored);
    const scale = thumbScale(page);
    const bg = page.backgroundAsset && page.backgroundPage !== null ? cachedBackground(page.backgroundAsset, page.backgroundPage)?.canvas ?? null : null;
    const thumb = renderPage(page.template, page.paperColor, page.width, page.height, this.elements, scale, this.images, bg).toDataURL("image/jpeg", 0.82);
    this.saving = this.saving
      .then(async () => {
        await api.saveElements(page.id, elements);
        await api.setPageThumbnail(page.id, thumb);
        page.thumbnail = thumb;
        const img = this.pageList.children[this.pages.indexOf(page)]?.querySelector(".page-thumb");
        if (img) img.replaceChildren(h("img", { src: thumb, alt: "" }));
      })
      .catch((e) => {
        this.dirty = true;
        reportError(e);
      });
    return this.saving;
  }

  // ------------------------------------------------------------- pointer

  private bindCanvas() {
    const c = this.canvas;
    // A tap on the page while a menu or the options drawer is open only
    // closes it: nothing is drawn. This runs before the menus close
    // themselves, so it can still see them.
    let dismissTap = false;
    window.addEventListener(
      "pointerdown",
      (e) => {
        dismissTap = this.root.isConnected && this.scroller.contains(e.target as Node) && this.menuOpen();
      },
      true,
    );
    c.addEventListener("pointerdown", (e) => {
      if (dismissTap) {
        dismissTap = false;
        e.preventDefault();
        if (this.drawerOpen && !this.drawerPinned) this.toggleDrawer(false);
        return;
      }
      this.onDown(e);
      // While writing, the floating pills fade so they never get in the way.
      if (this.gesture) this.root.classList.add("drawing");
    });
    const endDrawing = () => !this.gesture && this.root.classList.remove("drawing");
    window.addEventListener("pointerup", () => queueMicrotask(endDrawing));
    window.addEventListener("pointercancel", () => queueMicrotask(endDrawing));
    c.addEventListener("pointermove", (e) => this.onMove(e));
    c.addEventListener("pointerup", (e) => this.onUp(e));
    c.addEventListener("pointercancel", (e) => {
      if (this.navPointers.has(e.pointerId)) this.navUp(e);
      else this.cancelGesture();
    });
    c.addEventListener("pointerleave", () => {
      this.cursor = null;
      this.laser.pointAt(null);
      this.positionLaserDot();
      if (!this.gesture) this.draw();
    });
    c.addEventListener("dblclick", (e) => {
      const [x, y] = this.point(e);
      const hit = [...this.elements].reverse().find((el) => (el.kind === "text" || el.kind === "sticky") && rectContains(bounds(el), x, y));
      if (hit && (hit.kind === "text" || hit.kind === "sticky")) this.startTextEdit(hit, false);
    });
  }

  private point(e: { clientX: number; clientY: number }): P2 {
    const r = this.canvas.getBoundingClientRect();
    return [(e.clientX - r.left) / this.zoom, (e.clientY - r.top) / this.zoom];
  }

  /** A speed tracker for pens whose width follows drawing speed (fountain, brush). */
  private newInk(tool: "pen" | "highlighter" | "shape"): SpeedInk | null {
    return tool === "pen" && speedSensitive(this.penStyle) ? new SpeedInk() : null;
  }

  /** The pressure value stored for a point: stylus pressure, shaped by speed where it applies. */
  private inkPressure(ink: SpeedInk | null, x: number, y: number, e: PointerEvent): number {
    if (!ink) return this.pressure(e);
    return ink.next(x, y, e.timeStamp, e.pointerType === "pen" && e.pressure > 0 ? e.pressure : null);
  }

  private pressure(e: PointerEvent): number {
    if (e.pointerType === "pen") return e.pressure > 0 ? e.pressure : 0.5;
    return 0.5;
  }

  private onDown(e: PointerEvent) {
    if (e.pointerType === "pen") this.notePen();
    // Pen-only mode: anything that isn't the pen navigates instead of writing.
    // (A mouse can still present with the laser.)
    if (this.penOnly && e.pointerType !== "pen" && !(this.tool === "laser" && e.pointerType === "mouse")) {
      this.navDown(e);
      return;
    }
    if (e.button !== 0 && e.pointerType === "mouse") return;
    if (this.gesture) return;
    this.finishTextEdit();
    const [x, y] = this.point(e);
    const tool = this.tool;
    this.canvas.setPointerCapture(e.pointerId);

    switch (tool) {
      case "laser":
        this.gesture = { type: "laser" };
        this.laser.color = this.colors.laser;
        this.laserWake();
        this.laser.begin([x, y]);
        this.renderLaser();
        return; // no page redraw for the laser
      case "pen":
      case "highlighter":
      case "shape": {
        // Starting next to the ruler: the stroke runs along its edge.
        const edge = this.ruler.edgeNear([x, y], 26 / this.zoom);
        if (edge) {
          // Ink sits against the edge (a line shape is centred on it too).
          const offset = this.widths[tool] / 2 + 0.5 / this.zoom;
          const [px, py] = Ruler.project(edge, [x, y], offset);
          const ink = this.newInk(tool);
          this.gesture = { type: "draw", tool, points: [[px, py, this.inkPressure(ink, px, py, e)]], times: [e.timeStamp], preview: null, holdTimer: 0, held: null, ruler: { edge, offset }, ink };
        } else if (tool === "shape" && this.shapeKind !== "auto") {
          this.gesture = { type: "shape", start: [x, y], preview: null };
        } else {
          // Freehand ink; with the shape tool in auto mode it is recognised on lift.
          const ink = this.newInk(tool);
          this.gesture = { type: "draw", tool, points: [[x, y, this.inkPressure(ink, x, y, e)]], times: [e.timeStamp], preview: null, holdTimer: 0, held: null, ink };
          this.armHold();
        }
        break;
      }
      case "eraser":
        this.gesture = { type: "erase", changed: false, last: null };
        this.eraseAlong([x, y]);
        break;
      case "lasso": {
        const single = this.singleSegment();
        const handle = single ? handleAt(single, x, y, 10 / this.zoom) : null;
        if (single && handle) {
          this.gesture = { type: "handle", handle, original: single, preview: single };
          this.hiddenIds.add(single.id);
          this.rebuildCache();
          break;
        }
        // Pressing on the selected content itself always moves it; the
        // handles sit just outside, so small selections stay easy to drag.
        const content = this.selectionBounds();
        const onContent = !!content && rectContains(content, x, y);
        const tbox = single || onContent ? null : this.transformBox();
        const gh = tbox ? groupHandleAt(tbox, x, y, this.zoom) : null;
        if (tbox && gh) {
          const originals = this.elements.filter((el) => this.selection.has(el.id));
          this.gesture = { type: "transform", handle: gh, start: [x, y], box: tbox, originals, previews: originals, m: { a: 1, b: 0, c: 0, d: 1, e: 0, f: 0 } };
          for (const el of originals) this.hiddenIds.add(el.id);
          this.rebuildCache();
          break;
        }
        const sel = this.selectionBounds();
        if (sel && rectContains({ x: sel.x - 8, y: sel.y - 8, w: sel.w + 16, h: sel.h + 16 }, x, y)) {
          const original = this.elements.filter((el) => this.selection.has(el.id));
          this.gesture = { type: "move", start: [x, y], original, dx: 0, dy: 0, snap: this.buildSnap(original), guides: [] };
          for (const el of original) this.hiddenIds.add(el.id);
          this.rebuildCache();
        } else {
          this.selection.clear();
          this.gesture = { type: "lasso", points: [[x, y]] };
        }
        break;
      }
      case "text":
      case "sticky": {
        // Keep the browser from moving focus away from the new text editor.
        e.preventDefault();
        const existing = [...this.elements].reverse().find((el) => el.kind === tool && rectContains(bounds(el), x, y));
        if (existing && (existing.kind === "text" || existing.kind === "sticky")) {
          this.startTextEdit(existing, false);
        } else if (tool === "text") {
          const el: TextEl = { id: newId(), z: nextZ(this.elements), kind: "text", x, y: y - 12, w: Math.min(420, this.page.width - x - 20), text: "", fontSize: this.widths.text, color: this.colors.text, font: this.textFont, ...this.textFormat };
          el.w = Math.max(120, el.w);
          this.startTextEdit(el, true);
        } else {
          const el: StickyEl = { id: newId(), z: nextZ(this.elements), kind: "sticky", x: x - 20, y: y - 20, w: 220, h: 200, text: "", color: this.colors.sticky, font: this.textFont, fontSize: this.widths.sticky, ...this.textFormat };
          this.startTextEdit(el, true);
        }
        this.canvas.releasePointerCapture(e.pointerId);
        break;
      }
      case "hand":
        this.gesture = { type: "pan", sx: e.clientX, sy: e.clientY, left: this.scroller.scrollLeft, top: this.scroller.scrollTop };
        break;
    }
    this.draw();
  }

  private onMove(e: PointerEvent) {
    if (e.pointerType === "pen") this.notePen();
    if (this.navPointers.has(e.pointerId)) {
      this.navMove(e);
      return;
    }
    const g = this.gesture;
    this.cursor = this.point(e);
    if (!g) {
      if (this.tool === "laser" && e.pointerType !== "touch") {
        this.laser.pointAt(this.cursor);
        this.positionLaserDot();
      }
      if (this.tool === "eraser") this.requestDraw();
      if (this.tool === "lasso") this.updateHoverCursor();
      return;
    }
    const events = typeof e.getCoalescedEvents === "function" ? e.getCoalescedEvents() : [];
    const samples = events.length ? events : [e];
    switch (g.type) {
      case "laser": {
        let changed = false;
        for (const s of samples) changed = this.laser.extend(this.point(s), 0.75 / this.zoom, s.timeStamp, this.zoom) || changed;
        if (changed) this.renderLaser();
        else this.positionLaserDot();
        return; // the page itself doesn't change: skip the page redraw
      }
      case "draw": {
        if (g.preview) {
          // Refined shape stays under the pen: rotate/resize or move it.
          if (g.held) this.updateHeld(g, this.cursor);
          break;
        }
        for (const s of samples) {
          let [x, y] = this.point(s);
          if (g.ruler) [x, y] = Ruler.project(g.ruler.edge, [x, y], g.ruler.offset);
          const last = g.points[g.points.length - 1];
          if (Math.hypot(x - last[0], y - last[1]) < 0.4 / this.zoom) continue;
          g.points.push([x, y, this.inkPressure(g.ink ?? null, x, y, s)]);
          g.times.push(s.timeStamp);
        }
        if (g.ruler) this.ruler.setReadout(this.rulerReadout(g));
        else this.armHold();
        break;
      }
      case "erase":
        for (const s of samples) this.eraseAlong(this.point(s));
        break;
      case "lasso":
        g.points.push(this.point(e));
        break;
      case "move":
        g.dx = this.cursor[0] - g.start[0];
        g.dy = this.cursor[1] - g.start[1];
        g.guides = [];
        if (g.snap && !e.altKey) {
          const snapped = snapMove(g.snap.anchors, g.snap.targets, g.dx, g.dy, 7 / this.zoom);
          g.dx = snapped.dx;
          g.dy = snapped.dy;
          g.guides = snapped.guides;
        }
        break;
      case "handle":
        g.preview = resizeElement(g.original, g.handle, this.cursor, e.shiftKey);
        break;
      case "transform": {
        const t = groupTransform(g.box, g.handle, g.start, this.cursor, e.shiftKey);
        g.m = t.m;
        g.previews = g.originals.map((el) => transformElement(el, t.m, t.sx, t.sy, t.angle));
        break;
      }
      case "shape": {
        const shape = buildShape(this.shapeKind, g.start, this.cursor, e.shiftKey);
        g.preview = shape ? this.makeShapeEl(shape) : null;
        break;
      }
      case "pan":
        this.scroller.scrollLeft = g.left - (e.clientX - g.sx);
        this.scroller.scrollTop = g.top - (e.clientY - g.sy);
        return;
    }
    this.requestDraw();
  }

  /** Draw-and-hold: keeping the pen still at the end of a stroke snaps it to a shape. */
  private armHold() {
    const g = this.gesture;
    if (g?.type !== "draw") return;
    window.clearTimeout(g.holdTimer);
    g.holdTimer = window.setTimeout(() => {
      if (this.gesture !== g || g.preview || g.points.length < 4) return;
      const now = performance.now();
      const times = [...g.times.slice(0, -1), Math.max(now, g.times[g.times.length - 1])];
      if (!isHeld(times, g.points, HOLD_MS - 50, 4 / this.zoom)) return;
      const shape = this.refine(g);
      if (!shape) return;
      g.preview = shape;
      if (g.frozen) this.rebuildCache(); // drop the stroke pieces drawn so far
      const last = g.points[g.points.length - 1];
      const anchor: P2 = [last[0], last[1]];
      g.held = { mode: "transform", base: shape, anchor, pivot: this.pivotFor(shape), still: anchor, stillTimer: 0 };
      this.armModeToggle(g);
      this.draw();
    }, HOLD_MS);
  }

  /**
   * Refines a stroke: the Shape tool's Auto mode always returns something
   * (shape, brace or smoothed path); the pen only snaps to shapes and braces
   * so handwriting is never altered; the highlighter only straightens.
   */
  private refine(g: Extract<Gesture, { type: "draw" }>): ShapeEl | null {
    const pts = g.points.map((p) => [p[0], p[1]] as P2);
    let shape: RecognizedShape | null;
    if (g.tool === "shape") shape = beautify(pts);
    else if (g.tool === "highlighter") {
      const s = recognizeShape(pts);
      const len = s?.kind === "line" ? Math.hypot(s.points[1][0] - s.points[0][0], s.points[1][1] - s.points[0][1]) : 0;
      // Short dabs stay as drawn: straightening them isn't useful.
      shape = s?.kind === "line" && len > Math.max(24 / this.zoom, this.widths.highlighter * 1.5) ? s : null;
    } else shape = recognizeShape(pts) ?? detectBrace(pts);
    if (!shape) return null;
    return { id: newId(), z: nextZ(this.elements), kind: "shape", shape, color: this.colors[g.tool], width: this.widths[g.tool], fill: null };
  }

  /** Length (straight edge) or swept angle (protractor arc) of a guided stroke. */
  private rulerReadout(g: Extract<Gesture, { type: "draw" }>): string {
    const edge = g.ruler!.edge;
    if (edge.kind === "line") {
      const [a, b] = this.rulerExtent(g);
      return `${(Math.hypot(b[0] - a[0], b[1] - a[1]) / (10 * MM)).toFixed(1)} cm`;
    }
    // Unwrap the angle around the centre as the stroke goes.
    let prev = Math.atan2(g.points[0][1] - edge.c[1], g.points[0][0] - edge.c[0]);
    let acc = 0;
    let lo = 0;
    let hi = 0;
    for (const p of g.points.slice(1)) {
      const a = Math.atan2(p[1] - edge.c[1], p[0] - edge.c[0]);
      let d = a - prev;
      if (d > Math.PI) d -= 2 * Math.PI;
      if (d < -Math.PI) d += 2 * Math.PI;
      acc += d;
      lo = Math.min(lo, acc);
      hi = Math.max(hi, acc);
      prev = a;
    }
    return `${Math.round(((hi - lo) * 180) / Math.PI)}°`;
  }

  /** The two furthest points of a stroke drawn along a straight edge. */
  private rulerExtent(g: Extract<Gesture, { type: "draw" }>): [P2, P2] {
    const edge = g.ruler!.edge;
    const dir = edge.kind === "line" ? edge.dir : [1, 0];
    let lo = g.points[0];
    let hi = g.points[0];
    const t = (p: Pt) => p[0] * dir[0] + p[1] * dir[1];
    for (const p of g.points) {
      if (t(p) < t(lo)) lo = p;
      if (t(p) > t(hi)) hi = p;
    }
    return [[lo[0], lo[1]], [hi[0], hi[1]]];
  }

  /** The straight highlighter stroke for a held highlight's line. */
  private straightHighlight(g: Extract<Gesture, { type: "draw" }>, line: P2[]): StrokeEl {
    const a = line[0];
    const b = line[line.length - 1];
    const p = g.points[0][2];
    return { id: "live", z: 0, kind: "stroke", tool: "highlighter", color: this.colors.highlighter, width: this.widths.highlighter, points: [[a[0], a[1], p], [b[0], b[1], p]] };
  }

  /** Lines pivot around their start (the end follows the pen); others around their centre. */
  private pivotFor(el: ShapeEl): P2 {
    const s = el.shape;
    if ((s.kind === "line" || s.kind === "arrow") && s.points.length) return s.points[0];
    const r = geomBox(el);
    return [r.x + r.w / 2, r.y + r.h / 2];
  }

  /** Holding still for a moment switches between rotate/resize and move. */
  private armModeToggle(g: Extract<Gesture, { type: "draw" }>) {
    const held = g.held;
    if (!held) return;
    window.clearTimeout(held.stillTimer);
    // The highlighter only straightens: its end follows the pen, no move mode.
    if (g.tool === "highlighter") return;
    held.stillTimer = window.setTimeout(() => {
      if (this.gesture !== g || !g.held || !g.preview) return;
      held.mode = held.mode === "transform" ? "move" : "transform";
      held.base = g.preview;
      held.anchor = held.still;
      held.pivot = this.pivotFor(g.preview);
      this.draw();
    }, HOLD_MS + 150);
  }

  private updateHeld(g: Extract<Gesture, { type: "draw" }>, p: P2) {
    const h = g.held!;
    if (Math.hypot(p[0] - h.still[0], p[1] - h.still[1]) > 4 / this.zoom) {
      h.still = p;
      this.armModeToggle(g);
    }
    let shape: RecognizedShape;
    if (h.mode === "move") {
      shape = transformShape(h.base.shape, h.pivot, 0, 1, p[0] - h.anchor[0], p[1] - h.anchor[1]);
    } else {
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

  /** Alignment anchors for moving text, and what they can line up with. */
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

  private async onUp(e: PointerEvent) {
    if (this.navPointers.has(e.pointerId)) {
      this.navUp(e);
      return;
    }
    const g = this.gesture;
    if (!g) return;
    if (this.canvas.hasPointerCapture(e.pointerId)) this.canvas.releasePointerCapture(e.pointerId);
    switch (g.type) {
      case "laser":
        this.gesture = null;
        this.laser.end();
        // A touch has no hover: hide the dot once the finger lifts.
        if (e.pointerType === "touch") this.laser.pointAt(null);
        this.renderLaser(); // the stroke's last bit
        if (this.laser.strokeCount) this.laserScheduleFade();
        break;
      case "draw": {
        window.clearTimeout(g.holdTimer);
        if (g.held) window.clearTimeout(g.held.stillTimer);
        // Keep the stroke on screen as drawn until it's added (a queued frame
        // would otherwise briefly show the page without it).
        if (this.drawFrame) {
          cancelAnimationFrame(this.drawFrame);
          this.drawFrame = 0;
        }
        let shape = g.preview;
        if (g.ruler) {
          this.ruler.setReadout(null);
          // Along a straight edge the shape tool makes a clean line between
          // the extremes; along the protractor's arc, the arc itself.
          if (g.tool === "shape") {
            const geom: RecognizedShape =
              g.ruler.edge.kind === "line" ? { kind: "line", points: this.rulerExtent(g) } : { kind: "path", points: g.points.map((p) => [p[0], p[1]] as P2), closed: false };
            shape = { id: newId(), z: 0, kind: "shape", shape: geom, color: this.colors.shape, width: this.widths.shape, fill: null };
          }
        } else if (!shape && g.tool === "shape") shape = this.refine(g);
        this.gesture = null;
        if (shape && g.tool === "highlighter" && shape.shape.kind === "line") {
          this.addElement({ ...this.straightHighlight(g, shape.shape.points), id: newId(), z: nextZ(this.elements) });
        } else if (shape) {
          shape.z = nextZ(this.elements);
          this.addElement(shape);
        } else {
          const tool = g.tool === "highlighter" ? "highlighter" : "pen";
          let points = tool === "highlighter" ? steadyHighlight(g.points, this.widths.highlighter) : g.points;
          try {
            if (points.length > 8) points = await api.simplifyStroke(points, 0.15);
          } catch {
            /* keep the raw points */
          }
          this.addElement({
            id: newId(),
            z: nextZ(this.elements),
            kind: "stroke",
            tool,
            style: tool === "pen" ? this.penStyle : undefined,
            color: this.colors[g.tool],
            width: this.widths[g.tool],
            points,
          });
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
          // A tap selects the topmost item under the pointer.
          const [tx, ty] = g.points[0];
          const hit = [...this.elements].reverse().find((el) => hitForSelect(el, tx, ty, 6 / this.zoom));
          if (hit) this.selection.add(hit.id);
        } else {
          for (const el of this.elements) if (insideLasso(el, g.points)) this.selection.add(el.id);
        }
        this.draw();
        break;
      }
      case "transform": {
        this.gesture = null;
        this.hiddenIds.clear();
        if (g.previews !== g.originals) {
          const next = new Map(g.previews.map((el) => [el.id, el]));
          this.snapshot();
          this.elements = this.elements.map((el) => next.get(el.id) ?? el);
          this.selectionBarKey = "";
          this.changed();
          void this.loadFontsFor(g.previews);
        } else {
          this.rebuildCache();
          this.draw();
        }
        break;
      }
      case "handle": {
        this.gesture = null;
        this.hiddenIds.delete(g.original.id);
        if (g.preview !== g.original) {
          this.snapshot();
          this.elements = this.elements.map((el) => (el.id === g.original.id ? g.preview : el));
          this.selectionBarKey = "";
          this.changed();
        } else {
          this.rebuildCache();
          this.draw();
        }
        break;
      }
      case "move": {
        this.gesture = null;
        this.hiddenIds.clear();
        if (Math.abs(g.dx) > 0.5 || Math.abs(g.dy) > 0.5) {
          this.snapshot();
          this.elements = this.elements.map((el) => (this.selection.has(el.id) ? translate(el, g.dx, g.dy) : el));
          this.changed();
        } else {
          this.rebuildCache();
          this.draw();
        }
        break;
      }
      case "pan":
        this.gesture = null;
        break;
      case "shape":
        this.gesture = null;
        if (g.preview) {
          g.preview.z = nextZ(this.elements);
          this.addElement(g.preview);
        } else this.draw();
        break;
    }
  }

  private makeShapeEl(shape: RecognizedShape): ShapeEl {
    const color = this.colors.shape;
    const closed = isClosedShape(shape);
    return { id: newId(), z: 0, kind: "shape", shape, color, width: this.widths.shape, fill: this.shapeFill && closed ? `${color}33` : null };
  }

  // ------------------------------------------------- pen-only navigation

  private navDown(e: PointerEvent) {
    // Palm rejection: ignore touches while the pen is writing or hovering.
    if (e.pointerType === "touch" && (this.gesture || performance.now() - this.lastPenAt < PALM_GUARD_MS)) return;
    this.canvas.setPointerCapture(e.pointerId);
    this.navPointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    this.startNav();
  }

  private startNav() {
    const pts = [...this.navPointers.entries()];
    if (pts.length >= 2) {
      const [a, b] = [pts[0][1], pts[1][1]];
      const mid = { clientX: (a.x + b.x) / 2, clientY: (a.y + b.y) / 2 };
      this.nav = { kind: "pinch", startDist: Math.max(10, Math.hypot(a.x - b.x, a.y - b.y)), startZoom: this.zoom, anchor: this.point(mid) };
    } else if (pts.length === 1) {
      const [id, p] = pts[0];
      this.nav = { kind: "pan", id, sx: p.x, sy: p.y, left: this.scroller.scrollLeft, top: this.scroller.scrollTop };
    } else {
      this.nav = null;
    }
  }

  private navMove(e: PointerEvent) {
    this.navPointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    const nav = this.nav;
    if (nav?.kind === "pan" && nav.id === e.pointerId) {
      this.scroller.scrollLeft = nav.left - (e.clientX - nav.sx);
      this.scroller.scrollTop = nav.top - (e.clientY - nav.sy);
    } else if (nav?.kind === "pinch" && !this.pinchFrame) {
      // Relayout at most once per frame while pinching.
      this.pinchFrame = requestAnimationFrame(() => {
        this.pinchFrame = 0;
        const pts = [...this.navPointers.values()];
        if (this.nav?.kind !== "pinch" || pts.length < 2) return;
        const [a, b] = pts;
        const dist = Math.hypot(a.x - b.x, a.y - b.y);
        this.zoomAt(this.nav.startZoom * (dist / this.nav.startDist), { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }, this.nav.anchor);
      });
    }
  }

  private navUp(e: PointerEvent) {
    this.navPointers.delete(e.pointerId);
    if (this.canvas.hasPointerCapture(e.pointerId)) this.canvas.releasePointerCapture(e.pointerId);
    this.startNav();
  }

  private cancelGesture() {
    this.ruler.setReadout(null);
    const g = this.gesture;
    if (g?.type === "draw") {
      window.clearTimeout(g.holdTimer);
      if (g.held) window.clearTimeout(g.held.stillTimer);
    }
    if (g?.type === "erase" && g.changed) this.changed();
    this.gesture = null;
    this.hiddenIds.clear();
    this.rebuildCache();
    this.draw();
  }

  /** Erases along the path from the previous eraser position, so fast swipes leave no gaps. */
  private eraseAlong(p: P2) {
    const g = this.gesture;
    if (g?.type !== "erase") return;
    const from = g.last ?? p;
    const step = Math.max(1, this.widths.eraser / 4);
    const n = Math.max(1, Math.ceil(Math.hypot(p[0] - from[0], p[1] - from[1]) / step));
    for (let i = g.last ? 1 : 0; i <= n; i++) this.eraseAt(from[0] + ((p[0] - from[0]) * i) / n, from[1] + ((p[1] - from[1]) * i) / n);
    g.last = p;
  }

  /**
   * Precise mode cuts strokes and shape outlines exactly where the eraser
   * touches; "whole stroke" mode removes anything touched. Text boxes and
   * sticky notes are removed whole; images are never erased.
   */
  private eraseAt(x: number, y: number) {
    const g = this.gesture;
    if (g?.type !== "erase") return;
    const radius = this.widths.eraser / 2;
    let changed = false;
    const next: El[] = [];
    for (const el of this.elements) {
      if (el.kind === "image") {
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
    this.rebuildCache();
  }

  // ------------------------------------------------------------ selection

  private selectionBounds(): Rect | null {
    if (this.selection.size === 0) return null;
    return unionRects(this.elements.filter((el) => this.selection.has(el.id)).map(bounds));
  }

  private clearSelection() {
    if (this.selection.size === 0) return;
    this.selection.clear();
    this.draw();
  }

  private selectAll() {
    this.setTool("lasso");
    for (const el of this.elements) this.selection.add(el.id);
    this.draw();
  }

  private positionSelectionBar() {
    const sel = this.gesture ? null : this.selectionBounds();
    const bar = this.selectionBar;
    if (!sel) {
      bar.hidden = true;
      return;
    }
    const key = [...this.selection].sort().join();
    if (bar.hidden || key !== this.selectionBarKey) {
      this.selectionBarKey = key;
      this.renderSelectionBar();
    }
    bar.hidden = false;
    // Keep the bar inside the visible part of the page area: above the
    // selection if there's room, else below it (clear of the rotation
    // knob), else pinned to the top of the view.
    const wrap = this.pageWrap.getBoundingClientRect();
    const view = this.scroller.getBoundingClientRect();
    const pad = 8;
    const visL = view.left - wrap.left + pad;
    const visR = view.right - wrap.left - pad;
    // Below the floating tool-options pill.
    const pill = this.optionsBar.getBoundingClientRect();
    const visT = Math.max(view.top, pill.height ? pill.bottom : 0) - wrap.top + pad;
    const visB = view.bottom - wrap.top - pad;
    bar.style.maxWidth = `${Math.max(160, visR - visL)}px`;
    // Measure at the left edge, so the old position doesn't squeeze the bar.
    bar.style.left = `${visL}px`;
    const bw = bar.offsetWidth;
    const bh = bar.offsetHeight;
    const selL = sel.x * this.zoom;
    const selT = sel.y * this.zoom;
    const selB = (sel.y + sel.h) * this.zoom;
    let top = selT - bh - 18;
    if (top < visT) top = selB + 44 + bh <= visB ? selB + 44 : visT;
    const left = Math.max(visL, Math.min(selL, visR - bw));
    bar.style.left = `${left}px`;
    bar.style.top = `${top}px`;
  }

  /** The selected element when exactly one reshapeable item is selected. */
  /** The selected shape when exactly one shape is selected (for the shape bar). */
  private singleShape(): ShapeEl | null {
    if (this.selection.size !== 1) return null;
    const el = this.elements.find((x) => this.selection.has(x.id));
    return el?.kind === "shape" ? el : null;
  }

  /** A single selected line or arrow: edited by dragging its two ends. */
  private singleSegment(): ShapeEl | null {
    const s = this.singleShape();
    return s && (s.shape.kind === "line" || s.shape.kind === "arrow") && isResizable(s) ? s : null;
  }

  /** The box with resize/rotate handles around the current selection. */
  private transformBox(): Rect | null {
    if (this.tool !== "lasso" || this.singleSegment()) return null;
    const sel = this.selectionBounds();
    return sel ? { x: sel.x - 6, y: sel.y - 6, w: sel.w + 12, h: sel.h + 12 } : null;
  }

  private updateHoverCursor() {
    const single = this.singleSegment();
    const c = this.cursor;
    let cursor = "";
    if (single && c) {
      const hd = handleAt(single, c[0], c[1], 10 / this.zoom);
      if (hd) cursor = HANDLE_CURSORS[hd];
    }
    const content = this.selectionBounds();
    const tbox = c && content && rectContains(content, c[0], c[1]) ? null : this.transformBox();
    if (!cursor && tbox && c) {
      const gh = groupHandleAt(tbox, c[0], c[1], this.zoom);
      if (gh) cursor = gh === "rot" ? "grab" : HANDLE_CURSORS[gh];
    }
    if (!cursor && c) {
      const sel = this.selectionBounds();
      if (sel && rectContains({ x: sel.x - 8, y: sel.y - 8, w: sel.w + 16, h: sel.h + 16 }, c[0], c[1])) cursor = "move";
    }
    this.canvas.style.cursor = cursor;
  }

  /** Applies a change to the single selected shape, as one undo step. */
  private updateSelectedShape(change: (el: El) => El, refreshBar = true) {
    const single = this.singleShape();
    if (!single) return;
    const next = change(single);
    if (next === single) return;
    this.snapshot();
    this.elements = this.elements.map((el) => (el.id === single.id ? next : el));
    if (refreshBar) this.selectionBarKey = "";
    this.changed();
  }

  /** Pen strokes in the selection (highlighter marks are ignored). */
  private selectedInk(): StrokeEl[] {
    return this.elements.filter((el): el is StrokeEl => this.selection.has(el.id) && el.kind === "stroke" && el.tool === "pen");
  }

  /**
   * Replaces the selected handwriting with text in a handwriting font. The
   * OS recognizer suggests the words (on Windows); the user can correct them.
   */
  private async applyFontToInk() {
    const ink = this.selectedInk();
    if (!ink.length) return;
    const recognition = api.recognizeHandwriting(ink.map((s) => s.points.map((p) => [p[0], p[1]] as P2)));
    const result = await inkToTextDialog(recognition, this.textFont === "sans" ? "caveat" : this.textFont);
    if (!result) return;
    await ensureFonts([result.font]);
    const box = unionRects(ink.map(bounds))!;
    const { fontSize, width } = fitTextToBox(result.text, result.font, box);
    const counts = new Map<string, number>();
    for (const s of ink) counts.set(s.color, (counts.get(s.color) ?? 0) + s.points.length);
    const color = [...counts.entries()].sort((a, b) => b[1] - a[1])[0][0];
    const lines = result.text.split("\n").length;
    const text: TextEl = {
      id: newId(),
      z: nextZ(this.elements),
      kind: "text",
      x: box.x,
      y: box.y + (box.h - lines * fontSize * 1.35) / 2,
      w: Math.min(width, Math.max(60, this.page.width - box.x)),
      text: result.text,
      fontSize,
      color,
      font: result.font,
    };
    this.snapshot();
    if (!result.keepInk) {
      const gone = new Set(ink.map((s) => s.id));
      this.elements = this.elements.filter((el) => !gone.has(el.id));
    }
    this.elements.push(text);
    this.selection = new Set([text.id]);
    this.selectionBarKey = "";
    this.textFont = result.font;
    setPref(TEXT_FONT_KEY, result.font);
    this.changed();
    toast("Handwriting replaced with text. Use the font menu above it to try other fonts, or Ctrl+Z to undo");
  }

  private selectedText(): (TextEl | StickyEl)[] {
    return this.elements.filter((el): el is TextEl | StickyEl => this.selection.has(el.id) && (el.kind === "text" || el.kind === "sticky"));
  }

  private renderSelectionBar() {
    const texts = this.selectedText();
    const textControls: HTMLElement[] = [];
    if (texts.length) {
      const fonts = new Set(texts.map((t) => t.font ?? "sans"));
      const current = fonts.size === 1 ? fontPreset([...fonts][0]) : null;
      const fontBtn = h(
        "button",
        { class: "font-picker small", "aria-haspopup": "menu", title: "Handwriting font for the selected text", style: { fontFamily: current?.family ?? "inherit" } },
        h("span", { class: "font-name" }, current?.label ?? "Mixed fonts"),
        icon("down"),
      );
      fontBtn.addEventListener("click", () => this.openFontMenu(fontBtn, current?.id ?? null, (font) => this.styleSelectedText({ font })));
      textControls.push(
        fontBtn,
        h("button", { class: "icon-btn small text-btn", "aria-label": "Smaller text", title: "Smaller text", onclick: () => this.styleSelectedText({ scale: 1 / 1.15 }) }, "A−"),
        h("button", { class: "icon-btn small text-btn", "aria-label": "Larger text", title: "Larger text", onclick: () => this.styleSelectedText({ scale: 1.15 }) }, "A+"),
        ...this.selectionFormatButtons(texts),
        h("span", { class: "divider" }),
      );
    }
    const inkControls: HTMLElement[] = [];
    if (this.selectedInk().length) {
      inkControls.push(
        h(
          "button",
          { class: "icon-btn small apply-font", title: "Turn this handwriting into text in a handwriting font", onclick: () => void this.applyFontToInk() },
          icon("text"),
          "Apply font",
        ),
        h("span", { class: "divider" }),
      );
    }
    const shapeControls: HTMLElement[] = [];
    const single = this.singleShape();
    if (single?.kind === "shape") {
      const closed = isClosedShape(single.shape);
      const kindBtn = h("button", { class: "icon-btn small apply-font", "aria-haspopup": "menu", title: "Change shape" }, icon(SHAPE_KINDS.find((k) => k.id === single.shape.kind)?.icon ?? "shape"), icon("down"));
      kindBtn.addEventListener("click", () =>
        popupMenu(
          kindBtn,
          SHAPE_KINDS.filter((k) => k.id !== "auto" && (closed ? k.id !== "line" && k.id !== "arrow" : k.id === "line" || k.id === "arrow")).map((k) => ({
            label: k.label,
            checked: k.id === single.shape.kind,
            action: () => this.updateSelectedShape((el) => convertShape(el, k.id)),
          })),
        ),
      );
      shapeControls.push(
        kindBtn,
        sizeControl({
          label: "Line width",
          value: single.width,
          min: 0.5,
          max: 20,
          step: 0.5,
          unit: "px",
          presets: [],
          preview: "dot",
          onChange: (v) => this.updateSelectedShape((el) => (el.kind === "shape" ? { ...el, width: v } : el), false),
        }),
      );
      if (closed) {
        shapeControls.push(
          h(
            "button",
            {
              class: `icon-btn small${single.fill ? " active" : ""}`,
              "aria-pressed": String(!!single.fill),
              "aria-label": single.fill ? "Remove fill" : "Fill shape",
              title: single.fill ? "Remove fill" : "Fill shape",
              onclick: () => this.updateSelectedShape((el) => (el.kind === "shape" ? { ...el, fill: el.fill ? null : `${el.color}33` } : el)),
            },
            icon("fill"),
          ),
        );
      }
      shapeControls.push(h("span", { class: "divider" }));
    }
    this.selectionBar.replaceChildren(
      ...inkControls,
      ...shapeControls,
      ...textControls,
      ...INK_COLORS.slice(0, 6).map((c) => h("button", { class: "swatch small", style: { background: c }, "aria-label": `Recolour ${c}`, onclick: () => this.recolorSelection(c) })),
      customColorButton({ value: this.selectionColor(), presets: INK_COLORS.slice(0, 6), onPick: (c) => this.recolorSelection(c) }),
      h("span", { class: "divider" }),
      h("button", { class: "icon-btn small", "aria-label": "Duplicate selection", title: "Duplicate (Ctrl+D)", onclick: () => this.duplicateSelection() }, icon("copy")),
      h("button", { class: "icon-btn small", "aria-label": "Delete selection", title: "Delete", onclick: () => this.deleteSelection() }, icon("trash")),
    );
  }

  /** The selection's colour when every selected item shares one (for the custom swatch). */
  private selectionColor(): string | null {
    const colors = new Set(
      this.elements
        .filter((el) => this.selection.has(el.id) && (el.kind === "stroke" || el.kind === "shape" || el.kind === "text"))
        .map((el) => (el as StrokeEl | ShapeEl | TextEl).color.toLowerCase()),
    );
    return colors.size === 1 ? [...colors][0] || null : null;
  }

  /** Applies a font and/or size change to every selected text box and sticky note. */
  private selectionFormatButtons(texts: (TextEl | StickyEl)[]): HTMLElement[] {
    const all = (pred: (t: TextEl | StickyEl) => boolean) => texts.every(pred);
    const bold = all((t) => !!t.bold);
    const italic = all((t) => !!t.italic);
    const align = all((t) => (t.align ?? "left") === "center") ? "center" : all((t) => t.align === "right") ? "right" : "left";
    const list = texts.every((t) => currentList(t.text) === "bullet") ? "bullet" : texts.every((t) => currentList(t.text) === "number") ? "number" : null;
    const nextAlign: Record<TextAlign, TextAlign> = { left: "center", center: "right", right: "left" };
    const alignIcon = { left: "alignLeft", center: "alignCenter", right: "alignRight" } as const;
    const b = (label: string, content: HTMLElement | string, active: boolean, onclick: () => void) =>
      h("button", { class: `icon-btn small fmt-btn${active ? " active" : ""}`, "aria-pressed": String(active), "aria-label": label, title: label, onclick }, content);
    return [
      b("Bold", h("b", {}, "B"), bold, () => this.styleSelectedText({ bold: !bold })),
      b("Italic", h("i", { class: "serif" }, "I"), italic, () => this.styleSelectedText({ italic: !italic })),
      b("Bulleted list", icon("listBullet"), list === "bullet", () => this.styleSelectedText({ list: "bullet" })),
      b("Numbered list", icon("listNumber"), list === "number", () => this.styleSelectedText({ list: "number" })),
      b(`Alignment: ${align} (click to change)`, icon(alignIcon[align]), false, () => this.styleSelectedText({ align: nextAlign[align] })),
    ];
  }

  private styleSelectedText(change: { font?: FontId; scale?: number; bold?: boolean; italic?: boolean; align?: TextAlign; list?: ListKind }) {
    if (!this.selectedText().length) return;
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
    this.selectionBarKey = "";
    this.changed();
    void this.loadFontsFor(this.selectedText());
  }

  private deleteSelection() {
    if (!this.selection.size) return;
    this.snapshot();
    this.elements = this.elements.filter((el) => !this.selection.has(el.id));
    this.selection.clear();
    this.changed();
  }

  private recolorSelection(color: string) {
    this.snapshot();
    this.elements = this.elements.map((el) => {
      if (!this.selection.has(el.id)) return el;
      if (el.kind === "shape") return { ...el, color, fill: el.fill ? `${color}33` : null };
      if (el.kind === "stroke" || el.kind === "text") return { ...el, color };
      return el;
    });
    this.changed();
  }

  private copySelection() {
    this.clipboard = this.elements.filter((el) => this.selection.has(el.id)).map((el) => structuredClone(el));
    if (this.clipboard.length) toast(`Copied ${this.clipboard.length} item${this.clipboard.length > 1 ? "s" : ""}`);
  }

  private pasteClipboard() {
    if (!this.clipboard.length) return;
    this.placeCopies(this.clipboard);
  }

  private duplicateSelection() {
    const items = this.elements.filter((el) => this.selection.has(el.id));
    if (items.length) this.placeCopies(items);
  }

  private placeCopies(items: El[]) {
    this.snapshot();
    let z = nextZ(this.elements);
    const copies = items.map((el) => ({ ...translate(structuredClone(el), 24, 24), id: newId(), z: z++ }) as El);
    this.elements.push(...copies);
    this.tool = "lasso";
    this.renderToolbar();
    this.selection = new Set(copies.map((c) => c.id));
    this.changed();
  }

  // ------------------------------------------------------------ text edit

  private startTextEdit(el: TextEl | StickyEl, isNew: boolean) {
    this.finishTextEdit();
    if (isNew) {
      this.snapshot();
      this.elements.push(el);
    } else {
      this.snapshot();
    }
    const input = h("textarea", { class: `text-editor ${el.kind}`, "aria-label": el.kind === "text" ? "Text box" : "Sticky note", spellcheck: "true", dir: "auto" });
    input.value = el.text;
    this.editing = { el, input };
    this.hiddenIds.add(el.id);
    this.rebuildCache();
    this.draw();
    this.pageWrap.append(input);
    this.positionEditor();
    // A new text box opens to the right of the tap. If its first words turn
    // out to be Arabic, flip it so it extends to the left instead: Arabic
    // writing starts at the tap.
    let directionChosen = !isNew || el.kind !== "text";
    input.addEventListener("input", () => {
      el.text = input.value;
      if (!directionChosen && /\p{L}/u.test(el.text)) {
        directionChosen = true;
        if (isRtl(el.text)) el.x = Math.max(0, el.x - el.w);
      }
      this.positionEditor();
    });
    input.addEventListener("keydown", (e) => {
      if (e.key === "Escape") {
        e.preventDefault();
        input.blur();
      } else if (e.key === "Enter" && !e.shiftKey && input.selectionStart === input.selectionEnd) {
        const next = continueList(input.value, input.selectionStart);
        if (next) {
          e.preventDefault();
          input.value = next.value;
          input.setSelectionRange(next.caret, next.caret);
          el.text = input.value;
          this.positionEditor();
          this.renderToolbar();
        }
      } else if ((e.ctrlKey || e.metaKey) && (e.key.toLowerCase() === "b" || e.key.toLowerCase() === "i")) {
        e.preventDefault();
        if (e.key.toLowerCase() === "b") this.applyFormat({ bold: !el.bold });
        else this.applyFormat({ italic: !el.italic });
      }
      e.stopPropagation();
    });
    input.addEventListener("blur", (e) => {
      const to = e.relatedTarget as HTMLElement | null;
      // Choosing a font or size in the options bar keeps the text box open.
      if (to && (this.optionsBar.contains(to) || to.closest(".popup-menu"))) return;
      this.finishTextEdit();
    });
    input.focus({ preventScroll: true });
  }

  private positionEditor() {
    if (!this.editing) return;
    const { el, input } = this.editing;
    const z = this.zoom;
    input.style.left = `${el.x * z}px`;
    input.style.top = `${el.y * z}px`;
    input.style.width = `${el.w * z}px`;
    input.style.fontFamily = fontPreset(el.font).family;
    input.style.fontWeight = el.bold ? "bold" : "normal";
    input.style.fontStyle = el.italic ? "italic" : "normal";
    // Logical alignment, like the drawn text: "left" means the start side,
    // which is the right for Arabic (the box picks its direction from the text).
    input.style.textAlign = el.align === "center" ? "center" : el.align === "right" ? "end" : "start";
    input.style.transformOrigin = "center";
    input.style.transform = el.rotation ? `rotate(${el.rotation}rad)` : "";
    if (el.kind === "text") {
      input.style.fontSize = `${el.fontSize * z}px`;
      input.style.color = el.color;
      input.style.height = "auto";
      input.style.height = `${Math.max(el.fontSize * 1.6 * z, input.scrollHeight)}px`;
    } else {
      input.style.height = `${el.h * z}px`;
      input.style.fontSize = `${(el.fontSize ?? 18) * z}px`;
      input.style.lineHeight = "1.33";
      input.style.padding = `${12 * z}px`;
      input.style.background = el.color;
    }
  }

  private finishTextEdit() {
    const editing = this.editing;
    if (!editing) return;
    this.editing = null;
    const { el, input } = editing;
    el.text = input.value.replace(/\s+$/, "");
    input.remove();
    this.hiddenIds.delete(el.id);
    void this.loadFontsFor([el]);
    const before = this.undoStack[this.undoStack.length - 1];
    if (el.kind === "text" && !el.text.trim()) {
      this.elements = this.elements.filter((x) => x.id !== el.id);
    } else if (el.kind === "text") {
      // Shrink the box to its text when nothing needs wrapping, so selecting,
      // aligning and transforming follow the visible words.
      const natural = Math.ceil(naturalTextWidth(el) + 4);
      if (natural < el.w) {
        const shrunk = { ...el, w: Math.max(24, natural) };
        // Keep the edge the text is aligned to (alignment is logical: the
        // start side of Arabic text is the right).
        const rightAligned = isRtl(el.text) ? el.align !== "right" && el.align !== "center" : el.align === "right";
        if (el.align === "center") shrunk.x = el.x + (el.w - shrunk.w) / 2;
        else if (rightAligned) shrunk.x = el.x + el.w - shrunk.w;
        this.elements = this.elements.map((x) => (x.id === el.id ? shrunk : x));
      }
    }
    // Drop the undo entry when nothing changed.
    if (before === JSON.stringify(this.elements)) this.undoStack.pop();
    this.changed();
  }

  // ---------------------------------------------------------------- images

  private insertImageFile(file: File | undefined) {
    this.fileInput.value = "";
    if (!file) return;
    if (file.size > 15 * 1024 * 1024) return toast("Images larger than 15 MB are not supported", "error");
    const reader = new FileReader();
    reader.onload = () => {
      const src = String(reader.result);
      const img = new Image();
      img.onload = () => {
        const maxW = this.page.width * 0.6;
        const scale = Math.min(1, maxW / img.naturalWidth);
        const w = img.naturalWidth * scale;
        const hh = img.naturalHeight * scale;
        const cx = (this.scroller.scrollLeft + this.scroller.clientWidth / 2) / this.zoom;
        const cy = (this.scroller.scrollTop + this.scroller.clientHeight / 2) / this.zoom;
        this.images.set(src, img);
        const el: El = { id: newId(), z: nextZ(this.elements), kind: "image", x: Math.max(0, cx - w / 2), y: Math.max(0, cy - hh / 2), w, h: hh, src };
        this.addElement(el);
        this.setTool("lasso");
        this.selection = new Set([el.id]);
        this.draw();
      };
      img.onerror = () => toast("That image could not be read", "error");
      img.src = src;
    };
    reader.readAsDataURL(file);
  }

  /** Vector PDF: strokes, shapes and text stay sharp; text is selectable. */
  private async vectorPdf(pages: Page[], title: string): Promise<Uint8Array> {
    const input: PdfPageInput[] = [];
    for (const p of pages) {
      const elements = p.id === this.page.id ? this.elements : fromStored(await api.elements(p.id));
      await preloadImages(elements, this.images);
      await ensureFonts(elements.map((el) => (el.kind === "text" || el.kind === "sticky" ? el.font : undefined)));
      const background = await this.backgroundNow(p, EXPORT_SCALE.pdf);
      input.push({ page: p, elements, background });
    }
    return buildVectorPdf(input, this.images, title);
  }

  private async exportAs(format: ExportFormat, scope: "all" | "current") {
    try {
      this.finishTextEdit();
      await this.flush();
      const pages = scope === "all" ? this.pages : [this.page];
      const name = scope === "all" ? this.notebook.title : `${this.notebook.title} - page ${this.index + 1}`;
      const path = await api.chooseExportPath(name, format);
      if (!path) return;
      toast(`Exporting ${pages.length} page${pages.length > 1 ? "s" : ""}…`);
      if (format === "pdf") {
        try {
          const saved = await api.savePdfExport(path, await this.vectorPdf(pages, name));
          toast(`Exported to ${saved}`);
          return;
        } catch (e) {
          // Fall back to the image-based PDF rather than failing the export.
          console.error("vector PDF export failed", e);
        }
      }
      const rendered: ExportPage[] = [];
      for (const p of pages) {
        const els = p.id === this.page.id ? this.elements : fromStored(await api.elements(p.id));
        await preloadImages(els, this.images);
        await ensureFonts(els.map((el) => (el.kind === "text" || el.kind === "sticky" ? el.font : undefined)));
        const bg = await this.backgroundNow(p, EXPORT_SCALE[format]);
        const canvas = renderPage(p.template, p.paperColor, p.width, p.height, els, EXPORT_SCALE[format], this.images, bg);
        rendered.push({
          dataUrl: format === "png" ? canvas.toDataURL("image/png") : canvas.toDataURL("image/jpeg", 0.9),
          widthPx: canvas.width,
          heightPx: canvas.height,
          pageWidth: p.width,
          pageHeight: p.height,
        });
      }
      const saved = await api.exportDocument(path, format, rendered);
      toast(`Exported to ${saved}`);
    } catch (e) {
      reportError(e);
    }
  }

}
