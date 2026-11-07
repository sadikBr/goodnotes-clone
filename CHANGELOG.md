# What's new in Inkleaf

Each version's notes are shown in the app (the update card, and Settings →
Updates → What's new) and on its GitHub release. Keep them short, one line
per change, written for the people using Inkleaf. A release needs its
section here before it can be published (`scripts/check-version.py`).

## 0.20.0

- **Pens and brushes, apart:** the pen (P) has the fountain pen, ballpoint, a new fineliner, pencil and calligraphy; the new brush tool (B) has the brush pen, marker, watercolour, neon and tape, and five new brushes: **ink brush** (long tapers, deep ink), **flat brush** (a wide slanted tip with bristle streaks), **airbrush** (a soft spray), **chalk** and **crayon**. Each tool keeps its own kind, colour and size. They look the same on screen and in exports.
- **Dashed and dotted lines are an option now**, not a pen of their own: the ballpoint, fineliner, marker and neon can be solid, dashed or dotted, with close to very wide gaps, and so can shapes. Each pen remembers its own. Your dashed lines stay as they were (the dashed pen became a ballpoint with dashes).
- **The options strip is short again:** the line, its gaps and smoothing are at the bottom of the pen and brush cases; the strip keeps the pen (drawn as it writes), colours and sizes.
- **Select without the lasso:** with the pen, brush, highlighter or shape tool, tap a text box, picture, sticky note, graph or a shape's outline to select it; press and hold still on handwriting to select that word; or right-click anything with the mouse. The lasso takes over to move it or resize it, and your tool comes back when you deselect. (Dragging over things still writes on them, and the eraser always erases.)
- **What you draw is what stays:** the textured and effect pens (pencil, calligraphy, marker, watercolour, neon, chalk, crayon, the brushes) and dashed lines look the same while you draw as when you lift the pen; they used to show a plain line until then.
- After holding on handwriting to select it, keep the pen down and drag: it moves with you.
- **Tips:** the first time you pick a tool, a small card says what it can do that you wouldn't guess (hold still for clean shapes, hold on writing to select it, tap to select a picture…). Shortcuts and gestures lists every gesture, and can show the tips again.
- **Safer saving:** what you write is saved when Inkleaf quits and whenever you switch to another app, not only a moment after each change; and if a save fails (a full disk, say), it tells you once, keeps your changes and tries again until it works.
- **Smoother with heavy pages:** the soft brushes draw faster, and a page full of brush strokes sharpens over a few frames after zooming instead of pausing.
- **Small windows:** in a narrow window the library's heading goes on a line of its own and its buttons become icons; the options strip drops the pen's name and the sizes' numbers; and in a short window the tool rail gets smaller and shows only the colours that fit, so nothing runs off the edge.
- Blanking the screen while presenting is now **.** (B is the brush).

## 0.19.2

- **Fixed, properly this time:** exports and page pictures still showed blobs where a stroke started, stopped or turned sharply (0.19.1 only smoothed them). The fountain pen, ballpoint and brush are now exported with exactly the shape the screen draws, so a PDF or image looks like your page.

## 0.19.1

- **Fixed:** handwriting came out blotchy in exports (PDF, image, PowerPoint) and page pictures: a pen's pressure jumps from point to point, and the export followed every jump, so the lines swelled into blobs and their edges went lumpy. Exports now even the pressure out and smooth the line as the screen does, so they look like what you wrote.

## 0.19.0

- **Write from your phone:** no touch screen or drawing tablet? In a notebook, ⋯ → *Write from your phone* shows a code: scan it with your phone (on the same Wi-Fi) and the phone becomes a writing pad. Write with a finger or a stylus and it lands on the page you're on as you write, with the pen, colour and size you picked; two fingers move and zoom the part of the page the phone shows (a green frame shows it on the computer). The phone has the pen, highlighter and eraser, undo and redo, and the pages. Nothing to install on the phone: it's a page in its browser, served by Inkleaf only while this is on, and only to a phone with the code.
- If something keeps the phone out, the window says what: it finds this computer's firewall and gives the one command that lets the phone in (ufw, as on Omarchy and Ubuntu, or firewalld, as on Fedora), or on Windows, how to allow Inkleaf on private networks. When no phone has come after a while, it also says what to check on the network.
- The phone shows what you do as you do it: erasing as you rub, a stroke held still turning into a shape (and following your finger), a shape being drawn, selected things being moved. Before, these showed only once you lifted.
- Putting the pen down and holding it still starts the line straight away (it waited for you to move).
- Writing from the phone feels like the mouse now: a line's weight follows how fast you write (it was taken as a pen pressing at half strength, flat and a little thin), its moves keep the times they were written at (they arrive a few at a time over Wi-Fi, and the pens that follow speed read that as jerks), each move is sent at once, and the phone draws the line as smoothly as the computer does.

