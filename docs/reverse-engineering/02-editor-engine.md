# 02 — Editor engine and the JS↔Swift bridge

The WASM module talks to the page through **BridgeJS**-style generated glue
(`bjs_*` symbols) on top of JavaScriptKit. The export and import tables give
an exact inventory of what the editor can do and what it reports back. Every
name below is quoted from the binary **[WASM]**; the groupings and
descriptions are ours.

## Boot sequence

Exports: `bjs_GNWasmStartupOptions_init`, `bjs_GNWasmStartup_init`,
`bjs_GNWasmStartup_setup`, `bjs_GNWasmStartup_startup`,
`bjs_GNWasmStartup_editorBridge`.

**[Inference]** The shell builds a startup-options object, sets up the engine,
starts it, then obtains a single `GNWasmEditorBridge` instance and calls into it
for the rest of the session. Pointer and resize input go straight into the
engine through `gn_handlePointerEvent` and `gn_handleResizeViewPort`, and DOM
callbacks enter through `gn_handle*AddEventListener`.

## Editor commands (shell → engine)

`GNWasmEditorBridge` exports 133 methods. Grouped by feature:

| Feature | Methods |
| --- | --- |
| Tools | `selectTool`, `selectPreviouslySelectedTool`, `getSelectedTool`, `changeStrokeColor`, `changeStrokeWidth`, `changeStrokeStyle`, `changeFillAlpha`, `deactivateObjectEditingTool`, `disconnectStylus` |
| Shapes & ink cleanup | `selectBeautificationStyle`, `showBeautificationFeedback`, `trackBeautificationFeedback`, `convertStrokesToText`, `updateSnapToGrid`, `updateSnapToObjects`, `updateRulerConfig` |
| Text, sticky notes | `addTextToCurrentPage`, `addTextAtPosition`, `addStickyNoteToCurrentPage`, `showStickyNoteAuthor`, `onFontSizePickerDismissed`, `onLineHeightPickerDismissed`, `onTextBoxStylePickerDismissed` |
| Undo | `undo`, `redo` |
| Viewport | `moveViewport`, `setDocumentScale`, `getDocumentScale`, `increaseDocumentScale`, `decreaseDocumentScale`, `resetDocumentScale`, `zoomToFit`, `updateScrollDirection`, `setMinimapVisibility`, `updateVirtualKeyboardDimensions` |
| Pages | `goToPage`, `goToPageById`, `reloadPage`, `clearPage`, `clearCurrentPage`, `clearPageItems`, `clearPageTapes`, `relabelPage`, `removePage`, `trashPage`, `trashPages`, `movePages`, `movePagesToDocument`, `copyPages`, `recoverPage`, `favouritePage`, `rotatePage`, `rotatePages`, `renderThumbnail`, `cancelThumbnailRender` |
| Documents | `renameDocument`, `moveDocument`, `mergeDocument`, `deleteDocument`, `tombstoneDocument`, `recoverDocument`, `favouriteDocument`, `changeDocumentLanguage`, `unloadDocument`, `closeTabs`, `setMaxEditorsInMemory`, `getEditorsInMemoryCounts` |
| Folders | `createFolder`, `renameFolder`, `stylizeFolder`, `moveFolder`, `deleteFolder`, `tombstoneFolder`, `recoverFolder`, `favouriteFolder` |
| Workspaces (orgs) | `createWorkspaceFolder`, `deleteWorkspaceFolder`, `moveWorkspaceFolder`, `moveWorkspaceDocument`, `resolveWorkspaceFolderAlias` |
| Library & search | `preloadLibrary`, `loadRetailLibrary`, `getTotalNumberOfDocuments`, `getNumberOfTrashedDocuments`, `indexDocuments`, `startIndexing`, `clearSearchResults`, `highlightContent`, `clearHighlights` |
| Comments | `loadComments`, `addComment`, `editComment`, `deleteComment`, `sendComment`, `resolveThread`, `dismissCommentThread`, `updateShowResolvedComments` |
| Audio notes | `renameAudioNote`, `duplicateAudioNote`, `deleteAudioNote`, `updateTranscriptionText`, `deleteTranscription`, `deleteTranscriptionSummary` |
| Sync & connectivity | `sync`, `retrySync`, `stopSync`, `wentOffline`, `backOnline`, `getDocumentIdsFromSyncEvents`, `createP2pArrayIndices`, `storageQuotaExceeded`, `syncPasteboard` |
| Schema | `updateSchemaCompatibility`, `getSchemaVersion`, `getClassroomSchemaMitigationSchemaVersion` |
| Classroom | `setDocumentClassroomEnabled`, `setTeacherAnnotationVisible`, `updateClassroomUser`, `cleanupUnsharedDocument` |
| Session | `updateUserInfo`, `clearAuthCache`, `clearSession`, `darkThemeEnabled` |
| Rendering diagnostics | `captureRendererSnapshot`, `cancelRendererSnapshotCapture` |

