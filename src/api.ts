// Typed access to the Rust backend. Inside the Tauri shell calls go through
// `invoke`; in a plain browser (vite dev / UI tests) an in-memory mock with
// the same contract is used so the UI can be developed without the shell.

import { invoke } from "@tauri-apps/api/core";
import { MockBackend } from "./mockBackend";

export type Template = "blank" | "ruled" | "narrow-ruled" | "grid" | "dotted" | "cornell";
export const TEMPLATES: { id: Template; label: string }[] = [
  { id: "blank", label: "Blank" },
  { id: "ruled", label: "Ruled" },
  { id: "narrow-ruled", label: "Narrow ruled" },
  { id: "grid", label: "Grid" },
  { id: "dotted", label: "Dotted" },
  { id: "cornell", label: "Cornell" },
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
}

export type P2 = [number, number];
export type RecognizedShape =
  | { kind: "line" | "triangle" | "rectangle" | "polygon"; points: P2[] }
  | { kind: "ellipse"; cx: number; cy: number; rx: number; ry: number };

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
  recognizeShape(points: P2[]): Promise<RecognizedShape | null>;
  simplifyStroke(points: [number, number, number][], epsilon: number): Promise<[number, number, number][]>;
  exportPng(fileName: string, dataUrl: string): Promise<string>;
}

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
  recognizeShape = (points: P2[]) => invoke<RecognizedShape | null>("recognize_shape", { points });
  simplifyStroke = (points: [number, number, number][], epsilon: number) =>
    invoke<[number, number, number][]>("simplify_stroke", { points, epsilon });
  exportPng = (fileName: string, dataUrl: string) => invoke<string>("export_png", { fileName, dataUrl });
}

export const isTauri = typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;

export const api: Backend = isTauri ? new TauriBackend() : new MockBackend();