## 0.18.1

- **Fixed:** with a pen, a second eraser circle stayed where the pen first touched, and the real one was pulled back to it now and then. One circle now, under the pen.
- **Fixed:** renaming a notebook now changes the title on its cover too (from the library, the notebook and the assistant).
- **Fixed:** *Restore* in Recently deleted closed the window without bringing the page back.
- **Recently deleted**, redone: a list with each page's picture, when it was deleted and how long it stays, and *Restore* and delete right there (no need to hover). Pages deleted before they were ever saved now show what was on them.

## 0.18.0

- **Find in a notebook:** Ctrl+F, or the search button above the page. It finds words in typed text, tables, a PDF's own text (marked right on the word) and handwriting the AI has read; Enter goes to the next one. Arabic matches with or without short vowels and hamza.
- **Present:** F5, or ⋯ → Present. The page you're on goes full screen on a second screen or projector (or into its own window you can drag there), with your ink as you write and the laser pointer; your tools stay on your screen. B blanks the screen for a moment.
- **Undo for pages:** adding, deleting, duplicating and moving pages and changing their paper can be undone (and redone) with the rest, in the order you did them.
- **Recently deleted:** a deleted page goes to its notebook's *Recently deleted* (in the pages panel) for 30 days, with everything on it; restore it from there, or tap *Undo* in the message.
- **The assistant:** tap *Ask* in the library, or the sparkle in a notebook, and say what you want in your own words, in any language. It answers from your notes (handwriting the AI has read and PDF text included), and it can make things for you: folders and notebooks, typed pages (worksheets with questions and answer lines, summaries, tables), headings and a contents page, lessons in your daftar for a class on given days (following the timetable, skipping holidays), copying pages between notebooks, renaming, moving and covers.
- **You stay in charge:** before it changes anything, it shows the plan step by step: *Do it* or *Cancel*. One *Undo all of it* takes the whole job back. It never deletes anything, and password-protected notebooks are never read or changed.
- It needs an AI that can use tools (Settings → AI): Claude, GPT or Gemini, or a local model with tool support.
- **Fixed:** with a pen on Windows, the eraser (its circle and what it rubs out) fell well behind the pen and caught up in jumps. It keeps up now.
- **Scrolling speed:** *Settings → Pen & touch → Scrolling* sets how far the mouse wheel and a touchpad scroll, from a quarter to double, in notebooks and the library. Each computer keeps its own.
- **Fixed:** the drop-down for the shelf's first card cut its choices off.

## 0.17.0

