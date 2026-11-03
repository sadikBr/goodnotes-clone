# Inkleaf branding

One icon everywhere: `icon.svg` is the master. Everything else is made from it.

| File | Used for |
|---|---|
| `icon.svg` | Master (also `public/icon.svg`: the browser tab, and the mark in the app's library) |
| `icon-foreground.svg` | Android adaptive icon foreground: the leaf alone, inside the safe zone, on the brand green `#1F7A5A` |
| `icon-round.svg` | Android round launcher icon |

Colours: brand green `#1f7a5a` (gradient `#2b9a71` → `#15573f`), leaf cream `#fffdf7`.

To regenerate after changing the artwork:

1. Render `icon.svg` to a 1024×1024 PNG with a transparent background at
   `src-tauri/icons/app-icon.png`, then run `npx tauri icon src-tauri/icons/app-icon.png`
   (desktop PNGs, `icon.ico`, `icon.icns`, Windows Store logos, Android legacy icons).
   Delete the `ios` folder it makes (there is no iOS app).
2. Android: render `icon-foreground.svg` to `src-tauri/icons/android/mipmap-*/ic_launcher_foreground.png`
   at 108, 162, 216, 324 and 432 px (mdpi … xxxhdpi), `icon-round.svg` to `ic_launcher_round.png`
   at 48, 72, 96, 144 and 192 px, and keep `values/ic_launcher_background.xml` at `#1F7A5A`.
   The release workflow copies `src-tauri/icons/android` into the generated Android project.
3. Web: `public/favicon.png` (64 px), `public/apple-touch-icon.png` (180 px), `docs/icon.png` (128 px).
4. The library's mark (`BRAND_MARK` in `src/library.ts`) is the SVG inline: paste the new artwork there.
