// Per-device preferences in localStorage. Storage can be unavailable, so
// every access is guarded and falls back to the default.

export function getPref<T extends string>(key: string, fallback: T, allowed?: readonly T[]): T {
  try {
    const v = localStorage.getItem(key);
    if (v === null) return fallback;
    if (allowed && !allowed.includes(v as T)) return fallback;
    return v as T;
  } catch {
    return fallback;
  }
}

export function hasPref(key: string): boolean {
  try {
    return localStorage.getItem(key) !== null;
  } catch {
    return false;
  }
}

export function setPref(key: string, value: string) {
  try {
    localStorage.setItem(key, value);
  } catch {
    /* not persisted; the setting still applies for this session */
  }
}
