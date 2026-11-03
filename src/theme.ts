// Light/dark theme: follows the system until the user picks one.

const THEME_KEY = "inkleaf-theme";
const listeners: (() => void)[] = [];

function stored(): string | null {
  try {
    return localStorage.getItem(THEME_KEY);
  } catch {
    return null;
  }
}

export function applyStoredTheme() {
  const t = stored();
  if (t === "light" || t === "dark") document.documentElement.dataset.theme = t;
  else delete document.documentElement.dataset.theme;
}

export function isDark(): boolean {
  const t = document.documentElement.dataset.theme;
  return t ? t === "dark" : window.matchMedia("(prefers-color-scheme: dark)").matches;
}

export function toggleTheme() {
  const next = isDark() ? "light" : "dark";
  document.documentElement.dataset.theme = next;
  try {
    localStorage.setItem(THEME_KEY, next);
  } catch {
    /* preference is per session only */
  }
  listeners.forEach((fn) => fn());
}

/** "system" follows the computer's setting. */
export function themeMode(): "light" | "dark" | "system" {
  const t = stored();
  return t === "light" || t === "dark" ? t : "system";
}

export function setTheme(mode: "light" | "dark" | "system") {
  try {
    if (mode === "system") localStorage.removeItem(THEME_KEY);
    else localStorage.setItem(THEME_KEY, mode);
  } catch {
    /* preference is per session only */
  }
  applyStoredTheme();
  listeners.forEach((fn) => fn());
}

/** Called when the theme changes (to update switches). */
export function onThemeChange(fn: () => void) {
  listeners.push(fn);
}
