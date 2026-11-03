# Inkleaf — native Qt app

The native rewrite of Inkleaf's interface in Qt Quick (QML + C++), on the
same Rust core as the current app (`../core`, through `native/`). It opens
the same library, so your notebooks appear in both while the rewrite is
under way.

Ink is drawn on the GPU: every stroke is a triangle mesh built once, so
scrolling and zooming never re-draw ink.

## Build and run on Linux (Arch)

```sh
sudo pacman -S --needed qt6-base qt6-declarative qt6-svg qt6-webengine cmake base-devel rustup
rustup default stable
cmake -S qt -B build/qt -DCMAKE_BUILD_TYPE=Release
cmake --build build/qt -j
./build/qt/inkleaf
```

(`qt6-webengine` provides Qt PDF, used to import and show PDFs.
Ubuntu/Debian: `qt6-base-dev qt6-declarative-dev qt6-svg-dev qt6-pdf-dev
qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-window
qml6-module-qtquick-templates qml6-module-qtquick-dialogs qml6-module-qt-labs-folderlistmodel
qml6-module-qt-labs-settings qml6-module-qtqml-workerscript cmake`.)

`INKLEAF_DATA_DIR=/some/folder ./build/qt/inkleaf` uses another library folder (for testing).

After changing the web app's drawing code (`src/editor`, `src/daftar.ts`,
`src/cover.ts`…), run `npm run qt-bridge`: it bundles that code into
`js/bridge.js`, which the Qt app runs so both apps draw alike.

CI (`.github/workflows/qt.yml`) builds an AppImage and a Windows folder on
every push that touches the app; they are kept as run artifacts.

## What works

Everything the current app does, with its look:

- Library: notebooks with their covers, folders, favourites, trash, search,
  sorting, the New menu; import PDFs (with progress); dark mode.
- Editor: all pages one under the other; pens (fountain, ballpoint, pencil,
  brush, calligraphy) with pressure; highlighter; precise and whole-stroke
  eraser; lasso (move, resize, rotate, recolour, copy); shapes (drawn or
  recognised); text boxes and sticky notes with formatting and Arabic;
  pictures; ruler, protractor and set squares; laser pointer; papers and
  covers; page list (add, duplicate, move, bookmark, delete); undo/redo;
  zoom; focus mode; keyboard shortcuts; export (vector PDF, PowerPoint, PNG).
- Daftar annossous: setup (classes, timetable, holidays), class sections,
  New lesson, outlines, student lists, class export, settings.
- Settings: writing, stylus and touch, Google Drive sync, backups and
  restore, update checks, theme.

## Layout

| Path | What |
|---|---|
| `src/Core.*` | The Rust core for QML (`InkleafCore.call(command, args)`) |
| `src/InkCanvas.*` | The page canvas: input, layout, scene-graph rendering, saving |
| `src/Ink.*` | Ink geometry (smoothing, pen widths, stroke meshes) |
| `src/JsCanvas.*`, `src/Js.*` | A Canvas 2D for the web app's drawing code, and the script engine running it |
| `src/Pdf.*` | PDF import and PDF page pictures |
| `js/` | The web code the app uses (`bridge.ts` → `bridge.js`) and the editor core |
| `qml/` | Screens (`Library`, `Editor`), `Theme` (the web app's colours), icons |
| `native/` | The C interface to the Rust core |
