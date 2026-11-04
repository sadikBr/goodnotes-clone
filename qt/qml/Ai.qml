// The AI the user set up (Settings → AI): asking it things, and reading
// handwritten pages in the background so search finds handwriting.
pragma Singleton
import QtQuick
import Inkleaf

QtObject {
    id: root
    /// { provider, model, hasKey, keyHint, searchHandwriting, ready, location, effectiveUrl, effectiveModel }
    property var status: null
    readonly property bool ready: !!(status && status.ready)
    readonly property bool local: !!(status && status.location === "local")
    /// How much of the notes fits in one question (local models have less room).
    readonly property int notesBudget: local ? 16000 : 160000

    readonly property string transcribePrompt: "You read handwritten notes. Transcribe all the handwriting in the picture exactly as written, in the language it's written in (Arabic, French, English or any other), keeping its line breaks. Write maths in plain text (e.g. x^2 + 1 = 0). Ignore drawings and doodles that aren't writing. Reply with the text only, with no comments, quotes or headings. If there is no writing, reply with nothing."

    function refresh() {
        try { status = InkleafCore.call("aiStatus") } catch (e) {}
    }
    /// Changes settings ({ provider?, baseUrl?, model?, apiKey?, searchHandwriting? }); returns an error or "".
    function setup(change) {
        try {
            status = InkleafCore.call("aiSetup", { change: change })
            readError = ""
            refreshProgress()
            reader.restart()
            return ""
        } catch (e) {
            return String(e.message || e)
        }
    }
    /// Asks: messages [{ role, text, images? }]; then cb(answer, error).
    function ask(system, messages, quick, cb) {
        InkleafCore.callAsync("aiAsk", { request: { system: system, messages: messages, quick: !!quick } }, (r, err) => cb(err ? "" : String(r || ""), err || ""))
    }
    /// Asks with the answer streamed: onText(piece) as it's written, then onDone(answer, error). Returns an id for InkleafCore.cancelAsk.
    function askStream(system, messages, onText, onDone) {
        return InkleafCore.askStream({ system: system, messages: messages, quick: false }, onText, onDone)
    }
    function test(cb) {
        InkleafCore.callAsync("aiTest", {}, (r, err) => cb(err ? "" : String(r || ""), err || ""))
    }
    /// Reads handwriting in a picture (a data: URL); cb(text, error).
    function transcribe(image, cb) {
        ask(transcribePrompt, [{ role: "user", text: "Transcribe the handwriting.", images: [image] }], true, cb)
    }

    // ------------------------------------------------------------ reading handwriting for search

    property bool reading: false
    /// { done, total } handwritten pages read.
    property var progress: null
    property string readError: ""
    readonly property bool readingOn: ready && !!status.searchHandwriting

    function refreshProgress() {
        InkleafCore.callAsync("readingProgress", {}, (p, err) => { if (!err) root.progress = p })
    }
    /// Reads a few handwritten pages that changed; more soon after while there are more.
    function readSome() {
        if (!readingOn || reading) return
        let pages = []
        try { pages = InkleafCore.call("pagesToRead", { limit: 4 }) } catch (e) { return }
        if (!pages.length) { refreshProgress(); return }
        reading = true
        const next = i => {
            if (i >= pages.length || !root.readingOn) {
                root.reading = false
                root.refreshProgress()
                // More waiting: carry on shortly.
                if (pages.length === 4) soon.restart()
                return
            }
            const p = pages[i]
            const image = Catalog.pageWriting(p.pageId, p.width, p.height, 1400)
            if (!image) { next(i + 1); return }
            root.transcribe(image, (text, err) => {
                if (err) {
                    // Stop until the next round (no connection, no credit…).
                    root.readError = err
                    root.reading = false
                    return
                }
                root.readError = ""
                try { InkleafCore.call("setPageInkText", { pageId: p.pageId, text: text }) } catch (e) {}
                next(i + 1)
            })
        }
        next(0)
    }
    property Timer reader: Timer {
        running: root.readingOn
        repeat: true
        interval: 3 * 60000
        triggeredOnStart: true
        onTriggered: root.readSome()
    }
    property Timer soon: Timer { interval: 4000; onTriggered: root.readSome() }

    Component.onCompleted: { refresh(); refreshProgress() }
}
