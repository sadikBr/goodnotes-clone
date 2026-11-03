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
- **Interface:** a warm, paper-inspired design with an ink-green accent, in light and dark. The library greets you, shows notebooks as books on a shelf (sortable, searchable with `/`, N for a new notebook) and has friendly empty states. In a notebook, the tools sit in three trays (writing, arranging, presenting); the active tool lifts out *dipped in its ink colour*, and tapping it again opens its options in a drawer that hangs from it (and glides along when you switch tools). The drawer tucks away as soon as you touch the page, or can be pinned open. Menus and pickers close when you click their button again. Undo/redo and page things (paper, image, instruments, pen-only) are grouped on the right beside a single *Export* button; page and zoom controls float bottom-right; the pages sidebar can be hidden (`[`); **focus mode** (`F`) leaves just the page; `?` lists every keyboard shortcut; tooltips show shortcuts. The new-notebook dialog shows a large live preview of your cover
- **Covers:** new notebooks open with a cover page in the notebook's colour, in one of nine designs picked with live previews: *Classic* (stitched spine, elastic band, label plate), *Minimal*, *Geometric* (low-poly facets), *Waves*, *Marble* (composition book), *Leather* (embossed border, metal corners, gold title), *Kraft* (kraft paper, cloth tape, sticker label), *Dots* and *Zellige* (Moroccan eight-pointed star tilework around a cream panel). The title and month are ordinary text you can edit or write over. Your paper starts on page 2. The library shows covers full-size; *Change cover* switches design and colour (moving the title and date into place) or adds a cover to an older notebook
- **Daftar annossous (دفتر النصوص / cahier de textes):** *New → Daftar annossous* creates a teacher's lesson log laid out like the printed books: title page (بسم الله، الجهة، المديرية الإقليمية، المؤسسة، الأستاذ(ة)، القسم، رقم التأجير، الموسم الدراسي), the teacher's card (المعلومات الشخصية / الإدارية / المهنية, diplomas, previous posts), the timetable (استعمال الزمن, with your lessons as notes), school holidays, the diagnostic report, then for each class a student list (60 numbered places) and lesson pages — التاريخ | القسم | التوقيت | طبيعة الأنشطة المنجزة | التوقيع on 5 mm squared paper with a notes strip, framed in a colour per class — and finally ministerial and academic memos and the teacher's absences. Your details are filled in wherever a page asks for them. *حصة جديدة · New lesson* writes today's date, the class and the lesson time (from your timetable; the next unlogged lesson of the day) on the next free line of that class's page, adding a page when it's full; the class chip switches classes and *Add page* continues the current class. Headings carry a small French line
- **Arabic text:** text boxes and sticky notes support Arabic (Noto Sans Arabic is bundled): right-to-left paragraphs, logical alignment, lists with bullets on the right; mixed Arabic/French lines are ordered correctly
- **Paper:** 21 paper styles in six groups. *Basic:* blank, ruled, narrow ruled, wide ruled, grid, dotted. *Notes:* Cornell, legal pad, two columns, to-do list. *Maths & science:* millimetre graph paper (1 mm, 5 mm and 1 cm lines), coordinate plane on millimetre paper (axis ticks every 5 mm), isometric dots, hexagon. *Writing & art:* handwriting practice, calligraphy guide (55° slant), storyboard. *Planner:* daily and weekly. *Teacher:* daftar annossous and weekly timetable (bilingual, right to left). Every style adapts to A4, A5, US Letter, US Legal or square, portrait or landscape, and to white, cream, yellow, grey or dark paper; the picker shows them in tabs with previews. Paper style and colour can be changed per page or for all pages
- **Pen types:** fountain pen (pressure-sensitive), ballpoint, pencil (grain texture), calligraphy (flat 45° nib) and brush pen (tapered); translucent highlighter
- **Writing settings** (⋯ → Writing settings, or the pen's options): *smoothing* Off / Light / Normal / Strong (each stroke keeps the smoothing it was written with), and the *cursor under the pen* — cross, small dot or hidden — for the pen, highlighter and shape tools
- **Eraser:** *Precise* (default) erases only what it touches, cutting strokes and shape outlines; *Whole stroke* removes anything it touches. Text boxes and sticky notes are erased whole
- **Pen-only mode:** when a stylus is detected, only the pen writes; finger and mouse scroll, and two fingers pinch-zoom. Touches while the pen is writing or hovering are ignored (palm rejection). Toggle with the pen button in the top-right; the choice is remembered
- **Smooth, natural ink:** pens are drawn as one smooth outline with gradual width changes (no steps or beads), with sharp corners kept crisp. The fountain pen and brush pen respond to drawing speed like real ink — fuller when you slow down, thinner on quick strokes — and with a stylus its pressure leads while speed adjusts it gently. Ballpoint, pencil, highlighter and shapes keep an even width. The width is stored with the stroke, so it looks the same at any zoom and in exports
- **Shapes:** pick line, arrow, rectangle, ellipse/circle, triangle, diamond or star and drag (Shift for squares, circles and 45° lines; optional fill)
- **Auto shapes and ink refinement:** with the Shape tool in *Auto* mode, every stroke is cleaned up: basic shapes snap to perfect geometry; curly braces (any orientation, slanted or lopsided) are matched against a clean brace template and redrawn; near-circular runs such as parentheses become true arcs; everything else (graphs, curves, brackets…) is de-jittered and fitted with smooth curves, with straight runs made straight (snapped to horizontal/vertical when nearly so) and sharp corners kept sharp
- **Hold to refine:** keep the pen (or mouse) down at the end of a stroke and it snaps to its refined form while you're still holding. Keep moving to rotate and resize it; hold still again to switch to moving it (and back). It's placed only when you lift. With the pens it works like the shape tool: shapes, arcs, braces, maths symbols, and otherwise a smoothed version of the stroke; write and lift without pausing and your ink is kept exactly as written
- **Maths symbols:** hold at the end of a stroke to get a clean ∫ Σ √ ∞ (one stroke) or = ≡ ≠ ⇒ ⇔ → ↔ ≤ ≥ ± ∀ ∃ — for symbols of several strokes, the strokes you wrote just before (within a couple of seconds, right next to it) are included and replaced, in one undo step. Recognition ($P point clouds plus corner and proportion checks) ignores stroke order and direction and leaves letters and digits alone
- **Alignment guides:** when moving text, magenta guides appear and the text snaps when its baseline or letter centre lines up with other text, sits on a ruled line or centred between two, or its ink lines up left/right/centre with other text, the margin or the page centre. Guides follow the letters, not the text box. Hold Alt to move freely
- **PDF import and annotation:** *Import PDF* in the library turns a PDF (lecture slides, papers, worksheets…) into a notebook with one page per PDF page, at the PDF's own page size. The PDF is stored inside Inkleaf and rendered as each page's background, sharp at any zoom, so you can write, highlight, add text and shapes on top. Exports include the PDF content under your notes. Rendering uses the bundled pdf.js, so it works offline
- **Ruler, protractor and set squares (R):** see-through drawing instruments with real-size markings that float over the page; pick one from the ruler button's menu (R shows or hides the last one). Drag to move; rotate by dragging a round grip, scrolling the wheel over it (1° per notch, 15° with Shift) or twisting with two fingers; it snaps to common angles. Pen, highlighter and shape strokes started next to an edge lock to it, with the ink against the edge and a live readout: length in cm along straight edges, swept angle along the protractor's curved edge. The 30 cm *ruler* has mm/cm scales; the *protractor* has 0–180° scales (both directions) and two arms you drag to measure an angle; the *set squares* (45° and 30°/60°) have cm scales on both legs and work along all three edges. Only the instrument's body takes input, so you can draw right next to it. Instruments are never part of the page or exports
- **Laser pointer (K):** for presenting and explaining. *Trail* draws a glowing tube (bright colour with a white core, fuller when you move slowly and slimmer on fast sweeps) that stays while you keep drawing; shortly after you stop, the colour fades and then the core (drawing again during the fade brings it back). *Dot* is a small pointer that follows the pen or mouse. Red, green, blue, purple or orange (or any colour). It is drawn incrementally on its own layers, so it stays smooth however much you draw. Nothing is ever added to the page, undo history or exports; in pen-only mode the mouse can still use it
- **Objects:** text boxes, sticky notes, images (insert or paste)
- **Text formatting:** bold, italic, bulleted and numbered lists (Enter continues the list, wrapped lines indent under the text) and left/centre/right alignment, per text box or sticky note; Ctrl+B / Ctrl+I while typing
- **Handwriting fonts:** eight presets for typed text: Clean, Quick notes, Neat print, Casual, Bubbly, Light pencil, Cursive and Architect. Pick one before or while typing, or lasso-select text boxes and sticky notes and change the font or size (A− / A+) from the selection bar. The fonts are bundled, so they work offline (SIL Open Font License; see `public/third-party-licenses.txt`)
- **Tool options:** each tool keeps a compact bar of its own **5 favourite colours and 3 favourite sizes**, remembered between sessions. Tap a favourite to use it; tap it again (or the rainbow button) to swap it for a palette colour or any custom colour; the size chip opens a slider with exact values. The pen type and shape kind are one chip with a picture menu, and text lists and alignment sit behind one chip
- **Colours:** palette colours plus a custom colour picker (saturation/brightness square, hue slider, hex code, recently used colours and, where the system supports it, an eyedropper) for every tool and for recolouring a selection
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
