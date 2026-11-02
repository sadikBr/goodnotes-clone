// Browser-only stand-in for the Rust backend, persisted to localStorage.
// It is used for `vite dev` previews and UI tests outside the Tauri shell.
// Shape recognition is not mirrored here: it returns null, so strokes stay
// freehand in the browser preview.

import type {
  Backend,
  ExportFormat,
  ExportPage,
  HandwritingRecognition,
  NewPdfNotebook,
  Folder,
  Library,
  NewNotebook,
  Notebook,
  P2,
  Page,
  SearchHit,
  StoredElement,
  Template,
} from "./api";

interface State {
  folders: Folder[];
  notebooks: Omit<Notebook, "pageCount" | "thumbnail">[];
  pages: Page[];
  elements: Record<string, StoredElement[]>;
}

const KEY = "inkleaf-mock-db";
const uid = () => crypto.randomUUID();

function fail(message: string): never {
  throw new Error(message);
}

export class MockBackend implements Backend {
  private s: State;

  constructor() {
    let loaded: State | null = null;
    try {
      const raw = localStorage.getItem(KEY);
      loaded = raw ? (JSON.parse(raw) as State) : null;
    } catch {
      loaded = null;
    }
    this.s = loaded ?? { folders: [], notebooks: [], pages: [], elements: {} };
  }

  private persist() {
    try {
      localStorage.setItem(KEY, JSON.stringify(this.s));
    } catch {
      /* storage may be unavailable; the mock then lives in memory only */
    }
  }

  private nb(id: string): Notebook {
    const n = this.s.notebooks.find((x) => x.id === id) ?? fail(`not found: ${id}`);
    const pages = this.s.pages.filter((p) => p.notebookId === id).sort((a, b) => a.position - b.position);
    return { ...n, pageCount: pages.length, thumbnail: pages[0]?.thumbnail ?? null };
  }

  private page(id: string): Page {
    return this.s.pages.find((p) => p.id === id) ?? fail(`not found: ${id}`);
  }

  private notebookPages(id: string) {
    return this.s.pages.filter((p) => p.notebookId === id).sort((a, b) => a.position - b.position);
  }

  private renumber(notebookId: string) {
    this.notebookPages(notebookId).forEach((p, i) => (p.position = i));
  }

  async library(): Promise<Library> {
    const trashedFolders = new Set(this.s.folders.filter((f) => f.trashedAt).map((f) => f.id));
    return {
      folders: this.s.folders.filter((f) => !f.trashedAt),
      notebooks: this.s.notebooks
        .filter((n) => !n.trashedAt && !(n.folderId && trashedFolders.has(n.folderId)))
        .map((n) => this.nb(n.id))
        .sort((a, b) => b.updatedAt - a.updatedAt),
    };
  }

  async trash(): Promise<Library> {
    return {
      folders: this.s.folders.filter((f) => f.trashedAt),
      notebooks: this.s.notebooks.filter((n) => n.trashedAt).map((n) => this.nb(n.id)),
    };
  }

  async emptyTrash() {
    const gone = new Set(this.s.notebooks.filter((n) => n.trashedAt).map((n) => n.id));
    this.s.notebooks = this.s.notebooks.filter((n) => !gone.has(n.id));
    this.s.pages = this.s.pages.filter((p) => !gone.has(p.notebookId));
    this.s.folders = this.s.folders.filter((f) => !f.trashedAt);
    this.persist();
  }

  async createFolder(name: string, parentId: string | null, color?: string) {
    if (!name.trim()) fail("name must not be empty");
    const now = Date.now();
    const f: Folder = { id: uid(), name: name.trim(), parentId, color: color ?? "#6b7280", favourite: false, createdAt: now, updatedAt: now, trashedAt: null };
    this.s.folders.push(f);
    this.persist();
    return f;
  }

  private folder(id: string) {
    return this.s.folders.find((f) => f.id === id) ?? fail(`not found: ${id}`);
  }

  async renameFolder(id: string, name: string) {
    if (!name.trim()) fail("name must not be empty");
    this.folder(id).name = name.trim();
    this.persist();
  }

  async moveFolder(id: string, parentId: string | null) {
    let cursor = parentId;
    while (cursor) {
      if (cursor === id) fail("a folder cannot be moved inside itself");
      cursor = this.folder(cursor).parentId;
    }
    this.folder(id).parentId = parentId;
    this.persist();
  }

  async setFolderTrashed(id: string, trashed: boolean) {
    this.folder(id).trashedAt = trashed ? Date.now() : null;
    this.persist();
  }

  async deleteFolder(id: string) {
    this.s.folders = this.s.folders.filter((f) => f.id !== id);
    this.persist();
  }

