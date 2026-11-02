// Library screen: folders, notebooks, favourites, trash and search.

import { api, PAPER_COLORS, PAPER_SIZES, TEMPLATES, type Folder, type Library, type Notebook, type Template } from "./api";
import { confirmDialog, formDialog, h, icon, popupMenu, relativeTime, reportError, toast, type MenuItem } from "./ui";

export type LibraryView = { kind: "all" } | { kind: "favourites" } | { kind: "trash" } | { kind: "folder"; id: string };

export interface LibraryCallbacks {
  openNotebook(id: string, pageId?: string | null): void;
}

export class LibraryScreen {
  private view: LibraryView = { kind: "all" };
  private data: Library = { folders: [], notebooks: [] };
  private query = "";
  readonly root = h("div", { class: "library" });
  private pdfInput = h("input", { type: "file", accept: "application/pdf,.pdf", hidden: true });

  constructor(private cb: LibraryCallbacks) {
    document.body.append(this.pdfInput);
    this.pdfInput.addEventListener("change", () => void this.importPdf(this.pdfInput.files?.[0]));
  }

  async show(view?: LibraryView) {
    if (view) this.view = view;
    await this.refresh();
  }

  private async refresh() {
    try {
      this.data = this.view.kind === "trash" ? await api.trash() : await api.library();
      if (this.view.kind === "folder") {
        const id = this.view.id;
        if (!this.data.folders.some((f) => f.id === id)) this.view = { kind: "all" };
      }
      const sidebarData = this.view.kind === "trash" ? await api.library() : this.data;
      this.render(sidebarData.folders);
    } catch (e) {
      reportError(e);
    }
  }

  private render(allFolders: Folder[]) {
    this.root.replaceChildren(this.sidebar(allFolders), this.main());
  }

  private sidebar(folders: Folder[]): HTMLElement {
    const item = (label: string, ic: Parameters<typeof icon>[0], view: LibraryView, extra?: HTMLElement) => {
      const active = JSON.stringify(view) === JSON.stringify(this.view);
      return h(
        "button",
        { class: `nav-item${active ? " active" : ""}`, "aria-current": active ? "page" : undefined, onclick: () => this.show(view) },
        icon(ic),
        h("span", { class: "nav-label" }, label),
        extra,
      );
    };
    const tree = h("div", { class: "folder-tree" });
    const addLevel = (parent: string | null, depth: number) => {
      for (const f of folders.filter((x) => x.parentId === parent).sort((a, b) => a.name.localeCompare(b.name))) {
        const dot = h("span", { class: "folder-dot", style: { background: f.color } });
        const btn = item(f.name, "folder", { kind: "folder", id: f.id }, dot);
        btn.style.paddingLeft = `${12 + depth * 14}px`;
        tree.append(btn);
        addLevel(f.id, depth + 1);
      }
    };
    addLevel(null, 0);
    return h(
      "aside",
      { class: "sidebar" },
      h("div", { class: "brand" }, h("img", { src: "/favicon.png", alt: "" }), h("span", {}, "Inkleaf")),
      h("nav", { "aria-label": "Library" }, item("All notebooks", "book", { kind: "all" }), item("Favourites", "star", { kind: "favourites" })),
      h(
        "div",
        { class: "sidebar-section" },
        h("span", {}, "Folders"),
        h("button", { class: "icon-btn small", title: "New folder", "aria-label": "New folder", onclick: () => this.newFolder(null) }, icon("plus")),
      ),
      tree,
      h("div", { class: "sidebar-spacer" }),
      item("Trash", "trash", { kind: "trash" }),
    );
  }

  private title(): string {
    switch (this.view.kind) {
      case "all":
        return "All notebooks";
      case "favourites":
        return "Favourites";
      case "trash":
        return "Trash";
      case "folder": {
        const id = this.view.id;
        return this.data.folders.find((f) => f.id === id)?.name ?? "Folder";
      }
    }
  }

