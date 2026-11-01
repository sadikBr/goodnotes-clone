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
- **Ink:** pen with stylus pressure, translucent highlighter, stroke eraser, colour palette and size slider
- **Shapes:** the Shape tool, or *draw and hold* with the pen, turns rough strokes into lines, rectangles, triangles, polygons, circles and ellipses (recognizer in Rust)
- **Objects:** text boxes, sticky notes, images (insert or paste)
- **Lasso:** select, move, recolour, duplicate, copy/paste, delete
- **Pages:** sidebar thumbnails, add / insert / duplicate / reorder / delete / bookmark
- **Editing:** undo/redo, zoom (Ctrl+wheel, fit width), pan tool, palm rejection (touch pans once a pen is used)
- **Export:** current page as PNG to `~/Documents/Inkleaf Exports/`
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
  src/commands.rs     Tauri command layer, PNG export
  src/lib.rs          App setup and command registration
src/                  TypeScript frontend (Vite, no framework)
  api.ts              Typed backend API (Tauri invoke, or an in-browser mock)
  mockBackend.ts      localStorage backend for browser previews and UI tests
  library.ts          Library screen
  editor/             Canvas editor: model, geometry, rendering, tools
docs/reverse-engineering/   Goodnotes web architecture reports
```

## Development

Prerequisites: Node 20+, Rust 1.80+, and the
[Tauri system dependencies](https://tauri.app/start/prerequisites/) for your OS
(on Debian/Ubuntu: `libwebkit2gtk-4.1-dev libgtk-3-dev librsvg2-dev libayatana-appindicator3-dev`).

```bash
npm install
npm run tauri dev         # run the desktop app with hot reload
npm run tauri build       # build installers / bundles for this OS
npm run dev               # frontend only in a browser (uses the mock backend)
```

Tests:

```bash
npm run typecheck && npm test          # TypeScript + frontend unit tests (Vitest)
cd src-tauri && cargo test             # Rust unit tests (database + ink geometry)
```

Data lives in the OS app-data directory (e.g. `~/.local/share/app.inkleaf.desktop/inkleaf.db`
on Linux, `~/Library/Application Support/app.inkleaf.desktop/` on macOS).

## Not implemented yet

Cloud sync and collaboration, handwriting recognition, and PDF import/annotation.
[Report 05](docs/reverse-engineering/05-inkleaf-mapping.md) explains how each
could be added.