  async setFolderFavourite(id: string, favourite: boolean) {
    this.folder(id).favourite = favourite;
    this.persist();
  }

  async createNotebook(input: NewNotebook) {
    if (!input.title.trim()) fail("name must not be empty");
    const now = Date.now();
    const id = uid();
    const template = input.template ?? "ruled";
    this.s.notebooks.push({
      id,
      title: input.title.trim(),
      folderId: input.folderId ?? null,
      coverColor: input.coverColor ?? "#3b82f6",
      defaultTemplate: template,
      favourite: false,
      createdAt: now,
      updatedAt: now,
      trashedAt: null,
    });
    this.s.pages.push(this.newPage(id, 0, template, input.paperColor ?? "#fffdf7", input.width ?? 794, input.height ?? 1123));
    this.persist();
    return this.nb(id);
  }

  private newPage(notebookId: string, position: number, template: Template, paperColor: string, width: number, height: number): Page {
    const now = Date.now();
    return { id: uid(), notebookId, position, template, paperColor, width, height, favourite: false, thumbnail: null, createdAt: now, updatedAt: now, backgroundAsset: null, backgroundPage: null };
  }

  private raw(id: string) {
    return this.s.notebooks.find((n) => n.id === id) ?? fail(`not found: ${id}`);
  }

  async notebook(id: string) {
    return this.nb(id);
  }

  async renameNotebook(id: string, title: string) {
    if (!title.trim()) fail("name must not be empty");
    Object.assign(this.raw(id), { title: title.trim(), updatedAt: Date.now() });
    this.persist();
  }

  async moveNotebook(id: string, folderId: string | null) {
    this.raw(id).folderId = folderId;
    this.persist();
  }

  async setNotebookCover(id: string, color: string) {
    this.raw(id).coverColor = color;
    this.persist();
  }

  async setNotebookTrashed(id: string, trashed: boolean) {
    const n = this.raw(id);
    n.trashedAt = trashed ? Date.now() : null;
    if (!trashed && n.folderId && this.s.folders.find((f) => f.id === n.folderId)?.trashedAt) n.folderId = null;
    this.persist();
  }

  async deleteNotebook(id: string) {
    this.s.notebooks = this.s.notebooks.filter((n) => n.id !== id);
    this.s.pages = this.s.pages.filter((p) => p.notebookId !== id);
    this.persist();
  }

  async setNotebookFavourite(id: string, favourite: boolean) {
    this.raw(id).favourite = favourite;
    this.persist();
  }

  async duplicateNotebook(id: string) {
    const src = this.raw(id);
    const copy = await this.createNotebook({ title: `${src.title} copy`, folderId: src.folderId, coverColor: src.coverColor, template: src.defaultTemplate });
    this.s.pages = this.s.pages.filter((p) => p.notebookId !== copy.id);
    for (const p of this.notebookPages(id)) {
      const np = { ...p, id: uid(), notebookId: copy.id };
      this.s.pages.push(np);
      this.s.elements[np.id] = (this.s.elements[p.id] ?? []).map((e) => {
        const nid = uid();
        return { ...e, id: nid, data: { ...(e.data as object), id: nid } };
      });
    }
    this.persist();
    return this.nb(copy.id);
  }

  async pages(notebookId: string) {
    return this.notebookPages(notebookId).map((p) => ({ ...p }));
  }

  async addPage(notebookId: string, index: number | null, template: Template | null) {
    const pages = this.notebookPages(notebookId);
    const at = Math.max(0, Math.min(index ?? pages.length, pages.length));
    const ref = pages[Math.max(0, at - 1)];
    const p = this.newPage(notebookId, at, template ?? ref?.template ?? "ruled", ref?.paperColor ?? "#fffdf7", ref?.width ?? 794, ref?.height ?? 1123);
    pages.forEach((x) => x.position >= at && (x.position += 1));
    this.s.pages.push(p);
    this.raw(notebookId).updatedAt = Date.now();
    this.persist();
    return { ...p };
  }

  async duplicatePage(pageId: string) {
    const src = this.page(pageId);
    this.notebookPages(src.notebookId).forEach((x) => x.position > src.position && (x.position += 1));
    const p = { ...src, id: uid(), position: src.position + 1, favourite: false };
    this.s.pages.push(p);
    this.s.elements[p.id] = (this.s.elements[pageId] ?? []).map((e) => {
      const nid = uid();
      return { ...e, id: nid, data: { ...(e.data as object), id: nid } };
    });
    this.persist();
    return { ...p };
  }

