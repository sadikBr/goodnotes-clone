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

/** Called when the theme changes (to update switches). */
export function onThemeChange(fn: () => void) {
  listeners.push(fn);
}
