# 03 — Data, storage and sync

## Local storage in the browser

**Storage inventory [REA]** (`inspect-web-page --include-storage-keys`; values redacted by REA):

| Store | Names |
| --- | --- |
| IndexedDB databases | `elements`, `events`, `notes`, `sync-metadata`, `health-check-data` |
| Cache Storage | `static-assets-precache`, `good-notes-precache-v2-sw-cache-<build>`, `on-demand-resources` |
| localStorage keys | `APP_LAST_BUILD`, `browser_device_id`, `statsig.session_id.*`, `statsig.stable_id.*` |
| Origin usage | ≈185 MB on the login page alone (mostly the precached WASM and assets) |

A service worker (`/service-worker.js`) was running for the origin **[REA]**.

**SQLite inside the engine [WASM].** The module exports the SQLite C API: 177
`sqlite3*` functions (prepare/bind/step/column, virtual tables, VFS, including
`sqlite3__wasm_*` helpers), the same export surface as the official
`sqlite3.wasm` build. A Swift wrapper module `XPSQLiteDatabases` is present.

**OPFS [JS].** The JS uses `navigator.storage.getDirectory` (5×),
`createSyncAccessHandle` (2×) and the string "OPFS" (7×).

**[Inference]** The engine keeps its databases in the Origin Private File
System through a SQLite VFS backed by synchronous access handles. Those handles
only work in Web Workers, which fits the threaded build in report 01. The
IndexedDB stores carry the sync pipeline: an `events` log, `notes`/`elements`
payloads and `sync-metadata` checkpoints.

## Document model

- **Event-sourced edits [WASM] [Inference].** Callbacks such as `onEventsSaved`, `onSyncEventsSaved`, `onSyncEventsSynced` and `getDocumentIdsFromSyncEvents` indicate that edits are recorded as events, saved locally first and then synced.
- **CRDTs [WASM].** `yrs` 0.18.8 (Yjs in Rust) and Automerge are both linked through UniFFI, and the shell ships a JS `yjs` chunk **[REA]**. `P2PArrayIndex` / `createP2pArrayIndices` fits a CRDT-friendly ordering scheme for things like page order. **[Inference]** CRDTs let offline edits from several devices or collaborators merge without conflicts.
- **Soft deletes [WASM].** Separate `tombstone*` / `recover*` / `delete*` methods for documents and folders, plus `trashPage(s)` / `recoverPage`, implement a recoverable trash.
- **Schema versioning [WASM].** `getSchemaVersion`, `updateSchemaCompatibility` and a classroom-specific mitigation schema version, so clients of different ages can share data.
- **Protobuf [WASM].** `SwiftProtobuf` is linked, likely the wire or storage format for events.

## Sync lifecycle [WASM] [Inference]

```
edit ─▶ event saved locally (onEventsSaved)
     ─▶ queued (onSyncPending) ─▶ sync() ─▶ onSyncStarted
     ─▶ upload events / notes (onSyncEventsAttempted → onSyncEventsSucceeded,
                                onSyncNotesAttempted → onSyncNotesSucceeded)
     ─▶ onSyncCompleted
        ├─ transient error ─▶ onSyncErrorRetrying ─▶ … ─▶ onSyncErrorTooManyRetries
        ├─ 401 ─▶ onSyncAuthenticationFailed
        └─ quota ─▶ storageQuotaExceeded()
offline/online: wentOffline() / backOnline(); manual: retrySync(), stopSync()
integrity: onDocumentConsistencyCheck / onDocumentNotesConsistencyCheck
```

Backend hosts named in the shell **[JS]** include `api.goodnotes.com`,
`api.goodnotescloud.com`, `tile.goodnotescloud.com` and `app.goodnotes.com`.
**[Inference]** `tile.*` suggests page renders are served as tiles.

## Memory management [WASM]

`setMaxEditorsInMemory`, `getEditorsInMemoryCounts` and `unloadDocument` (the
shell has a `useMaxEditorsInMemory` hook), plus a tabbed editor
(`useTabbedEditorEnabled`, `closeTabs`). The app caps how many open documents
stay resident, which matters inside a 32-bit WASM address space.
