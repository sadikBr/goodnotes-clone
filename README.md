# Inkleaf

A handwritten note-taking desktop app built with **Tauri 2 + Rust** and a
TypeScript canvas frontend. It organizes notebooks in folders, takes
pressure-sensitive ink, snaps hand-drawn shapes into clean ones, and stores
everything locally in SQLite.

The repository also contains the reports from studying how the Goodnotes web
app is built (see [`docs/reverse-engineering/`](docs/reverse-engineering/README.md)).
Inkleaf is an independent implementation informed by those findings; it contains
no Goodnotes code, assets or branding.

## Features

- **Library:** notebooks with colour covers and live first-page previews, nested folders, favourites, search over titles and typed text, trash with recover / delete forever, duplicate, move, rename
- **Paper:** blank, ruled, narrow ruled, grid, dotted and Cornell templates, switchable per page
- **Pen types:** fountain pen (pressure-sensitive), ballpoint, pencil (grain texture), calligraphy (flat 45° nib) and brush pen (tapered); translucent highlighter, stroke eraser, colour palette and size slider
- **Pen-only mode:** when a stylus is detected, only the pen writes; finger and mouse scroll, and two fingers pinch-zoom. Touches while the pen is writing or hovering are ignored (palm rejection). Toggle with the pen button in the top-right; the choice is remembered
- **Shapes:** pick line, arrow, rectangle, ellipse/circle, triangle, diamond or star and drag (Shift for squares, circles and 45° lines; optional fill), or use *Auto* / *draw and hold* with the pen to turn rough strokes into clean shapes (recognizer in Rust)
- **Objects:** text boxes, sticky notes, images (insert or paste)
- **Handwriting fonts:** eight presets for typed text: Clean, Quick notes, Neat print, Casual, Bubbly, Light pencil, Cursive and Architect. Pick one before or while typing, or lasso-select text boxes and sticky notes and change the font or size (A− / A+) from the selection bar. The fonts are bundled, so they work offline (SIL Open Font License; see `public/third-party-licenses.txt`)
- **Sizes:** each tool shows its exact size in px, with one-tap presets, − / + buttons and a box to type a value. Sizes are remembered
- **Lasso:** draw around items or tap one to select it; move, recolour, duplicate, copy/paste, delete
- **Edit shapes:** tap a shape with the lasso to get handles. Drag corners or edges to resize (Shift keeps proportions) and drag line/arrow ends to move them. The bar above it changes the shape type, line width (exact px), fill and colour. Images and sticky notes resize the same way
- **Apply a font to handwriting:** lasso-select pen writing (e.g. a title) and choose *Apply font*. On Windows the built-in offline handwriting recognizer (Windows Ink) reads the words and suggests alternatives; you can correct the text, preview it in every handwriting font, and it replaces the ink at the same place and size (optionally keeping the ink). Elsewhere you type what you wrote
- **Pages:** sidebar thumbnails, add / insert / duplicate / reorder / delete / bookmark
- **Editing:** undo/redo, zoom (Ctrl+wheel, pinch, fit width), pan tool
- **Export:** PDF (whole notebook or one page), PowerPoint .pptx (one slide per page) or PNG, through a native Save dialog that defaults to `Documents/Inkleaf Exports/`. Pages are exported as high-resolution images (PDF/PNG about 290 DPI, PowerPoint about 190 DPI), so text isn't selectable in the export
- **Themes:** light and dark mode

### Keyboard shortcuts

| Key | Action | Key | Action |
| --- | --- | --- | --- |
| P | Pen | Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| H | Highlighter | Ctrl+C / Ctrl+V / Ctrl+D | Copy / paste / duplicate selection |
| E | Eraser | Delete | Delete selection |
| L | Lasso | Ctrl+A | Select all |
| S | Shape | Ctrl+= / Ctrl+- / Ctrl+0 | Zoom in / out / fit |
| T | Text box | ← / → (PgUp / PgDn) | Previous / next page |
| N | Sticky note | Esc | Clear selection / finish text |

## Project layout

```
src-tauri/            Rust backend (Tauri 2)
  src/db.rs           SQLite schema, library/page/element persistence, search (+ tests)
  src/ink.rs          Stroke simplification and shape recognition (+ tests)
  src/commands.rs     Tauri command layer
  src/export.rs       PDF and PowerPoint writers (+ tests)
  src/handwriting.rs  Handwriting recognition via Windows Ink (Windows only)
  src/lib.rs          App setup and command registration
src/                  TypeScript frontend (Vite, no framework)
  api.ts              Typed backend API (Tauri invoke, or an in-browser mock)
  mockBackend.ts      localStorage backend for browser previews and UI tests
  library.ts          Library screen
  editor/             Canvas editor: model, geometry, rendering, tools
docs/reverse-engineering/   Goodnotes web architecture reports
```

## Development

Prerequisites: Node 20+, Rust 1.90+, and the
[Tauri system dependencies](https://tauri.app/start/prerequisites/) for your OS
(on Debian/Ubuntu: `libwebkit2gtk-4.1-dev libgtk-3-dev librsvg2-dev libayatana-appindicator3-dev`).

```bash
npm install
npm run tauri dev         # run the desktop app with hot reload
npm run tauri build       # build installers / bundles for this OS
npm run dev               # frontend only in a browser (uses the mock backend)

# Windows installer from Linux (MinGW): needs mingw-w64 and nsis
rustup target add x86_64-pc-windows-gnu
npm run tauri build -- --target x86_64-pc-windows-gnu --bundles nsis
```

### Linux packages

Arch Linux (including Omarchy): `packaging/arch/PKGBUILD` installs a release
build. Put it next to `inkleaf-<version>-x86_64.tar.gz` (binary, desktop entry,
icons) and run `makepkg -si`. It uses the system `webkit2gtk-4.1`.

```bash
npm run tauri build -- --no-bundle         # release binary: src-tauri/target/release/inkleaf
npm run tauri build -- --bundles deb,rpm   # Debian/Ubuntu and Fedora packages
```

If the window stays blank on Wayland with an NVIDIA GPU, start it with
`WEBKIT_DISABLE_DMABUF_RENDERER=1 inkleaf` (a WebKitGTK driver issue).

Tests:

```bash
npm run typecheck && npm test          # TypeScript + frontend unit tests (Vitest)
cd src-tauri && cargo test             # Rust unit tests (database, ink geometry, PDF/PPTX export)
```

Data lives in the OS app-data directory (e.g. `~/.local/share/app.inkleaf.desktop/inkleaf.db`
on Linux, `~/Library/Application Support/app.inkleaf.desktop/` on macOS).

## Not implemented yet

Cloud sync and collaboration, handwriting recognition outside Windows, and PDF import/annotation.
[Report 05](docs/reverse-engineering/05-inkleaf-mapping.md) explains how each
could be added.
