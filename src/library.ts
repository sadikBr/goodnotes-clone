// Library screen: folders, notebooks, favourites, trash and search.

import { applyCover, coverPickerField, coverPreviewPanel, setUpCover } from "./cover";
import { coverDesignOf, coverTemplate, isCoverTemplate, type CoverDesign } from "./editor/covers";
import { api, PAPER_COLORS, PAPER_SIZES, TEMPLATE_CATEGORIES, TEMPLATES, type Folder, type Library, type Notebook, type Template } from "./api";
import { confirmDialog, formDialog, h, icon, popupMenu, relativeTime, reportError, toast, type MenuItem } from "./ui";
import { getPref, setPref } from "./prefs";
import { isDark, onThemeChange, toggleTheme } from "./theme";

type SortKey = "recent" | "name" | "created";
const SORT_KEY = "inkleaf-library-sort";
const SORTS: { id: SortKey; label: string }[] = [
  { id: "recent", label: "Recently edited" },
  { id: "created", label: "Recently created" },
  { id: "name", label: "Name" },
];

function sortNotebooks(list: Notebook[], key: SortKey): Notebook[] {
  const out = [...list];
  if (key === "name") out.sort((a, b) => a.title.localeCompare(b.title));
  else if (key === "created") out.sort((a, b) => b.createdAt - a.createdAt);
  else out.sort((a, b) => b.updatedAt - a.updatedAt);
  return out;
}

function greeting(): string {
  const hr = new Date().getHours();
  return hr < 5 ? "Good night" : hr < 12 ? "Good morning" : hr < 18 ? "Good afternoon" : "Good evening";
}

function folderMeta(f: Folder, data: Library): string {
  const n = data.notebooks.filter((x) => x.folderId === f.id).length;
  const sub = data.folders.filter((x) => x.parentId === f.id).length;
  const parts = [`${n} ${n === 1 ? "notebook" : "notebooks"}`];
  if (sub) parts.push(`${sub} ${sub === 1 ? "folder" : "folders"}`);
  return parts.join(" · ");
}

/** The brand mark: a leaf whose vein is a pen nib's slit. */
const BRAND_MARK = `<svg viewBox="0 0 32 32" width="30" height="30" aria-hidden="true">
  <rect width="32" height="32" rx="9" fill="var(--accent)"/>
  <path d="M9 23c0-8 5-13.5 14-14.5-.6 8.8-6 14-14 14.5z" fill="var(--accent-contrast)" opacity="0.95"/>
  <path d="M9.5 22.5l8-8" stroke="var(--accent)" stroke-width="1.6" stroke-linecap="round"/>
  <circle cx="18.6" cy="13.4" r="1.4" fill="var(--accent)"/>
</svg>`;

