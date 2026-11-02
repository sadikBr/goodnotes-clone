// Typed access to the Rust backend. Inside the Tauri shell calls go through
// `invoke`; in a plain browser (vite dev / UI tests) an in-memory mock with
// the same contract is used so the UI can be developed without the shell.

import { invoke } from "@tauri-apps/api/core";
import { save } from "@tauri-apps/plugin-dialog";
import { MockBackend } from "./mockBackend";

export type Template =
  | "blank"
  | "ruled"
  | "narrow-ruled"
  | "wide-ruled"
  | "grid"
  | "dotted"
  | "cornell"
  | "legal"
  | "two-column"
  | "checklist"
  | "graph"
  | "coordinate"
  | "isometric"
  | "hexagon"
  | "handwriting"
  | "calligraphy"
  | "storyboard"
  | "planner-day"
  | "planner-week"
  /** Teacher's lesson log (دفتر النصوص / cahier de textes). */
  | "daftar"
  /** Notebook cover designs (page 1 of new notebooks); not offered in the paper picker. */
  | "cover"
  | "cover-minimal"
  | "cover-geometric"
  | "cover-waves"
  | "cover-marble"
  | "cover-leather"
  | "cover-kraft"
  | "cover-dots";

export type TemplateCategory = "basic" | "notes" | "science" | "writing" | "planner" | "teacher";

/** `short` is the tab label in the paper picker. */
export const TEMPLATE_CATEGORIES: { id: TemplateCategory; label: string; short: string }[] = [
  { id: "basic", label: "Basic", short: "Basic" },
  { id: "notes", label: "Notes", short: "Notes" },
  { id: "science", label: "Maths & science", short: "Maths" },
  { id: "writing", label: "Writing & art", short: "Writing" },
  { id: "planner", label: "Planner", short: "Planner" },
  { id: "teacher", label: "Teacher", short: "Teacher" },
];

/**
 * Paper styles. `preview: "page"` shows the whole page in the picker (for
 * structured layouts); otherwise a close-up of the top-left corner.
 */
export const TEMPLATES: { id: Template; label: string; category: TemplateCategory; preview?: "page" }[] = [
  { id: "blank", label: "Blank", category: "basic" },
  { id: "ruled", label: "Ruled", category: "basic" },
  { id: "narrow-ruled", label: "Narrow ruled", category: "basic" },
  { id: "wide-ruled", label: "Wide ruled", category: "basic" },
  { id: "grid", label: "Grid", category: "basic" },
  { id: "dotted", label: "Dotted", category: "basic" },
  { id: "cornell", label: "Cornell", category: "notes", preview: "page" },
  { id: "legal", label: "Legal pad", category: "notes" },
  { id: "two-column", label: "Two columns", category: "notes", preview: "page" },
  { id: "checklist", label: "To-do list", category: "notes" },
  { id: "graph", label: "Graph", category: "science" },
  { id: "coordinate", label: "Coordinates", category: "science", preview: "page" },
  { id: "isometric", label: "Isometric", category: "science" },
  { id: "hexagon", label: "Hexagon", category: "science" },
  { id: "handwriting", label: "Handwriting", category: "writing" },
  { id: "calligraphy", label: "Calligraphy", category: "writing" },
  { id: "storyboard", label: "Storyboard", category: "writing", preview: "page" },
  { id: "planner-day", label: "Daily plan", category: "planner", preview: "page" },
  { id: "planner-week", label: "Weekly plan", category: "planner", preview: "page" },
  { id: "daftar", label: "Daftar annossous", category: "teacher", preview: "page" },
];

export interface Folder {
  id: string;
  name: string;
  parentId: string | null;
  color: string;
  favourite: boolean;
  createdAt: number;
  updatedAt: number;
  trashedAt: number | null;
}

export interface Notebook {
  id: string;
  title: string;
  folderId: string | null;
  coverColor: string;
  defaultTemplate: Template;
  favourite: boolean;
  pageCount: number;
  thumbnail: string | null;
  createdAt: number;
  updatedAt: number;
  trashedAt: number | null;
  /** The first page is a cover (shown full-size in the library). */
  hasCover: boolean;
  /** App-defined settings as JSON (e.g. a daftar's classes and timetable). */
  meta: string | null;
}

export interface Page {
  id: string;
  notebookId: string;
  position: number;
  template: Template;
  paperColor: string;
  width: number;
  height: number;
  favourite: boolean;
  thumbnail: string | null;
  createdAt: number;
  updatedAt: number;
  /** Imported PDF shown behind this page, and which page of it (0-based). */
  backgroundAsset: string | null;
  backgroundPage: number | null;
  /** The notebook section the page belongs to (a class, in a daftar). */
  section: string | null;
}

export interface StoredElement {
  id: string;
  kind: string;
  z: number;
  data: unknown;
}

export interface Library {
  folders: Folder[];
  notebooks: Notebook[];
}

export interface SearchHit {
  notebookId: string;
  notebookTitle: string;
  pageId: string | null;
  pageNumber: number | null;
  snippet: string;
}

