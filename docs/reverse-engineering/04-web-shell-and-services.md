# 04 — Web shell, routes and services

## The shell

**Build and framework [REA] [JS].** A Vite build: content-hashed `assets/*.js`
chunks, `preload-helper` and `__vite-browser-external`. It uses React with Redux
(`redux`, `with-selector`, `store`, `selectors`, `state`), `react-dnd`
(`DndProvider`) and DOMPurify (`purify.es`), plus `html2canvas`, `dayjs`, `yjs`
and its own i18n (`translation`, `i18n-helpers`, `cjk-detection`, Noto Sans CJK
fonts). The UI typeface is Roobert (`Roobert-*.ttf`).

**Static routes [REA]** (`route_inventory`, low confidence, router-configuration basis):

| Route | Notes |
| --- | --- |
| `/*` | App catch-all |
| `/open/*` | Open a document by link |
| `/deeplink/` | Deep links (also `useIsOpenInAppEnabled`: hand-off to native apps) |
| `/share-target` | PWA Web Share Target: share files into Goodnotes from the OS |

Observed at runtime **[REA]**: `/` redirects to `/login`. The accessibility tree shows a
"Welcome to" heading, the tagline "Note down, shape, & share your ideas with the
world's best loved digital paper.", and links to Terms, Privacy, Cookie Policy
and Contact support. Login chunks include `LoginScreen`, `LoginRoute`,
`useAppLoginFlow`, `CheckYourInbox`, `OtpFunnelTracking` and
`DifferentAccountDialog`, consistent with an email one-time-code login
**[Inference]**. `kratos.goodnotes.com` points to Ory Kratos as the identity
service **[JS] [Inference]**.

**Feature chunks [REA] [JS]** show the product surface around the editor:
`NavigationTabBar`, `TopBar*`, `Notebook`, `StudySet` (flashcards; the engine has
`SpacedRepetition`), `WhiteboardIcon` (whiteboards), `CreateTemplateModal`,
`DynamicTemplateLoader` / `Renderer` (templates generated from data),
`marketplace` (paid templates, stickers and planners), `ElementPicker`,
`getGiphyService` (GIF stickers), `GoodnotesAIShareChatHistoryPopover` (AI chat),
`useMeetingAssistantLogger` and `useAudioSupportedLocales` (audio recording and
transcription), `SharedByLabelType` / `UnshareDialogBox` / `ShareMenuItems`
(sharing), `useCurrentWorkspace` and `useIsOrgEmailVerificationRequired`
(organisations), `push-manager-factory` / `request-push-permission` (web push),
`use-diagnostics-exporter`.

## Backend and third-party hosts [JS]

Counted string occurrences in the downloaded JS (a host being present does not
prove the logged-out page calls it):

| Host | Inferred role |
| --- | --- |
| `api.goodnotes.com`, `api.goodnotescloud.com`, `api.goodnotesbeta.com` | Core API (prod, cloud, beta) |
| `tile.goodnotescloud.com` | Page tile rendering |
| `kratos.goodnotes.com` | Identity (Ory Kratos) |
| `asknotebook-api.goodnotes.com`, `hk-asknotebook-api…` | "Ask your notebook" AI |
| `math-api.goodnotes.com`, `hk-math-api…` | Math recognition/assistance |
| `ai-feedback-api.goodnotes.com` | AI feedback collection |
| `classroom.goodnotes.com`, `edu-admin.goodnotes.com` | Classroom / education |
| `org-api.goodnotes.com`, `org-admin.goodnotes.com` | Organisations / workspaces |
| `user-discovery.goodnotes.com` | User lookup for sharing |
| `api.integrations.goodnotes.com` | Third-party integrations |
| `gn-shop.csan.goodnotes.com`, `isi.csan.goodnotes.com` | Shop / marketplace content (CDN) |
| `unleash-edge.goodnotes-devops.com` | Feature flags (Unleash) |
| `otlp.o11y.goodnotes-devops.com` | OpenTelemetry traces/metrics |
| `events-webhook.insights…`, `ff-events-webhook.insights…` | Product analytics ingestion |
| `api2.amplitude.com`, `api.eu.amplitude.com` | Amplitude analytics (US/EU) |
| `android.googleapis.com` | Web push (FCM) |
| `apps.apple.com`, `play.google.com` | Links to native apps |

The `hk-` hosts suggest regional deployments for AI services **[Inference]**.

## REA graph statistics [REA]

- 165 JavaScript assets → 542 modules, 53 source-map links, 3 network endpoint nodes
- Semantic graph: 1,428 functions, 4,730 call sites, 11 promises, 12 async tasks, 2 request sites, 13 boundary crossings (concentrated in `DynamicTemplateRenderer` / `DynamicTemplateLoader` / `CreateTemplateModal`)
- 10,134 findings; 238 explicit unknowns (mostly files left unanalyzed after the per-file timeout)
