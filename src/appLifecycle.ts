// Hooks for whole-library operations (restoring a backup) that the open
// screens need to take part in.

let flushOpenWork: () => Promise<void> = async () => {};
let restoring = false;

/** Called by the app shell: how to save what's open (the editor's page). */
export function onFlushRequest(fn: () => Promise<void>) {
  flushOpenWork = fn;
}

/** Saves what's open, then stops further saves until the app reloads. */
export async function prepareForRestore() {
  await flushOpenWork();
  restoring = true;
}

/** Restore failed: the open screens may save again. */
export function cancelRestore() {
  restoring = false;
}

/** While true, nothing may be written (the library is being replaced). */
export function isRestoring() {
  return restoring;
}
