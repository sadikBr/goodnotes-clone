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
- **Paper:** blank, ruled, narrow ruled, grid, dotted and Cornell templates; A4, A5, US Letter, US Legal or square, portrait or landscape; white, cream, yellow, grey or dark paper. Paper style and colour can be changed per page or for all pages
- **Pen types:** fountain pen (pressure-sensitive), ballpoint, pencil (grain texture), calligraphy (flat 45° nib) and brush pen (tapered); translucent highlighter, colour palette and size control
- **Eraser:** *Precise* (default) erases only what it touches, cutting strokes and shape outlines; *Whole stroke* removes anything it touches. Text boxes and sticky notes are erased whole
- **Pen-only mode:** when a stylus is detected, only the pen writes; finger and mouse scroll, and two fingers pinch-zoom. Touches while the pen is writing or hovering are ignored (palm rejection). Toggle with the pen button in the top-right; the choice is remembered
- **Shapes:** pick line, arrow, rectangle, ellipse/circle, triangle, diamond or star and drag (Shift for squares, circles and 45° lines; optional fill)
- **Auto shapes and ink refinement:** with the Shape tool in *Auto* mode, every stroke is cleaned up: basic shapes snap to perfect geometry; curly braces (any orientation, slanted or lopsided) are matched against a clean brace template and redrawn; near-circular runs such as parentheses become true arcs; everything else (graphs, curves, brackets…) is de-jittered and fitted with smooth curves, with straight runs made straight (snapped to horizontal/vertical when nearly so) and sharp corners kept sharp
- **Hold to refine:** keep the pen (or mouse) down at the end of a stroke and it snaps to its refined form while you're still holding. Keep moving to rotate and resize it; hold still again to switch to moving it (and back). It's placed only when you lift. With the pen tool this applies to shapes and braces only, so handwriting is never altered
- **Alignment guides:** when moving text, magenta guides appear and the text snaps when its baseline or letter centre lines up with other text, sits on a ruled line or centred between two, or its ink lines up left/right/centre with other text, the margin or the page centre. Guides follow the letters, not the text box. Hold Alt to move freely
- **PDF import and annotation:** *Import PDF* in the library turns a PDF (lecture slides, papers, worksheets…) into a notebook with one page per PDF page, at the PDF's own page size. The PDF is stored inside Inkleaf and rendered as each page's background, sharp at any zoom, so you can write, highlight, add text and shapes on top. Exports include the PDF content under your notes. Rendering uses the bundled pdf.js, so it works offline
- **Objects:** text boxes, sticky notes, images (insert or paste)
- **Text formatting:** bold, italic, bulleted and numbered lists (Enter continues the list, wrapped lines indent under the text) and left/centre/right alignment, per text box or sticky note; Ctrl+B / Ctrl+I while typing
- **Handwriting fonts:** eight presets for typed text: Clean, Quick notes, Neat print, Casual, Bubbly, Light pencil, Cursive and Architect. Pick one before or while typing, or lasso-select text boxes and sticky notes and change the font or size (A− / A+) from the selection bar. The fonts are bundled, so they work offline (SIL Open Font License; see `public/third-party-licenses.txt`)
- **Colours:** preset colours plus a custom colour picker (saturation/brightness square, hue slider, hex code, recently used colours and, where the system supports it, an eyedropper) for every tool and for recolouring a selection
- **Sizes:** each tool shows its exact size in px, with one-tap presets, − / + buttons and a box to type a value. Sizes are remembered
- **Lasso:** draw around items or tap one to select it; move, recolour, duplicate, copy/paste, delete
- **Resize and rotate anything:** any selection (handwriting, shapes, text, sticky notes, images, or a mix) gets a box with resize handles and a rotation knob below it. Corners keep proportions, edges stretch (Shift keeps proportions on edges too); rotation snaps to 15° with Shift. Text scales its font size; text, sticky notes and images can be rotated
- **Edit shapes:** a selected shape's bar changes its type, line width (exact px), fill and colour; a selected line or arrow has handles on both ends
- **Apply a font to handwriting:** lasso-select pen writing (e.g. a title) and choose *Apply font*. On Windows the built-in offline handwriting recognizer (Windows Ink) reads the words and suggests alternatives; you can correct the text, preview it in every handwriting font, and it replaces the ink at the same place and size (optionally keeping the ink). Elsewhere you type what you wrote
- **Pages:** all pages scroll continuously in one column; write on any page directly (it becomes the current page), and the page in view becomes current when scrolling stops. Sidebar thumbnails, add / insert / duplicate / reorder / delete / bookmark; undo history is kept per page
- **Editing:** undo/redo, zoom (Ctrl+wheel, pinch, fit width), pan tool
- **Export:** PDF (whole notebook or one page), PowerPoint .pptx (one slide per page) or PNG, through a native Save dialog that defaults to `Documents/Inkleaf Exports/`. PDFs are vector: handwriting, shapes and text stay sharp at any zoom and the text can be selected and searched (text is drawn from the bundled fonts' glyph outlines, with an invisible text layer on top). Imported PDF pages and pictures are embedded as images (PDF pages at 288 DPI). PowerPoint and PNG exports are high-resolution images
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
  src/ink.rs          Stroke simplification (+ tests)
  src/commands.rs     Tauri command layer
  src/export.rs       PDF and PowerPoint writers (+ tests)
  src/handwriting.rs  Handwriting recognition via Windows Ink (Windows only)
  src/lib.rs          App setup and command registration
src/                  TypeScript frontend (Vite, no framework)
  api.ts              Typed backend API (Tauri invoke, or an in-browser mock)
  mockBackend.ts      localStorage backend for browser previews and UI tests
  library.ts          Library screen
  editor/             Canvas editor: model, geometry, rendering, tools,
                      beautify.ts (shape/brace recognition, smoothing), snap.ts (guides)
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

Cloud sync and collaboration, handwriting recognition outside Windows, and password-protected PDFs.
[Report 05](docs/reverse-engineering/05-inkleaf-mapping.md) explains how each
could be added.
