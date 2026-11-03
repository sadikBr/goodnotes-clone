// The app's look: the same tokens as the web app (src/styles.css :root).
pragma Singleton
import QtQuick

QtObject {
    // Follows the system's dark mode where Qt can tell (6.5+), or the setting.
    property bool dark: false

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

    readonly property int radius: 10
    readonly property int radiusLg: 14
    readonly property int radiusXl: 20

    readonly property string font: "Inter"

    /// `a` mixed with `b` by `t` (0 = a, 1 = b), like CSS color-mix.
    function mix(a, b, t) {
        const c1 = Qt.color(a), c2 = Qt.color(b)
        return Qt.rgba(c1.r + (c2.r - c1.r) * t, c1.g + (c2.g - c1.g) * t, c1.b + (c2.b - c1.b) * t, c1.a + (c2.a - c1.a) * t)
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