  private main(): HTMLElement {
    const search = h("input", {
      type: "search",
      class: "search",
      placeholder: "Search notebooks and typed text",
      "aria-label": "Search",
      value: this.query,
    });
    let debounce: number | undefined;
    search.addEventListener("input", () => {
      this.query = search.value;
      window.clearTimeout(debounce);
      debounce = window.setTimeout(() => this.renderResults(results), 200);
    });
    const actions = h("div", { class: "header-actions" });
    if (this.view.kind === "trash") {
      actions.append(h("button", { class: "btn danger", onclick: () => this.emptyTrash() }, "Empty trash"));
    } else {
      const folderId = this.view.kind === "folder" ? this.view.id : null;
      actions.append(
        h("button", { class: "btn", onclick: () => this.pdfInput.click(), title: "Open a PDF as a notebook you can write on" }, icon("page"), "Import PDF"),
        h("button", { class: "btn", onclick: () => this.newFolder(folderId) }, icon("folder"), "New folder"),
        h("button", { class: "btn primary", onclick: () => this.newNotebook(folderId) }, icon("plus"), "New notebook"),
      );
    }
    const results = h("section", { class: "content", "aria-live": "polite" });
    this.renderResults(results);
    return h(
      "main",
      { class: "library-main" },
      h("header", { class: "library-header" }, h("h1", {}, this.title()), h("div", { class: "search-wrap" }, icon("search"), search), actions),
      results,
    );
  }

  private async renderResults(container: HTMLElement) {
    if (this.query.trim() && this.view.kind !== "trash") {
      try {
        const hits = await api.search(this.query);
        container.replaceChildren(
          hits.length === 0
            ? h("p", { class: "empty" }, `No results for “${this.query}”.`)
            : h(
                "ul",
                { class: "search-results" },
                ...hits.map((hit) =>
                  h(
                    "li",
                    {},
                    h(
                      "button",
                      { onclick: () => this.cb.openNotebook(hit.notebookId, hit.pageId) },
                      h("strong", {}, hit.notebookTitle),
                      hit.pageNumber ? h("span", { class: "muted" }, ` · page ${hit.pageNumber}`) : null,
                      h("div", { class: "snippet" }, hit.snippet),
                    ),
                  ),
                ),
              ),
        );
      } catch (e) {
        reportError(e);
      }
      return;
    }
    container.replaceChildren(...this.gridContent());
  }

  private gridContent(): HTMLElement[] {
    const v = this.view;
    let folders: Folder[] = [];
    let notebooks: Notebook[] = this.data.notebooks;
    if (v.kind === "all") {
      folders = this.data.folders.filter((f) => f.parentId === null);
      notebooks = notebooks.filter((n) => n.folderId === null);
    } else if (v.kind === "favourites") {
      folders = this.data.folders.filter((f) => f.favourite);
      notebooks = notebooks.filter((n) => n.favourite);
    } else if (v.kind === "folder") {
      folders = this.data.folders.filter((f) => f.parentId === v.id);
      notebooks = notebooks.filter((n) => n.folderId === v.id);
    } else {
      folders = this.data.folders;
    }
    if (folders.length === 0 && notebooks.length === 0) {
      const msg =
        v.kind === "trash" ? "Trash is empty." : v.kind === "favourites" ? "Star a notebook or folder to see it here." : "No notebooks yet. Create one to start writing.";
      return [h("p", { class: "empty" }, msg)];
    }
    return [h("div", { class: "grid" }, ...folders.map((f) => this.folderCard(f)), ...notebooks.map((n) => this.notebookCard(n)))];
  }

  private folderCard(f: Folder): HTMLElement {
    const trash = this.view.kind === "trash";
    const more = h("button", { class: "icon-btn card-more", "aria-label": `Options for ${f.name}` }, icon("more"));
    more.addEventListener("click", (e) => {
      e.stopPropagation();
      const items: MenuItem[] = trash
        ? [
            { label: "Recover", action: () => this.run(() => api.setFolderTrashed(f.id, false)) },
            { label: "Delete forever", danger: true, action: () => this.deleteForever("folder", f.id, f.name) },
          ]
        : [
            { label: "Rename", action: () => this.renameFolder(f) },
            { label: f.favourite ? "Remove from favourites" : "Add to favourites", action: () => this.run(() => api.setFolderFavourite(f.id, !f.favourite)) },
            { label: "Move to…", action: () => this.moveFolder(f) },
            { label: "Move to trash", danger: true, action: () => this.run(() => api.setFolderTrashed(f.id, true), "Folder moved to trash") },
          ];
      popupMenu(more, items);
    });
    return h(
      "article",
      { class: "card folder-card" },
      h(
        "button",
        { class: "card-open", onclick: () => !trash && this.show({ kind: "folder", id: f.id }), "aria-label": `Open folder ${f.name}` },
        h("div", { class: "folder-cover", style: { color: f.color } }, icon("folder")),
        h("div", { class: "card-title" }, f.favourite ? "★ " : "", f.name),
        h("div", { class: "card-meta" }, "Folder"),
      ),
      more,
    );
  }

