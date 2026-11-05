// The app's settings (the Settings screen), saved and shared by every screen.
pragma Singleton
import QtQuick

QtObject {
    id: root
    readonly property var smoothingLevels: ({ off: 0, light: 0.2, normal: 0.38, strong: 0.62 })

    /// The Settings section to open next (then cleared): "pen", "ai", "sync", "look", "about".
    property string settingsPage: ""
    property Prefs store: Prefs {
        category: "settings"
        /// "off" | "light" | "normal" | "strong"
        property string smoothing: "normal"
        /// "light" | "normal" | "firm"
        property string pressure: "normal"
        /// "cross" | "dot" | "none"
        property string penCursor: "cross"
        /// "eraser" | "lasso" | "none"
        property string sideButton: "eraser"
        property bool eraserEnd: true
        property bool palmRejection: true
        /// Scratching out ink with the pen erases it.
        property bool scratchOut: true
        /// Two fingers tapped: undo; three: redo.
        property bool fingerTaps: true
        property bool penOnly: false
        /// "light" | "dark" | "system"
        property string theme: "system"
        /// Where the editor's tools sit: "top" (in the top bar) | "rail" (down the left side).
        property string toolbar: "rail"
        /// What the shelf's first card makes: "notebook", "daftar", "pdf", "folder", "sketchbook", "whiteboard".
        property string newCard: "notebook"
    }
    readonly property real streamline: smoothingLevels[store.smoothing] ?? 0.38

    /// Dark when chosen, or when "system" and the desktop is dark (Qt 6.5+ can tell).
    readonly property bool systemDark: Qt.styleHints.colorScheme !== undefined && Qt.styleHints.colorScheme === 2
    readonly property bool dark: store.theme === "dark" || (store.theme === "system" && systemDark)
    onDarkChanged: Theme.dark = dark
    Component.onCompleted: Theme.dark = dark
}