export interface NewNotebook {
  title: string;
  folderId?: string | null;
  coverColor?: string;
  template?: Template;
  paperColor?: string;
  /** Page size in units of 1/96 inch. */
  width?: number;
  height?: number;
  /** Start with a cover page in the cover colour. */
  coverPage?: boolean;
}

/** Paper sizes in units of 1/96 inch (portrait). */
export const PAPER_SIZES: { id: string; label: string; w: number; h: number }[] = [
  { id: "a4", label: "A4", w: 794, h: 1123 },
  { id: "a5", label: "A5", w: 559, h: 794 },
  { id: "letter", label: "US Letter", w: 816, h: 1056 },
  { id: "legal", label: "US Legal", w: 816, h: 1344 },
  { id: "square", label: "Square", w: 900, h: 900 },
];

export const PAPER_COLORS: { color: string; label: string }[] = [
  { color: "#ffffff", label: "White" },
  { color: "#fffdf7", label: "Cream" },
  { color: "#fdf3c8", label: "Yellow" },
  { color: "#eef1f4", label: "Grey" },
  { color: "#22262d", label: "Dark" },
];

export type P2 = [number, number];
export type RecognizedShape =
  | { kind: "line" | "arrow" | "triangle" | "rectangle" | "polygon"; points: P2[] }
  | { kind: "ellipse"; cx: number; cy: number; rx: number; ry: number }
  /** A smoothed freehand path (graphs, curves, braces...). */
  | { kind: "path"; points: P2[]; closed: boolean };

export type ExportFormat = "pdf" | "pptx" | "png";

export interface HandwritingRecognition {
  /** False when this computer has no handwriting recognizer. */
  available: boolean;
  /** Best guess first. */
  candidates: string[];
}

/** One rendered page handed to the exporter. */
export interface ExportPage {
  dataUrl: string;
  widthPx: number;
  heightPx: number;
  pageWidth: number;
  pageHeight: number;
}

export interface Backend {
  library(): Promise<Library>;
  trash(): Promise<Library>;
  emptyTrash(): Promise<void>;
  createFolder(name: string, parentId: string | null, color?: string): Promise<Folder>;
  renameFolder(id: string, name: string): Promise<void>;
  moveFolder(id: string, parentId: string | null): Promise<void>;
  setFolderTrashed(id: string, trashed: boolean): Promise<void>;
  deleteFolder(id: string): Promise<void>;
  setFolderFavourite(id: string, favourite: boolean): Promise<void>;
  createNotebook(input: NewNotebook): Promise<Notebook>;
  notebook(id: string): Promise<Notebook>;
  renameNotebook(id: string, title: string): Promise<void>;
  moveNotebook(id: string, folderId: string | null): Promise<void>;
  setNotebookCover(id: string, color: string): Promise<void>;
  setNotebookMeta(id: string, meta: string | null): Promise<void>;
  setPageSection(pageId: string, section: string | null): Promise<void>;
  setNotebookTrashed(id: string, trashed: boolean): Promise<void>;
  deleteNotebook(id: string): Promise<void>;
  setNotebookFavourite(id: string, favourite: boolean): Promise<void>;
  duplicateNotebook(id: string): Promise<Notebook>;
  pages(notebookId: string): Promise<Page[]>;
  addPage(notebookId: string, index: number | null, template: Template | null): Promise<Page>;
  duplicatePage(pageId: string): Promise<Page>;
  deletePage(pageId: string): Promise<void>;
  movePage(pageId: string, newIndex: number): Promise<void>;
  setPageTemplate(pageId: string, template: Template, paperColor: string | null): Promise<void>;
  setPageFavourite(pageId: string, favourite: boolean): Promise<void>;
  setPageThumbnail(pageId: string, thumbnail: string): Promise<void>;
  elements(pageId: string): Promise<StoredElement[]>;
  saveElements(pageId: string, elements: StoredElement[]): Promise<void>;
  search(query: string): Promise<SearchHit[]>;
  simplifyStroke(points: [number, number, number][], epsilon: number): Promise<[number, number, number][]>;
  /** Asks where to save; resolves to null when the user cancels. */
  chooseExportPath(fileName: string, format: ExportFormat): Promise<string | null>;
  exportDocument(path: string, format: ExportFormat, pages: ExportPage[]): Promise<string>;
  /** Writes a finished PDF (vector export) to `path`; returns the final path. */
  savePdfExport(path: string, bytes: Uint8Array): Promise<string>;
  recognizeHandwriting(strokes: P2[][]): Promise<HandwritingRecognition>;
  /** Stores PDF bytes; returns an asset id for createPdfNotebook. */
  storePdfAsset(bytes: Uint8Array): Promise<string>;
  createPdfNotebook(input: NewPdfNotebook): Promise<Notebook>;
  assetBytes(id: string): Promise<Uint8Array>;
}

