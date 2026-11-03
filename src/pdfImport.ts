// Importing PDFs: several at once, one after another, with a progress panel
// (each file's stage and an overall bar). Imported PDFs appear in the
// library; nothing is opened by itself — each finished file has an Open button.

import { api } from "./api";
import { h, icon } from "./ui";

const MAX_BYTES = 300 * 1024 * 1024;

type State =
  | { kind: "waiting" }
  | { kind: "reading"; f: number }
  | { kind: "pages"; done: number; total: number }
  | { kind: "saving" }
  | { kind: "done"; notebookId: string; pages: number }
  | { kind: "failed"; reason: string }
  | { kind: "cancelled" };

interface Job {
  file: File;
  folderId: string | null;
  state: State;
}

export interface ImportHooks {
  /** A notebook was added (refresh the library). */
  added(): void;
  open(notebookId: string): void;
}

/** Share of a file's import each stage stands for (reading, pages, saving). */
const W = { read: 0.3, pages: 0.25, save: 0.45 };

function fraction(s: State): number {
  switch (s.kind) {
    case "waiting":
      return 0;
    case "reading":
      return W.read * s.f;
    case "pages":
      return W.read + W.pages * (s.total ? s.done / s.total : 0);
    case "saving":
      return W.read + W.pages + W.save * 0.5;
    default:
      return 1;
  }
}

function label(s: State): string {
  switch (s.kind) {
    case "waiting":
      return "Waiting";
    case "reading":
      return `Reading the file… ${Math.round(s.f * 100)}%`;
    case "pages":
      return s.total ? `Reading pages… ${s.done} of ${s.total}` : "Opening the PDF…";
    case "saving":
      return "Saving into Inkleaf…";
    case "done":
      return `${s.pages} page${s.pages === 1 ? "" : "s"}`;
    case "failed":
      return s.reason;
    case "cancelled":
      return "Not imported";
  }
}

/** Reads a file, reporting how much has been read (0–1). */
async function readWithProgress(file: File, onProgress: (f: number) => void): Promise<Uint8Array> {
  const out = new Uint8Array(file.size);
  const reader = file.stream().getReader();
  let at = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    out.set(value, at);
    at += value.length;
    onProgress(file.size ? at / file.size : 1);
  }
  return at === out.length ? out : out.slice(0, at);
}

function reasonFor(e: unknown): string {
  const name = (e as { name?: string })?.name;
  if (name === "PasswordException") return "Password-protected: can't be imported";
  if (name === "InvalidPDFException") return "Not a valid PDF";
  return e instanceof Error ? e.message : String(e);
}

export class PdfImporter {
  private jobs: Job[] = [];
  private running = false;
  private panel: HTMLElement | null = null;
  private collapsed = false;

  constructor(private hooks: ImportHooks) {}

  /** Queues PDFs for import into `folderId` (the library's current folder, or the top level). */
  add(files: Iterable<File>, folderId: string | null) {
    for (const file of files) {
      const isPdf = file.type === "application/pdf" || /\.pdf$/i.test(file.name);
      const job: Job = { file, folderId, state: { kind: "waiting" } };
      if (!isPdf) job.state = { kind: "failed", reason: "Not a PDF file" };
      else if (file.size > MAX_BYTES) job.state = { kind: "failed", reason: "Larger than 300 MB" };
      this.jobs.push(job);
    }
    this.collapsed = false;
    this.render();
    void this.run();
  }

  private async run() {
    if (this.running) return;
    this.running = true;
    try {
      for (let job = this.next(); job; job = this.next()) await this.importOne(job);
    } finally {
      this.running = false;
      this.render();
    }
  }

  private next(): Job | undefined {
    return this.jobs.find((j) => j.state.kind === "waiting");
  }

  private set(job: Job, state: State) {
    job.state = state;
    this.render();
  }

