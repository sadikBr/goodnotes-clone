// Notebook editor: page canvas, tools, page sidebar, undo/redo and autosave.

import { api, TEMPLATES, type ExportFormat, type ExportPage, type Notebook, type P2, type Page, type RecognizedShape, type Template } from "../api";
import { ensureFonts, FONT_PRESETS, fontPreset, type FontId } from "../fonts";
import { getPref, hasPref, setPref } from "../prefs";
import { confirmDialog, formDialog, h, icon, popupMenu, reportError, sizeControl, toast } from "../ui";
import {
  bounds,
  buildShape,
  convertShape,
  HANDLE_CURSORS,
  handleAt,
  hitForSelect,
  hitTest,
  insideLasso,
  isHeld,
  isResizable,
  rectContains,
  resizeElement,
  translate,
  unionRects,
  type Handle,
  type Rect,
  type ShapeKind,
} from "./geometry";
import { inkToTextDialog } from "./inkToText";
import { fromStored, newId, nextZ, PEN_STYLES, toStored, type El, type PenStyle, type Pt, type ShapeEl, type StickyEl, type StrokeEl, type TextEl } from "./model";
import { drawElement, drawHandles, drawLasso, drawSelection, drawStroke, drawTemplate, fitTextToBox, preloadImages, renderPage, type ImageCache } from "./render";

type Tool = "pen" | "highlighter" | "eraser" | "lasso" | "shape" | "text" | "sticky" | "hand";

const TOOLS: { id: Tool; label: string; key: string }[] = [
  { id: "pen", label: "Pen", key: "p" },
  { id: "highlighter", label: "Highlighter", key: "h" },
  { id: "eraser", label: "Eraser", key: "e" },
  { id: "lasso", label: "Lasso select", key: "l" },
  { id: "shape", label: "Shape", key: "s" },
  { id: "text", label: "Text box", key: "t" },
  { id: "sticky", label: "Sticky note", key: "n" },
  { id: "hand", label: "Pan", key: " " },
];

