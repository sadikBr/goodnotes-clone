// Sync through Google Drive: the Settings section, signing in with a code,
// and syncing in the background (at start, every few minutes, when a
// notebook is closed and when the tablet app is put away).

import { api, isAndroid, isTauri, type SyncReport, type SyncStatus } from "./api";
import { h, icon, relativeTime, reportError, toast } from "./ui";

export interface SyncHooks {
  /** Saves the open notebook and says which it is (sync leaves it alone). */
  prepare(): Promise<string | null>;
  /** Something changed in the library: show it. */
  changed(): void;
}

let hooks: SyncHooks = { prepare: async () => null, changed: () => {} };
let status: SyncStatus | null = null;
const listeners = new Set<() => void>();
let syncing = false;

const notify = () => listeners.forEach((fn) => fn());

/** One sync now (quiet unless `manual`); skipped when not signed in. */
export async function syncNow(manual = false): Promise<SyncReport | null> {
  if (!isTauri || syncing) return null;
  status ??= await api.syncStatus().catch(() => null);
  if (!status?.signedIn) return null;
  syncing = true;
  notify();
  try {
    const open = await hooks.prepare();
    const r = await api.syncNow(open);
    if (r.downloaded || r.deleted || r.conflicts.length) hooks.changed();
    if (r.conflicts.length) toast(`Changed on both devices, both versions kept: ${r.conflicts.join(", ")}`);
    else if (manual) toast(r.uploaded || r.downloaded || r.deleted ? `Synced: ${r.uploaded} sent, ${r.downloaded} received` : "Everything is in sync");
    return r;
  } catch (e) {
    if (manual) reportError(e);
    return null;
  } finally {
    syncing = false;
    status = await api.syncStatus().catch(() => status);
    notify();
  }
}

/** Background syncing for the app shell. */
export function startSync(h: SyncHooks) {
  hooks = h;
  if (!isTauri) return;
  window.setTimeout(() => void syncNow(), 4_000);
  window.setInterval(() => void syncNow(), 5 * 60_000);
  // A tablet app is often just put away: send what was written first.
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "hidden") void syncNow();
  });
}

const defaultDevice = () => (isAndroid ? "Tablet" : navigator.userAgent.includes("Windows") ? "Windows PC" : "Computer");

/** Signing in in the browser; resolves when signed in (or cancelled). */
async function signIn(): Promise<void> {
  const code = await api.syncSignInStart();
  const openPage = async () => {
    const { openUrl } = await import("@tauri-apps/plugin-opener");
    await openUrl(code.verificationUrl).catch(reportError);
  };
  const again = h("button", { class: "btn" }, "Open the sign-in page again");
  const cancel = h("button", { class: "btn ghost" }, "Cancel");
  const waiting = h("p", { class: "set-help" }, "Waiting for you to allow access…");
  const backdrop = h(
    "div",
    { class: "modal-backdrop" },
    h(
      "div",
      { class: "modal signin-modal", role: "dialog", "aria-modal": "true", "aria-label": "Sign in to Google Drive" },
      h("h2", {}, "Sign in to Google Drive"),
      h("p", {}, "Continue in your browser: choose your Google account and allow Inkleaf to keep its files in your Drive. Inkleaf only sees its own hidden folder there, not your other files."),
      h("div", { class: "signin-actions" }, again),
      waiting,
      h("div", { class: "signin-foot" }, cancel),
    ),
  );
  document.body.append(backdrop);
  const close = () => backdrop.remove();
  again.addEventListener("click", () => void openPage());
  void openPage();
  cancel.addEventListener("click", () => {
    void api.syncSignInCancel();
    close();
  });
  try {
    status = await api.syncSignInWait(defaultDevice());
    close();
    notify();
    toast(`Signed in${status.email ? ` as ${status.email}` : ""}. Syncing…`);
    await syncNow(true);
  } catch (e) {
    close();
    if (!String(e).includes("cancelled")) reportError(e);
  }
}

/** Settings rows for sync. */
export function syncSection(): HTMLElement {
  const body = h("div", {}, h("p", { class: "set-help" }, "Loading…"));
  const row = (title: string, help: string | HTMLElement, control?: HTMLElement) =>
    h("div", { class: "set-row" }, h("div", { class: "set-text" }, h("div", { class: "set-title" }, title), h("div", { class: "set-help" }, help)), control ?? h("span", {}));
  let shown = false;
  const draw = () => {
    // The Settings screen was closed: stop following sync.
    if (shown && !body.isConnected) {
      listeners.delete(draw);
      return;
    }
    shown = true;
    const s = status;
    if (!s) return;
    if (!s.available) {
      body.replaceChildren(row("Google Drive", isTauri ? "This build of Inkleaf can't sign in to Google (it was built without a Google key)." : "Sync works in the installed app (computer and tablet)."));
      return;
    }
    if (!s.signedIn) {
      const b = h("button", { class: "btn primary" }, "Sign in…");
      b.addEventListener("click", () => void signIn().catch(reportError));
      body.replaceChildren(row("Google Drive", "Keep your notebooks the same on your computer and tablet. Each device signs in once; everything syncs by itself.", b));
      return;
    }
    const now = h("button", { class: "btn", disabled: syncing }, syncing ? "Syncing…" : "Sync now");
    now.addEventListener("click", () => void syncNow(true));
    const out = h("button", { class: "btn ghost" }, "Sign out");
    out.addEventListener("click", async () => {
      try {
        status = await api.syncSignOut();
        notify();
        toast("Signed out. Your notebooks stay on this device and in Drive.");
      } catch (e) {
        reportError(e);
      }
    });
    const state = s.lastError ? h("span", { class: "backup-error" }, s.lastError) : s.lastSyncAt ? `Last synced ${relativeTime(s.lastSyncAt)}` : "Not synced yet";
    body.replaceChildren(
      row("Google Drive", h("span", {}, "Signed in as ", h("strong", {}, s.email ?? "your Google account"), s.device ? ` · this device: ${s.device}` : ""), out),
      row("Status", state, now),
    );
  };
  listeners.add(draw);
  void api
    .syncStatus()
    .then((s) => {
      status = s;
      draw();
    })
    .catch((e) => body.replaceChildren(h("p", { class: "set-help" }, String(e))));
  return h("section", { class: "set-section" }, h("h3", {}, icon("docs"), "Sync"), body);
}
