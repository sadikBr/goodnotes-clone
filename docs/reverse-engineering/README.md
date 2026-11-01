# How the Goodnotes web app works — reverse-engineering reports

These reports describe how the Goodnotes **web app** (`https://web.goodnotes.com`) is
built. They are based only on what any browser downloads from that site before
logging in. No account was used, no authentication was bypassed, and no
Goodnotes code, assets or branding were copied into this repository; the Inkleaf
app in this repo is an independent implementation.

| Report | What it covers |
| --- | --- |
| [01 — Architecture](01-architecture.md) | The big picture: a Swift app compiled to WebAssembly inside a React shell |
| [02 — Editor engine](02-editor-engine.md) | The WASM module, its JS↔Swift bridge and the editor's feature surface |
| [03 — Data, storage and sync](03-data-storage-sync.md) | SQLite-in-WASM, OPFS/IndexedDB, CRDTs (Yjs/Automerge) and the sync lifecycle |
| [04 — Web shell and services](04-web-shell-and-services.md) | The React/Vite shell, routes, backend hosts and third-party services |
| [05 — Mapping to Inkleaf](05-inkleaf-mapping.md) | Which findings shaped the Tauri/Rust app here, and what was left out |

## Method

Target: `https://web.goodnotes.com/` in headless Chromium 153, redirected to
`/login` (unauthenticated). Captured on 2026-11-01.

| Step | Tool | Result |
| --- | --- | --- |
| Passive page inspection (DOM, accessibility tree, scripts, resources, storage names) | **REA** `inspect-web-page` | 167 scripts, 136 resources, storage inventory |
| Static analysis of the downloaded JavaScript (165 files, 28 MB) | **REA** `analyze-javascript-application` | Module graph, 4 static routes, network endpoints; 137 files parsed |
| WASM inspection (74 MB module) | **WABT 1.0.34** `wasm-objdump` + `strings` (not REA, see limits) | Sections, 669 imports, 376 exports, producers, embedded source paths |
| Bundle capture with source maps | REA `analyze-web-bundle` | **Failed**, see limits |

Evidence labels used in the reports:

- **[REA]**: from REA evidence records (the inspection and static analysis above).
- **[WASM]**: read directly from the WebAssembly binary's import/export tables, custom sections or embedded strings.
- **[JS]**: string and module-name observations in the downloaded JavaScript files.
- **[Inference]**: our interpretation; plausible but not proven by the artifacts.

## Limits (read before relying on these reports)

- **Logged-out only.** Everything after login (the library, editor and sync traffic) was never run. Runtime behavior is inferred from the shipped code's interfaces, not observed.
- **REA's bundle capture hit a fixed limit.** `analyze-web-bundle` and `inspect-web-page --include-script-sources` both failed with "CDP message exceeded the 67108864 byte protocol budget", which REA 6.8.0 cannot override. The 74 MB WASM module is the likely cause. As a workaround the same public `/assets/*` files were downloaded and analyzed locally.
- **REA's WASM inspector didn't run.** `inspect-wasm-artifact` requires WABT exactly 1.0.42. The environment had 1.0.34, and downloading 1.0.42 was blocked by the session's permission policy. WASM findings therefore come from WABT/`strings` directly, not from REA evidence.
- **Partial JS analysis.** REA hit its 300 s per-file limit on one 4 MB file (`state-*.js`) and returned partial evidence: 137 of 165 files parsed, 238 recorded "unknowns".
- **Not covered.** The native iPad/Mac/Android apps were not examined. `www.goodnotes.com` (the marketing site) was blocked by the environment's network policy.
- **Snapshot.** Asset hashes change with every deploy, so file names quoted here will go stale.
