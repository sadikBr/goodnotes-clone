# 05 — From findings to Inkleaf

Inkleaf (this repository) is a desktop note-taking app built with **Tauri 2 +
Rust** and a TypeScript canvas frontend. It is an independent implementation:
the reports above informed *what* to build and how to structure it, but no
Goodnotes code, assets, templates or branding are used.

## Architecture decisions

| Goodnotes web (finding) | Inkleaf |
| --- | --- |
| Heavy native core (Swift→WASM) + thin web shell, talking over a typed bridge | Rust core (`src-tauri/src`) + thin TS shell, talking over typed Tauri commands (`src/api.ts`) |
| SQLite as the local store | SQLite through `rusqlite` (bundled), one file in the OS app-data directory |
| Event log + CRDT sync | **Not implemented.** Inkleaf is single-device; each page's elements are saved atomically as rows. A CRDT layer (e.g. `yrs`, as Goodnotes uses) is the natural next step for sync. |
| Tombstone / recover for documents and folders | `trashed_at` soft deletes with recover and "delete forever"; a notebook in a trashed folder recovers to the root |
| Page operations (`movePages`, `copyPages`, `trashPage`, `relabelPage`, …) | Add/insert, duplicate, move up/down, delete (keeps at least one page), bookmark |
| `ShapeBeautifier` / draw-and-hold shapes | `ink::recognize` in Rust: line, triangle, rectangle (snapped axis-aligned or squared up), polygon, circle/ellipse. Used by the Shape tool and by holding the pen still at the end of a stroke |
| Stroke processing | `ink::simplify` (Ramer–Douglas–Peucker, keeping pressure) on every pen stroke |
| `scribbleToErase`, `circleToLasso` gestures | Stroke eraser and lasso selection (move, recolor, duplicate, delete) |
| Text boxes and sticky notes | Text-box and sticky-note tools, inline editing, word wrap |
| Search indexing of typed content | `text_content` column on elements; search over titles and typed text with snippets |
| `renderThumbnail` | Frontend renders a 200 px JPEG thumbnail on save; stored per page and used as the notebook cover preview |
| Viewport API (`setDocumentScale`, `zoomToFit`, …) | Zoom in/out, Ctrl+wheel, fit width, pan tool |
| Dynamic templates | Paper templates: blank, ruled, narrow ruled, grid, dotted, Cornell |
| `darkThemeEnabled` | Light/dark theme for the app UI (paper stays paper-coloured) |
| `undo`/`redo` (`UnifiedUndo`) | Per-page snapshot undo/redo (100 steps) |
| Pointer events handled in-engine, stylus detection | Pointer events with pressure; once a pen is seen, touch only pans (palm rejection) |

## Deliberately out of scope

These Goodnotes features rely on services, models or licensed content that a
local-only app can't reasonably reproduce:

- Accounts, cloud sync, sharing, comments, classroom, organisations
- Handwriting recognition / ink-to-text and handwriting beautification (needs ML models)
- AI features (Ask your notebook, math assistance), audio recording/transcription
- PDF import/annotation (Goodnotes uses PDFium; candidates for Inkleaf would be `pdfium-render` or pdf.js)
- Marketplace, GIF stickers, flashcards / spaced repetition
