# Inkleaf — the Qt app

Inkleaf's app in Qt Quick (QML + C++), on the Rust core (`../core`, through
`native/`). Its library folder is the one the earlier Tauri app used, so
notebooks from it open as they were.

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

After changing the shared drawing code (`src/editor`, `src/daftar.ts`,
`src/cover.ts`…) or `js/`, run `npm run qt-bridge`: it bundles that code into
`js/bridge.js`, which the app runs.

CI (`.github/workflows/qt.yml`) builds the AppImage, the Windows folder and
the APK on every push that touches the app (kept as run artifacts); the
release workflow publishes them.

## What works

Everything:

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
| `src/JsCanvas.*`, `src/Js.*` | A Canvas 2D for the shared drawing code, and the script engine running it |
| `src/Pdf.*` | PDF import and PDF page pictures |
| `js/` | The editor core and the bridge to the shared code in `../src` (`bridge.ts` → `bridge.js`) |
| `qml/` | Screens (`Library`, `Editor`), `Theme` (colours), icons |
| `native/` | The C interface to the Rust core |
