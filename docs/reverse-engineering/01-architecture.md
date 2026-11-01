# 01 — Architecture

## Summary

Goodnotes on the web is **not a web rewrite of the app**. The core of the product
(notebooks, pages, ink, rendering, search indexing, sync) is the same Swift code
base as the native apps, compiled to **WebAssembly** and hosted inside a thin
**React** shell that handles login, the library chrome, dialogs and browser
integration.

```
┌──────────────────────────── browser tab: web.goodnotes.com ────────────────────────────┐
│ React + Redux shell (Vite build, ~165 JS chunks, 28 MB)                                 │
│   login · library chrome · dialogs · marketplace · i18n · analytics · push             │
│        │  BridgeJS calls (bjs_GNWasmEditorBridge_*)          ▲ callbacks (on…)         │
│        ▼                                                     │                         │
│ WASMGoodNotesApp.wasm  (74 MB, Swift toolchain + C/C++ + Rust)                             │
│   WASMGoodNotesLibrary · CommonCore · NotesItems · RendererV4/V5 · Tools · Ink          │
│   HandwritingRecognition/Beautification · ShapeBeautifier · RichText · CommonNLI       │
│   SQLite (exported sqlite3_* API) · PDFium · Automerge + Yrs (Rust, via UniFFI)         │
│        │ host imports                                                                    │
│        ▼                                                                                 │
│ Browser APIs: WebGL2 · Canvas2D/Path2D · OffscreenCanvas · Pointer events · WASI shim   │
│               OPFS (sync access handles) · IndexedDB · Cache Storage · Service Worker   │
│               SharedArrayBuffer + worker threads · WebSocket/RTC                        │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

## Evidence

**Languages and toolchain [WASM].** The module's `producers` section lists the
languages *Swift, C11, C99, C++14, Rust*, processed by `clang 21.0.0` from
`github.com/swiftlang/llvm-project` (the Swift toolchain's LLVM) and
`rustc 1.82.0`. Target features: atomics, SIMD128, bulk-memory,
reference-types, multivalue and mutable-globals, i.e. a multithreaded WASM build.

**Size and shape [WASM].** 171,052 functions; a 52.4 MB code section and 20.8 MB
of data segments. Debug names are stripped (an `external_debug_info` section
points to a separate symbol file), but about 900 Swift source paths survive in
assertion and diagnostic strings.

**Swift modules [WASM].** These source directories appear in the binary (count = paths seen):

| Area | Modules |
| --- | --- |
| App/bridge | `WASMGoodNotesLibrary` (98), `WASMAPIBridge`, `WebUtils`, `JavaScriptKit`, `JavaScriptEventLoop` |
| Core model | `CommonCore` (74), `NotesItems`, `Schema`, `CommonGoodNotesLibrary`, `UnifiedUndo`, `P2PArrayIndex` |
| Rendering | `RendererV5` (54), `RendererV4` (37), `RendererInterface`, `CommonRendering` (incl. `PDFiumWrapper.swift`), `CommonHybridRendering`, `CommonPagingView`, `PagingViewModelV2` |
| Ink & tools | `Tools`, `Ink`, `Geometry`, `BezierKit`, `Gestures`, `StrokeClassification`, `ShapeBeautifier`, `HandwritingBeautification`, `HandwritingRecognition`, `BulletDetection` |
| Text | `RichTextModel`, `GNRichTextView`, `RichTextWeb` |
| Intelligence | `CommonNLI`, `SpacedRepetition`, `NotesOrganisationService`, `Matft` (NumPy-like arrays) |
| Collaboration | `CommonRTCBuffer`, `CommonRTCLogger`, `Automerge` |
| Infra | `SwiftProtobuf`, `CryptoSwift`, `AsyncAlgorithms`, `XPSQLiteDatabases`, `CommonSettings`, `GNFeatureCoordinator`, `CommonTimeTracker`, `CommonCPUTimeMonitoring` |

**Two renderer generations [WASM] [Inference].** `RendererV4` and `RendererV5`
ship side by side, with a `RendererInterface` abstraction. A new renderer is
probably being rolled out behind remote-config flags (see the
`RemoteConfigurationClientPort` imports in report 02).

**Rust components [WASM].** Embedded Cargo paths identify `yrs 0.18.8` (the
Rust port of the Yjs CRDT), `uniffi_core 0.28.3` / `0.29.4` (Swift↔Rust FFI) and
`uuid 1.11`. The strings "automerge-swift" and "The type in the automerge
document" indicate the Automerge CRDT core as well.

**Host interface [WASM].** 669 imports, grouped by host module:
`goodnotes_webgl2_context` (119), `goodnotes_context2d` (80),
`goodnotes_events` (77, engine→shell callbacks), `bjs` (77),
`WASMGoodNotesLibrary` (62, BridgeJS class wrappers and service ports), `wasi_snapshot_preview1` (35), `javascript_kit` (32),
`goodnotes_cssstyledeclaration` (31), plus small groups for `Path2D`, `canvas`,
`OffscreenCanvas`, pointer events, `window`, `document`, images and URL search
params. The engine draws pages itself through WebGL2/Canvas2D and handles
pointer events directly, instead of building DOM.

**Threads [WASM] [JS].** JavaScriptKit's worker-thread imports
(`swjs_send_job_to_main_thread`, `swjs_wake_up_worker_thread`, …), a
`wasi_thread_start` export, the `atomics` feature and 22 `SharedArrayBuffer`
references in the JS mean the engine runs work on Web Workers sharing memory
with the main thread.

**Shell [REA] [JS].** REA inventoried 165 JavaScript assets with 542 modules and
53 source-map references. Chunk names (`NavigationTabBar`, `DocumentMoveDialog`,
`CreateTemplateModal`, `LoginScreen`, `marketplace`, `redux`, `react`, `yjs`,
`html2canvas`, `purify.es`, `dayjs`, `i18n-helpers`, …) point to a React + Redux
application bundled with Vite (`__vite-browser-external`, `preload-helper`).

## Why build it this way [Inference]

Compiling the shared Swift core gives the web app the same document model,
renderer, handwriting features and sync logic as iPad/Mac, so a notebook behaves
the same everywhere. The cost is a 74 MB download, which the service worker
precaches (report 03).
