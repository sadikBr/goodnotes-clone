// The editor's remembered tool choices: each tool's favourite colours and
// sizes (src/editor/toolkit.ts Favourites), pen nib, shape kind, eraser and
// laser modes, font and the tool itself.
import QtQuick
import Inkleaf

QtObject {
    id: root
    readonly property var defaults: ({
        pen: { colors: ["#111827", "#2563eb", "#dc2626", "#16a34a", "#9333ea"], color: 0, sizes: [1.5, 3, 6], size: 1 },
        highlighter: { colors: ["#facc15", "#4ade80", "#f472b6", "#60a5fa", "#fb923c"], color: 0, sizes: [10, 18, 28], size: 1 },
        shape: { colors: ["#111827", "#2563eb", "#dc2626", "#16a34a", "#9333ea"], color: 0, sizes: [2, 3, 6], size: 1 },
        text: { colors: ["#111827", "#2563eb", "#dc2626", "#16a34a", "#6b7280"], color: 0, sizes: [16, 24, 36], size: 1 },
        sticky: { colors: ["#fef08a", "#bbf7d0", "#bfdbfe", "#fbcfe8", "#fed7aa"], color: 0, sizes: [14, 18, 24], size: 1 },
        laser: { colors: ["#ff1f1f", "#12c95a", "#1f7bff", "#b44dff", "#ff8a00"], color: 0, sizes: [], size: 0 },
        eraser: { colors: [], color: 0, sizes: [8, 16, 32], size: 1 },
        // Each pen type remembers its own colours and sizes.
        "pen:fountain": { colors: ["#111827", "#2563eb", "#dc2626", "#16a34a", "#9333ea"], color: 0, sizes: [1.5, 3, 6], size: 1 },
        "pen:ballpoint": { colors: ["#111827", "#2563eb", "#dc2626", "#16a34a", "#9333ea"], color: 0, sizes: [1.5, 3, 6], size: 1 },
        "pen:pencil": { colors: ["#374151", "#111827", "#2563eb", "#dc2626", "#16a34a"], color: 0, sizes: [1.5, 3, 6], size: 1 },
        "pen:calligraphy": { colors: ["#111827", "#1e3a8a", "#7c2d12", "#dc2626", "#16a34a"], color: 0, sizes: [3, 5, 8], size: 1 },
        "pen:brush": { colors: ["#111827", "#2563eb", "#dc2626", "#16a34a", "#9333ea"], color: 0, sizes: [3, 5, 8], size: 1 },
        "pen:marker": { colors: ["#f59e0b", "#ec4899", "#22c55e", "#3b82f6", "#8b5cf6"], color: 0, sizes: [3, 5, 8], size: 1 },
        "pen:watercolor": { colors: ["#3b82f6", "#ef4444", "#10b981", "#f59e0b", "#8b5cf6"], color: 0, sizes: [3, 5, 8], size: 1 },
        "pen:neon": { colors: ["#22d3ee", "#f472b6", "#a3e635", "#facc15", "#c084fc"], color: 0, sizes: [2, 3, 5], size: 1 },
        "pen:construction": { colors: ["#111827", "#2563eb", "#dc2626", "#16a34a", "#6b7280"], color: 0, sizes: [1, 2, 3], size: 1 },
        "pen:tape": { colors: ["#f9a8d4", "#93c5fd", "#fde68a", "#a7f3d0", "#d1d5db"], color: 0, sizes: [3, 4, 6], size: 1 }
    })
    readonly property var writingPens: ["fountain", "ballpoint", "pencil", "brush", "construction"]
    /// Where a tool's colours and sizes are kept: the pen's under its type ("pen:marker").
    function key(tool) { return tool === "pen" ? "pen:" + (store.penStyle || "fountain") : tool }
    // Size ranges (src/editor/editor.ts SIZE_SPECS).
    readonly property var specs: ({
        pen: { label: "Pen size", min: 0.5, max: 20, step: 0.5 },
        highlighter: { label: "Highlighter size", min: 4, max: 48, step: 1 },
        eraser: { label: "Eraser size", min: 4, max: 80, step: 1 },
        shape: { label: "Line width", min: 0.5, max: 20, step: 0.5 },
        text: { label: "Text size", min: 8, max: 96, step: 1 },
        sticky: { label: "Text size", min: 10, max: 48, step: 1 }
    })
    // The fuller palettes offered in the colour popover.
    readonly property var palettes: ({
        ink: ["#111827", "#374151", "#6b7280", "#9ca3af", "#ffffff", "#7c2d12", "#1e3a8a", "#2563eb", "#0891b2", "#0d9488", "#16a34a", "#65a30d", "#ca8a04", "#ea580c", "#dc2626", "#be123c", "#db2777", "#9333ea"],
        highlighter: ["#facc15", "#fde047", "#a3e635", "#4ade80", "#2dd4bf", "#38bdf8", "#60a5fa", "#c084fc", "#f472b6", "#fb7185", "#fb923c", "#e5e7eb"],
        sticky: ["#fef08a", "#fde68a", "#bbf7d0", "#a7f3d0", "#bfdbfe", "#c7d2fe", "#fbcfe8", "#fecaca", "#fed7aa", "#e5e7eb", "#ddd6fe", "#99f6e4"],
        laser: ["#ff1f1f", "#ff8a00", "#ffd400", "#12c95a", "#00c2d1", "#1f7bff", "#b44dff", "#ff3fa4", "#ffffff"]
    })
    function paletteFor(tool) {
        const k = key(tool)
        if (k === "pen:marker" || k === "pen:neon") return palettes.highlighter
        if (k === "pen:tape") return palettes.sticky
        return tool === "highlighter" ? palettes.highlighter : tool === "sticky" ? palettes.sticky : tool === "laser" ? palettes.laser : palettes.ink
    }

    property Prefs store: Prefs {
        category: "editor"
        property string favs: "{}"
        /// Your colours in the tool strip, for every tool: ["#hex", …] (none at first).
        property string railColors: "[]"
        property string daftarOutlines: "[]"
        property string tool: "pen"
        property string penStyle: "fountain"
        property string penDash: "dash"
        property real penGap: 1
        property bool penCaseSeen: false
        property string shapeKind: "auto"
        property string eraserMode: "precise"
        property string laserMode: "trail"
        property string textFont: "sans"
        property bool drawerPinned: false
        property bool sidebar: true
    }

    property var data: {
        const out = JSON.parse(JSON.stringify(defaults))
        try {
            const saved = JSON.parse(store.favs)
            for (const t in out) {
                // Pen colours saved before each pen type had its own: the writing pens start from them.
                const s = saved[t] || (t.startsWith("pen:") && writingPens.indexOf(t.slice(4)) >= 0 ? saved.pen : null)
                if (!s) continue
                if (Array.isArray(s.colors) && s.colors.length === out[t].colors.length) out[t].colors = s.colors
                if (Array.isArray(s.sizes) && s.sizes.length === out[t].sizes.length) out[t].sizes = s.sizes
                if (typeof s.color === "number" && s.color < out[t].colors.length) out[t].color = s.color
                if (typeof s.size === "number" && s.size < out[t].sizes.length) out[t].size = s.size
            }
        } catch (e) {}
        return out
    }
    function save(next) {
        data = next
        store.favs = JSON.stringify(next)
    }
    /// Every pen type's current ink and size: { fountain: { hex, width }, … } (for the pen case).
    function penInks() {
        const out = {}
        for (const k in data) if (k.startsWith("pen:")) { const f = data[k]; out[k.slice(4)] = { hex: (f.colors[f.color] || "#111827").replace("#", ""), width: f.sizes[f.size] || 3 } }
        return out
    }
    function get(tool) { return data[key(tool)] || data[tool] || null }
    function color(tool) { const f = get(tool); return f && f.colors.length ? f.colors[f.color] : "" }
    function size(tool) { const f = get(tool); return f && f.sizes.length ? f.sizes[f.size] : 0 }
    function edit(tool, change) { const d = JSON.parse(JSON.stringify(data)); const k = d[key(tool)] ? key(tool) : tool; change(d[k]); save(d) }
    function selectColor(tool, i) { edit(tool, f => f.color = i) }
    function setColor(tool, i, c) { edit(tool, f => { f.colors[i] = c; f.color = i }) }
    function selectSize(tool, i) { edit(tool, f => f.size = i) }
    function setSize(tool, i, v) { edit(tool, f => { f.sizes[i] = v; f.size = i }) }

    // --- your colours (in the tool strip; one list for every tool) ---
    readonly property int maxRailColors: 8
    /// Saved as a list; colours saved per tool (0.16.0) are joined into it.
    property var railList: {
        try {
            const v = JSON.parse(store.railColors)
            if (Array.isArray(v)) return v.filter(c => typeof c === "string")
            const out = []
            if (v && typeof v === "object")
                for (const k in v) if (Array.isArray(v[k])) for (const c of v[k]) if (typeof c === "string" && !out.some(x => x.toLowerCase() === c.toLowerCase())) out.push(c)
            return out.slice(-maxRailColors)
        } catch (e) { return [] }
    }
    function railColors() { return railList }
    function saveRail(list) {
        railList = list
        store.railColors = JSON.stringify(list)
    }
    function addRailColor(c) {
        const list = railList.filter(x => x.toLowerCase() !== c.toLowerCase())
        if (list.length >= maxRailColors) list.shift()
        saveRail(list.concat([c]))
    }
    function removeRailColor(c) { saveRail(railList.filter(x => x !== c)) }
    /// Writes with `c`: picks it if it's one of the tool's favourites, else puts it in the current one.
    function useColor(tool, c) {
        const f = get(tool)
        if (!f) return
        const i = f.colors.findIndex(x => x.toLowerCase() === c.toLowerCase())
        if (i >= 0) selectColor(tool, i)
        else setColor(tool, f.color, c)
    }

    readonly property var colors: ({ pen: color("pen"), highlighter: color("highlighter"), shape: color("shape"), text: color("text"), sticky: color("sticky"), laser: color("laser") })
    readonly property var widths: ({ pen: size("pen"), highlighter: size("highlighter"), eraser: size("eraser"), shape: size("shape"), text: size("text"), sticky: size("sticky") })
}
