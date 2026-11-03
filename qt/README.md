# Inkleaf — native Qt app (in progress)

The native rewrite of Inkleaf's interface in Qt Quick (QML + C++), on the
same Rust core as the current app (`../core`, through `native/`). It opens
the same library, so your notebooks appear in both while the rewrite is
under way.

Ink is drawn on the GPU: every stroke is a triangle mesh built once, so
scrolling and zooming never re-draw ink.

## Build and run on Linux (Arch)

```sh
sudo pacman -S --needed qt6-base qt6-declarative qt6-svg cmake base-devel rustup
rustup default stable
cmake -S qt -B build/qt -DCMAKE_BUILD_TYPE=Release
cmake --build build/qt -j
./build/qt/inkleaf
```

(Ubuntu/Debian: `qt6-base-dev qt6-declarative-dev qt6-svg-dev qml6-module-qtquick-controls
qml6-module-qtquick-layouts qml6-module-qtquick-window qml6-module-qtquick-templates
qml6-module-qt-labs-settings qml6-module-qtqml-workerscript cmake`.)

`INKLEAF_DATA_DIR=/some/folder ./build/qt/inkleaf` uses another library folder (for testing).

## What works so far (milestone 1)

- Library: your notebooks with their covers; new notebook; dark mode.
- Editor: all pages one under the other; pen (fountain, ballpoint, pencil,
  brush) with pressure, speed-sensitive width and smoothing levels;
  highlighter; eraser (also the pen's eraser end and side button);
  colours and sizes; undo/redo; zoom (Ctrl+wheel, pinch, Ctrl+±, fit width);
  pen-only mode; add page. Pages save as you write.
- Paper styles so far: blank, ruled (normal, narrow, wide), legal, grid, dotted.

Text, shapes, pictures, stickers and PDF backgrounds aren't drawn yet; they
stay in your notebooks untouched. The roadmap follows the current app's
features screen by screen.

## Layout

| Path | What |
|---|---|
| `src/Core.*` | The Rust core for QML (`InkleafCore.call(command, args)`) |
| `src/InkCanvas.*` | The page canvas: input, layout, scene-graph rendering, saving |
| `src/Ink.*` | Ink geometry (smoothing, pen widths, stroke meshes) |
| `src/Paper.*` | Paper styles as geometry |
| `qml/` | Screens (`Library`, `Editor`), `Theme` (the web app's colours), icons |
| `native/` | The C interface to the Rust core |
