// Text presets (as qt/src/draw/Fonts.cpp has them): id, label and the face to show it in.
.pragma library
var presets = [
    { id: "sans", label: "Clean", face: "Inter" },
    { id: "book", label: "Book", face: "Lora" },
    { id: "playpen", label: "Friendly", face: "Playpen Sans" },
    { id: "mynerve", label: "Natural", face: "Mynerve" },
    { id: "school", label: "School print", face: "Andika" },
    { id: "cursive-fr", label: "French cursive", face: "Playwrite FR Moderne" },
    { id: "cursive-trad", label: "Classic cursive", face: "Playwrite FR Trad" },
    { id: "marker", label: "Marker", face: "Shantell Sans" },
    { id: "chalk", label: "Chalkboard", face: "Schoolbell" },
    { id: "quick", label: "Quick notes", face: "Gochi Hand" },
    { id: "neat", label: "Neat", face: "Handlee" }
]
function preset(id) {
    for (var i = 0; i < presets.length; i++) if (presets[i].id === id) return presets[i]
    return presets[0]
}
/// Family list for QML text (the face, Arabic, then Inter).
function families(id) {
    var p = preset(id)
    return [p.face, "Noto Sans Arabic", "Inter"]
}