  private notebookCard(n: Notebook): HTMLElement {
    const trash = this.view.kind === "trash";
    const more = h("button", { class: "icon-btn card-more", "aria-label": `Options for ${n.title}` }, icon("more"));
    more.addEventListener("click", (e) => {
      e.stopPropagation();
      const items: MenuItem[] = trash
        ? [
            { label: "Recover", action: () => this.run(() => api.setNotebookTrashed(n.id, false), "Notebook recovered") },
            { label: "Delete forever", danger: true, action: () => this.deleteForever("notebook", n.id, n.title) },
          ]
        : [
            { label: "Rename", action: () => this.renameNotebook(n) },
            { label: n.favourite ? "Remove from favourites" : "Add to favourites", action: () => this.run(() => api.setNotebookFavourite(n.id, !n.favourite)) },
            { label: "Change cover", action: () => this.changeCover(n) },
            { label: "Duplicate", action: () => this.run(() => api.duplicateNotebook(n.id), "Notebook duplicated") },
            { label: "Move to…", action: () => this.moveNotebook(n) },
            { label: "Move to trash", danger: true, action: () => this.run(() => api.setNotebookTrashed(n.id, true), "Notebook moved to trash") },
          ];
      popupMenu(more, items);
    });
    const cover = h(
      "div",
      { class: "nb-cover", style: { background: n.coverColor } },
      n.thumbnail ? h("img", { src: n.thumbnail, alt: "", class: "nb-thumb" }) : h("div", { class: "nb-label" }, n.title),
    );
    return h(
      "article",
      { class: "card" },
      h(
        "button",
        { class: "card-open", onclick: () => !trash && this.cb.openNotebook(n.id), "aria-label": `Open ${n.title}` },
        cover,
        h("div", { class: "card-title" }, n.favourite ? "★ " : "", n.title),
        h("div", { class: "card-meta" }, `${n.pageCount} ${n.pageCount === 1 ? "page" : "pages"} · ${relativeTime(n.updatedAt)}`),
      ),
      more,
    );
  }

  private async run(fn: () => Promise<unknown>, message?: string) {
    try {
      await fn();
      if (message) toast(message);
      await this.refresh();
    } catch (e) {
      reportError(e);
    }
  }

  private folderOptions(exclude?: string) {
    // Exclude a folder and its descendants (cannot move into itself).
    const blocked = new Set<string>();
    if (exclude) {
      blocked.add(exclude);
      let grew = true;
      while (grew) {
        grew = false;
        for (const f of this.data.folders) if (f.parentId && blocked.has(f.parentId) && !blocked.has(f.id)) (blocked.add(f.id), (grew = true));
      }
    }
    return [{ value: "", label: "Library (no folder)" }, ...this.data.folders.filter((f) => !blocked.has(f.id)).map((f) => ({ value: f.id, label: f.name }))];
  }

  private async newNotebook(folderId: string | null) {
    const r = await formDialog(
      "New notebook",
      [
        { name: "title", label: "Title", value: "Untitled notebook" },
        { name: "template", label: "Paper", type: "select", value: "ruled", options: TEMPLATES.map((t) => ({ value: t.id, label: t.label })) },
        { name: "size", label: "Page size", type: "select", value: "a4", options: PAPER_SIZES.map((p) => ({ value: p.id, label: p.label })) },
        { name: "orientation", label: "Orientation", type: "select", value: "portrait", options: [{ value: "portrait", label: "Portrait" }, { value: "landscape", label: "Landscape" }] },
        { name: "paper", label: "Paper colour", type: "color-swatches", value: "#fffdf7", colors: PAPER_COLORS },
        { name: "cover", label: "Cover", type: "color-swatches" },
      ],
      "Create",
    );
    if (!r) return;
    try {
      const size = PAPER_SIZES.find((p) => p.id === r.size) ?? PAPER_SIZES[0];
      const landscape = r.orientation === "landscape";
      const nb = await api.createNotebook({
        title: r.title,
        folderId,
        template: r.template as Template,
        coverColor: r.cover,
        paperColor: r.paper,
        width: landscape ? size.h : size.w,
        height: landscape ? size.w : size.h,
      });
      this.cb.openNotebook(nb.id);
    } catch (e) {
      reportError(e);
    }
  }