  private async importOne(job: Job) {
    try {
      this.set(job, { kind: "reading", f: 0 });
      let last = 0;
      const bytes = await readWithProgress(job.file, (f) => {
        // Redraw at most every few percent.
        if (f - last >= 0.03 || f === 1) {
          last = f;
          this.set(job, { kind: "reading", f });
        }
      });
      const { pdfImportInfo } = await import("./editor/pdfRender");
      this.set(job, { kind: "pages", done: 0, total: 0 });
      // The first page's thumbnail is made now (same size as the editor's),
      // so the library shows the PDF without it being opened.
      const { sizes: pages, thumbnail } = await pdfImportInfo(bytes, 200, (done, total) => this.set(job, { kind: "pages", done, total }));
      this.set(job, { kind: "saving" });
      const assetId = await api.storePdfAsset(bytes);
      const title = job.file.name.replace(/\.pdf$/i, "").trim() || "Imported PDF";
      const nb = await api.createPdfNotebook({ title: title.slice(0, 200), folderId: job.folderId, assetId, pages });
      if (thumbnail) {
        const [first] = await api.pages(nb.id);
        if (first) await api.setPageThumbnail(first.id, thumbnail).catch(() => {});
      }
      this.set(job, { kind: "done", notebookId: nb.id, pages: pages.length });
      this.hooks.added();
    } catch (e) {
      this.set(job, { kind: "failed", reason: reasonFor(e) });
    }
  }

  private cancelWaiting() {
    for (const j of this.jobs) if (j.state.kind === "waiting") j.state = { kind: "cancelled" };
    this.render();
  }

  private close() {
    this.jobs = this.jobs.filter((j) => !["done", "failed", "cancelled"].includes(j.state.kind));
    if (!this.jobs.length) {
      this.panel?.remove();
      this.panel = null;
    } else this.render();
  }

  private render() {
    if (!this.jobs.length) return;
    const busy = this.jobs.some((j) => ["waiting", "reading", "pages", "saving"].includes(j.state.kind));
    const done = this.jobs.filter((j) => j.state.kind === "done").length;
    const failed = this.jobs.filter((j) => j.state.kind === "failed").length;
    const counted = this.jobs.filter((j) => j.state.kind !== "cancelled");
    const overall = counted.length ? counted.reduce((s, j) => s + fraction(j.state), 0) / counted.length : 1;
    const title = busy
      ? `Importing ${counted.length === 1 ? "a PDF" : `${counted.length} PDFs`} · ${done} of ${counted.length} done`
      : failed
        ? `${done} imported, ${failed} failed`
        : `${done === 1 ? "PDF" : `${done} PDFs`} imported`;

    const panel = (this.panel ??= h("div", { class: "import-panel", role: "status", "aria-live": "polite" }));
    if (!panel.isConnected) document.body.append(panel);
    panel.classList.toggle("collapsed", this.collapsed);

    const toggle = h("button", { class: "icon-btn small", "aria-label": this.collapsed ? "Show imports" : "Hide details", title: this.collapsed ? "Show" : "Hide details" }, icon(this.collapsed ? "up" : "down"));
    toggle.addEventListener("click", () => {
      this.collapsed = !this.collapsed;
      this.render();
    });
    const actions: HTMLElement[] = [toggle];
    if (busy && this.next()) {
      const stop = h("button", { class: "btn ghost small-btn" }, "Stop the rest");
      stop.addEventListener("click", () => this.cancelWaiting());
      actions.unshift(stop);
    }
    if (!busy) {
      const x = h("button", { class: "icon-btn small", "aria-label": "Close", title: "Close" }, icon("close"));
      x.addEventListener("click", () => this.close());
      actions.push(x);
    }
    const bar = h("div", { class: `import-bar${busy ? "" : " complete"}` }, h("span", { style: { width: `${Math.round(overall * 100)}%` } }));

    const rows = this.jobs.map((j) => {
      const s = j.state;
      const status = h("span", { class: `import-status ${s.kind}` }, label(s));
      const right: HTMLElement[] = [];
      if (s.kind === "done") {
        const open = h("button", { class: "btn small-btn" }, "Open");
        open.addEventListener("click", () => this.hooks.open(s.notebookId));
        right.push(open);
      }
      const mark = s.kind === "done" ? icon("check") : s.kind === "failed" ? icon("close") : icon("page");
      mark.classList.add("import-mark", s.kind);
      const fileBar =
        s.kind === "reading" || s.kind === "pages" || s.kind === "saving"
          ? h("div", { class: `import-bar thin${s.kind === "saving" ? " indeterminate" : ""}` }, h("span", { style: { width: `${Math.round((fraction(s) / 1) * 100)}%` } }))
          : null;
      return h(
        "li",
        { class: "import-row" },
        mark,
        h("div", { class: "import-text" }, h("span", { class: "import-name", title: j.file.name }, j.file.name), status, fileBar),
        ...right,
      );
    });

    panel.replaceChildren(
      h("div", { class: "import-head" }, icon("download"), h("strong", {}, title), h("div", { class: "import-actions" }, ...actions)),
      bar,
      this.collapsed ? h("span", {}) : h("ul", { class: "import-list" }, ...rows),
    );
  }
}