const INK_COLORS = ["#111827", "#2563eb", "#dc2626", "#16a34a", "#9333ea", "#ea580c", "#0891b2", "#facc15"];
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
  | { type: "draw"; tool: "pen" | "highlighter" | "shape"; points: Pt[]; times: number[]; preview: ShapeEl | null; holdTimer: number }
  | { type: "erase"; changed: boolean }
  | { type: "lasso"; points: P2[] }
  | { type: "move"; start: P2; original: El[]; dx: number; dy: number }
  | { type: "pan"; sx: number; sy: number; left: number; top: number }
  | { type: "shape"; start: P2; preview: ShapeEl | null }
  | { type: "handle"; handle: Handle; original: El; preview: El };

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
  private pages: Page[] = [];
  private index = 0;
  private elements: El[] = [];
  private undoStack: string[] = [];
  private redoStack: string[] = [];
  private tool: Tool = "pen";
  private colors: Record<string, string> = { pen: INK_COLORS[0], highlighter: "#facc15", shape: INK_COLORS[0], text: INK_COLORS[0], sticky: STICKY_COLORS[0] };
  private widths: Record<string, number> = loadWidths();
  private textFont: FontId = getPref(TEXT_FONT_KEY, "sans", FONT_PRESETS.map((f) => f.id));
  private selectionBarKey = "";
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
  private scroller = h("div", { class: "page-scroller" }, this.pageWrap);
  private pageList = h("ol", { class: "page-list", "aria-label": "Pages" });
  private toolbar = h("div", { class: "toolbar", role: "toolbar", "aria-label": "Tools" });
  private statusBar = h("div", { class: "status-bar" });
  private optionsBar = h("div", { class: "options-bar", role: "toolbar", "aria-label": "Tool options" });
  private selectionBar = h("div", { class: "selection-bar", hidden: true });
  private titleBtn = h("button", { class: "title-btn", title: "Rename notebook" });
  private fileInput = h("input", { type: "file", accept: "image/png,image/jpeg,image/gif,image/webp", hidden: true });

  constructor(private cb: EditorCallbacks) {
    this.root.append(
      h("header", { class: "editor-header" }, this.headerLeft(), this.toolbar, this.headerRight()),
      this.optionsBar,
      h("div", { class: "editor-body" }, h("aside", { class: "page-sidebar" }, this.pageList, h("button", { class: "btn add-page", onclick: () => this.addPage(this.pages.length) }, icon("plus"), "Add page")), h("div", { class: "canvas-area" }, this.scroller, this.statusBar)),
      this.fileInput,
    );
    this.pageWrap.append(this.selectionBar);
    this.bindCanvas();
    this.fileInput.addEventListener("change", () => this.insertImageFile(this.fileInput.files?.[0]));
    this.scroller.addEventListener("wheel", (e) => this.onWheel(e), { passive: false });
    window.addEventListener("resize", () => this.root.isConnected && this.layout());
  }

  // ------------------------------------------------------------- lifecycle

  async open(notebookId: string, pageId?: string | null) {
    try {
      this.notebook = await api.notebook(notebookId);
      this.pages = await api.pages(notebookId);
      const i = pageId ? this.pages.findIndex((p) => p.id === pageId) : 0;
      this.titleBtn.textContent = this.notebook.title;
      this.renderToolbar();
      await this.loadPage(Math.max(0, i), true);
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
    if (target.closest("input, textarea, select, .modal")) return false;
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

  private async loadPage(i: number, fit = false) {
    if (i < 0 || i >= this.pages.length) return;
    await this.flush();
    this.index = i;
    try {
      this.elements = fromStored(await api.elements(this.page.id));
      void this.loadFontsFor(this.elements);
    } catch (e) {
      reportError(e);
      this.elements = [];
    }
    this.undoStack = [];
    this.redoStack = [];
    this.selection.clear();
    this.renderPageList();
    if (fit) this.fitWidth();
    else this.layout();
    this.scroller.scrollTop = 0;
    this.updateStatus();
  }

  private goTo(i: number) {
    if (i >= 0 && i < this.pages.length && i !== this.index) this.loadPage(i);
  }

  // ---------------------------------------------------------------- header

  private headerLeft(): HTMLElement {
    this.titleBtn.addEventListener("click", () => this.renameNotebook());
    return h(
      "div",
      { class: "header-left" },
      h("button", { class: "icon-btn", "aria-label": "Back to library", title: "Library", onclick: () => this.cb.close() }, icon("back")),
      this.titleBtn,
    );
  }

  private headerRight(): HTMLElement {
    const template = h("select", { class: "template-select", "aria-label": "Page template", title: "Page template" }, ...TEMPLATES.map((t) => h("option", { value: t.id }, t.label)));
    template.addEventListener("change", () => this.setTemplate(template.value as Template));
    this.templateSelect = template;
    return h(
      "div",
      { class: "header-right" },
      h("button", { class: "icon-btn", "aria-label": "Undo", title: "Undo (Ctrl+Z)", onclick: () => this.undo() }, icon("undo")),
      h("button", { class: "icon-btn", "aria-label": "Redo", title: "Redo (Ctrl+Shift+Z)", onclick: () => this.redo() }, icon("redo")),
      h("span", { class: "divider" }),
      template,
      h("button", { class: "icon-btn", "aria-label": "Insert image", title: "Insert image", onclick: () => this.fileInput.click() }, icon("image")),
      this.exportButton(),
      h("span", { class: "divider" }),
      this.penOnlyBtn,
    );
  }

  private templateSelect!: HTMLSelectElement;

  private exportButton(): HTMLButtonElement {
    const btn = h("button", { class: "icon-btn", "aria-label": "Export", title: "Export", "aria-haspopup": "menu" }, icon("download"));
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
    const tools = TOOLS.map((t) =>
      h(
        "button",
        {
          class: `tool-btn${this.tool === t.id ? " active" : ""}`,
          "aria-pressed": String(this.tool === t.id),
          "aria-label": t.label,
          title: `${t.label}${t.key.trim() ? ` (${t.key.toUpperCase()})` : ""}`,
          onclick: () => this.setTool(t.id),
        },
        icon(t.id === "highlighter" ? "highlighter" : t.id === "sticky" ? "sticky" : t.id),
      ),
    );
    const colorKey = this.tool === "sticky" ? "sticky" : this.tool === "lasso" ? null : this.colors[this.tool] ? this.tool : null;
    const palette = colorKey === "sticky" ? STICKY_COLORS : INK_COLORS;
    const swatches = colorKey
      ? h(
          "div",
          { class: "ink-colors", role: "radiogroup", "aria-label": "Colour" },
          ...palette.map((c) =>
            h("button", {
              class: "swatch small",
              role: "radio",
              "aria-checked": String(this.colors[colorKey] === c),
              "aria-label": c,
              style: { background: c },
              onclick: () => {
                this.colors[colorKey] = c;
                this.renderToolbar();
              },
            }),
          ),
        )
      : null;
    let width: HTMLElement | null = null;
    const spec = SIZE_SPECS[this.tool];
    if (spec) {
      const tool = this.tool;
      width = sizeControl({
        ...spec,
        value: this.widths[tool],
        unit: "px",
        onChange: (v) => {
          this.widths[tool] = v;
          setPref(WIDTHS_KEY, JSON.stringify(this.widths));
          if (tool === "text" || tool === "sticky") this.applyToEditing({ fontSize: v });
          if (tool === "eraser") this.draw();
        },
      });
    }
    this.toolbar.replaceChildren(h("div", { class: "tool-group" }, ...tools));
    const options = [this.toolOptions(), swatches, width].filter((x): x is HTMLElement => x !== null);
    const hints: Partial<Record<Tool, string>> = {
      lasso: "Draw around items to select them, then drag to move",
      hand: "Drag to scroll · Ctrl + scroll or pinch to zoom",
    };
    if (options.length === 0) options.push(h("span", { class: "options-hint" }, hints[this.tool] ?? ""));
    this.optionsBar.replaceChildren(...options);
    this.renderPenOnlyButton();
    this.canvas.dataset.tool = this.tool;
  }

  /** Secondary choices for the active tool: pen type, or shape kind and fill. */
  private toolOptions(): HTMLElement | null {
    const chip = (label: string, ic: Parameters<typeof icon>[0], active: boolean, onclick: () => void) =>
      h("button", { class: `tool-btn small${active ? " active" : ""}`, "aria-pressed": String(active), "aria-label": label, title: label, onclick }, icon(ic));
    if (this.tool === "pen") {
      return h(
        "div",
        { class: "tool-group options", role: "group", "aria-label": "Pen type" },
        ...PEN_STYLES.map((p) =>
          chip(p.label, p.id, this.penStyle === p.id, () => {
            this.penStyle = p.id;
            setPref(PEN_STYLE_KEY, p.id);
            this.renderToolbar();
          }),
        ),
      );
    }
    if (this.tool === "text" || this.tool === "sticky") return this.fontPicker();
    if (this.tool === "shape") {
      return h(
        "div",
        { class: "tool-group options", role: "group", "aria-label": "Shape" },
        ...SHAPE_KINDS.map((k) =>
          chip(k.label, k.icon, this.shapeKind === k.id, () => {
            this.shapeKind = k.id;
            setPref(SHAPE_KIND_KEY, k.id);
            this.renderToolbar();
          }),
        ),
        h("span", { class: "divider" }),
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
    if (tool !== "lasso") this.clearSelection();
    this.tool = tool;
    this.renderToolbar();
    this.draw();
  }

  private updateStatus() {
    const zoom = Math.round(this.zoom * 100);
    this.statusBar.replaceChildren(
      h("button", { class: "icon-btn small", "aria-label": "Previous page", onclick: () => this.goTo(this.index - 1), disabled: this.index === 0 }, icon("up")),
      h("span", {}, `Page ${this.index + 1} of ${this.pages.length}`),
      h("button", { class: "icon-btn small", "aria-label": "Next page", onclick: () => this.goTo(this.index + 1), disabled: this.index >= this.pages.length - 1 }, icon("down")),
      h("span", { class: "divider" }),
      h("button", { class: "icon-btn small", "aria-label": "Zoom out", onclick: () => this.setZoom(this.zoom / 1.2) }, icon("zoomOut")),
      h("span", { class: "zoom-label" }, `${zoom}%`),
      h("button", { class: "icon-btn small", "aria-label": "Zoom in", onclick: () => this.setZoom(this.zoom * 1.2) }, icon("zoomIn")),
      h("button", { class: "icon-btn small", "aria-label": "Fit width", title: "Fit width (Ctrl+0)", onclick: () => this.fitWidth() }, icon("fit")),
    );
    if (this.templateSelect && this.page) this.templateSelect.value = this.page.template;
  }

  // ----------------------------------------------------------- page sidebar

  private renderPageList() {
    this.pageList.replaceChildren(
      ...this.pages.map((p, i) => {
        const thumb = p.thumbnail ? h("img", { src: p.thumbnail, alt: "" }) : h("div", { class: "thumb-blank", style: { background: p.paperColor } });
        const more = h("button", { class: "icon-btn small page-more", "aria-label": `Page ${i + 1} options` }, icon("more"));
        more.addEventListener("click", (e) => {
          e.stopPropagation();
          popupMenu(more, [
            { label: "Add page after", action: () => this.addPage(i + 1) },
            { label: "Duplicate page", action: () => this.duplicatePage(p) },
            { label: "Move up", action: () => this.movePage(p, i - 1) },
            { label: "Move down", action: () => this.movePage(p, i + 1) },
            { label: p.favourite ? "Remove bookmark" : "Bookmark page", action: () => this.toggleBookmark(p) },
            { label: "Delete page", danger: true, action: () => this.deletePage(p) },
          ]);
        });
        return h(
          "li",
          { class: `page-item${i === this.index ? " active" : ""}` },
          h("button", { class: "page-thumb", "aria-label": `Go to page ${i + 1}`, "aria-current": i === this.index ? "page" : undefined, onclick: () => this.goTo(i) }, thumb),
          h("div", { class: "page-num" }, p.favourite ? "★ " : "", String(i + 1)),
          more,
        );
      }),
    );
    this.pageList.querySelector(".active")?.scrollIntoView({ block: "nearest" });
  }

  private async reloadPages(selectIndex: number) {
    this.pages = await api.pages(this.notebook.id);
    await this.loadPage(Math.max(0, Math.min(selectIndex, this.pages.length - 1)));
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
      this.pages = await api.pages(this.notebook.id);
      this.index = this.pages.findIndex((x) => x.id === currentId);
      this.renderPageList();
      this.updateStatus();
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

  private async setTemplate(template: Template) {
    try {
      await api.setPageTemplate(this.page.id, template, null);
      this.page.template = template;
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
    const available = Math.max(200, this.scroller.clientWidth - 64);
    this.zoom = Math.max(0.25, Math.min(2, available / this.page.width));
    this.layout();
  }

  private onWheel(e: WheelEvent) {
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
    const dpr = window.devicePixelRatio || 1;
    const w = this.page.width * this.zoom;
    const hgt = this.page.height * this.zoom;
    this.canvas.style.width = `${w}px`;
    this.canvas.style.height = `${hgt}px`;
    this.pageWrap.style.width = `${w}px`;
    this.pageWrap.style.height = `${hgt}px`;
    // Cap the backing store so very high zoom levels stay within canvas limits.
    const scale = Math.min(this.zoom * dpr, 8192 / Math.max(this.page.width, this.page.height));
    this.canvas.width = Math.round(this.page.width * scale);
    this.canvas.height = Math.round(this.page.height * scale);
    this.cache.width = this.canvas.width;
    this.cache.height = this.canvas.height;
    this.rebuildCache();
    this.draw();
    this.updateStatus();
    if (this.editing) this.positionEditor();
  }

  private get pixelScale() {
    return this.canvas.width / this.page.width;
  }

  /** Re-renders the committed content (paper + elements) into the cache. */
  private rebuildCache() {
    const c = this.cache.getContext("2d")!;
    c.setTransform(1, 0, 0, 1, 0, 0);
    c.clearRect(0, 0, this.cache.width, this.cache.height);
    c.setTransform(this.pixelScale, 0, 0, this.pixelScale, 0, 0);
    drawTemplate(c, this.page.template, this.page.width, this.page.height, this.page.paperColor);
    for (const el of this.elements) {
      if (this.hiddenIds.has(el.id)) continue;
      drawElement(c, el, this.images, () => {
        this.rebuildCache();
        this.draw();
      });
    }
  }

  /** Composites the cache with transient overlays (live ink, lasso, selection). */
  private draw() {
    if (!this.page) return;
    const ctx = this.ctx;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.drawImage(this.cache, 0, 0);
    ctx.setTransform(this.pixelScale, 0, 0, this.pixelScale, 0, 0);
    const g = this.gesture;
    if (g?.type === "draw") {
      if (g.preview) drawElement(ctx, g.preview, this.images, () => {});
      else
        drawStroke(ctx, {
          id: "live",
          z: 0,
          kind: "stroke",
          tool: g.tool === "highlighter" ? "highlighter" : "pen",
          style: g.tool === "pen" ? this.penStyle : undefined,
          color: this.colors[g.tool],
          width: this.widths[g.tool],
          points: g.points,
        });
    } else if (g?.type === "shape") {
      if (g.preview) drawElement(ctx, g.preview, this.images, () => {});
    } else if (g?.type === "lasso") {
      drawLasso(ctx, g.points, this.zoom);
    } else if (g?.type === "move") {
      for (const el of g.original) drawElement(ctx, translate(el, g.dx, g.dy), this.images, () => {});
    } else if (g?.type === "handle") {
      drawElement(ctx, g.preview, this.images, () => {});
    }
    const sel = g?.type === "handle" ? bounds(g.preview) : this.selectionBounds();
    if (sel) {
      const r = g?.type === "move" ? { ...sel, x: sel.x + g.dx, y: sel.y + g.dy } : sel;
      drawSelection(ctx, r, this.zoom);
    }
    const editable = g?.type === "handle" ? g.preview : g ? null : this.singleResizable();
    if (editable) drawHandles(ctx, editable, this.zoom);
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
    const thumbScale = 200 / page.width;
    const thumb = renderPage(page.template, page.paperColor, page.width, page.height, this.elements, thumbScale, this.images).toDataURL("image/jpeg", 0.82);
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
    c.addEventListener("pointerdown", (e) => this.onDown(e));
    c.addEventListener("pointermove", (e) => this.onMove(e));
    c.addEventListener("pointerup", (e) => this.onUp(e));
    c.addEventListener("pointercancel", (e) => {
      if (this.navPointers.has(e.pointerId)) this.navUp(e);
      else this.cancelGesture();
    });
    c.addEventListener("pointerleave", () => {
      this.cursor = null;
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

  private pressure(e: PointerEvent): number {
    if (e.pointerType === "pen") return e.pressure > 0 ? e.pressure : 0.5;
    return 0.5;
  }

  private onDown(e: PointerEvent) {
    if (e.pointerType === "pen") this.notePen();
    // Pen-only mode: anything that isn't the pen navigates instead of writing.
    if (this.penOnly && e.pointerType !== "pen") {
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
      case "pen":
      case "highlighter":
      case "shape":
        if (tool === "shape" && this.shapeKind !== "auto") {
          this.gesture = { type: "shape", start: [x, y], preview: null };
          break;
        }
        // Freehand ink; with the shape tool in auto mode it is recognised on lift.
        this.gesture = { type: "draw", tool, points: [[x, y, this.pressure(e)]], times: [e.timeStamp], preview: null, holdTimer: 0 };
        this.armHold();
        break;
      case "eraser":
        this.gesture = { type: "erase", changed: false };
        this.eraseAt(x, y);
        break;
      case "lasso": {
        const single = this.singleResizable();
        const handle = single ? handleAt(single, x, y, 10 / this.zoom) : null;
        if (single && handle) {
          this.gesture = { type: "handle", handle, original: single, preview: single };
          this.hiddenIds.add(single.id);
          this.rebuildCache();
          break;
        }
        const sel = this.selectionBounds();
        if (sel && rectContains({ x: sel.x - 8, y: sel.y - 8, w: sel.w + 16, h: sel.h + 16 }, x, y)) {
          const original = this.elements.filter((el) => this.selection.has(el.id));
          this.gesture = { type: "move", start: [x, y], original, dx: 0, dy: 0 };
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
          const el: TextEl = { id: newId(), z: nextZ(this.elements), kind: "text", x, y: y - 12, w: Math.min(420, this.page.width - x - 20), text: "", fontSize: this.widths.text, color: this.colors.text, font: this.textFont };
          el.w = Math.max(120, el.w);
          this.startTextEdit(el, true);
        } else {
          const el: StickyEl = { id: newId(), z: nextZ(this.elements), kind: "sticky", x: x - 20, y: y - 20, w: 220, h: 200, text: "", color: this.colors.sticky, font: this.textFont, fontSize: this.widths.sticky };
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
      if (this.tool === "eraser") this.draw();
      if (this.tool === "lasso") this.updateHoverCursor();
      return;
    }
    const events = typeof e.getCoalescedEvents === "function" ? e.getCoalescedEvents() : [];
    const samples = events.length ? events : [e];
    switch (g.type) {
      case "draw": {
        if (g.preview) return; // shape is locked in until the pen lifts
        for (const s of samples) {
          const [x, y] = this.point(s);
          const last = g.points[g.points.length - 1];
          if (Math.hypot(x - last[0], y - last[1]) < 0.4 / this.zoom) continue;
          g.points.push([x, y, this.pressure(s)]);
          g.times.push(s.timeStamp);
        }
        this.armHold();
        break;
      }
      case "erase":
        for (const s of samples) this.eraseAt(...this.point(s));
        break;
      case "lasso":
        g.points.push(this.point(e));
        break;
      case "move":
        g.dx = this.cursor[0] - g.start[0];
        g.dy = this.cursor[1] - g.start[1];
        break;
      case "handle":
        g.preview = resizeElement(g.original, g.handle, this.cursor, e.shiftKey);
        break;
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
    this.draw();
  }

  /** Draw-and-hold: keeping the pen still at the end of a stroke snaps it to a shape. */
  private armHold() {
    const g = this.gesture;
    if (g?.type !== "draw") return;
    window.clearTimeout(g.holdTimer);
    g.holdTimer = window.setTimeout(async () => {
      if (this.gesture !== g || g.preview || g.points.length < 4) return;
      const now = performance.now();
      const times = [...g.times.slice(0, -1), Math.max(now, g.times[g.times.length - 1])];
      if (!isHeld(times, g.points, HOLD_MS - 50, 4 / this.zoom)) return;
      const shape = await this.recognize(g);
      if (shape && this.gesture === g) {
        g.preview = shape;
        this.draw();
      }
    }, HOLD_MS);
  }

  private async recognize(g: Extract<Gesture, { type: "draw" }>): Promise<ShapeEl | null> {
    try {
      const shape = await api.recognizeShape(g.points.map((p) => [p[0], p[1]] as P2));
      if (!shape) return null;
      const tool = g.tool === "highlighter" ? "highlighter" : g.tool;
      const color = this.colors[tool];
      // A highlighter line stays a translucent stroke (straightened) rather than a shape.
      if (tool === "highlighter") return shape.kind === "line" ? { id: newId(), z: 0, kind: "shape", shape, color, width: this.widths.highlighter, fill: null } : null;
      return { id: newId(), z: nextZ(this.elements), kind: "shape", shape, color, width: this.widths[tool], fill: null };
    } catch (e) {
      reportError(e);
      return null;
    }
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
      case "draw": {
        window.clearTimeout(g.holdTimer);
        let shape = g.preview;
        if (!shape && g.tool === "shape") shape = await this.recognize(g);
        this.gesture = null;
        if (shape && g.tool === "highlighter" && shape.shape.kind === "line") {
          const [a, b] = shape.shape.points;
          const p = g.points[0][2];
          this.addElement({ id: newId(), z: nextZ(this.elements), kind: "stroke", tool: "highlighter", color: this.colors.highlighter, width: this.widths.highlighter, points: [[a[0], a[1], p], [b[0], b[1], p]] });
        } else if (shape) {
          shape.z = nextZ(this.elements);
          this.addElement(shape);
        } else {
          const tool = g.tool === "highlighter" ? "highlighter" : "pen";
          let points = g.points;
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
    const closed = shape.kind !== "line" && shape.kind !== "arrow";
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
    const g = this.gesture;
    if (g?.type === "draw") window.clearTimeout(g.holdTimer);
    if (g?.type === "erase" && g.changed) this.changed();
    this.gesture = null;
    this.hiddenIds.clear();
    this.rebuildCache();
    this.draw();
  }

  private eraseAt(x: number, y: number) {
    const g = this.gesture;
    if (g?.type !== "erase") return;
    const radius = this.widths.eraser / 2;
    const hits = this.elements.filter((el) => el.kind !== "image" && hitTest(el, x, y, radius));
    if (hits.length === 0) return;
    if (!g.changed) this.snapshot();
    g.changed = true;
    const gone = new Set(hits.map((el) => el.id));
    this.elements = this.elements.filter((el) => !gone.has(el.id));
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
    const top = Math.max(4, sel.y * this.zoom - 52);
    bar.style.left = `${Math.max(4, sel.x * this.zoom)}px`;
    bar.style.top = `${top}px`;
  }

  /** The selected element when exactly one reshapeable item is selected. */
  private singleResizable(): El | null {
    if (this.selection.size !== 1) return null;
    const el = this.elements.find((x) => this.selection.has(x.id));
    return el && isResizable(el) ? el : null;
  }

  private updateHoverCursor() {
    const single = this.singleResizable();
    const c = this.cursor;
    let cursor = "";
    if (single && c) {
      const hd = handleAt(single, c[0], c[1], 10 / this.zoom);
      if (hd) cursor = HANDLE_CURSORS[hd];
    }
    if (!cursor && c) {
      const sel = this.selectionBounds();
      if (sel && rectContains({ x: sel.x - 8, y: sel.y - 8, w: sel.w + 16, h: sel.h + 16 }, c[0], c[1])) cursor = "move";
    }
    this.canvas.style.cursor = cursor;
  }

  /** Applies a change to the single selected shape, as one undo step. */
  private updateSelectedShape(change: (el: El) => El, refreshBar = true) {
    const single = this.singleResizable();
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
    const single = this.singleResizable();
    if (single?.kind === "shape") {
      const closed = single.shape.kind !== "line" && single.shape.kind !== "arrow";
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
      h("span", { class: "divider" }),
      h("button", { class: "icon-btn small", "aria-label": "Duplicate selection", title: "Duplicate (Ctrl+D)", onclick: () => this.duplicateSelection() }, icon("copy")),
      h("button", { class: "icon-btn small", "aria-label": "Delete selection", title: "Delete", onclick: () => this.deleteSelection() }, icon("trash")),
    );
  }

  /** Applies a font and/or size change to every selected text box and sticky note. */
  private styleSelectedText(change: { font?: FontId; scale?: number }) {
    if (!this.selectedText().length) return;
    this.snapshot();
    this.elements = this.elements.map((el) => {
      if (!this.selection.has(el.id) || (el.kind !== "text" && el.kind !== "sticky")) return el;
      const next = { ...el };
      if (change.font) next.font = change.font;
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
    const input = h("textarea", { class: `text-editor ${el.kind}`, "aria-label": el.kind === "text" ? "Text box" : "Sticky note", spellcheck: "true" });
    input.value = el.text;
    this.editing = { el, input };
    this.hiddenIds.add(el.id);
    this.rebuildCache();
    this.draw();
    this.pageWrap.append(input);
    this.positionEditor();
    input.addEventListener("input", () => {
      el.text = input.value;
      this.positionEditor();
    });
    input.addEventListener("keydown", (e) => {
      if (e.key === "Escape") {
        e.preventDefault();
        input.blur();
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

  private async exportAs(format: ExportFormat, scope: "all" | "current") {
    try {
      this.finishTextEdit();
      await this.flush();
      const pages = scope === "all" ? this.pages : [this.page];
      const name = scope === "all" ? this.notebook.title : `${this.notebook.title} - page ${this.index + 1}`;
      const path = await api.chooseExportPath(name, format);
      if (!path) return;
      toast(`Exporting ${pages.length} page${pages.length > 1 ? "s" : ""}…`);
      const rendered: ExportPage[] = [];
      for (const p of pages) {
        const els = p.id === this.page.id ? this.elements : fromStored(await api.elements(p.id));
        await preloadImages(els, this.images);
        await ensureFonts(els.map((el) => (el.kind === "text" || el.kind === "sticky" ? el.font : undefined)));
        const canvas = renderPage(p.template, p.paperColor, p.width, p.height, els, EXPORT_SCALE[format], this.images);
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
