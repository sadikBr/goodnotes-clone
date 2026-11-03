import "./styles.css";
import { EditorScreen } from "./editor/editor";
import { LibraryScreen } from "./library";
import { installTooltips } from "./ui";
import { applyStoredTheme } from "./theme";
import { isRestoring, onFlushRequest } from "./appLifecycle";
import { startBackupTimer } from "./backupSettings";
import { startUpdateChecks } from "./updates";
import { startSync, syncNow } from "./syncSettings";

const app = document.getElementById("app")!;

applyStoredTheme();
installTooltips();

let editor: EditorScreen | null = null;

const library = new LibraryScreen({
  openNotebook: (id, pageId) => openEditor(id, pageId ?? null),
});

async function showLibrary() {
  const closing = editor !== null;
  if (editor) {
    await editor.flush();
    editor = null;
  }
  app.replaceChildren(library.root);
  await library.show();
  // Send what was just written, and take in changes the open notebook held back.
  if (closing) void syncNow();
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
startUpdateChecks();
startSync({
  prepare: async () => {
    await editor?.flush();
    return editor?.notebookId ?? null;
  },
  changed: () => {
    if (!editor) void library.show();
  },
});