  /** Imports a PDF as a notebook: one page per PDF page, ready to annotate. */
  private async importPdf(file: File | undefined) {
    this.pdfInput.value = "";
    if (!file) return;
    if (file.size > 300 * 1024 * 1024) return toast("PDFs larger than 300 MB aren't supported", "error");
    const folderId = this.view.kind === "folder" ? this.view.id : null;
    toast(`Importing ${file.name}…`);
    try {
      const bytes = new Uint8Array(await file.arrayBuffer());
      const { pdfPageSizes } = await import("./editor/pdfRender");
      const pages = await pdfPageSizes(bytes);
      const assetId = await api.storePdfAsset(bytes);
      const title = file.name.replace(/\.pdf$/i, "").trim() || "Imported PDF";
      const nb = await api.createPdfNotebook({ title: title.slice(0, 200), folderId, assetId, pages });
      toast(`Imported ${pages.length} page${pages.length === 1 ? "" : "s"}`);
      this.cb.openNotebook(nb.id);
    } catch (e) {
      const name = (e as { name?: string })?.name;
      if (name === "PasswordException") toast("This PDF is password-protected, so it can't be imported", "error");
      else if (name === "InvalidPDFException") toast("That file isn't a valid PDF", "error");
      else reportError(e);
    }
  }

  private async newFolder(parentId: string | null) {
    const r = await formDialog("New folder", [
      { name: "name", label: "Name", value: "New folder" },
      { name: "color", label: "Colour", type: "color-swatches" },
    ], "Create");
    if (r) await this.run(() => api.createFolder(r.name, parentId, r.color));
  }

  private async renameFolder(f: Folder) {
    const r = await formDialog("Rename folder", [{ name: "name", label: "Name", value: f.name }]);
    if (r) await this.run(() => api.renameFolder(f.id, r.name));
  }

  private async renameNotebook(n: Notebook) {
    const r = await formDialog("Rename notebook", [{ name: "title", label: "Title", value: n.title }]);
    if (r) await this.run(() => api.renameNotebook(n.id, r.title));
  }

  private async changeCover(n: Notebook) {
    const r = await formDialog("Change cover", [{ name: "cover", label: "Cover", type: "color-swatches", value: n.coverColor }]);
    if (r) await this.run(() => api.setNotebookCover(n.id, r.cover));
  }

  private async moveNotebook(n: Notebook) {
    const r = await formDialog("Move notebook", [{ name: "folder", label: "Destination", type: "select", value: n.folderId ?? "", options: this.folderOptions() }], "Move");
    if (r) await this.run(() => api.moveNotebook(n.id, r.folder || null), "Notebook moved");
  }

  private async moveFolder(f: Folder) {
    const r = await formDialog("Move folder", [{ name: "folder", label: "Destination", type: "select", value: f.parentId ?? "", options: this.folderOptions(f.id) }], "Move");
    if (r) await this.run(() => api.moveFolder(f.id, r.folder || null), "Folder moved");
  }

  private async deleteForever(kind: "folder" | "notebook", id: string, name: string) {
    const ok = await confirmDialog(`Delete “${name}” forever?`, kind === "folder" ? "The folder and every notebook inside it will be permanently deleted." : "This notebook and all of its pages will be permanently deleted.");
    if (ok) await this.run(() => (kind === "folder" ? api.deleteFolder(id) : api.deleteNotebook(id)), "Deleted");
  }

  private async emptyTrash() {
    if (await confirmDialog("Empty trash?", "Everything in the trash will be permanently deleted.", "Empty trash")) await this.run(() => api.emptyTrash(), "Trash emptied");
  }
}