- **Crop pictures:** select a picture and tap *Crop*. Drag the corners and sides, or drag inside to move what's kept; cut it as a rectangle, with rounded corners or as a circle, and keep it square, 4 : 3, 16 : 9 or as it was. *Reset* brings the whole picture back, any time.
- **Your own templates:** make a page once (a form, a planner, a worksheet, a PDF page) and keep it: *Paper → Yours → This page*. Use it on any page, on every page, for a new page, or for a new notebook; pages added after it carry on with it. It's part of the paper, so the eraser leaves it alone.
- **Contents:** the pages panel switches between pages, bookmarks and contents (the icons at its top). Contents lists a PDF's own chapters, a daftar's classes and headings you add for any page (*Add heading*, or *Add to contents…* in a page's ⋯ menu), one clean line each ("Slide 3: Objectives" shows *Objectives* under a small *Slide 3*), with the one you're on marked.
- **Contents page:** *Insert → Contents page* puts a page at the front listing your headings and a PDF's chapters with their page numbers; tap an entry's arrow to go there. It keeps itself up to date as you add headings or pages.
- **Chapters in exported PDFs:** a PDF you export opens with its contents (your headings, a PDF's own chapters, a daftar's classes) in the reader's sidebar.
- **Links:** select anything (ink, text, a picture) and tap the link button to link it to a page of this notebook, another notebook or a website. A small chip shows where it goes; tap it to follow, and *Back to page …* takes you back.
- **Zoom box:** for small spaces (forms, a daftar's cells). Tap the zoom box button by the zoom controls (or press Z): write large in the panel at the bottom and it lands small in the frame on the page. End a stroke in the panel's shaded right side and the frame moves along; at the end of the line it goes to the next. Drag the frame to move it, its side to change how much it magnifies.
- **Spelling for handwriting:** *⋯ → Check spelling on this page* (or *Ask AI → Check spelling* for a selection). The AI reads your words in whatever language you write, and the misspelled ones get a red wavy line; tap it for suggestions. One puts the word there as text in a handwriting font, or choose *Ignore*. Needs the AI (Settings → AI).
- **Lesson log in any of its colours:** in *Paper → Teacher*, the dots under *Lesson log* pick plum, blue, green, pink, navy or orange.
- **Sizes you can see, and one of your own:** every tool's sizes now show how they look (the pen's stroke, a highlighter swipe, the eraser's circle, letters for text), and after its three sizes there's a fourth that's yours. Tap it to use it, and tap it again to set it: a live preview, a slider, − and +, or type a size like 12.
- **The shelf's first card is yours to choose:** it makes one thing with a tap (a notebook at first). Its ⇄ button, or *Settings → Appearance → Library*, switches it to a daftar, PDFs, a folder, a sketchbook or a whiteboard. *New* in the sidebar still has them all.
- **Fixed:** the eraser's circle stays under the pen instead of trailing behind it.
- **Fixed:** with the pen in the library, tapping a document's or folder's ⋯ opens its menu, and tapping a menu or a dialog over the shelf no longer opens what's behind it too.
- **Fixed:** hovering a tool in the side rail or top bar now keeps it highlighted (it flashed grey and then vanished).
- **Fixed:** a tool's options could open away from it right after a notebook opened.

## 0.16.1

- **Fixed:** the tools no longer shift. Your colours stay in the side rail and top bar for every tool (dimmed for tools without colours), and a tool's options always open right beside it.
- **Each pen type has its own colours and sizes:** the marker, tape, neon and the rest remember theirs (bright markers, pastel tape…), and the options show each size as a line drawn with that pen.
- **Class colours:** in the daftar setup, tap a class's colour to choose another from 15 distinct colours; the timetable uses it.
- **Timetable step** redesigned: the week in a card with today marked, lesson blocks in their class colour, hours per class and per week, and a preview of where a tap will place the class.
- **Shapes stay on the paper too:** anything past a page's edge is hidden.
- **Fixed:** a stroke that closes the tool's options now writes; only a plain tap just closes them.
- **Fixed:** an empty folder no longer shows a sheet of paper inside it.
- **Fixed:** ink stays on the paper: a stroke that runs off the edge is cut there, while you write and once it's saved. The eraser and the lasso work when you start them just off the paper, so ink already outside can be cleaned up.
- **One look for corners:** buttons, fields, menus, panels, dialogs, badges, switches (in menus too), sliders, colour swatches and the handles on a selection, the ruler and the protractor share one set of squarer corners across the app. *More colours* is a rainbow square.
- **Colour picker:** the swatches fill the width, the knobs have a dark edge so they show on white and yellow, the hue bar is easier to grab, dark swatches have a visible edge in dark mode, and hex codes work with or without #.
- **Tools in the top bar** look and work like the side rail: one strip, Select first, the same groups, and your colours with **+** at the end.
- **Your colours are shared:** a colour you add shows for every tool, in the top bar and the side rail. Colours added per tool in 0.16.0 are kept.
- **Fixed:** holding the pen still at the end of a stroke no longer turns tape into a thin line. Dashed, dotted, marker, watercolour and neon strokes keep their look when they're straightened into a line or shape.

## 0.16.0

- **Daftar setup, step by step:** school, classes, timetable, holidays and cover, one at a time, in Arabic first. Classes are added as chips; the timetable is painted on a weekly grid (pick a class, tap or drag over its hours). The same steps open from Daftar settings.
- **Six new covers:** Arabesque (an interlaced star lattice under a pointed arch), Berber rug, Botanical, Terrazzo, Sunrise and Blueprint. A daftar can now have the Zellige, Arabesque or Berber cover.
- **Rename a class** in Daftar settings and its pages, student list and lessons follow the new name.
- **Tools on the side:** the tools now sit in a rail down the left side, leaving the top bar to the notebook. Prefer them in the top bar? Settings → Appearance → Tools, or ⋯ → Tools on the side. Select comes first, and the rail and its options have squarer corners.
- **New tool icons:** clearer select, pen, highlighter, eraser, shapes, text, sticky note, ruler, laser and hand icons, with a soft tint.
- **Your colours in the rail:** add the colours you use with **+**, for each tool; tap one to write with it, hold or right-click it to remove it.

## 0.15.0

- **Sketchbook:** blank landscape pages for drawing.
- **Whiteboard:** one big dotted page that grows as you write near its edges.
- **Pen case:** every pen shown as a stroke, in trays, with a line on what each does.
- **New pens:** marker (see-through, darker where strokes cross), watercolour (soft washes that deepen as you layer them), neon (a glowing line), dashed construction lines (dashes or dots, close or wide) and masking tape (tap it to peek under it).
- **Guides** show for anything you move, not just text: its edges and centre against the page and other items.
- **Fixed:** both protractor arms can be dragged smoothly.
- **Fixed:** Escape in a table cell finishes typing instead of leaving the page.

## 0.14.0

- **Settings** shows one section at a time (Pen & touch, AI, Sync & backups, Appearance, Updates), in tidy groups.
- **Menus** have icons, switches for settings and groups; Export lists formats under *Whole notebook* and *This page*.
- **New** on the shelf offers a notebook, a daftar, PDFs or a folder in one tap.
- **New notebook** comes in two steps, cover then paper, with every design and paper shown as pictures and the result beside them.
- **Undo** right in the message when you move something to the trash; messages show what happened with an icon.
- **Empty pages** of the library (favourites, a folder, no search results) say what to do next.
- **Selection bar** is half as wide: one colour button, and duplicate, copy and *Save to my pieces* under ⋯.
- **Daftar** controls (class, new lesson, outline, menu) sit together in one group.
- **Pages** list: show only bookmarked pages.
- **Ask** opens with clear suggestions and shows which AI answers.
- **Zoom bar** fades while you write, so it doesn't cover the page.

## 0.13.0

- **Scratch out to erase:** scribble back and forth over ink with the pen and it's gone (Undo brings it back). Writing, circling and shading are never mistaken for it.
- **Finger taps:** tap with two fingers to undo, three to redo.
- **Fixed:** the eraser keeps up with the pen.
- **Fixed:** a tap next to typed text (an Arabic word, say) no longer selects it; only a tap on its letters does.
- **Fixed:** clicks on the update card no longer reach the buttons behind it.

## 0.12.0

- **Tables:** draw a rough grid and choose *Make table* to get a clean one, or *Insert → Table*. Type in the cells, write in them, drag the lines to resize, add rows and columns.
- **What's new** shows in the update card, and once more after updating (Settings → Updates → What's new).
- **My pieces** has a new look, with *Save selection* right in it.
- **Fixed:** with a pen, taps on menus, the tool drawer and buttons now reach them, instead of drawing a dot or closing the menu.

## 0.11.0

- **Maths:** select written maths and Inkleaf works it out: it writes the answer beside it, typesets it, solves equations or shows the steps.
- **Graphs:** plot functions as you'd write them (y = x² − 2x, sin x…) and edit them later.
- **My pieces:** save a selection (a diagram, a signature, a stamp) and put it on any page from Insert → My pieces.
- **Side by side:** open another page or notebook next to the one you're writing in.
- **Tidier toolbar:** one Insert menu, and the ruler sits with the drawing tools.
- **Fixed:** the Windows setup no longer leaves an update half done when a file is in use.

## 0.10.0

- **Update now:** Inkleaf downloads, checks and installs new versions itself on Windows, Linux (AppImage) and Android.
- **AI answers** appear as they're written, and can be stopped.
- **Arabic text** gets a matching Arabic face for each handwriting font.
- **Text selection** fits the letters, and handles no longer make text jump or vanish.
- **Page previews** match their paper, and the eraser shows where it rubs.
