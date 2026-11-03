// The Backups section of the Settings screen, and the daily backup check.

import { api, isAndroid, type BackupConfig } from "./api";
import { cancelRestore, prepareForRestore } from "./appLifecycle";
import { confirmDialog, h, icon, relativeTime, reportError, toast } from "./ui";

const pad = (n: number) => String(n).padStart(2, "0");

/** Local date and time for backup file names, e.g. "2026-11-03 0915". */
export function backupLabel(d = new Date()): string {
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}${pad(d.getMinutes())}`;
}

const fileName = (p: string) => p.split(/[\\/]/).pop() ?? p;

let reportedError: string | null = null;

/** Runs the daily backup when it's due: shortly after start, then every half hour. */
export function startBackupTimer() {
  const check = async () => {
    try {
      const s = await api.backupIfDue(backupLabel());
      // Say once per session when backups can't be made (e.g. the drive is missing).
      if (s.lastError && s.lastError !== reportedError) toast(`Backup didn't run: ${s.lastError}`, "error");
      reportedError = s.lastError;
    } catch {
      /* browser preview: no backups */
    }
  };
  window.setTimeout(check, 20_000);
  window.setInterval(check, 30 * 60_000);
}

function seg<T extends string | number>(label: string, choices: { value: T; label: string }[], current: T, onPick: (v: T) => void): HTMLElement {
  const g = h("div", { class: "seg", role: "radiogroup", "aria-label": label });
  const draw = (v: T) =>
    g.replaceChildren(...choices.map((c) => h("button", { type: "button", role: "radio", "aria-checked": String(c.value === v), class: c.value === v ? "on" : "", onclick: () => (onPick(c.value), draw(c.value)) }, c.label)));
  draw(current);
  return g;
}

function row(title: string, help: string | HTMLElement, control: HTMLElement): HTMLElement {
  return h("div", { class: "set-row" }, h("div", { class: "set-text" }, h("div", { class: "set-title" }, title), h("div", { class: "set-help" }, help)), control);
}

/** Settings rows for backups: folder, daily, how many to keep, now, export everything, restore. */
export function backupSection(): HTMLElement {
  const body = h("div", {}, h("p", { class: "set-help" }, "Loading…"));
  const section = h("section", { class: "set-section" }, h("h3", {}, icon("download"), "Backups"), body);
  let cfg: BackupConfig;

  const save = async (folder: string | null, daily: boolean, keep: number) => {
    try {
      cfg = await api.setBackupSettings(folder, daily, keep);
    } catch (e) {
      reportError(e);
    }
    draw();
  };

  const status = () => {
    if (cfg.lastError) return h("span", { class: "backup-error" }, cfg.lastError);
    if (cfg.lastBackupAt && cfg.lastBackupFile) return `Last backup ${relativeTime(cfg.lastBackupAt)}: ${fileName(cfg.lastBackupFile)}`;
    return cfg.folder ? "No backup made yet." : "Choose a folder to turn backups on.";
  };

  const draw = () => {
    const parts = (cfg.folder ?? "").split(/[\\/]/).filter(Boolean);
    const short = parts.length > 2 ? `…/${parts.slice(-2).join("/")}` : cfg.folder;
    const help = cfg.folder
      ? h("span", {}, "Saving to ", h("span", { class: "backup-folder", title: cfg.folder }, short ?? ""))
      : "A USB key, or a folder that Google Drive, Dropbox or OneDrive keeps in sync.";
    const choose = h("button", { type: "button", class: "btn" }, cfg.folder ? "Change…" : "Choose…");
    choose.addEventListener("click", async () => {
      try {
        const f = await api.chooseBackupFolder(cfg.folder);
        if (f) await save(f, cfg.daily, cfg.keep);
        if (f && cfg.daily) {
          cfg = await api.backupIfDue(backupLabel());
          draw();
          if (!cfg.lastError) toast("First backup saved");
        }
      } catch (e) {
        reportError(e);
      }
    });
    const daily = h("button", { type: "button", class: "switch-btn", role: "switch", "aria-checked": String(cfg.daily), "aria-label": "Back up every day", disabled: !cfg.folder }, h("span", { class: "switch" }, h("span", {})));
    daily.addEventListener("click", () => void save(cfg.folder, !cfg.daily, cfg.keep));
    const now = h("button", { type: "button", class: "btn", disabled: !cfg.folder }, "Back up now");
    now.addEventListener("click", async () => {
      now.setAttribute("disabled", "");
      try {
        cfg = await api.backupNow(backupLabel());
        toast("Backup saved");
      } catch (e) {
        cfg = await api.backupStatus();
        reportError(e);
      }
      draw();
    });
    const exportAll = h("button", { type: "button", class: "btn" }, "Export…");
    exportAll.addEventListener("click", async () => {
      try {
        const p = await api.exportLibrary(backupLabel());
        if (p) toast(`Saved ${fileName(p)}`);
      } catch (e) {
        reportError(e);
      }
    });
    const restore = h("button", { type: "button", class: "btn danger" }, "Restore…");
    restore.addEventListener("click", () => void restoreFlow());
    const folderRows = isAndroid
      ? []
      : [
          row("Backup folder", help, choose),
      row("Back up every day", "While Inkleaf is open, once a day. Only the newest backups are kept in the folder.", daily),
      row("Keep", "How many daily backups stay in the folder.", seg("Keep", [{ value: 7, label: "7" }, { value: 14, label: "14" }, { value: 30, label: "30" }], cfg.keep, (v) => void save(cfg.folder, cfg.daily, v))),
          row("Status", status(), now),
        ];
    body.replaceChildren(
      ...folderRows,
      row("Export everything", "Your whole library (notebooks, PDFs, settings of each daftar) as one .inkleaf file.", exportAll),
      row("Restore from a backup", "Replaces everything in Inkleaf with a backup or export. Your current library is kept as a safety copy.", restore),
    );
  };

  void api
    .backupStatus()
    .then((c) => {
      cfg = c;
      draw();
    })
    .catch((e) => body.replaceChildren(h("p", { class: "set-help" }, String(e))));
  return section;
}

async function restoreFlow() {
  try {
    const path = await api.chooseBackupFile();
    if (!path) return;
    const s = await api.inspectBackup(path);
    const what = `${s.notebooks} notebook${s.notebooks === 1 ? "" : "s"}, ${s.folders} folder${s.folders === 1 ? "" : "s"}, ${s.pages} page${s.pages === 1 ? "" : "s"}`;
    if (!(await confirmDialog("Restore this backup?", `${fileName(path)} holds ${what}. Everything in Inkleaf now will be replaced by it (a safety copy of your current library is kept).`, "Restore"))) return;
    await prepareForRestore();
    try {
      await api.restoreBackup(path);
    } catch (e) {
      cancelRestore();
      throw e;
    }
    toast("Backup restored");
    window.setTimeout(() => location.reload(), 600);
  } catch (e) {
    reportError(e);
  }
}