/** A small illustration for empty views: a stack of notebooks and a pen. */
const EMPTY_ART = `<svg viewBox="0 0 160 120" width="160" height="120" aria-hidden="true">
  <ellipse cx="80" cy="108" rx="58" ry="7" fill="currentColor" opacity="0.08"/>
  <g transform="rotate(-8 60 70)"><rect x="34" y="28" width="52" height="70" rx="5" fill="var(--empty-a)"/><rect x="34" y="28" width="8" height="70" rx="3" fill="#000" opacity="0.15"/><rect x="48" y="44" width="30" height="12" rx="3" fill="#fff" opacity="0.9"/></g>
  <g transform="rotate(6 98 66)"><rect x="70" y="22" width="54" height="74" rx="5" fill="var(--empty-b)"/><rect x="70" y="22" width="8" height="74" rx="3" fill="#000" opacity="0.15"/><rect x="84" y="38" width="32" height="13" rx="3" fill="#fff" opacity="0.92"/><path d="M84 66h30M84 74h24" stroke="#fff" stroke-opacity="0.5" stroke-width="2" stroke-linecap="round"/></g>
  <g transform="rotate(38 128 40)"><rect x="122" y="6" width="9" height="58" rx="3" fill="var(--empty-c)"/><path d="M122 64l4.5 10 4.5-10z" fill="var(--empty-c)"/><rect x="122" y="6" width="9" height="10" rx="3" fill="#000" opacity="0.2"/></g>
</svg>`;

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
  private searchInput: HTMLInputElement | null = null;
  private sort: SortKey = getPref<SortKey>(SORT_KEY, "recent", ["recent", "name", "created"]);

  /** Library shortcuts: / to search, N for a new notebook. */
  handleKey(e: KeyboardEvent) {
    if ((e.target as HTMLElement).closest("input, textarea, select, .modal") || e.ctrlKey || e.metaKey || e.altKey) return;
    if (e.key === "/") {
      e.preventDefault();
      this.searchInput?.focus();
    } else if (e.key.toLowerCase() === "n" && this.view.kind !== "trash") {
      e.preventDefault();
      void this.newNotebook(this.view.kind === "folder" ? this.view.id : null);
    }
  }

  /** A line under the heading: how many notebooks, and what was edited last. */
  private summary(): string {
    const nbs = this.view.kind === "folder" ? this.data.notebooks.filter((n) => n.folderId === (this.view as { id: string }).id) : this.view.kind === "favourites" ? this.data.notebooks.filter((n) => n.favourite) : this.data.notebooks;
    if (this.view.kind === "trash") return nbs.length ? `${nbs.length} ${nbs.length === 1 ? "notebook" : "notebooks"} in the trash` : "Nothing in the trash";
    if (!nbs.length) return this.view.kind === "all" ? "Let's start something new" : "Nothing here yet";
    const latest = [...nbs].sort((a, b) => b.updatedAt - a.updatedAt)[0];
    return `${nbs.length} ${nbs.length === 1 ? "notebook" : "notebooks"} · last edited “${latest.title}” ${relativeTime(latest.updatedAt)}`;
  }

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
    const all = this.view.kind === "trash" ? null : this.data;
    const count = (n: number | undefined) => (n ? h("span", { class: "nav-count" }, String(n)) : undefined);
    const themeSwitch = h(
      "button",
      { class: "nav-item theme-switch", role: "switch", "aria-checked": String(isDark()), onclick: () => toggleTheme() },
      icon("moon"),
      h("span", { class: "nav-label" }, "Dark mode"),
      h("span", { class: "switch" }, h("span", {})),
    );
    onThemeChange(() => themeSwitch.setAttribute("aria-checked", String(isDark())));
    const folderId = this.view.kind === "folder" ? this.view.id : null;
    return h(
      "aside",
      { class: "sidebar" },
      h("div", { class: "brand" }, h("span", { class: "brand-mark", html: BRAND_MARK }), h("span", { class: "brand-name" }, "Inkleaf")),
      h("button", { class: "btn primary new-btn", title: "New notebook (N)", onclick: () => this.newNotebook(folderId) }, icon("plus"), h("span", { class: "new-label" }, "New notebook")),
      h(
        "nav",
        { "aria-label": "Library" },
        item("All notebooks", "book", { kind: "all" }, count(all?.notebooks.length)),
        item("Favourites", "star", { kind: "favourites" }, count(all ? all.notebooks.filter((n) => n.favourite).length + all.folders.filter((f) => f.favourite).length : undefined)),
      ),
      h(
        "div",
        { class: "sidebar-section" },
        h("span", {}, "Folders"),
        h("button", { class: "icon-btn small", title: "New folder", "aria-label": "New folder", onclick: () => this.newFolder(null) }, icon("plus")),
      ),
      folders.length ? tree : h("p", { class: "sidebar-hint" }, "Group notebooks into folders with +"),
      h("div", { class: "sidebar-spacer" }),
      h("div", { class: "sidebar-footer" }, item("Trash", "trash", { kind: "trash" }), themeSwitch),
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
      placeholder: "Search notebooks and text…",
      "aria-label": "Search",
      value: this.query,
    });
    let debounce: number | undefined;
    search.addEventListener("input", () => {
      this.query = search.value;
      window.clearTimeout(debounce);
      debounce = window.setTimeout(() => this.renderResults(results), 200);
    });
    this.searchInput = search;
    const actions = h("div", { class: "header-actions" });
    if (this.view.kind === "trash") {
      actions.append(h("button", { class: "btn danger", onclick: () => this.emptyTrash() }, icon("trash"), "Empty trash"));
    } else {
      const folderId = this.view.kind === "folder" ? this.view.id : null;
      const sortBtn = h("button", { class: "btn ghost", title: "Sort notebooks", "aria-haspopup": "menu" }, icon("sort"), SORTS.find((x) => x.id === this.sort)!.label);
      sortBtn.addEventListener("click", () =>
        popupMenu(
          sortBtn,
          SORTS.map((x) => ({
            label: x.label,
            checked: this.sort === x.id,
            action: () => {
              this.sort = x.id;
              setPref(SORT_KEY, x.id);
              void this.refresh();
            },
          })),
        ),
      );
      actions.append(
        sortBtn,
        h("button", { class: "btn ghost", onclick: () => this.pdfInput.click(), title: "Open a PDF as a notebook you can write on" }, icon("page"), "Import PDF"),
        h("button", { class: "btn ghost", onclick: () => this.newFolder(folderId) }, icon("folder"), "New folder"),
      );
    }
    const results = h("section", { class: "content", "aria-live": "polite" });
    this.renderResults(results);
    const heading = this.view.kind === "all" ? greeting() : this.title();
    return h(
      "main",
      { class: "library-main" },
      h(
        "header",
        { class: "library-header" },
        h("div", { class: "library-heading" }, h("h1", {}, heading), h("p", { class: "library-sub" }, this.summary())),
        h("div", { class: "library-tools" }, h("label", { class: "search-wrap" }, icon("search"), search, h("kbd", { class: "search-key" }, "/")), actions),
      ),
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
    notebooks = sortNotebooks(notebooks, this.sort);
    folders = [...folders].sort((a, b) => a.name.localeCompare(b.name));
    const canCreate = v.kind === "all" || v.kind === "folder";
    if (folders.length === 0 && notebooks.length === 0) {
      const folderId = v.kind === "folder" ? v.id : null;
      const [title, text] =
        v.kind === "trash"
          ? ["Trash is empty", "Notebooks and folders you delete wait here before they're gone for good."]
          : v.kind === "favourites"
            ? ["No favourites yet", "Star a notebook or folder from its ⋯ menu to keep it close at hand."]
            : v.kind === "folder"
              ? ["This folder is empty", "Create a notebook here, or move one in from its ⋯ menu."]
              : ["Your shelf is empty", "Create your first notebook — pick a cover, choose your paper and start writing."];
      return [
        h(
          "div",
          { class: "empty-state" },
          h("div", { class: "empty-art", html: EMPTY_ART }),
          h("h2", {}, title),
          h("p", {}, text),
          canCreate
            ? h(
                "div",
                { class: "empty-actions" },
                h("button", { class: "btn primary", onclick: () => this.newNotebook(folderId) }, icon("plus"), "New notebook"),
                h("button", { class: "btn", onclick: () => this.pdfInput.click() }, icon("page"), "Import a PDF"),
              )
            : null,
        ),
      ];
    }
    const tile = canCreate
      ? h(
          "button",
          { class: "new-tile", onclick: () => this.newNotebook(v.kind === "folder" ? v.id : null), "aria-label": "New notebook" },
          h("span", { class: "new-tile-plus" }, icon("plus")),
          h("span", {}, "New notebook"),
        )
      : null;
    return [h("div", { class: "grid" }, tile, ...folders.map((f) => this.folderCard(f)), ...notebooks.map((n) => this.notebookCard(n)))];
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
        h("div", { class: "folder-cover", style: { "--folder": f.color } }, h("span", { class: "folder-tab" }), h("span", { class: "folder-front" }, icon("folder"))),
        h("div", { class: "card-title" }, f.favourite ? "★ " : "", f.name),
        h("div", { class: "card-meta" }, folderMeta(f, this.data)),
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
    // A cover page is the notebook's face: shown full-size. Otherwise the
    // first page peeks out of a plain cover in the cover colour.
    const cover =
      n.hasCover && n.thumbnail
        ? h("div", { class: "nb-cover full", style: { background: n.coverColor } }, h("img", { src: n.thumbnail, alt: "", class: "nb-face" }))
        : h(
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
        h("div", { class: "book" }, cover, n.favourite ? h("span", { class: "fav-badge", title: "Favourite" }, icon("star")) : null),
        h("div", { class: "card-title" }, n.title),
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
        { name: "template", label: "Paper", type: "select", value: "ruled", options: TEMPLATES.map((t) => ({ value: t.id, label: t.label, group: TEMPLATE_CATEGORIES.find((c) => c.id === t.category)?.label })) },
        { name: "size", label: "Page size", type: "select", value: "a4", options: PAPER_SIZES.map((p) => ({ value: p.id, label: p.label })) },
        { name: "orientation", label: "Orientation", type: "select", value: "portrait", options: [{ value: "portrait", label: "Portrait" }, { value: "landscape", label: "Landscape" }] },
        { name: "paper", label: "Paper colour", type: "color-swatches", value: "#fffdf7", colors: PAPER_COLORS },
        { name: "cover", label: "Cover colour", type: "color-swatches" },
        coverPickerField("classic"),
      ],
      "Create",
      { aside: (ctx) => coverPreviewPanel(ctx) },
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
        coverPage: true,
      });
      // The chosen cover design, its title and date, and its picture for the library.
      const design = (r.design || "classic") as CoverDesign;
      if (design !== "classic") {
        const [first] = await api.pages(nb.id);
        await api.setPageTemplate(first.id, coverTemplate(design), r.cover);
      }
      await setUpCover(nb).catch(reportError);
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

  /** Pick a cover design and colour (notebooks without a cover can get one). */
  private async changeCover(n: Notebook) {
    let current: CoverDesign | "none" = "none";
    if (n.hasCover) {
      const [first] = await api.pages(n.id);
      if (first && isCoverTemplate(first.template)) current = coverDesignOf(first.template);
    }
    const r = await formDialog(
      "Change cover",
      [
        { name: "title", label: "", type: "custom", value: n.title, render: () => h("span", { hidden: true }) },
        { name: "cover", label: "Cover colour", type: "color-swatches", value: n.coverColor },
        coverPickerField(current, !n.hasCover),
      ],
      "Save",
      { aside: (ctx) => coverPreviewPanel(ctx) },
    );
    if (!r) return;
    await this.run(async () => {
      if (r.design === "none") await api.setNotebookCover(n.id, r.cover);
      else await applyCover(n, r.design as CoverDesign, r.cover);
    });
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
