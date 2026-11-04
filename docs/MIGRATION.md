# Moving the last TypeScript to C++ and Rust

The app is Qt (QML + C++) on a Rust core, but about 9,000 lines of
TypeScript still run inside it, in Qt's JavaScript engine: the editor's
logic (`qt/js/editorCore.ts`), drawing of papers, covers, daftar pages and
elements (`src/editor/*`), the recognisers, and the daftar and cover setup
(`src/daftar.ts`, `src/cover.ts`). This plan moves all of it out, so the app
is C++ and Rust only, and removes the JavaScript engine, the bridge bundle
and npm from the build.

Why: speed (pages and text drawn in the script engine take 20–40 ms each;
every call copies data across), stability (Qt 6.12's script engine crash),
and one codebase to read and test.

## Where things go

- **Rust core (`core/`)**: logic that runs once per stroke or per action,
  with its tests ported from Vitest to `cargo test`: shape and ink
  refinement (`beautify`), maths symbols (`symbols`), and the daftar's and
  covers' data (classes, timetable, holidays, student lists, the text
  elements their pages get).
- **C++ (`qt/src/`)**: everything that draws or needs fonts: papers, covers,
  daftar pages, elements (strokes, shapes, text with Arabic and lists,
  sticky notes, images), selection and handles, export painting; and the
  editor (tools, gestures, selection, undo, text editing) with what it uses
  on every pen movement: geometry (bounds, hit tests, lasso, transforms),
  alignment guides (`snap`), list continuation (`textFormat`) and
  instrument maths (`ruler`) — calling across to Rust that often would
  cost more than it does.
- **QML** keeps its small inline helpers (that's part of Qt), but no longer
  calls `Js.run`.

## How we keep it identical

1. **The same canvas**: ported drawing code (`qt/src/draw/`) draws on the
   Canvas 2D implementation the scripts already use (`JsCanvas`, over
   QPainter), calling the same operations in the same order, so it draws
   the same pixels. (Its script-facing parts go in step 8; it becomes a
   plain C++ class.)
2. **Side-by-side checks**: `inkleaf --compare-render` draws every case
   (every paper at every size, colour and orientation; every cover; every
   daftar page; sample elements of every kind) both ways, the scripts' and
   C++'s, and fails if any picture differs by more than a hair. CI runs it
   on every push while both exist. Before the scripts are deleted, small
   reference pictures are saved (`qt/tests/golden/`) and the check keeps
   comparing against them.
3. **Tests move with the code**: each TypeScript test file is ported next to
   the code it tests (Rust or Qt Test) before the TypeScript is deleted.
4. **One switch at a time**: each step replaces one piece, keeps the app
   working, and is released only when its checks pass.

## Steps

| # | Step | Replaces | Check |
|---|---|---|---|
| 0 | C++ entry points on the canvas, compare harness in CI | — | builds; harness runs |
| 1 | Papers in C++ | `papers.ts` | compare: all papers |
| 2 | Covers in C++ | `covers.ts` | compare: all designs, colours |
| 3 | Daftar pages in C++ | `daftarPapers.ts` | compare: all daftar pages |
| 4 | Elements in C++: strokes, shapes, text layout, sticky notes, images; export painting | `render.ts`, `ink.ts`, `fonts.ts` | compare: element samples; text wrap identical |
| 5 | Recognisers in Rust | `beautify.ts`, `symbols.ts` | ported tests; script vs Rust on generated strokes |
| 6 | Editor in C++, with its geometry | `editorCore.ts`, `geometry.ts`, `snap.ts`, `textFormat.ts`, `ruler.ts`, `toolkit.ts`, `colorPicker.ts`, `model.ts`, `papers.ts` | ported tests (Qt Test); editor UI tests (draw, select, transform, text, undo) |
| 7 | Daftar and covers setup in Rust | `daftar.ts`, `cover.ts`, `daftarQt.ts` | ported tests |
| 8 | Remove the script engine | `Js.*`, `JsCanvas.*`, `qt/js/`, `src/`, npm | build without Node; golden pictures |

Steps 1–4 bring the speed-up; step 6 is the largest (the editor's
1,500 lines plus its helpers). After step 8, fonts (`scripts/qt-fonts.py`)
read their list from a data file instead of `src/fonts.ts`.

## Progress

- [x] 0. C++ entry points on the canvas, compare harness in CI
- [x] 1. Papers (`qt/src/draw/Papers.cpp`, `Fonts.cpp`): 1,033 cases identical. `papers.ts`
  stays until step 6 for the editor's alignment guides (`paperLayout`), which
  then use the C++ one.
- [x] 2. Covers (`qt/src/draw/Covers.cpp`): every design in every cover colour, all
  sizes and orientations: 270 cases identical. `covers.ts` stays for the
  cover setup (`cover.ts`) until step 7.
- [x] 3. Daftar pages (`qt/src/draw/DaftarPapers.cpp`): every page at every size,
  orientation and colour, and where their filled-in values go: identical.
  All paper drawing is now C++ (`Js::paper` no longer runs scripts).
  `daftarPapers.ts` stays for the daftar setup until step 7.
- [x] 4. Elements and export (`qt/src/draw/Elements.cpp`, `Strokes.cpp`,
  `Freehand.cpp`, a port of perfect-freehand): every pen style (taps, sharp
  corners, short strokes), shapes, text in every font with wrapping, lists,
  alignment and Arabic, sticky notes, pictures, rotation, whole pages: 171
  element cases at two zooms and 3 pages, identical. The canvas's pictures,
  thumbnails, image and PDF export now draw in C++. Still in the scripts
  until their steps: the editor's overlay (selection, handles, guides) and
  its text measuring (step 6), and the cover preview (step 7).
- [x] 5. Recognisers in Rust (`core/src/recognize/`): shapes, braces, smoothing
  and maths symbols, with their 24 tests ported; before the switch, script
  and Rust gave the same answers on 1,783 generated hand-drawn strokes (every
  shape, brace orientation and symbol). The editor calls them natively.
- [ ] 6. Editor and its geometry (C++)
- [ ] 7. Daftar and cover setup (Rust)
- [ ] 8. Script engine removed