  async deletePage(pageId: string) {
    const p = this.page(pageId);
    if (this.notebookPages(p.notebookId).length <= 1) fail("a notebook must keep at least one page");
    this.s.pages = this.s.pages.filter((x) => x.id !== pageId);
    delete this.s.elements[pageId];
    this.renumber(p.notebookId);
    this.persist();
  }

  async movePage(pageId: string, newIndex: number) {
    const p = this.page(pageId);
    const pages = this.notebookPages(p.notebookId).filter((x) => x.id !== pageId);
    pages.splice(Math.max(0, Math.min(newIndex, pages.length)), 0, p);
    pages.forEach((x, i) => (x.position = i));
    this.persist();
  }

  async setPageTemplate(pageId: string, template: Template, paperColor: string | null) {
    const p = this.page(pageId);
    p.template = template;
    if (paperColor) p.paperColor = paperColor;
    this.persist();
  }

  async setPageFavourite(pageId: string, favourite: boolean) {
    this.page(pageId).favourite = favourite;
    this.persist();
  }

  async setPageThumbnail(pageId: string, thumbnail: string) {
    this.page(pageId).thumbnail = thumbnail;
    this.persist();
  }

  async elements(pageId: string) {
    this.page(pageId);
    return structuredClone(this.s.elements[pageId] ?? []);
  }

  async saveElements(pageId: string, elements: StoredElement[]) {
    const p = this.page(pageId);
    this.s.elements[pageId] = structuredClone(elements);
    p.updatedAt = Date.now();
    this.raw(p.notebookId).updatedAt = p.updatedAt;
    this.persist();
  }

  async search(query: string): Promise<SearchHit[]> {
    const q = query.trim().toLowerCase();
    if (!q) return [];
    const hits: SearchHit[] = [];
    for (const n of this.s.notebooks.filter((x) => !x.trashedAt)) {
      if (n.title.toLowerCase().includes(q)) hits.push({ notebookId: n.id, notebookTitle: n.title, pageId: null, pageNumber: null, snippet: n.title });
      for (const p of this.notebookPages(n.id)) {
        for (const e of this.s.elements[p.id] ?? []) {
          const text = (e.data as { text?: string }).text;
          if (text && text.toLowerCase().includes(q)) {
            hits.push({ notebookId: n.id, notebookTitle: n.title, pageId: p.id, pageNumber: p.position + 1, snippet: text.slice(0, 80) });
          }
        }
      }
    }
    return hits;
  }

  async simplifyStroke(points: [number, number, number][], _epsilon: number) {
    return points;
  }

  async chooseExportPath(fileName: string, format: ExportFormat): Promise<string | null> {
    return `${fileName}.${format}`;
  }

  /** PDFs are kept in memory only (too large for localStorage). */
  private assets = new Map<string, Uint8Array>();

  async storePdfAsset(bytes: Uint8Array): Promise<string> {
    const head = String.fromCharCode(...bytes.slice(0, 4));
    if (head !== "%PDF") fail("that file isn't a PDF");
    const id = uid();
    this.assets.set(id, bytes.slice());
    return id;
  }

  async createPdfNotebook(input: NewPdfNotebook): Promise<Notebook> {
    if (!this.assets.has(input.assetId)) fail(`not found: ${input.assetId}`);
    const nb = await this.createNotebook({ title: input.title, folderId: input.folderId, coverColor: input.coverColor ?? "#dc2626", template: "blank", paperColor: "#ffffff" });
    this.s.pages = this.s.pages.filter((p) => p.notebookId !== nb.id);
    input.pages.forEach((size, i) => {
      const p = this.newPage(nb.id, i, "blank", "#ffffff", size.width, size.height);
      p.backgroundAsset = input.assetId;
      p.backgroundPage = i;
      this.s.pages.push(p);
    });
    this.persist();
    return this.nb(nb.id);
  }

  async assetBytes(id: string): Promise<Uint8Array> {
    return this.assets.get(id) ?? fail(`not found: ${id}`);
  }

  async recognizeHandwriting(_strokes: P2[][]): Promise<HandwritingRecognition> {
    return { available: false, candidates: [] };
  }

  /** Browser preview: PNG downloads directly; PDF/PPTX need the desktop app. */
  async savePdfExport(path: string, bytes: Uint8Array): Promise<string> {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([bytes as BlobPart], { type: "application/pdf" }));
    a.download = path;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10_000);
    return path;
  }

  async exportDocument(path: string, format: ExportFormat, pages: ExportPage[]): Promise<string> {
    if (format !== "png") throw new Error("PDF and PowerPoint export are available in the desktop app");
    const a = document.createElement("a");
    a.href = pages[0].dataUrl;
    a.download = path;
    a.click();
    return path;
  }
}
