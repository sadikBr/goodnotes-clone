# Moving the TypeScript to C++ and Rust (done)

The Qt app used to run about 9,000 lines of TypeScript inside Qt's
JavaScript engine: the editor's logic, drawing of papers, covers, daftar
pages and elements, the shape and symbol recognisers, and the daftar and
cover setup. All of it now lives in C++ and Rust. The JavaScript engine,
the script bundle, npm and Node are gone from the app and its build.

Why: speed (pages and text drawn in the script engine took 20–40 ms each,
and every call copied data across), stability (Qt 6.12's script engine
crash), and one codebase to read and test.

## Where things went

- **Rust core (`core/`)**: the shape and ink refinement (`beautify`) and
  maths symbols (`symbols`), with their tests (`cargo test`).
- **C++ (`qt/src/`)**:
  - `draw/`: papers, covers, daftar pages, strokes (a port of
    perfect-freehand), shapes, text with lists and Arabic, sticky notes,
    pictures, whole pages, the paper and picture caches (`Render`).
  - `editor/`: the editor (`EditorCore`: tools, gestures, holding a stroke
    still, the lasso, moving with alignment guides, handles, transforms,
    erasing, text editing, undo, copying, instruments, the overlay) and what
    it uses on every pen movement: geometry, lists, guides, instruments.
  - `daftar/`: the daftar's details and the text boxes its pages are filled
    in with, lessons, holidays, student lists, outlines, class exports,
    creating and updating a daftar, and cover titles, designs and pictures
    (`DaftarApi` for QML; `DaftarEditor` for the in-editor actions).
  - `Catalog`: the paper list and small helpers for QML.
  - `Canvas`: the drawing surface (a canvas-2D style API over QPainter),
    kept from the script days so the ported code drew the same pixels.
- **QML** keeps its own small inline helpers; it no longer calls scripts.

## How it was kept identical

1. **The same canvas**: ported drawing code called the same canvas
   operations in the same order as the scripts did.
2. **Side by side**: while both existed, CI ran the scripts and C++ on the
   same inputs and failed on any difference:
   - drawing: every paper at every size, colour and orientation, every cover
     design in every colour, every daftar page, 171 sample elements at two
     zooms, whole pages and the pen previews: 1,620 pictures, identical;
   - the editor: 11 scripted editing sessions, 304 steps, comparing every
     page's elements, the selection bar, the text being edited, history, the
     instrument, live transforms, cursors and the overlay's picture after
     each step: identical;
   - the daftar and covers: 3,416 checks, from page contents at three sizes
     to whole flows (creating and updating a daftar, 130 students, lessons
     until pages fill, outlines, exports, every cover design): identical;
   - the recognisers: 1,783 generated hand-drawn strokes: identical.

   Each check was shown to catch planted mistakes.
3. **Tests moved with the code**: the Vitest tests became C++ self-tests
   (`inkleaf --self-test`) or Rust tests.
4. **Reference results**: before the scripts were deleted, what the C++
   code gives (identical to the scripts) was recorded in
   `qt/tests/golden/`: the editing sessions step by step, the daftar and
   cover results, and pictures of every paper, cover, daftar page, element
   kind, whole pages and the pen previews. CI checks every push against
   them (`inkleaf --golden check qt/tests/golden`). After an intended
   change, record them again with **Actions → Record reference results**
   (they're recorded on CI's Qt version; text can measure a hair
   differently on others).

## Steps

| # | Step | Replaced |
|---|---|---|
| 0 | C++ entry points on the canvas, side-by-side checks in CI | — |
| 1 | Papers in C++ | `papers.ts` |
| 2 | Covers in C++ | `covers.ts` |
| 3 | Daftar pages in C++ | `daftarPapers.ts` |
| 4 | Elements and export in C++ | `render.ts`, `ink.ts`, `fonts.ts` |
| 5 | Recognisers in Rust | `beautify.ts`, `symbols.ts` |
| 6 | The editor in C++, with its geometry | `editorCore.ts`, `geometry.ts`, `snap.ts`, `textFormat.ts`, `ruler.ts`, `model.ts` |
| 7 | Daftar and cover setup in C++ | `daftar.ts`, `cover.ts`, `daftarQt.ts`, `samples.ts` |
| 8 | The script engine removed | `Js.*`, the canvas's script parts, `qt/js/`, `src/`, npm |

The daftar and cover setup went to C++ rather than Rust as first planned:
it depends on the page layouts and text measuring, which are C++.

The version is in `VERSION` (`scripts/set-version.py`), the fonts' list in
`scripts/qt-fonts.py`, and the icons in `qt/qml/IconData.js`.
