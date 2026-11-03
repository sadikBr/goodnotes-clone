// Updates. On the desktop, signed updates from GitHub Releases are found,
// downloaded and installed by Tauri's updater, after the user agrees. On
// Android (no in-app updater) the app only says a new APK is out and opens
// its download in the browser.

import { isAndroid, isTauri } from "./api";
import { saveOpenWork } from "./appLifecycle";
import { getPref, setPref } from "./prefs";
import { h, icon, reportError, toast } from "./ui";

const AUTO_KEY = "inkleaf-auto-update";
const REPO = "sadikBr/goodnotes-clone";
export { isAndroid };

export const autoCheckEnabled = () => getPref<string>(AUTO_KEY, "on") === "on";
export const setAutoCheck = (on: boolean) => setPref(AUTO_KEY, on ? "on" : "off");

/** The installed version (desktop or Android app), or null in the browser. */
export async function appVersion(): Promise<string | null> {
  if (!isTauri) return null;
  const { getVersion } = await import("@tauri-apps/api/app");
  return getVersion();
}

/** "0.10.1" > "0.9.3". */
export function newer(a: string, b: string): boolean {
  const pa = a.replace(/^v/, "").split(".").map((x) => parseInt(x, 10) || 0);
  const pb = b.replace(/^v/, "").split(".").map((x) => parseInt(x, 10) || 0);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) if ((pa[i] ?? 0) !== (pb[i] ?? 0)) return (pa[i] ?? 0) > (pb[i] ?? 0);
  return false;
}

interface Found {
  version: string;
  notes: string;
  /** Desktop: download, install, restart. Android: open the APK download. */
  apply: (progress: (text: string) => void) => Promise<void>;
}

/** Versions the user said "Later" to (until the app restarts). */
const later = new Set<string>();
let banner: HTMLElement | null = null;

async function findUpdate(): Promise<Found | null> {
  if (!isTauri) return null;
  if (isAndroid) {
    const r = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, { headers: { Accept: "application/vnd.github+json" } });
    if (!r.ok) throw new Error(`couldn't reach GitHub (${r.status})`);
    const rel = (await r.json()) as { tag_name: string; body?: string; html_url: string; assets: { name: string; browser_download_url: string }[] };
    const current = await appVersion();
    if (!current || !newer(rel.tag_name, current)) return null;
    const apk = rel.assets.find((a) => a.name.endsWith(".apk"));
    return {
      version: rel.tag_name.replace(/^v/, ""),
      notes: rel.body ?? "",
      apply: async () => {
        const { openUrl } = await import("@tauri-apps/plugin-opener");
        await openUrl(apk?.browser_download_url ?? rel.html_url);
      },
    };
  }
  const { check } = await import("@tauri-apps/plugin-updater");
  const update = await check();
  if (!update) return null;
  return {
    version: update.version,
    notes: update.body ?? "",
    apply: async (progress) => {
      let total = 0;
      let got = 0;
      await update.download((e) => {
        if (e.event === "Started") total = e.data.contentLength ?? 0;
        else if (e.event === "Progress") {
          got += e.data.chunkLength;
          progress(total ? `Downloading… ${Math.round((got / total) * 100)}%` : "Downloading…");
        } else progress("Installing…");
      });
      await saveOpenWork();
      await update.install();
      const { relaunch } = await import("@tauri-apps/plugin-process");
      await relaunch();
    },
  };
}

function showBanner(f: Found) {
  banner?.remove();
  const status = h("span", { class: "update-status" });
  const notes = f.notes.trim() ? h("details", { class: "update-notes" }, h("summary", {}, "What's new"), h("div", {}, f.notes.trim())) : null;
  const go = h("button", { class: "btn primary" }, isAndroid ? "Download" : "Update and restart");
  const no = h("button", { class: "btn ghost" }, "Later");
  go.addEventListener("click", async () => {
    go.setAttribute("disabled", "");
    no.setAttribute("disabled", "");
    try {
      status.textContent = isAndroid ? "Opening the download…" : "Starting…";
      await f.apply((t) => (status.textContent = t));
      if (isAndroid) close();
    } catch (e) {
      reportError(e);
      go.removeAttribute("disabled");
      no.removeAttribute("disabled");
      status.textContent = "";
    }
  });
  const close = () => {
    banner?.remove();
    banner = null;
  };
  no.addEventListener("click", () => {
    later.add(f.version);
    close();
  });
  banner = h(
    "div",
    { class: "update-banner", role: "status" },
    icon("download"),
    h("div", { class: "update-text" }, h("strong", {}, `Inkleaf ${f.version} is available`), notes, status),
    h("div", { class: "update-actions" }, no, go),
  );
  document.body.append(banner);
}

/** Looks for an update; `manual` also reports "up to date" and errors. */
export async function checkForUpdates(manual = false): Promise<void> {
  try {
    const f = await findUpdate();
    if (!f) {
      if (manual) toast(isTauri ? "Inkleaf is up to date" : "Updates are for the installed app");
      return;
    }
    if (!manual && later.has(f.version)) return;
    showBanner(f);
  } catch (e) {
    if (manual) reportError(e);
    else console.warn("update check failed", e);
  }
}

/** Checks shortly after start, then every six hours (when allowed in Settings). */
export function startUpdateChecks() {
  if (!isTauri) return;
  const run = () => autoCheckEnabled() && void checkForUpdates(false);
  window.setTimeout(run, 8_000);
  window.setInterval(run, 6 * 3600_000);
}

/** Settings rows: version, automatic checks, check now. */
export function updatesSection(): HTMLElement {
  const version = h("span", {}, "…");
  void appVersion().then((v) => (version.textContent = v ? `Version ${v}` : "Browser preview"));
  const sw = h("button", { type: "button", class: "switch-btn", role: "switch", "aria-checked": String(autoCheckEnabled()), "aria-label": "Check for updates automatically" }, h("span", { class: "switch" }, h("span", {})));
  sw.addEventListener("click", () => {
    const on = sw.getAttribute("aria-checked") !== "true";
    sw.setAttribute("aria-checked", String(on));
    setAutoCheck(on);
  });
  const now = h("button", { type: "button", class: "btn" }, "Check now");
  now.addEventListener("click", () => void checkForUpdates(true));
  const row = (title: string, help: string | HTMLElement, control: HTMLElement) =>
    h("div", { class: "set-row" }, h("div", { class: "set-text" }, h("div", { class: "set-title" }, title), h("div", { class: "set-help" }, help)), control);
  return h(
    "section",
    { class: "set-section" },
    h("h3", {}, icon("download"), "Updates"),
    row("Check for updates automatically", isAndroid ? "You're told when a new version is out, and it downloads in your browser." : "You're asked before anything is installed; your notes are saved first.", sw),
    row("Inkleaf", version, now),
  );
}
