// The app's look: the same tokens as the web app (src/styles.css :root).
pragma Singleton
import QtQuick

QtObject {
    // Follows the system's dark mode where Qt can tell (6.5+), or the setting.
    property bool dark: false
    /// A phone-sized window (set by the app window): one column, tools at the bottom, sheets for popups.
    property bool phone: false

    readonly property color bg: dark ? "#131412" : "#f6f4ef"
    readonly property color surface: dark ? "#1b1c1a" : "#ffffff"
    readonly property color surface2: dark ? "#242522" : "#f2f0ea"
    readonly property color surface3: dark ? "#2d2e2a" : "#e8e5dd"
    readonly property color border: dark ? "#31322e" : "#e4e0d7"
    readonly property color borderStrong: dark ? "#44453f" : "#d3cec3"
    readonly property color text: dark ? "#ecebe6" : "#1d1f23"
    readonly property color muted: dark ? "#a19c91" : "#706b61"
    readonly property color accent: dark ? "#5cc796" : "#1f7a5a"
    readonly property color accentHover: dark ? "#78d6aa" : "#186a4d"
    readonly property color accentSoft: dark ? Qt.rgba(92 / 255, 199 / 255, 150 / 255, 0.15) : Qt.rgba(31 / 255, 122 / 255, 90 / 255, 0.12)
    readonly property color accentContrast: dark ? "#0c2419" : "#ffffff"
    readonly property color danger: dark ? "#ef7c66" : "#c2412d"
    readonly property color desk: dark ? "#0e0f0d" : "#e7e4dc"

    // Corners, one scale for the whole app: tiny marks 4, small buttons and
    // chips 6, buttons and fields 8, panels, menus and popovers 10, dialogs and cards 12.
    readonly property int radiusXs: 4
    readonly property int radiusSm: 6
    readonly property int radius: 8
    readonly property int radiusLg: 10
    readonly property int radiusXl: 12

    readonly property string font: "Inter"

    /// `a` mixed with `b` by `t` (0 = a, 1 = b), like CSS color-mix.
    function mix(a, b, t) {
        const c1 = Qt.color(a), c2 = Qt.color(b)
        return Qt.rgba(c1.r + (c2.r - c1.r) * t, c1.g + (c2.g - c1.g) * t, c1.b + (c2.b - c1.b) * t, c1.a + (c2.a - c1.a) * t)
    }
    /// The corner of a colour swatch `size` wide (rounded squares everywhere colours are picked).
    function swatchRadius(size) { return Math.max(3, Math.round(size * 0.27)) }
    /// "More colours" as a rounded square of the rainbow (`hole`: with a white middle), as an SVG data URL.
    function rainbow(hole) {
        const r = 6.5, colors = ["#ef4444", "#eab308", "#22c55e", "#06b6d4", "#3b82f6", "#a855f7"]
        // The rounded square's outline, as points, then cut into six slices around the middle.
        const pts = []
        const corner = (cx, cy, a0) => { for (let k = 0; k <= 8; k++) { const a = a0 + k * Math.PI / 16; pts.push([cx + r * Math.cos(a), cy + r * Math.sin(a)]) } }
        corner(24 - r, r, -Math.PI / 2); corner(24 - r, 24 - r, 0); corner(r, 24 - r, Math.PI / 2); corner(r, r, Math.PI)
        const angleOf = p => (Math.atan2(p[1] - 12, p[0] - 12) + Math.PI * 2.5) % (Math.PI * 2)
        const at = a => {
            // Where a ray from the middle leaves the outline (between two outline points).
            for (let i = 0; i < pts.length; i++) {
                const p = pts[i], q = pts[(i + 1) % pts.length]
                let ap = angleOf(p), aq = angleOf(q)
                if (aq < ap) aq += Math.PI * 2
                let x = a
                if (x < ap) x += Math.PI * 2
                if (x >= ap && x <= aq) { const t = (x - ap) / Math.max(1e-6, aq - ap); return [p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t] }
            }
            return [12, 0]
        }
        let body = ""
        for (let k = 0; k < 6; k++) {
            const a0 = k * Math.PI / 3, a1 = (k + 1) * Math.PI / 3
            let d = "M12 12L" + at(a0).map(v => v.toFixed(2)).join(" ")
            for (const p of pts) { let a = angleOf(p); if (a > a0 && a < a1) d += "L" + p[0].toFixed(2) + " " + p[1].toFixed(2) }
            d += "L" + at(a1 % (Math.PI * 2)).map(v => v.toFixed(2)).join(" ") + "Z"
            body += "<path d='" + d + "' fill='" + colors[k] + "'/>"
        }
        if (hole) body += "<rect x='7.5' y='7.5' width='9' height='9' rx='2.5' fill='white'/>"
        return "data:image/svg+xml;utf8," + encodeURIComponent("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>" + body + "</svg>")
    }
    function alpha(c, a) {
        const k = Qt.color(c)
        return Qt.rgba(k.r, k.g, k.b, a)
    }

    // Folder colours (src/library.ts FOLDER_COLORS).
    readonly property var folderColors: [{ color: "#3b82f6", label: "Blue" }, { color: "#0ea5e9", label: "Sky" }, { color: "#14b8a6", label: "Teal" }, { color: "#22c55e", label: "Green" }, { color: "#eab308", label: "Yellow" }, { color: "#f97316", label: "Orange" }, { color: "#ef4444", label: "Red" }, { color: "#ec4899", label: "Pink" }, { color: "#8b5cf6", label: "Violet" }, { color: "#64748b", label: "Slate" }]
    // Cover colours (src/ui.ts COVER_COLORS).
    readonly property var coverColors: ["#2563eb", "#0d9488", "#16a34a", "#ca8a04", "#ea580c", "#dc2626", "#db2777", "#7c3aed", "#475569", "#111827"]
    // Paper colours and sizes (src/api.ts).
    readonly property var paperColors: [{ color: "#ffffff", label: "White" }, { color: "#fffdf7", label: "Cream" }, { color: "#fdf3c8", label: "Yellow" }, { color: "#eef1f4", label: "Grey" }, { color: "#22262d", label: "Dark" }]
    readonly property var paperSizes: [{ id: "a4", label: "A4", w: 794, h: 1123 }, { id: "a5", label: "A5", w: 559, h: 794 }, { id: "letter", label: "US Letter", w: 816, h: 1056 }, { id: "legal", label: "US Legal", w: 816, h: 1344 }, { id: "square", label: "Square", w: 900, h: 900 }]

    // Ink and highlighter colours (src/editor/toolkit.ts).
    readonly property var inkColors: ["#111827", "#374151", "#6b7280", "#9ca3af", "#ffffff", "#7c2d12", "#1e3a8a", "#2563eb", "#0891b2", "#0d9488", "#16a34a", "#65a30d", "#ca8a04", "#ea580c", "#dc2626", "#be123c", "#db2777", "#9333ea"]
    readonly property var highlighterColors: ["#facc15", "#fde047", "#a3e635", "#4ade80", "#2dd4bf", "#38bdf8", "#60a5fa", "#c084fc", "#f472b6", "#fb7185", "#fb923c", "#e5e7eb"]
}
