# Inkleaf

A handwritten note-taking app for Windows, Linux and Android tablets, built
with **Qt 6** (QML and C++, ink drawn on the GPU) on a **Rust** core. It
organizes notebooks in folders, takes pressure-sensitive ink, snaps
hand-drawn shapes into clean ones, and stores everything locally in SQLite.

The repository also contains the reports from studying how the Goodnotes web
app is built (see [`docs/reverse-engineering/`](docs/reverse-engineering/README.md)).
Inkleaf is an independent implementation informed by those findings; it contains
no Goodnotes code, assets or branding.

## Features

- **Library:** notebooks with colour covers and live first-page previews, nested folders, favourites, search over titles and typed text, trash with recover / delete forever, duplicate, move, rename. **Select** (or Ctrl+click, Shift+click for a range, a long press) picks several notebooks and folders to favourite, move, trash, recover or delete at once (Ctrl+A, Delete, Esc)
- **Maths:** select a sum, an equation or a function (typed, or handwritten: read by the AI, or by the computer's handwriting recognition) and press ∑. What was read shows in a field you can correct, with the answer as you type: values (2,5 × 4 = 10), checks (3 + 4 = 7 ✓), equations in one unknown (3x + 5 = 20 → x = 5; up to degree 2 exactly, numerically beyond), expansions ((x + 1)² → x² + 2x + 1). Decimal commas, ×, ÷, :, √, π, powers, |x|, degrees and Arabic digits are understood. **Write the answer** beside it (in the same size and font for typed text, in the ink's colour for handwriting), **Typeset** it, see the **Steps** (AI), or **Graph it**
- **Graphs:** insert a graph (toolbar) with one or more functions as you'd write them (y = x² − 2x, f(x) = 2 sin x, 1/x, √x), its window and a grid, with a live preview and Fit y. It's a page element: move it, resize it, write on it, Edit graph to change it; it's drawn the same in previews and exports
- **My pieces:** save any selection (a diagram you draw often, a signature, a stamp, a table, a graph) with *Save to my pieces*, and put it on any page from the pieces button. Nothing from a password-protected notebook can be saved there
- **Side by side:** the split button shows another notebook (a textbook PDF next to your notes) to read and write in, or this one again (view only, following your edits) to look at another page. The tools act on the side you last touched, and copy and paste work between the two
- **AI assistant** (Settings → AI, optional): use a model on your own computer (Ollama, LM Studio or any OpenAI-compatible server) or your own API key for Claude, ChatGPT or Gemini. *Ask* in the library answers from your notes (the most related pages); the ✨ button in a notebook asks about it, with ready-made questions (summarise this page or the notebook, quiz me, key points) and, in a daftar, lesson help (plan the next lesson, activities, write up a page, what's been covered). **Ask AI** on a lasso selection explains, solves step by step, summarises or translates what's selected (handwriting, text or part of a PDF), and answers can be put on the page as text. *Apply font* reads handwriting with the AI (any language, maths too). **Make handwriting searchable** reads handwritten pages in the background once you stop writing on them, so search and questions find what you wrote by hand. The API key is kept in `ai.json` in the app's data folder, readable only by you, and never goes into backups or sync; locked notebooks are never read in the background or used for library questions
- **Passwords:** *Lock with password…* on a notebook (including imported PDFs) or a folder. Its pages, ink, text, pictures, PDF and page previews are encrypted (Argon2id key, XChaCha20-Poly1305) on the device, in backups and in Google Drive sync; another device opens it with the same password. A locked folder covers its subfolders and everything added to it later; a notebook with its own password keeps it. Unlocked items stay open until you press the lock button in the library or quit. Titles and folder names stay visible, and locked text isn't searchable. There's no recovery: a forgotten password means the content is gone
- **Interface:** a warm, paper-inspired design with an ink-green accent, in light and dark. The library greets you, shows notebooks as books on a shelf (sortable, searchable with `/`, N for a new notebook) and has friendly empty states. In a notebook, the tools sit in three trays (writing, arranging, presenting); the active tool lifts out *dipped in its ink colour*, and tapping it again opens its options in a drawer that hangs from it (and glides along when you switch tools). The drawer tucks away as soon as you touch the page, or can be pinned open. Menus and pickers close when you click their button again. Undo/redo and page things (paper, image, instruments, pen-only) are grouped on the right beside a single *Export* button; page and zoom controls float bottom-right; the pages sidebar can be hidden (`[`); **focus mode** (`F`) leaves just the page; `?` lists every keyboard shortcut; tooltips show shortcuts. The new-notebook dialog shows a large live preview of your cover
- **Covers:** new notebooks open with a cover page in the notebook's colour, in one of nine designs picked with live previews: *Classic* (stitched spine, elastic band, label plate), *Minimal*, *Geometric* (low-poly facets), *Waves*, *Marble* (composition book), *Leather* (embossed border, metal corners, gold title), *Kraft* (kraft paper, cloth tape, sticker label), *Dots* and *Zellige* (Moroccan eight-pointed star tilework around a cream panel). The title and month are ordinary text you can edit or write over. Your paper starts on page 2. The library shows covers full-size; *Change cover* switches design and colour (moving the title and date into place) or adds a cover to an older notebook
- **Daftar annossous (دفتر النصوص / cahier de textes):** *New → Daftar annossous* creates a teacher's lesson log laid out like the printed books: title page (بسم الله، الجهة، المديرية الإقليمية، المؤسسة، الأستاذ(ة)، القسم، رقم التأجير، الموسم الدراسي), the teacher's card (المعلومات الشخصية / الإدارية / المهنية, diplomas, previous posts), the timetable (استعمال الزمن, with your lessons as notes), school holidays, the diagnostic report, then for each class a student list (60 numbered places) and lesson pages — التاريخ | القسم | التوقيت | طبيعة الأنشطة المنجزة | التوقيع on 5 mm squared paper with a notes strip, framed in a colour per class — and finally ministerial and academic memos and the teacher's absences. Your details are filled in wherever a page asks for them. *حصة جديدة · New lesson* writes today's date, the class and the lesson time (from your timetable; the next unlogged lesson of the day) on the next free line of that class's page, adding a page when it's full; the class chip switches classes and *Add page* continues the current class. The page button writes a *lesson outline* (التمهيد، النشاط 1، النشاط 2… or your own) down the activities column next to the lesson just started. In the daftar's ⋯ menu: *paste the student list* from Massar or Excel (Massar codes go to الرقم الوطني, names to the name column, 60 per page with pages added as needed), *export a class as PDF* for the inspector (whole year, a semester or this month), and *daftar settings* to change the details, classes, timetable and holidays later — every filled-in value is written again and new classes get a section (removed classes keep their pages). Holidays (with Morocco's fixed national holidays one click away) fill the holidays page, and New lesson asks first when today is a holiday. Headings carry a small French line
- **Arabic text:** text boxes and sticky notes support Arabic (Noto Sans Arabic is bundled): right-to-left paragraphs, logical alignment, lists with bullets on the right; mixed Arabic/French lines are ordered correctly
- **Paper:** 21 paper styles in six groups. *Basic:* blank, ruled, narrow ruled, wide ruled, grid, dotted. *Notes:* Cornell, legal pad, two columns, to-do list. *Maths & science:* millimetre graph paper (1 mm, 5 mm and 1 cm lines), coordinate plane on millimetre paper (axis ticks every 5 mm), isometric dots, hexagon. *Writing & art:* handwriting practice, calligraphy guide (55° slant), storyboard. *Planner:* daily and weekly. *Teacher:* daftar annossous and weekly timetable (bilingual, right to left). Every style adapts to A4, A5, US Letter, US Legal or square, portrait or landscape, and to white, cream, yellow, grey or dark paper; the picker shows them in tabs with previews. Paper style and colour can be changed per page or for all pages
- **Pen types:** fountain pen (pressure-sensitive), ballpoint, pencil (grain texture), calligraphy (flat 45° nib) and brush pen (tapered); translucent highlighter
- **Sync through Google Drive** (Settings → Sync): sign in once on each device (in the browser, with your Google account) and notebooks, folders and imported PDFs stay the same on your computer and tablet. Sync runs at start, every five minutes, when you close a notebook and when the tablet app is put away. Files live in Drive's hidden app folder, which only Inkleaf can see. A notebook changed on both devices keeps both versions (one as a copy); a notebook deleted for good on one device goes on the other; the notebook you're writing in is never replaced while open
- **Updates:** the app checks GitHub Releases and, when there's a new version, shows a banner with its notes and **Update now**: it downloads the new version with a progress bar, checks it against the checksum GitHub publishes, saves the open notebook and restarts into it. On Windows (installed with the setup) the new setup runs silently and reopens Inkleaf; the AppImage replaces itself; on Android the system installer asks you to confirm (the first time, Android asks you to allow Inkleaf to install apps). A portable Windows copy or a build from source gets the download page instead. Settings → Updates: automatic checks, Check now. See [docs/RELEASING.md](docs/RELEASING.md)
- **Android tablets:** the same app on Android, with a touch-first layout
- **Backups** (Settings → Backups): pick a folder — a USB key, or a folder Google Drive, Dropbox or OneDrive syncs — and Inkleaf saves the whole library there once a day while it's open, as `Inkleaf backup <date time>.inkleaf`, keeping the newest 7, 14 or 30. Files are written under a temporary name and renamed when complete, so a sync client never uploads half a backup; if the folder is missing (drive unplugged) you're told and it tries again later. *Export everything* saves the library as one `.inkleaf` file anywhere; *Restore* checks a backup, shows what's in it, and replaces the library (the current one is kept as `inkleaf-before-restore.db` in the app's data folder). A backup is a SQLite snapshot, so it holds notebooks, pages, ink, imported PDFs and daftar settings
- **Settings** (library sidebar → Settings, or ⋯ → Settings in a notebook), applied at once. *Writing:* smoothing Off / Light / Normal / Strong (each stroke keeps the smoothing it was written with; also in the pen's options), pressure Light / Normal / Firm (how hard to press for a full line), and the cursor under the pen — cross, small dot or hidden. *Stylus and touch:* the pen's side button erases or lassos while held (or does nothing), the eraser end erases, palm rejection, pen-only mode. *Appearance:* light, dark or system theme
- **Eraser:** *Precise* (default) erases only what it touches, cutting strokes and shape outlines; *Whole stroke* removes anything it touches. Text boxes and sticky notes are erased whole
- **Pen-only mode:** when a stylus is detected, only the pen writes; finger and mouse scroll, and two fingers pinch-zoom. Touches while the pen is writing or hovering are ignored. Palm rejection also works with pen-only mode off: touches just after the pen, or with a large contact area, are ignored, and a touch stroke already started is dropped when the pen comes down. Toggle with the pen button in the top-right; the choice is remembered
- **Smooth, natural ink:** pens are drawn as one smooth outline with gradual width changes (no steps or beads), with sharp corners kept crisp. The fountain pen and brush pen respond to drawing speed like real ink — fuller when you slow down, thinner on quick strokes — and with a stylus its pressure leads while speed adjusts it gently. Ballpoint, pencil, highlighter and shapes keep an even width. The width is stored with the stroke, so it looks the same at any zoom and in exports
- **Shapes:** pick line, arrow, rectangle, ellipse/circle, triangle, diamond or star and drag (Shift for squares, circles and 45° lines; optional fill)
- **Auto shapes and ink refinement:** with the Shape tool in *Auto* mode, every stroke is cleaned up: basic shapes snap to perfect geometry; curly braces (any orientation, slanted or lopsided) are matched against a clean brace template and redrawn; near-circular runs such as parentheses become true arcs; everything else (graphs, curves, brackets…) is de-jittered and fitted with smooth curves, with straight runs made straight (snapped to horizontal/vertical when nearly so) and sharp corners kept sharp
- **Hold to refine:** keep the pen (or mouse) down at the end of a stroke and it snaps to its refined form while you're still holding. Keep moving to rotate and resize it; hold still again to switch to moving it (and back). It's placed only when you lift. With the pens it works like the shape tool: shapes, arcs, braces, maths symbols, and otherwise a smoothed version of the stroke; write and lift without pausing and your ink is kept exactly as written
- **Maths symbols:** hold at the end of a stroke to get a clean ∫ Σ √ ∞ (one stroke) or = ≡ ≠ ⇒ ⇔ → ↔ ≤ ≥ ± ∀ ∃ — for symbols of several strokes, the strokes you wrote just before (within a couple of seconds, right next to it) are included and replaced, in one undo step. Recognition ($P point clouds plus corner and proportion checks) ignores stroke order and direction and leaves letters and digits alone
- **Alignment guides:** when moving text, magenta guides appear and the text snaps when its baseline or letter centre lines up with other text, sits on a ruled line or centred between two, or its ink lines up left/right/centre with other text, the margin or the page centre. Guides follow the letters, not the text box. Hold Alt to move freely
- **PDF import and annotation:** *Import PDF* in the library turns a PDF (lecture slides, papers, worksheets…) into a notebook with one page per PDF page, at the PDF's own page size. The PDF is stored inside Inkleaf and rendered as each page's background, sharp at any zoom, so you can write, highlight, add text and shapes on top. Exports include the PDF content under your notes. PDFs are rendered on the device (Qt PDF; Android's PDF renderer), so it works offline
- **Ruler, protractor and set squares (R):** see-through drawing instruments with real-size markings that float over the page; pick one from the ruler button's menu (R shows or hides the last one). Drag to move; rotate by dragging a round grip, scrolling the wheel over it (1° per notch, 15° with Shift) or twisting with two fingers; it snaps to common angles. Pen, highlighter and shape strokes started next to an edge lock to it, with the ink against the edge and a live readout: length in cm along straight edges, swept angle along the protractor's curved edge. The 30 cm *ruler* has mm/cm scales; the *protractor* has 0–180° scales (both directions) and two arms you drag to measure an angle; the *set squares* (45° and 30°/60°) have cm scales on both legs and work along all three edges. Only the instrument's body takes input, so you can draw right next to it. Instruments are never part of the page or exports
- **Laser pointer (K):** for presenting and explaining. *Trail* draws a glowing tube (bright colour with a white core, fuller when you move slowly and slimmer on fast sweeps) that stays while you keep drawing; shortly after you stop, the colour fades and then the core (drawing again during the fade brings it back). *Dot* is a small pointer that follows the pen or mouse. Red, green, blue, purple or orange (or any colour). It is drawn incrementally on its own layers, so it stays smooth however much you draw. Nothing is ever added to the page, undo history or exports; in pen-only mode the mouse can still use it
- **Objects:** text boxes, sticky notes, images (insert or paste)
- **Text formatting:** bold, italic, bulleted and numbered lists (Enter continues the list, wrapped lines indent under the text) and left/centre/right alignment, per text box or sticky note; Ctrl+B / Ctrl+I while typing
- **Fonts:** eleven presets for typed text. Typed: Clean (Inter) and Book (Lora, a serif), both with real bold and italic. Handwriting: Friendly (Playpen Sans), Natural (Mynerve), School print (Andika), French cursive (Playwrite FR Moderne), Classic cursive (Playwrite FR Trad), Marker (Shantell Sans), Chalkboard (Schoolbell), Quick notes (Gochi Hand) and Neat (Handlee). All cover French accents; characters a font lacks (such as ² or ½) come from Inter, on screen and in PDF exports alike. Text written in the earlier fonts shows in the closest new one. Pick one before or while typing, or lasso-select text boxes and sticky notes and change the font or size (A− / A+) from the selection bar. The fonts are bundled, so they work offline (SIL Open Font License)
- **Tool options:** each tool keeps a compact bar of its own **5 favourite colours and 3 favourite sizes**, remembered between sessions. Tap a favourite to use it; tap it again (or the rainbow button) to swap it for a palette colour or any custom colour; the size chip opens a slider with exact values. The pen type and shape kind are one chip with a picture menu, and text lists and alignment sit behind one chip
- **Colours:** palette colours plus a custom colour picker (saturation/brightness square, hue slider, hex code, recently used colours and, where the system supports it, an eyedropper) for every tool and for recolouring a selection
- **Sizes:** each tool shows its exact size in px, with one-tap presets, − / + buttons and a box to type a value. Sizes are remembered
- **Lasso:** draw around items or tap one to select it; move, recolour, duplicate, copy/paste, delete
- **Resize and rotate anything:** any selection (handwriting, shapes, text, sticky notes, images, or a mix) gets a box with resize handles and a rotation knob below it. Corners keep proportions, edges stretch (Shift keeps proportions on edges too); rotation snaps to 15° with Shift. Text scales its font size; text, sticky notes and images can be rotated
- **Edit shapes:** a selected shape's bar changes its type, line width (exact px), fill and colour; a selected line or arrow has handles on both ends
- **Apply a font to handwriting:** lasso-select pen writing (e.g. a title) and choose *Apply font*. On Windows the built-in offline handwriting recognizer (Windows Ink) reads the words and suggests alternatives; you can correct the text, preview it in every handwriting font, and it replaces the ink at the same place and size (optionally keeping the ink). Elsewhere you type what you wrote
- **Pages:** all pages scroll continuously in one column; write on any page directly (it becomes the current page), and the page in view becomes current when scrolling stops. Sidebar thumbnails, add / insert / duplicate / reorder / delete / bookmark; undo history is kept per page
- **Editing:** undo/redo, zoom (Ctrl+wheel, pinch, fit width; zoomed in far, only the visible part of the page is drawn, so big and HiDPI screens stay fast), pan tool
- **Export:** PDF (whole notebook or one page), PowerPoint .pptx (one slide per page) or PNG, through the system's Save dialog. PDFs are vector: handwriting, shapes and text stay sharp at any zoom. PowerPoint and PNG exports are high-resolution images
- **Themes:** light and dark mode

### Keyboard shortcuts

| Key | Action | Key | Action |
| --- | --- | --- | --- |
| P | Pen | Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| H | Highlighter | Ctrl+C / Ctrl+V / Ctrl+D | Copy / paste / duplicate selection |
| E | Eraser | Delete | Delete selection |
| L | Lasso | Ctrl+A | Select all |
| S | Shape | Ctrl+= / Ctrl+- / Ctrl+0 | Zoom in / out / fit |
| T | Text box | ← / → (PgUp / PgDn) | Previous / next page |
| N | Sticky note | Esc | Clear selection / finish text |

## Project layout

```
qt/                   The app (Qt 6: QML screens, C++); see qt/README.md
  src/editor/         The editor: gestures, selection, undo, text, instruments
  src/draw/           Drawing: papers, covers, daftar pages, pens, shapes, text
  src/daftar/         The daftar annossous and cover setup
  src/tests/          Self-tests and the reference results' checks
  tests/golden/       Reference results (recorded by CI)
  native/             The C interface to the Rust core
core/                 Rust core: SQLite library, ink, shape and symbol recognition,
                      export (PDF/PPTX), backups, Google Drive sync, handwriting
                      recognition (Windows Ink)
docs/                 Release guide, web pages (privacy, terms),
                      reverse-engineering/ (Goodnotes web architecture reports)
```

## Development

Prerequisites: Qt 6.5+ (Quick, Controls, Svg, PDF), CMake and Rust stable.
On Arch: `sudo pacman -S --needed qt6-base qt6-declarative qt6-svg qt6-webengine cmake base-devel rustup`.

```bash
cmake -S qt -B build/qt -DCMAKE_BUILD_TYPE=Release
cmake --build build/qt -j
./build/qt/inkleaf
```

Tests:

```bash
cargo test -p inkleaf-core             # Rust core (database, ink, recognition, export, sync)
QT_QPA_PLATFORM=offscreen ./build/qt/inkleaf --self-test   # C++ tests (geometry, lists, guides, daftar…)
QT_QPA_PLATFORM=offscreen ./build/qt/inkleaf --golden check qt/tests/golden   # same results as recorded
QT_QPA_PLATFORM=offscreen ./build/qt/inkleaf --smoke   # the app starts and its screens load
```

The reference results are recorded by CI (on its Qt version; text can measure
a hair differently on others). After an intended change to drawing, the
editor or the daftar, run **Actions → Record reference results**: it records
them again and commits them.

Data lives in the OS app-data directory (e.g. `~/.local/share/app.inkleaf.desktop/inkleaf.db`
on Linux); `INKLEAF_DATA_DIR=/some/folder` uses another one.

## Not implemented yet

Collaboration, and handwriting recognition outside Windows.
[Report 05](docs/reverse-engineering/05-inkleaf-mapping.md) explains how each
could be added.
