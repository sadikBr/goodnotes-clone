// Notebook editor: page canvas, tools, page sidebar, undo/redo and autosave.

import { api, TEMPLATES, type Notebook, type P2, type Page, type Template } from "../api";
import { confirmDialog, formDialog, h, icon, popupMenu, reportError, toast } from "../ui";
import { bounds, hitTest, insideLasso, isHeld, rectContains, translate, unionRects, type Rect } from "./geometry";
import { fromStored, newId, nextZ, toStored, type El, type Pt, type ShapeEl, type StickyEl, type TextEl } from "./model";
import { drawElement, drawLasso, drawSelection, drawStroke, drawTemplate, renderPage, type ImageCache } from "./render";

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
const MAX_UNDO = 100;

type Gesture =
  | { type: "draw"; tool: "pen" | "highlighter" | "shape"; points: Pt[]; times: number[]; preview: ShapeEl | null; holdTimer: number }
  | { type: "erase"; changed: boolean }
  | { type: "lasso"; points: P2[] }
  | { type: "move"; start: P2; original: El[]; dx: number; dy: number }
  | { type: "pan"; sx: number; sy: number; left: number; top: number };

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
  private widths: Record<string, number> = { pen: 3, highlighter: 18, eraser: 14, shape: 3 };
  private zoom = 1;
  private selection = new Set<string>();
  private clipboard: El[] = [];
  private gesture: Gesture | null = null;
  private penSeen = false;
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
  private selectionBar = h("div", { class: "selection-bar", hidden: true });
  private titleBtn = h("button", { class: "title-btn", title: "Rename notebook" });
  private fileInput = h("input", { type: "file", accept: "image/png,image/jpeg,image/gif,image/webp", hidden: true });

  constructor(private cb: EditorCallbacks) {
    this.root.append(
      h("header", { class: "editor-header" }, this.headerLeft(), this.toolbar, this.headerRight()),
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
      h("button", { class: "icon-btn", "aria-label": "Export page as PNG", title: "Export page as PNG", onclick: () => this.exportPage() }, icon("download")),
    );
  }

  private templateSelect!: HTMLSelectElement;

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
    if (this.widths[this.tool] !== undefined) {
      const tool = this.tool;
      const max = tool === "highlighter" ? 40 : tool === "eraser" ? 60 : 16;
      const input = h("input", { type: "range", min: "1", max: String(max), step: "0.5", value: String(this.widths[tool]), "aria-label": "Size" });
      input.addEventListener("input", () => (this.widths[tool] = Number(input.value)));
      width = h("label", { class: "width-control", title: "Size" }, input);
    }
    this.toolbar.replaceChildren(h("div", { class: "tool-group" }, ...tools), swatches ?? "", width ?? "");
    this.canvas.dataset.tool = this.tool;
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
    this.setZoom(this.zoom * (e.deltaY < 0 ? 1.1 : 1 / 1.1));
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
      else drawStroke(ctx, { id: "live", z: 0, kind: "stroke", tool: g.tool === "highlighter" ? "highlighter" : "pen", color: this.colors[g.tool], width: this.widths[g.tool], points: g.points });
    } else if (g?.type === "lasso") {
      drawLasso(ctx, g.points, this.zoom);
    } else if (g?.type === "move") {
      for (const el of g.original) drawElement(ctx, translate(el, g.dx, g.dy), this.images, () => {});
    }
    const sel = this.selectionBounds();
    if (sel) {
      const r = g?.type === "move" ? { ...sel, x: sel.x + g.dx, y: sel.y + g.dy } : sel;
      drawSelection(ctx, r, this.zoom);
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
    c.addEventListener("pointercancel", () => this.cancelGesture());
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
    if (e.button !== 0 && e.pointerType === "mouse") return;
    if (this.gesture) return;
    this.finishTextEdit();
    if (e.pointerType === "pen") this.penSeen = true;
    const [x, y] = this.point(e);
    let tool = this.tool;
    // Palm rejection: once a stylus has been used, touch only scrolls.
    if (e.pointerType === "touch" && this.penSeen) tool = "hand";
    this.canvas.setPointerCapture(e.pointerId);

    switch (tool) {
      case "pen":
      case "highlighter":
      case "shape":
        this.gesture = { type: "draw", tool, points: [[x, y, this.pressure(e)]], times: [e.timeStamp], preview: null, holdTimer: 0 };
        this.armHold();
        break;
      case "eraser":
        this.gesture = { type: "erase", changed: false };
        this.eraseAt(x, y);
        break;
      case "lasso": {
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
          const el: TextEl = { id: newId(), z: nextZ(this.elements), kind: "text", x, y: y - 12, w: Math.min(420, this.page.width - x - 20), text: "", fontSize: 20, color: this.colors.text };
          el.w = Math.max(120, el.w);
          this.startTextEdit(el, true);
        } else {
          const el: StickyEl = { id: newId(), z: nextZ(this.elements), kind: "sticky", x: x - 20, y: y - 20, w: 220, h: 200, text: "", color: this.colors.sticky };
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
    const g = this.gesture;
    this.cursor = this.point(e);
    if (!g) {
      if (this.tool === "eraser") this.draw();
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
          this.addElement({ id: newId(), z: nextZ(this.elements), kind: "stroke", tool, color: this.colors[g.tool], width: this.widths[g.tool], points });
        }
        break;
      }
      case "erase":
        this.gesture = null;
        if (g.changed) this.changed();
        break;
      case "lasso":
        this.gesture = null;
        for (const el of this.elements) if (insideLasso(el, g.points)) this.selection.add(el.id);
        this.draw();
        break;
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
    }
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
    if (bar.hidden || !bar.childElementCount) this.renderSelectionBar();
    bar.hidden = false;
    const top = Math.max(4, sel.y * this.zoom - 52);
    bar.style.left = `${Math.max(4, sel.x * this.zoom)}px`;
    bar.style.top = `${top}px`;
  }

  private renderSelectionBar() {
    this.selectionBar.replaceChildren(
      ...INK_COLORS.slice(0, 6).map((c) => h("button", { class: "swatch small", style: { background: c }, "aria-label": `Recolour ${c}`, onclick: () => this.recolorSelection(c) })),
      h("span", { class: "divider" }),
      h("button", { class: "icon-btn small", "aria-label": "Duplicate selection", title: "Duplicate (Ctrl+D)", onclick: () => this.duplicateSelection() }, icon("copy")),
      h("button", { class: "icon-btn small", "aria-label": "Delete selection", title: "Delete", onclick: () => this.deleteSelection() }, icon("trash")),
    );
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
      if (el.kind === "stroke" || el.kind === "shape" || el.kind === "text") return { ...el, color };
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
    input.addEventListener("blur", () => this.finishTextEdit());
    input.focus({ preventScroll: true });
  }

  private positionEditor() {
    if (!this.editing) return;
    const { el, input } = this.editing;
    const z = this.zoom;
    input.style.left = `${el.x * z}px`;
    input.style.top = `${el.y * z}px`;
    input.style.width = `${el.w * z}px`;
    if (el.kind === "text") {
      input.style.fontSize = `${el.fontSize * z}px`;
      input.style.color = el.color;
      input.style.height = "auto";
      input.style.height = `${Math.max(el.fontSize * 1.6 * z, input.scrollHeight)}px`;
    } else {
      input.style.height = `${el.h * z}px`;
      input.style.fontSize = `${18 * z}px`;
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

  private async exportPage() {
    try {
      this.finishTextEdit();
      const p = this.page;
      const canvas = renderPage(p.template, p.paperColor, p.width, p.height, this.elements, 2, this.images);
      const path = await api.exportPng(`${this.notebook.title} - page ${this.index + 1}`, canvas.toDataURL("image/png"));
      toast(`Exported to ${path}`);
    } catch (e) {
      reportError(e);
    }
  }
}