Value types passed across the bridge include `GNWasmPoint`, `GNWasmRect`,
`GNWasmColor`, `GNWasmRulerConfig`, `GNWasmCopyPagesRequest`,
`GNWasmMovePagesToDocumentResult`, `GNWasmMergeDocumentResult`,
`GNWasmNewFolderInfo`, `GNWasmRendererSnapshot`, `GNWasmClassroomUser` and
`GNWasmCommentThreadDismissInfo`. `GNWasmIndexingRun` exposes
`waitForResult`, `cancel` and `getAdmissionTiming` for background search indexing.

## Engine events (engine → shell)

77 `on…` callbacks are imported from the `goodnotes_events` host module.
Representative groups:

- **Editor state:** `onEditorLoaded`, `onEditorUnloaded`, `onCurrentPageUpdated`, `onCanUndoRedoUpdated`, `onToolSelected`, `onRulerToggled`, `onStylusEnabledUpdated`, `onRenderingStatusUpdated`, `onContentPositionedStatusUpdated`, `onFetchStatusUpdated`, `onPageNavigationStarted`, `onThumbnailUpdated`
- **Shape analytics:** `onLineDrawn`, `onArrowDrawn`, `onConnectorDrawn`, `onFreeShapeDrawn`, `onDefinedShapeDrawn`, `onShapeUndo`
- **Gesture features:** `onScribbleToEraseApplied` / `onUndoScribbleToEraseApplied` (scratch out ink to erase it), `onCircleToLassoStarted` / `Completed` / `Undone` (circle content to select it), `onConvertToTextUndone`
- **Sync:** `onSyncPending`, `onSyncStarted`, `onSyncCompleted`, `onSyncErrorRetrying`, `onSyncErrorTooManyRetries`, `onSyncAuthenticationFailed`, `onEventsSaved`, `onSyncEventsSaved`, `onSyncEventsSynced`, `onSyncEventsAttempted`, `onSyncEventsSucceeded`, `onSyncNotesAttempted`, `onSyncNotesSucceeded`, `onFailedToStoreNotes`
- **Integrity:** `onDocumentConsistencyCheck`, `onDocumentNotesConsistencyCheck`, `onDocumentError`, `onDocumentNotFound`, `onClassroomCompatibilityError`
- **Text:** `onTextBoxUsage`, `onTextEditIntercepted`, `onUpdateTextEditorIsEditing`, `onResetTextToolbarStyles`, `onCopyAsTextCompleted`, `onTextErrorRenderingIssues`
- **PDF:** `onInternalPDFLinkClicked`, `onOpenPDFExternalUrl`, `onPDFLinkTargetPageNotFound`, `onExportingFileProgressUpdate`
- **Other:** `onPullToAddState` / `Update` (pull past the last page to add one), `onAnswerZonesUpdated` (worksheets), `onParticipantsRemoved`, `onDocumentUnshared`, `onNotesPreloadProgress`, `onNexusDocumentNotesUpdated`

## Services the engine borrows from JS

Typed "ports" imported from the `WASMGoodNotesLibrary` host module let the Swift code use browser-side services:

| Port | Purpose [Inference from names] |
| --- | --- |
| `JSRTCSocket` (connect, publications, waiting room, presence stats) | Real-time collaboration and classroom sessions |
| `RemoteConfigurationClientPort` (`stableBool/Int/String`, `latest*`, subscribe) | Feature flags and remote config (the shell includes an Unleash edge host and Statsig keys; report 04) |
| `MonitoringPort` (views, actions, resources), `RemoteLoggerPort`, `PerformanceMarkTracer` | Observability |
| `TaskSchedulerPort` / `TaskSchedulerRequest` (`waitForSlot`, `releaseSlot`) | Cooperative scheduling of background work |
| `IndexingExecutionMarkerPort`, `IndexingPageMetadataPort.hasTypedContent` | Search-indexing coordination |
| `PreloadLanguageModels`, `GNTextCheckerODRLoader.loadModel` | On-demand downloading of handwriting and text-checking language models |

## Handwriting features

Present as modules **[WASM]**: `HandwritingRecognition`, `HandwritingBeautification`,
`ShapeBeautifier`, `StrokeClassification`, `BulletDetection`, `Ink`, `BezierKit`.
Bridge methods `convertStrokesToText` and `selectBeautificationStyle`, plus the
shape callbacks above, show the user-facing results: ink to text,
cleaned-up handwriting, and shapes snapped from rough strokes. **[Inference]**
Recognition models are not inside the 74 MB module; they are fetched on demand
(`PreloadLanguageModels`, plus the `on-demand-resources` cache in report 03).