export interface NewPdfNotebook {
  title: string;
  folderId: string | null;
  coverColor?: string;
  assetId: string;
  /** Page sizes in units of 1/96 inch. */
  pages: { width: number; height: number }[];
}

const FORMAT_FILTERS: Record<ExportFormat, { name: string; extensions: string[] }> = {
  pdf: { name: "PDF document", extensions: ["pdf"] },
  pptx: { name: "PowerPoint presentation", extensions: ["pptx"] },
  png: { name: "PNG image", extensions: ["png"] },
};

class TauriBackend implements Backend {
  library = () => invoke<Library>("library");
  trash = () => invoke<Library>("trash");
  emptyTrash = () => invoke<void>("empty_trash");
  createFolder = (name: string, parentId: string | null, color?: string) =>
    invoke<Folder>("create_folder", { name, parentId, color });
  renameFolder = (id: string, name: string) => invoke<void>("rename_folder", { id, name });
  moveFolder = (id: string, parentId: string | null) => invoke<void>("move_folder", { id, parentId });
  setFolderTrashed = (id: string, trashed: boolean) => invoke<void>("set_folder_trashed", { id, trashed });
  deleteFolder = (id: string) => invoke<void>("delete_folder", { id });
  setFolderFavourite = (id: string, favourite: boolean) => invoke<void>("set_folder_favourite", { id, favourite });
  createNotebook = (input: NewNotebook) => invoke<Notebook>("create_notebook", { input });
  notebook = (id: string) => invoke<Notebook>("notebook", { id });
  renameNotebook = (id: string, title: string) => invoke<void>("rename_notebook", { id, title });
  moveNotebook = (id: string, folderId: string | null) => invoke<void>("move_notebook", { id, folderId });
  setNotebookCover = (id: string, color: string) => invoke<void>("set_notebook_cover", { id, color });
  setNotebookMeta = (id: string, meta: string | null) => invoke<void>("set_notebook_meta", { id, meta });
  setPageSection = (pageId: string, section: string | null) => invoke<void>("set_page_section", { pageId, section });
  setNotebookTrashed = (id: string, trashed: boolean) => invoke<void>("set_notebook_trashed", { id, trashed });
  deleteNotebook = (id: string) => invoke<void>("delete_notebook", { id });
  setNotebookFavourite = (id: string, favourite: boolean) => invoke<void>("set_notebook_favourite", { id, favourite });
  duplicateNotebook = (id: string) => invoke<Notebook>("duplicate_notebook", { id });
  pages = (notebookId: string) => invoke<Page[]>("pages", { notebookId });
  addPage = (notebookId: string, index: number | null, template: Template | null) =>
    invoke<Page>("add_page", { notebookId, index, template });
  duplicatePage = (pageId: string) => invoke<Page>("duplicate_page", { pageId });
  deletePage = (pageId: string) => invoke<void>("delete_page", { pageId });
  movePage = (pageId: string, newIndex: number) => invoke<void>("move_page", { pageId, newIndex });
  setPageTemplate = (pageId: string, template: Template, paperColor: string | null) =>
    invoke<void>("set_page_template", { pageId, template, paperColor });
  setPageFavourite = (pageId: string, favourite: boolean) => invoke<void>("set_page_favourite", { pageId, favourite });
  setPageThumbnail = (pageId: string, thumbnail: string) => invoke<void>("set_page_thumbnail", { pageId, thumbnail });
  elements = (pageId: string) => invoke<StoredElement[]>("elements", { pageId });
  saveElements = (pageId: string, elements: StoredElement[]) => invoke<void>("save_elements", { pageId, elements });
  search = (query: string) => invoke<SearchHit[]>("search", { query });
  simplifyStroke = (points: [number, number, number][], epsilon: number) =>
    invoke<[number, number, number][]>("simplify_stroke", { points, epsilon });
  chooseExportPath = async (fileName: string, format: ExportFormat) => {
    const defaultPath = await invoke<string>("default_export_dir", { fileName, format });
    return save({ title: "Export", defaultPath, filters: [FORMAT_FILTERS[format]] });
  };
  exportDocument = (path: string, format: ExportFormat, pages: ExportPage[]) =>
    invoke<string>("export_document", { path, format, pages });
  savePdfExport = (path: string, bytes: Uint8Array) => invoke<string>("save_pdf_export", bytes, { headers: { "x-export-path": encodeURIComponent(path) } });
  recognizeHandwriting = (strokes: P2[][]) => invoke<HandwritingRecognition>("recognize_handwriting", { strokes });
  // Raw bytes go over IPC as a binary body, not JSON.
  storePdfAsset = (bytes: Uint8Array) => invoke<string>("store_pdf_asset", bytes);
  createPdfNotebook = (input: NewPdfNotebook) => invoke<Notebook>("create_pdf_notebook", { input });
  assetBytes = async (id: string) => new Uint8Array(await invoke<ArrayBuffer>("asset_bytes", { id }));
}

export const isTauri = typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;

export const api: Backend = isTauri ? new TauriBackend() : new MockBackend();
