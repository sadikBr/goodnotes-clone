// Text presets (as qt/src/draw/Fonts.cpp has them): id, label, the face to show
// it in, and its Arabic partner (each preset's Arabic has its own character).
.pragma library
var presets = [
    { id: "sans", label: "Clean", face: "Inter", arabic: "Noto Sans Arabic" },
    { id: "book", label: "Book", face: "Lora", arabic: "Noto Naskh Arabic" },
    { id: "playpen", label: "Friendly", face: "Playpen Sans", arabic: "Lemonada" },
    { id: "mynerve", label: "Natural", face: "Mynerve", arabic: "Katibeh" },
    { id: "school", label: "School print", face: "Andika", arabic: "Baloo Bhaijaan 2" },
    { id: "cursive-fr", label: "French cursive", face: "Playwrite FR Moderne", arabic: "Aref Ruqaa" },
    { id: "cursive-trad", label: "Classic cursive", face: "Playwrite FR Trad", arabic: "Amiri" },
    { id: "marker", label: "Marker", face: "Shantell Sans", arabic: "Rakkas" },
    { id: "chalk", label: "Chalkboard", face: "Schoolbell", arabic: "Changa" },
    { id: "quick", label: "Quick notes", face: "Gochi Hand", arabic: "Marhey" },
    { id: "neat", label: "Neat", face: "Handlee", arabic: "El Messiri" }
]
function preset(id) {
    for (var i = 0; i < presets.length; i++) if (presets[i].id === id) return presets[i]
    return presets[0]
}
/// The face to show `text` in: the preset's Arabic face when it has Arabic.
function faceFor(id, text) {
    var p = preset(id)
    return /[\u0600-\u06FF\u0750-\u077F\u08A0-\u08FF\uFB50-\uFDFF\uFE70-\uFEFF]/.test(text || "") ? p.arabic : p.face
}
/// Family list for QML text (the face, its Arabic face, then Noto Sans Arabic and Inter).
function families(id) {
    var p = preset(id)
    return [p.face, p.arabic, "Noto Sans Arabic", "Inter"]
}
