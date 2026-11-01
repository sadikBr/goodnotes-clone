import "./styles.css";
import { EditorScreen } from "./editor/editor";
import { LibraryScreen } from "./library";
import { h, icon } from "./ui";

const app = document.getElementById("app")!;

const THEME_KEY = "inkleaf-theme";
function applyTheme(theme: string | null) {
  if (theme === "light" || theme === "dark") document.documentElement.dataset.theme = theme;
  else delete document.documentElement.dataset.theme;
}
function storedTheme(): string | null {
  try {
    return localStorage.getItem(THEME_KEY);
  } catch {
    return null;
  }
}
applyTheme(storedTheme());

const themeBtn = h("button", { class: "icon-btn theme-toggle", "aria-label": "Toggle dark mode", title: "Toggle dark mode" }, icon("moon"));
themeBtn.addEventListener("click", () => {
  const dark = document.documentElement.dataset.theme
    ? document.documentElement.dataset.theme === "dark"
    : window.matchMedia("(prefers-color-scheme: dark)").matches;
  const next = dark ? "light" : "dark";
  applyTheme(next);
  try {
    localStorage.setItem(THEME_KEY, next);
  } catch {
    /* preference is per session only */
  }
});

let editor: EditorScreen | null = null;

const library = new LibraryScreen({
  openNotebook: (id, pageId) => openEditor(id, pageId ?? null),
});

async function showLibrary() {
  if (editor) {
    await editor.flush();
    editor = null;
  }
  app.replaceChildren(library.root, themeBtn);
  await library.show();
}

async function openEditor(id: string, pageId: string | null) {
  editor = new EditorScreen({ close: () => void showLibrary() });
  app.replaceChildren(editor.root);
  await editor.open(id, pageId);
}

document.addEventListener("keydown", (e) => {
  if (editor) editor.handleKey(e);
});
document.addEventListener("paste", (e) => {
  if (editor) editor.handlePaste(e);
});
window.addEventListener("beforeunload", () => {
  void editor?.flush();
});

void showLibrary();
