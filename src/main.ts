import "./styles.css";
import { EditorScreen } from "./editor/editor";
import { LibraryScreen } from "./library";
import { installTooltips } from "./ui";
import { applyStoredTheme } from "./theme";
import { isRestoring, onFlushRequest } from "./appLifecycle";
import { startBackupTimer } from "./backupSettings";

const app = document.getElementById("app")!;

applyStoredTheme();
installTooltips();

let editor: EditorScreen | null = null;

const library = new LibraryScreen({
  openNotebook: (id, pageId) => openEditor(id, pageId ?? null),
});

async function showLibrary() {
  if (editor) {
    await editor.flush();
    editor = null;
  }
  app.replaceChildren(library.root);
  await library.show();
}

async function openEditor(id: string, pageId: string | null) {
  editor = new EditorScreen({ close: () => void showLibrary() });
  app.replaceChildren(editor.root);
  await editor.open(id, pageId);
}

document.addEventListener("keydown", (e) => {
  if (editor) editor.handleKey(e);
  else library.handleKey(e);
});
document.addEventListener("paste", (e) => {
  if (editor) editor.handlePaste(e);
});
window.addEventListener("beforeunload", () => {
  if (!isRestoring()) void editor?.flush();
});
onFlushRequest(async () => {
  await editor?.flush();
});

void showLibrary();
startBackupTimer();
